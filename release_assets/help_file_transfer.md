# File Transfer with R.COM and W.COM

FreeDOS includes two utilities for moving files between DOS and your device.

## R.COM — Read from Host

Copies a file from your device into a DOS file:

```
A:\> R hostfile.txt DOSFILE.TXT
File transferred.
```

## W.COM — Write to Host

Copies a DOS file out to your device:

```
A:\> W DOSFILE.TXT hostfile.txt
File transferred.
```

## Where Are Host Files?

**iOS (iPhone / iPad):**
Open the **Files** app, go to **On My iPhone** (or iPad), then **FreeDOS**. This is where R.COM reads from and W.COM writes to. You can use AirDrop, iCloud, or any sharing method to get files into this folder.

**Mac (Catalyst):**
Host files are in the app's container:
`~/Library/Containers/com.awohl.FreeDOS/Data/Documents/`

Open Finder, press Cmd+Shift+G, and paste that path.

**Command Line (dosemu_cli):**
Host paths are relative to the directory where you launched `dosemu_cli`. Absolute paths also work.

## Tips

- DOS filenames follow the 8.3 convention: `MYFILE.TXT`, not `My Long File.txt`
- R.COM creates (or overwrites) the DOS file
- W.COM creates (or overwrites) the host file
- Files transfer one byte at a time — large files take a moment
- Both utilities are on the FreeDOS boot floppy (drive A:)
