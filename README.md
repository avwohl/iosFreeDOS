# iosFreeDOS

> **This project is abandonware.** Implementing a full 386 CPU, x87 FPU, and
> DPMI DOS extender from scratch proved too complicated to maintain. The
> successor project, [iosFreeDOS2](https://github.com/avwohl/iosFreeDOS2),
> uses the DOSBox emulator core for protected-mode emulation instead.

An 8088/386 IBM PC emulator for iOS, macOS, and the command line. Runs FreeDOS
and other DOS-compatible operating systems on your iPhone, iPad, or Mac.

## Features

- **8088/80186/286/386 CPU emulator** written in C++ — executes real DOS binaries in real mode and 386 protected mode (V86, ring transitions, paging)
- **CGA, EGA, VGA, MDA, and Hercules** display adapters (configurable, including dual CGA+MDA)
- **NE2000 network adapter** — DP8390-based Ethernet with standard packet driver support
- **AdLib and Sound Blaster** sound card emulation
- **Host file transfer** — R.COM and W.COM utilities move files between DOS and the host
- **INT 33h mouse driver** with touch-to-mouse mapping on iOS
- **Keyboard input** with Ctrl key toolbar, Esc, Tab, arrow keys, copy/paste
- **Floppy, hard disk, and CD-ROM ISO** image support
- **Disk image catalog** — browse and download disk images from GitHub releases
- **Download from URL** — load floppy, HDD, or ISO images from any URL
- **Named configuration profiles** — save/load machine setups
- **Speed control** — Full speed, IBM PC 4.77 MHz, IBM AT 8 MHz, 386SX 16 MHz, 386DX 33 MHz, or 486DX2 66 MHz

## Compatibility

The 386 CPU includes full protected mode support: GDT/LDT/IDT descriptor
tables, ring 0–3 privilege transitions, call gates, V86 mode, paging with
4KB/4MB pages, and all system instructions. The built-in XMS 3.0 driver
provides up to 64 MB of extended memory. CWSDPMI ships on the FreeDOS hard
disk image and loads at boot, so programs that need DPMI (DOS4GW, DOS32A,
DJGPP, etc.) can run.

The emulator runs a wide range of DOS software from the late 1980s through the
mid-1990s, including real-mode games and protected-mode programs that use
DPMI-based DOS extenders. See [GAMES.md](GAMES.md) for the bundled game list.

## Quick Start

### iOS / macOS App

1. Open the app and scroll to the **Disk Catalog** section
2. Download a FreeDOS floppy image and tap **Use as A:**
3. Tap **Start Emulator**

### Command-Line Interface

```bash
make
./freedos_cli -a fd/freedos.img              # Boot FreeDOS from floppy
./freedos_cli -c fd/freedos_hd.img -boot c   # Boot from hard disk
./freedos_cli -d image.iso -boot cd           # Boot from CD-ROM ISO
./freedos_cli -a fd/freedos.img -net          # Boot with NE2000 networking
./freedos_cli -a fd/freedos.img -s pc         # Run at IBM PC 4.77 MHz speed
./freedos_cli -C newdisk.img 100              # Create 100 MB blank hard disk
```

## File Transfer

R.COM reads a file from the host into DOS. W.COM writes a DOS file out to the host.

```
A:\> R /path/to/hostfile.txt MYFILE.TXT
A:\> W MYFILE.TXT /path/to/output.txt
```

Where the host paths point depends on the platform:

| Platform | Host file location |
|---|---|
| **CLI** | Relative to the directory where you ran `freedos_cli`  |
| **macOS app** | `~/Library/Containers/com.awohl.FreeDOS/Data/Documents/` |
| **iOS app** | Files app → FreeDOS folder |

See [docs/FILE_TRANSFER.md](docs/FILE_TRANSFER.md) for the full guide.

## Networking

iosFreeDOS emulates an NE2000 Ethernet adapter (DP8390-based) at I/O base 0x300,
IRQ 3. Use any standard NE2000 packet driver and TCP/IP stack.

```bash
# CLI: start with networking enabled
./freedos_cli -a fd/freedos.img -net
```

Inside DOS, load the packet driver and use mTCP or similar:

```
C:\DRIVERS> NE2000 0x60 3 0x300
C:\MTCP> SET MTCPCFG=C:\MTCP\MTCP.CFG
C:\MTCP> DHCP
C:\MTCP> FTP ftp.example.com
```

See [docs/NETWORKING.md](docs/NETWORKING.md) for the complete setup guide.

## Building

### Prerequisites

- **Xcode 15+** (for iOS/macOS app)
- **g++ with C++17** (for CLI)
- **NASM** (for assembling R.COM/W.COM)
- **XcodeGen** (for regenerating the Xcode project)

### CLI Build

```bash
make                  # Builds freedos_cli, test_emu88, dos/r.com, dos/w.com
make test_emu88      # Build and run CPU tests
make clean           # Remove build artifacts
```

### iOS / macOS App

```bash
xcodegen                   # Generate Xcode project from project.yml
open iosFreeDOS.xcodeproj      # Open in Xcode, select target, build
```

Or from the command line:

```bash
# iOS Simulator
xcodebuild -project iosFreeDOS.xcodeproj \
  -scheme iosFreeDOS \
  -destination 'platform=iOS Simulator,name=iPhone 16' \
  SYMROOT="$(pwd)/build" build

# macOS (Catalyst)
xcodebuild -project iosFreeDOS.xcodeproj \
  -scheme iosFreeDOS \
  -destination 'platform=macOS,variant=Mac Catalyst' \
  SYMROOT="$(pwd)/build" build
```

See [docs/BUILDING.md](docs/BUILDING.md) for full details.

## Project Structure

```
iosFreeDOS/                iOS/macOS app (SwiftUI + Obj-C++ bridge)
  Views/                   ContentView, TerminalView, HelpView,
                           EmulatorViewModel, MachineConfig
  Bridge/                  DOSEmulator.h/.mm — Obj-C++ bridge
src/                       C++ emulator core
  emu88.h / emu88.cc       8088/80186/286/386 CPU emulator
  emu88_pmode.cc           386 protected mode, V86, paging, exceptions
  emu88_mem.h/.cc          Memory subsystem (up to 64 MB)
  dos_machine.h/.cc        PC hardware: PIC, PIT, DMA, PPI, CMOS, NE2000
  dos_bios.cc              BIOS interrupts (INT 10h, 13h, 16h, etc.)
  dos_io.h                 Abstract I/O interface for platform portability
  ne2000.h / ne2000.cc     NE2000 (DP8390) NIC emulation
  main_cli.cc              Terminal-based CLI harness
  test_emu88.cc            CPU instruction tests (245 tests)
dos/                       DOS guest utilities
  r.asm / r.com            R.COM — read host file into DOS
  w.asm / w.com            W.COM — write DOS file to host
fd/                        FreeDOS boot disk images
scripts/                   Build and utility scripts
release_assets/            GitHub release assets (disk catalog, help files)
docs/                      Documentation
```

## Architecture

```
┌─────────────────────────────────────────┐
│  SwiftUI (ContentView, TerminalView)    │
│  EmulatorViewModel, ConfigManager       │
└──────────────┬──────────────────────────┘
               │ Obj-C bridge
┌──────────────▼──────────────────────────┐
│  DOSEmulator.mm  (dos_io_ios)           │
│  Disk I/O, video, mouse, file transfer  │
└──────────────┬──────────────────────────┘
               │ C++
┌──────────────▼──────────────────────────┐
│  dos_machine  →  emu88 (8088/386 CPU)   │
│  dos_bios     →  emu88_mem              │
│  ne2000       →  dos_io (abstract I/O)  │
└─────────────────────────────────────────┘
```

The C++ core (`src/`) is platform-independent. `dos_io` is an abstract
interface that platform bridges implement for disk access, video refresh,
host file I/O, and network I/O.

## CPU Compatibility

The emulator implements four CPU modes selectable at runtime:

| CPU | Features | Test Suite |
|-----|----------|------------|
| **8088** | Full 8088 instruction set | [SingleStepTests/8088](https://github.com/SingleStepTests/8088) — 100% pass (98.69% before SST-specific quirk fixes) |
| **80186** | PUSHA/POPA, ENTER/LEAVE, PUSH imm, IMUL imm, BOUND, INS/OUTS | — |
| **286** | Protected mode basics (LGDT, LIDT, LMSW), SMSW | Mostly passes — known edge cases with IMUL/IDIV sign-extension corner cases |
| **386** | Full 32-bit ALU, protected mode, V86, paging, DPMI support | [test386.asm](https://github.com/barotto/test386.asm) — 100% pass (POST 0xFF) |

### 386 Protected Mode

- **Descriptor tables**: GDT, LDT, IDT with full descriptor parsing
- **Privilege levels**: Ring 0–3 with CPL/DPL/RPL enforcement
- **Segment types**: Code (conforming/non-conforming), data (expand-up/down), system (TSS, call gates)
- **V86 mode**: IOPL-sensitive instructions trap to ring 0; segment register save/restore on transitions
- **Paging**: 2-level page tables, 4KB and 4MB (PSE) pages, U/S and R/W protection, accessed/dirty bits
- **Exceptions**: #GP, #SS, #NP, #TS, #PF with proper error codes; double/triple fault detection
- **System instructions**: LGDT, LIDT, LLDT, LTR, SGDT, SIDT, SLDT, STR, SMSW, LMSW, VERR, VERW, LAR, LSL, ARPL, CLTS, INVLPG, MOV CR0–CR4, MOV DR0–DR7, CPUID, RDTSC
- **Memory**: XMS 3.0 driver (up to 64 MB extended), A20 gate, CWSDPMI resident at boot

FreeDOS 1.4 (which requires a 386) boots and runs successfully.

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).
