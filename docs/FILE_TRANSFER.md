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

The meaning of the host path depends on which platform you are running DOSEmu on.

### Command-Line Interface (`dosemu_cli`)

Host paths are relative to the **current working directory** where you launched
`dosemu_cli`. If you ran `./dosemu_cli` from `/home/user/dos`, then:

```
A:\> R myfile.txt MYFILE.TXT
```

reads `/home/user/dos/myfile.txt`.

You can also use absolute paths:

```
A:\> R /tmp/data.bin DATA.BIN
```

### macOS App (Mac Catalyst)

The macOS sandboxed app stores host files in its container:

```
~/Library/Containers/com.awohl.FreeDOS/Data/Documents/
```

To transfer a file **into** DOS:

1. Open Finder and press **Cmd+Shift+G**
2. Paste: `~/Library/Containers/com.awohl.FreeDOS/Data/Documents/`
3. Copy your file into this folder
4. In DOS, run: `R myfile.txt DOSNAME.TXT`

To transfer a file **out of** DOS:

1. In DOS, run: `W DOSFILE.TXT output.txt`
2. Find `output.txt` in the same container folder in Finder

### iOS App (iPhone / iPad)

On iOS, the app's Documents folder is accessible through the **Files** app:

1. Open the **Files** app on your device
2. Navigate to **On My iPhone** (or iPad) → **FreeDOS**
3. This is the folder where R.COM reads from and W.COM writes to

To transfer a file **into** DOS:

1. Place the file in the FreeDOS folder using Files, AirDrop, or any other method
2. In DOS, run: `R myfile.txt DOSNAME.TXT`

To transfer a file **out of** DOS:

1. In DOS, run: `W DOSFILE.TXT output.txt`
2. Open the Files app and find `output.txt` in the FreeDOS folder
3. Share, AirDrop, or copy it wherever you need

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
