# FreeDOS License Compatibility

## Emulator License

The DOSEmu emulator source code is licensed under the **GNU General Public License v3.0** (GPLv3).

## FreeDOS License

The FreeDOS kernel and most FreeDOS packages are licensed under the **GNU General Public License v2 or later** (GPLv2+).

- FreeDOS kernel source: https://github.com/FDOS/kernel
- FreeDOS project: https://www.freedos.org

## Compatibility

GPLv2+ ("version 2 or any later version") is forward-compatible with GPLv3. When distributing GPLv2+ code alongside GPLv3 code, the combined distribution can be made under GPLv3, since GPLv3 is a "later version" permitted by the GPLv2+ license.

## How We Distribute FreeDOS

FreeDOS disk images are distributed as **separate works** from the emulator:

1. The emulator (GPLv3) is the iOS/macOS app itself
2. FreeDOS disk images (GPLv2+) are downloaded on-demand from GitHub Releases
3. The emulator runs FreeDOS as guest software — they are not linked or combined

This is analogous to how QEMU or VirtualBox can run any operating system. The emulator and the guest OS remain separate works under their respective licenses.

## GPL Compliance

To comply with the GPL for FreeDOS distribution:

- **Source availability**: FreeDOS source code is publicly available at https://github.com/FDOS/kernel and https://github.com/fdos
- **License notice**: The app's About screen identifies FreeDOS as GPLv2+ and links to the source
- **No modifications**: We distribute unmodified FreeDOS binaries; no modifications require disclosure

## FreeDOS Packages

FreeDOS includes packages under various licenses:

| Component | License |
|-----------|---------|
| FreeDOS Kernel | GPLv2+ |
| FreeCOM (command.com) | GPLv2+ |
| FDISK, FORMAT, etc. | GPLv2+ |
| EDIT | GPLv2+ |

All core FreeDOS components we distribute are GPLv2+, which is compatible with our GPLv3 license.

## References

- [FreeDOS kernel license](https://github.com/FDOS/kernel) — "released under the GPL v2 or later"
- [GPLv3 compatibility FAQ](https://www.gnu.org/licenses/gpl-faq.html#AllCompatibility)
- [FreeDOS project](https://www.freedos.org)
