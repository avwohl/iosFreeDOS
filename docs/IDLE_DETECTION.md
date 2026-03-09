# DOS Emulator Idle Detection and Battery Optimization

Research notes on how DOS emulators detect guest idle states and reduce
host CPU usage. Applicable to any 8086/286/386 real-mode emulator running
DOS programs.

## The Problem

DOS programs busy-wait. There is no multitasking scheduler to yield to.
A DOS prompt polling for keyboard input burns 100% of a host CPU core
unless the emulator detects the idle pattern and sleeps.

## Five Idle Signals

### 1. HLT Instruction (opcode 0xF4)

The most reliable idle indicator. When the CPU executes HLT with
interrupts enabled (IF=1), it means "stop until the next hardware
interrupt." FreeDOS supports this natively via `IDLEHALT=1` in
CONFIG.SYS (safe hooks — CPU halts only when waiting for CON char
device input). `IDLEHALT=-1` enables all hooks.

**DOSBox implementation:** When HLT is executed, DOSBox zeros
`CPU_Cycles` and switches to a minimal `hlt_decode` stub:

```cpp
void CPU_HLT(Bitu oldeip) {
    CPU_IODelayRemoved += CPU_Cycles;  // Don't count remaining cycles
    CPU_Cycles = 0;                     // Stop executing immediately
    cpudecoder = &hlt_decode;           // Switch to minimal decoder
}
```

The `hlt_decode` stub checks if the instruction pointer changed (meaning
an interrupt fired). If not, it zeros cycles again. This causes the main
loop to fall through to the timing code, which sleeps ~1ms per iteration.

**Key insight:** `CPU_IODelayRemoved` prevents the dynamic cycles
algorithm from incorrectly boosting the cycle count just because the CPU
was idle.

### 2. INT 16h AH=00h/10h (Wait for Keystroke)

This BIOS call blocks until a key is pressed. In DOSBox, when
`get_key()` returns false (no key available), the handler calls
`CPU_HLT(reg_eip)` — making the keyboard wait equivalent to HLT.

**Implementation approach:** When INT 16h AH=00/10 finds the keyboard
buffer empty (BDA head == tail), set an idle flag and rewind IP to
re-execute the INT on the next batch. The host run loop checks the idle
flag and sleeps.

### 3. INT 16h AH=01h/11h (Check Keyboard Status) in a Tight Loop

Many DOS programs poll the keyboard in a tight loop:

```asm
poll:
    mov ah, 01h
    int 16h
    jz  poll        ; no key, keep polling
```

This is harder to detect because it's a pattern, not a single
instruction. DOSBox-X and DR-DOS use heuristics: if the timer byte at
BDA address `0040:006C` hasn't changed between consecutive INT 16h
AH=01 calls, the system is likely idle.

**Implementation approach:** Count consecutive AH=01/11 calls that
return "no key" (ZF=1). After N consecutive polls AND a minimum
emulated time threshold (to avoid false triggers during DOS Ctrl-C
checks or boot prompts), treat it as idle.

The dual threshold is important:
- **Count alone is insufficient:** DOS Ctrl-C checking does 5-20
  polls per batch during file I/O — fast but brief.
- **Time alone is insufficient:** The FreeDOS boot "Press F8" prompt
  uses a 2-second timer-based timeout with sparse polling.
- **Both together:** Require enough consecutive polls AND enough
  emulated time (~3 timer ticks / ~165ms) to confirm genuine idle.

Reset the poll counter on any non-keyboard/timer/video interrupt
(which indicates the program is doing real work, not just polling).

### 4. INT 2Fh AX=1680h (Release Current VM Time Slice)

The standard DOS idle API, originally designed for OS/2 2.0 and
Windows 386 enhanced mode multi-tasking. Programs call it to
explicitly declare they are idle. Return AL=0 to indicate the call
is supported.

**Who calls it:** FDAPM, DOSIDLE, and some DOS programs that are
multi-tasking aware. FreeDOS kernel itself can be configured to call
it.

**Implementation:** Trivial — set AL=0 and yield to the host.

### 5. INT 28h (DOS Idle Interrupt)

Called by the DOS kernel (since DOS 2.0) during keyboard input waits
within the DOS kernel itself — specifically, when COMMAND.COM is
waiting for input at the prompt. The default handler is just IRET.

**Implementation:** Hook INT 28h to trigger idle behavior. DOSBox-X
limits the idle response to fire once per keyboard read cycle to avoid
sluggish input response.

## DOSBox Dynamic Cycles Algorithm

DOSBox's `cycles=auto` mode dynamically adjusts the number of
instructions per time slice to match a target CPU utilization.

### Core Concept

The emulator runs in 1ms "frames." Each frame executes
`CPU_CycleMax` instructions. The dynamic system adjusts
`CPU_CycleMax` to keep the emulator using approximately 90% of one
host CPU core.

### Algorithm (from DOSBox Staging `dosbox.cpp`)

Every ~100-250ms (`ticks.scheduled >= 100 || ticks.done >= 100`):

