# File Transfer with R.COM and W.COM

FreeDOS includes two small DOS utilities for transferring files between the
guest DOS environment and the host operating system.

- **R.COM** — **R**ead a file from the host into a DOS file
- **W.COM** — **W**rite a DOS file out to the host

Both programs use INT E0h, a custom BIOS interrupt that communicates with the
emulator's host file I/O layer.

## Usage

### Reading a file from the host into DOS

```
A:\> R <host-path> <dos-file>
```

Example: copy `readme.txt` from the host into `C:\DOCS\README.TXT`:

```
A:\> R readme.txt C:\DOCS\README.TXT
File transferred.
```

### Writing a DOS file out to the host

```
A:\> W <dos-file> <host-path>
```

Example: copy `C:\OUTPUT.DAT` to the host as `output.dat`:

```
A:\> W C:\OUTPUT.DAT output.dat
File transferred.
```

## Where Are Host Files?

The meaning of the host path depends on which platform you are running
iosFreeDOS on.

### iOS App (iPhone / iPad)

Every iOS app runs in its own **sandbox** — a private directory that no other
app can see. Inside the sandbox, the app's **Documents** directory is the
only place R.COM and W.COM can reach.

When you give R.COM or W.COM a **bare filename** (no `/` prefix), the
emulator resolves it relative to the Documents directory:

```
A:\> W DOSFILE.TXT output.txt
```

`output.txt` is created at `<sandbox>/Documents/output.txt`.

An **absolute path** (starting with `/`) is passed through as-is, but iOS
still enforces the sandbox — you can only access files inside the app's own
container. In practice, absolute paths are not useful on iOS.

**Finding the Documents folder on your device:**

1. Open the **Files** app
2. Navigate to **On My iPhone** (or iPad) → **FreeDOS**
3. This is the Documents directory — the same place R.COM and W.COM use

To transfer a file **into** DOS:

1. Place the file in the FreeDOS folder using Files, AirDrop, or any sharing
   method
2. In DOS, run: `R myfile.txt DOSNAME.TXT`

To transfer a file **out of** DOS:

1. In DOS, run: `W DOSFILE.TXT output.txt`
2. Open the Files app and find `output.txt` in the FreeDOS folder
3. Share, AirDrop, or copy it wherever you need

**Note:** The Disks subfolder (`Documents/Disks/`) is also inside this
directory. You can read/write files there too (e.g., `R Disks/boot.img
BOOT.IMG`), but be careful not to overwrite your disk images.

### macOS App (Mac Catalyst)

The macOS sandboxed app stores host files in its container:

```
~/Library/Containers/com.awohl.FreeDOS/Data/Documents/
```

Bare filenames resolve to this directory. To find it:

1. Open Finder and press **Cmd+Shift+G**
2. Paste: `~/Library/Containers/com.awohl.FreeDOS/Data/Documents/`

To transfer a file **into** DOS:

1. Copy your file into the folder above
2. In DOS, run: `R myfile.txt DOSNAME.TXT`

To transfer a file **out of** DOS:

1. In DOS, run: `W DOSFILE.TXT output.txt`
2. Find `output.txt` in the same container folder in Finder

### Command-Line Interface (`freedos_cli`)

Host paths are relative to the **current working directory** where you
launched `freedos_cli`. If you ran `./freedos_cli` from `/home/user/dos`,
then:

```
A:\> R myfile.txt MYFILE.TXT
```

reads `/home/user/dos/myfile.txt`.

You can also use absolute paths:

```
A:\> R /tmp/data.bin DATA.BIN
```

The CLI is not sandboxed — it can access any file the user account can.

## iOS Sandboxing and Path Names

On iOS (and Mac Catalyst), each app gets its own container directory. The
full path looks like:

```
/var/mobile/Containers/Data/Application/<UUID>/Documents/
```

The `<UUID>` is assigned by iOS at install time and changes if you delete
and reinstall the app. You never need to know this path — just use bare
filenames and they resolve to this directory automatically.

What this means in practice:

- **Bare filenames** (`output.txt`) — resolve to the Documents directory.
  This is what you should normally use.
- **Absolute paths** (`/tmp/foo`) — will fail on iOS because `/tmp` is
  outside the sandbox. Only paths inside the app's container work.
- **Subdirectories** (`subdir/file.txt`) — work if the subdirectory exists
  inside Documents.
- **No drive letters** — the host path is a native path, not a DOS path.
  Don't use `C:\` syntax for the host side.

## Tips

- DOS filenames follow the 8.3 convention (e.g., `MYFILE.TXT`, not
  `My Long Filename.txt`). The host side has no such restriction.
- Files are transferred one byte at a time. Large files (multi-megabyte)
  will take a noticeable amount of time.
- R.COM **creates** the DOS file (overwrites if it exists). W.COM **reads**
  the DOS file and creates the host file.
- If you get "Cannot open host file", check that the file exists and the
  path is correct for your platform.
- R.COM and W.COM are on the FreeDOS boot floppy. If you boot from hard
  disk, copy them to C: or add A: to your PATH.

## Technical Details

R.COM and W.COM are tiny COM-format programs (under 600 bytes each),
assembled from NASM source in `dos/r.asm` and `dos/w.asm`.

They communicate with the emulator through **INT E0h**:

| AH | Function | Parameters | Returns |
|----|----------|------------|---------|
| 01 | Open host file for reading | DS:DX → ASCIIZ path | CF=0 success, CF=1 error |
| 02 | Open host file for writing | DS:DX → ASCIIZ path | CF=0 success, CF=1 error |
| 03 | Read one byte | — | AL=byte, CF=1 on EOF |
| 04 | Write one byte | DL=byte | — |
| 05 | Close file | AL=0 read, AL=1 write | — |
