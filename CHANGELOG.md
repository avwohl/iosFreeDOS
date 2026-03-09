# Changelog

## Version 1.0.0 (Build 4)

Initial release.

### Emulator Core
- 8088/80186/386 CPU emulator (real mode only — no protected mode, DPMI, or DOS extenders) with 245 unit tests
- FreeDOS 1.4 boots successfully (386 CPU detection passes)
- CGA, MDA, and Hercules display adapters
- INT 33h mouse driver with touch-to-mouse mapping
- Floppy, hard disk, and CD-ROM ISO image support
- NE2000 (DP8390) Ethernet adapter emulation
- Speed control: Full speed, IBM PC 4.77 MHz, IBM AT 8 MHz, Turbo 25 MHz

### File Transfer
- R.COM and W.COM utilities for host-to-DOS and DOS-to-host file transfer
- INT E0h host file service interface

### App Features
- Disk image catalog with download from GitHub releases
- Download disk images from any URL
- Named configuration profiles
- Create blank floppy and hard disk images
- Boot drive selection (floppy, hard disk, CD-ROM)
- In-app help browser with remote content
- Keyboard toolbar with Ctrl, Esc, Tab, arrow keys
- Copy/paste support
- Mac Catalyst support