1. Compute ratio of scheduled work to elapsed time:
   ```
   ratio = (ticks.scheduled * (CPU_CyclePercUsed * 1024 / 100))
           / ticks.done
   ```

2. Subtract `CPU_IODelayRemoved` (cycles consumed by I/O or HLT that
   shouldn't count as "useful work").

3. Apply safety caps to prevent runaway growth:
   - If `ticks.done < 10` (barely ran): cap ratio at 16384
     (or 5120 if `CPU_CycleMax > 50000`)
   - If brief period (`ticks.scheduled <= 20`): cap ratio at 800

4. Adjust `CPU_CycleMax` proportionally. Under target (ratio <= 1024)
   decreases smoothly. Over target increases.

5. Emergency brake: if `ticks.added > 15` but `ticks.scheduled < 5`,
   the system is badly behind — divide `CPU_CycleMax` by 3.

### Protected Mode Trigger

`cycles=auto` runs at a fixed 3000 cycles in real mode. When the CPU
detects the PE bit being set in CR0 (entering protected mode), it
switches to dynamic adjustment. Rationale: real-mode programs are older
and need predictable lower speeds.

### Smoothing Techniques

- `ticks.remain` capped at 20 to prevent catch-up bursts (max 20ms
  burst after a delay)
- Dynamic adjustment runs at minimum every 100ms, not every frame
- Changes < 10% are ignored (hysteresis)
- Small adjustments (ratio 10-120) only apply when `ticks.done < 700`
- I/O delay removal prevents cycle count spikes after I/O waits

## Sleep Duration

The single most impactful decision for perceived responsiveness.

| Emulator       | Idle Sleep | Notes                          |
|----------------|------------|--------------------------------|
| DOSBox         | 1ms        | `sleep_for(1000us)` in idle    |
| DOSBox Staging | 1ms        | Same as DOSBox                 |
| DOSBox-Pure    | Frame-locked | Tied to host vsync via libretro |
| 86Box/PCem     | None       | No built-in DOS idle detection |

**Guidance:**
- **1ms** is the DOSBox sweet spot — fine enough for smooth emulation,
  long enough to actually yield the CPU.
- **10ms** is a reasonable mobile compromise — saves more battery than
  1ms with barely perceptible input latency.
- **16ms (one frame)** causes visible jank. Avoid.
- **50ms+** makes key echo feel sluggish. Too long for interactive use.

## 86Box / PCem

These hardware-accurate emulators do NOT have built-in DOS-level idle
detection. They emulate the hardware cycle-accurately and rely on the
guest OS or third-party tools to manage idle:

- **Windows 9x:** Users must install `AmnHLT` (a VxD that issues HLT
  when Windows is idle) or `Rain` to prevent 100% CPU usage.
- **DOS:** No automatic idle — burns 100% CPU at the prompt.

This is a known limitation documented in the 86Box FAQ.

## Practical Recommendations

1. **Handle HLT first.** It's the highest-signal idle indicator and
   the simplest to implement. When HLT fires with IF=1, stop
   executing and yield to the host.

2. **Add INT 2Fh AX=1680h.** Trivial to implement (set AL=0, yield).
   FreeDOS and idle utilities use it.

3. **Track INT 16h keyboard polling.** Use a dual threshold (poll
   count + emulated time) to distinguish genuine idle from DOS's
   internal Ctrl-C checks during file I/O.

4. **Use 1-10ms sleep granularity.** Never sleep 16ms+ in the idle
   path. On mobile, 10ms is a good battery/responsiveness tradeoff.

5. **Per-batch throttle with drift cap.** Don't track time from boot
   start — if the emulator falls behind, it runs full-speed to catch
   up, causing choppy bursts. Instead, reset the timing baseline when
   drift exceeds a threshold (e.g., 100ms behind).

6. **Deduct idle cycles from speed calculation.** If using dynamic
   speed adjustment, subtract HLT/IO cycles from the "work done"
   counter to avoid inflating the cycle target.

## Sources

- DOSBox Staging source: `cpu.cpp`, `dosbox.cpp`
  (github.com/dosbox-staging/dosbox-staging)
- DOSBox-X CPU settings guide and `bios_keyboard.cpp` source
- VOGONS forums: "How DOSBox cpu cycles auto mode works"
  (vogons.org/viewtopic.php?t=75057)
- VOGONS forums: "Green DOSBox dosidle-alike patch"
  (vogons.org/viewtopic.php?t=25117)
- FreeDOS IDLEHALT documentation
  (help.fdos.org/en/hhstndrd/cnfigsys/idlehalt.htm)
- OS/2 Museum: "Idle DR-DOS"
  (os2museum.com/wp/idle-dr-dos/)
- dosemu2 INT 2F.1680 timeslice issue
  (github.com/dosemu2/dosemu2/issues/674)
- DOSBox-Pure libretro
  (github.com/schellingb/dosbox-pure)
- 86Box FAQ
  (86box.readthedocs.io/en/latest/usage/faq.html)
- INT 2Fh Function 1680h reference
  (delorie.com/djgpp/doc/dpmi/api/2f1680.html)
