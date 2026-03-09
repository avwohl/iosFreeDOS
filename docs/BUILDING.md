# Building DOSEmu

## Prerequisites

| Tool | Required for | Install |
|------|-------------|---------|
| Xcode 15+ | iOS/macOS app | Mac App Store |
| XcodeGen | Regenerating Xcode project | `brew install xcodegen` |
| g++ (C++17) | CLI build | Xcode CLT or `brew install gcc` |
| NASM | Assembling R.COM/W.COM | `brew install nasm` |
| mtools | Building hard disk images | `brew install mtools` |
| Python 3 + Pillow | App icon generation (optional) | `pip3 install Pillow` |

## CLI Build

```bash
make
```

This produces:

- `dosemu_cli` — command-line emulator
- `test_emu88` — CPU instruction tests
- `dos/r.com` — host-to-DOS file transfer utility
- `dos/w.com` — DOS-to-host file transfer utility

To run tests:

```bash
./test_emu88
```

To clean:

```bash
make clean
```

## iOS / macOS App

### First-time setup

```bash
brew install xcodegen
xcodegen
open DOSEmu.xcodeproj
```

### After modifying project.yml or adding/removing source files

```bash
xcodegen
```

This regenerates the Xcode project from `project.yml`. Always run this after:

- Editing `project.yml`
- Adding or removing files in `DOSEmu/` or `src/`
- Changing build settings, deployment target, or entitlements

### Building from Xcode

1. Open `DOSEmu.xcodeproj`
2. Select your target (iPhone simulator, iPad, or My Mac)
3. Build and run (Cmd+R)

### Building from the command line

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

Always pass `SYMROOT="$(pwd)/build"` so the build output goes into `build/`
instead of conflicting with the `DOSEmu/` source directory.

## Hard Disk Image

To build a bootable 64 MB FAT16 hard disk image:

```bash
scripts/build_hdd.sh
```

This creates `fd/freedos_hd.img` with an MBR, partition table, and FreeDOS
kernel. Requires `mtools` and `fd/freedos.img` as the source floppy.

## App Icon

```bash
pip3 install Pillow
python3 scripts/gen_icon.py
```

This generates all required iOS and macOS App Store icon sizes from a
rendered retro DOS prompt image.

## Release Assets

Files in `release_assets/` are uploaded as GitHub release assets:

- `disks.xml` — disk image catalog fetched by the app
- `help_index.json` — help topic index for the in-app help browser
- `help_*.md` — help topic content (markdown)

When creating a release, upload all files from this directory.
