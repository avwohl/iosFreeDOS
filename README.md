# DOSEmu

An 8088/386 IBM PC emulator for iOS, macOS, and the command line. Runs FreeDOS
and other DOS-compatible operating systems on your iPhone, iPad, or Mac.

## Features

- **8088/80186/386 CPU emulator** written in C++ — executes real DOS binaries (real mode only — no protected mode, DPMI, or DOS extenders)
- **CGA, MDA, and Hercules** display adapters (configurable, including dual CGA+MDA)
- **NE2000 network adapter** — DP8390-based Ethernet with standard packet driver support
- **Host file transfer** — R.COM and W.COM utilities move files between DOS and the host
- **INT 33h mouse driver** with touch-to-mouse mapping on iOS
- **Keyboard input** with Ctrl key toolbar, Esc, Tab, arrow keys, copy/paste
- **Floppy, hard disk, and CD-ROM ISO** image support
- **Disk image catalog** — browse and download disk images from GitHub releases
- **Download from URL** — load floppy, HDD, or ISO images from any URL
- **Named configuration profiles** — save/load machine setups
- **Speed control** — Full speed, IBM PC 4.77 MHz, IBM AT 8 MHz, or Turbo 25 MHz

## Compatibility Note

The 386 CPU support is **real mode only**. Programs that require protected mode,
DPMI, or DOS extenders (DOS4GW, DOS32A, CWSDPMI, etc.) will not run. This
includes DOOM, Quake, Duke Nukem 3D, and most post-1993 games that use 32-bit
DOS extenders. Games requiring HIMEM.SYS or EMM386 (XMS/EMS memory managers)
also cannot run, as these require protected mode CPU transitions.

The emulator runs a wide range of real-mode DOS software from the late 1980s
through mid-1990s, including many classic Apogee, Epic MegaGames, and id
Software titles. See [GAMES.md](GAMES.md) for the full list of bundled games.

## Quick Start

### iOS / macOS App

1. Open the app and scroll to the **Disk Catalog** section
2. Download a FreeDOS floppy image and tap **Use as A:**
3. Tap **Start Emulator**

### Command-Line Interface

```bash
make
./dosemu_cli -a fd/freedos.img              # Boot FreeDOS from floppy
./dosemu_cli -c fd/freedos_hd.img -boot c   # Boot from hard disk
./dosemu_cli -cd image.iso -boot cd          # Boot from CD-ROM ISO
./dosemu_cli -a fd/freedos.img -net          # Boot with NE2000 networking
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
| **CLI** | Relative to the directory where you ran `dosemu_cli` |
| **macOS app** | `~/Library/Containers/com.awohl.FreeDOS/Data/Documents/` |
| **iOS app** | Files app → FreeDOS folder |

See [docs/FILE_TRANSFER.md](docs/FILE_TRANSFER.md) for the full guide.

## Networking

DOSEmu emulates an NE2000 Ethernet adapter (DP8390-based) at I/O base 0x300,
IRQ 3. Use any standard NE2000 packet driver and TCP/IP stack.

```bash
# CLI: start with networking enabled
./dosemu_cli -a fd/freedos.img -net
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
make                 # Builds dosemu_cli, test_emu88, dos/r.com, dos/w.com
make test_emu88      # Build and run CPU tests
make clean           # Remove build artifacts
```

### iOS / macOS App

```bash
xcodegen                   # Generate Xcode project from project.yml
open DOSEmu.xcodeproj      # Open in Xcode, select target, build
```

Or from the command line:

```bash
# iOS Simulator
xcodebuild -project DOSEmu.xcodeproj \
  -scheme DOSEmu \
  -destination 'platform=iOS Simulator,name=iPhone 16' \
  SYMROOT="$(pwd)/build" build

# macOS (Catalyst)
xcodebuild -project DOSEmu.xcodeproj \
  -scheme DOSEmu \
  -destination 'platform=macOS,variant=Mac Catalyst' \
  SYMROOT="$(pwd)/build" build
```

See [docs/BUILDING.md](docs/BUILDING.md) for full details.

## Project Structure

```
DOSEmu/                    iOS/macOS app (SwiftUI + Obj-C++ bridge)
  Views/                   ContentView, TerminalView, HelpView
  Bridge/                  DOSEmulator.h/.mm — Obj-C++ bridge to C++ core
src/                       C++ emulator core
  emu88.h / emu88.cc       8088/80186/386 CPU emulator
  emu88_mem.h/.cc          Memory subsystem (up to 16 MB)
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

The emulator implements the full 8088 instruction set plus:

- **80186**: PUSHA/POPA, ENTER/LEAVE, PUSH imm, IMUL imm, BOUND, INS/OUTS
- **386 (real mode)**: 32-bit operand/address prefixes, full 32-bit ALU,
  string operations, MOVZX/MOVSX, SETcc, BT/BTS/BTR/BTC, BSF/BSR, BSWAP,
  SHLD/SHRD, CMPXCHG, XADD, and system instructions (LGDT, LIDT, MOV CRn)

FreeDOS 1.4 (which requires a 386) boots and runs successfully.

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).
