#!/bin/bash
# Build a bootable FreeDOS hard disk image
# Creates a FAT16 HDD image with MBR + partition table

set -e

IMGDIR="$(cd "$(dirname "$0")/.." && pwd)"
OUTIMG="$IMGDIR/fd/freedos_hd.img"
BOOTIMG="$IMGDIR/fd/x86BOOT.img"
SRCIMG="$IMGDIR/fd/freedos.img"

# Geometry: 16 heads, 63 sectors/track
HEADS=16
SPT=63
CYLS=200  # ~98MB
TOTAL_SECTORS=$((CYLS * HEADS * SPT))
PART_START=63  # First partition starts at sector 63
PART_SECTORS=$((TOTAL_SECTORS - PART_START))
IMG_SIZE=$((TOTAL_SECTORS * 512))

echo "Creating ${IMG_SIZE} byte ($((IMG_SIZE/1024/1024))MB) hard disk image..."
echo "Geometry: C=$CYLS H=$HEADS S=$SPT = $TOTAL_SECTORS sectors"

# 1. Create blank image
dd if=/dev/zero of="$OUTIMG" bs=512 count=$TOTAL_SECTORS 2>/dev/null

# 2. Write MBR boot code + partition table
# The MBR boot code loads the partition boot sector
# We write a minimal MBR that:
#   - Finds the active partition
#   - Reads its boot sector (LBA=63) to 0x7C00
#   - Jumps to it
python3 -c "
import struct, sys

# Standard MBR boot code (simplified - loads active partition's boot sector)
# This is the standard IBM PC MBR logic
code = bytearray(446)

# Bootstrap code: scan partition table, find active, read boot sector
boot = bytes([
    0xFA,                   # CLI
    0x33, 0xC0,             # XOR AX,AX
    0x8E, 0xD0,             # MOV SS,AX
    0xBC, 0x00, 0x7C,       # MOV SP,0x7C00
    0x8E, 0xD8,             # MOV DS,AX
    0x8E, 0xC0,             # MOV ES,AX
    0xFB,                   # STI
    0xBE, 0xBE, 0x07,       # MOV SI,0x7BE (first partition entry)
    0xB9, 0x04, 0x00,       # MOV CX,4 (4 partitions)
    # Loop: check if active
    0x80, 0x3C, 0x80,       # CMP byte [SI],0x80
    0x74, 0x09,             # JZ found
    0x83, 0xC6, 0x10,       # ADD SI,16
    0xE2, 0xF5,             # LOOP
    # No active partition - halt
    0xCD, 0x18,             # INT 18h (no boot)
    0xEB, 0xFE,             # JMP $
    # Found active partition
    0x8B, 0x44, 0x08,       # MOV AX,[SI+8] (LBA low word)
    0x8B, 0x54, 0x0A,       # MOV DX,[SI+10] (LBA high word, should be 0)
    # Use INT 13h extended read (AH=42h) with DAP
    0xBB, 0x00, 0x7C,       # MOV BX,0x7C00
    0x66, 0x50,             # PUSH EAX (push LBA)
    # Build DAP on stack
    0x06,                   # PUSH ES
    0x53,                   # PUSH BX (buffer)
    0x6A, 0x01,             # PUSH 1 (sector count)
    0x6A, 0x10,             # PUSH 16 (DAP size)
    # Actually, let's use CHS instead for 8088 compat
])

# Simpler: use CHS read from partition entry
boot2 = bytes([
    0xFA,                   # CLI
    0x33, 0xC0,             # XOR AX,AX
    0x8E, 0xD0,             # MOV SS,AX
    0xBC, 0x00, 0x7C,       # MOV SP,0x7C00
    0x8E, 0xD8,             # MOV DS,AX
    0x8E, 0xC0,             # MOV ES,AX
    0xFB,                   # STI
    0xBE, 0xBE, 0x7D,       # MOV SI,0x7DBE (partition table at 7C00+1BE)
    0xB9, 0x04, 0x00,       # MOV CX,4
    # scan_loop:
    0x80, 0x3C, 0x80,       # CMP byte [SI],0x80
    0x74, 0x09,             # JZ found (skip 9 bytes)
    0x83, 0xC6, 0x10,       # ADD SI,16
    0xE2, 0xF5,             # LOOP scan_loop
    0xCD, 0x18,             # INT 18h
    0xEB, 0xFE,             # JMP $
    # found: read boot sector using CHS from partition entry
    0x8A, 0x74, 0x01,       # MOV DH,[SI+1] (start head)
    0x8B, 0x4C, 0x02,       # MOV CX,[SI+2] (start cyl/sec)
    0x8A, 0x54, 0x00,       # MOV DL,[SI+0] ... no, DL should be drive
    0xB2, 0x80,             # MOV DL,0x80 (first hard disk)
    0xBB, 0x00, 0x7C,       # MOV BX,0x7C00
    0xB8, 0x01, 0x02,       # MOV AX,0x0201 (read 1 sector)
    0xCD, 0x13,             # INT 13h
    0x72, 0xFE,             # JC $ (hang on error)
    0xEA, 0x00, 0x7C, 0x00, 0x00,  # JMP FAR 0000:7C00
])

code[:len(boot2)] = boot2

# Partition table at offset 446
# Entry 1: active FAT16 partition
part = bytearray(16)
part[0] = 0x80  # Active/bootable

# Start CHS: head=1, cyl=0, sec=1
part[1] = 1     # Start head
part[2] = 1     # Start sector (bits 0-5) + cyl high (bits 6-7)
part[3] = 0     # Start cylinder low

# Partition type
part[4] = 0x06  # FAT16 > 32MB (or 0x04 for < 32MB)

# End CHS
end_cyl = $CYLS - 1
end_head = $HEADS - 1
end_sec = $SPT
part[5] = end_head & 0xFF
part[6] = (end_sec & 0x3F) | ((end_cyl >> 2) & 0xC0)
part[7] = end_cyl & 0xFF

# LBA start and size
struct.pack_into('<I', part, 8, $PART_START)
struct.pack_into('<I', part, 12, $PART_SECTORS)

# Build MBR
mbr = bytearray(512)
mbr[:len(code)] = code
mbr[446:462] = part
mbr[510] = 0x55
mbr[511] = 0xAA

sys.stdout.buffer.write(mbr)
" > /tmp/mbr.bin

dd if=/tmp/mbr.bin of="$OUTIMG" bs=512 count=1 conv=notrunc 2>/dev/null

# 3. Format the partition as FAT16
# Extract the partition, format it, put it back
PART_SIZE=$((PART_SECTORS * 512))

dd if=/dev/zero of=/tmp/partition.img bs=512 count=$PART_SECTORS 2>/dev/null
mkfs.fat -F 16 -n "FREEDOS" -h $PART_START -S 512 -s 4 /tmp/partition.img
dd if=/tmp/partition.img of="$OUTIMG" bs=512 seek=$PART_START conv=notrunc 2>/dev/null

# 4. Set up mtools config for the partition
export MTOOLS_SKIP_CHECK=1
PART_OFFSET=$((PART_START * 512))
cat > /tmp/mtoolsrc_hd << EOF
mtools_skip_check=1
drive c: file="$OUTIMG" offset=$PART_OFFSET
EOF
export MTOOLSRC=/tmp/mtoolsrc_hd

# 5. Install FreeDOS kernel
echo "Installing FreeDOS kernel and utilities..."

# Copy KERNEL.SYS from boot floppy
mcopy -i "$SRCIMG" ::KERNEL.SYS /tmp/KERNEL.SYS
mcopy -D o /tmp/KERNEL.SYS c:

# Install SYS boot sector - we need FreeDOS's SYS command to write the boot sector
# For now, let's use a FreeDOS-compatible boot sector
# Actually, we'll write the boot sector from the floppy image as a starting point
# and patch the BPB to match our partition

# Copy all FreeDOS files
mmd c:/FREEDOS 2>/dev/null || true
mmd c:/FREEDOS/BIN 2>/dev/null || true
mmd c:/FREEDOS/NLS 2>/dev/null || true
mmd c:/FREEDOS/HELP 2>/dev/null || true

# Copy all BIN files
for f in $(mdir -b -i "$SRCIMG" ::/FREEDOS/BIN/ 2>/dev/null | grep -v "^$"); do
    basename=$(echo "$f" | sed 's|.*/||')
    mcopy -i "$SRCIMG" "::FREEDOS/BIN/$basename" /tmp/"$basename" 2>/dev/null || true
    mcopy -D o /tmp/"$basename" "c:/FREEDOS/BIN/$basename" 2>/dev/null || true
done

# Copy COMMAND.COM to root
mcopy -i "$SRCIMG" ::/FREEDOS/BIN/COMMAND.COM /tmp/COMMAND.COM
mcopy -D o /tmp/COMMAND.COM c:

# Copy NLS files
for f in $(mdir -b -i "$SRCIMG" ::/FREEDOS/NLS/ 2>/dev/null | grep -v "^$"); do
    basename=$(echo "$f" | sed 's|.*/||')
    mcopy -i "$SRCIMG" "::FREEDOS/NLS/$basename" /tmp/"$basename" 2>/dev/null || true
    mcopy -D o /tmp/"$basename" "c:/FREEDOS/NLS/$basename" 2>/dev/null || true
done

# Copy extra utilities from boot image
for f in SETUP.BAT; do
    mcopy -i "$BOOTIMG" "::$f" /tmp/"$f" 2>/dev/null || true
done

# 6. Create CONFIG.SYS
cat > /tmp/FDCONFIG.SYS << 'CFGEOF'
LASTDRIVE=Z
FILES=40
BUFFERS=20
DOS=HIGH
SHELL=C:\COMMAND.COM C:\ /E:1024 /P
CFGEOF
mcopy -D o /tmp/FDCONFIG.SYS c:

# 7. Create AUTOEXEC.BAT
cat > /tmp/AUTOEXEC.BAT << 'BATEOF'
@ECHO OFF
SET DOSDIR=C:\FREEDOS
SET PATH=C:\FREEDOS\BIN;C:\GAMES
SET TEMP=C:\TEMP
SET DIRCMD=/P /OGN
PROMPT $P$G
IF NOT EXIST C:\TEMP\NUL MD C:\TEMP
CLS
ECHO.
ECHO FreeDOS 1.4 - DOSEmu
ECHO.
BATEOF
mcopy -D o /tmp/AUTOEXEC.BAT c:

# 8. Create directories
mmd c:/TEMP 2>/dev/null || true
mmd c:/GAMES 2>/dev/null || true
mmd c:/APPS 2>/dev/null || true

# 8a. Install R.COM and W.COM (host file transfer utilities)
if [ -f "$IMGDIR/dos/r.com" ]; then
    mcopy -D o "$IMGDIR/dos/r.com" "c:/FREEDOS/BIN/R.COM"
    mcopy -D o "$IMGDIR/dos/w.com" "c:/FREEDOS/BIN/W.COM"
    echo "Installed R.COM and W.COM"
fi

# 8b. Install games
if [ -f "$IMGDIR/scripts/install_games.sh" ]; then
    echo "Installing games..."
    bash "$IMGDIR/scripts/install_games.sh" 2>&1 | tail -3
fi

# 9. Install the FreeDOS boot sector on the partition
# We need to write a proper FAT16 boot sector that loads KERNEL.SYS
# Extract the boot sector from the floppy and use it as a template
dd if="$SRCIMG" of=/tmp/floppy_boot.bin bs=512 count=1 2>/dev/null

# The FreeDOS boot sector on the floppy loads KERNEL.SYS
# We need to patch the BPB (BIOS Parameter Block) to match our partition
# but keep the boot code
python3 -c "
import struct, sys

# Read floppy boot sector (has FreeDOS boot code)
floppy = open('/tmp/floppy_boot.bin', 'rb').read()

# Read current partition boot sector (has correct BPB from mkfs.fat)
partition = open('$OUTIMG', 'rb')
partition.seek($PART_START * 512)
part_boot = bytearray(partition.read(512))
partition.close()

# The BPB is at offset 0x0B to 0x3E in a FAT16 boot sector
# Keep the BPB from mkfs.fat, replace the boot code from FreeDOS floppy
# Jump instruction at offset 0 (3 bytes)
# OEM name at offset 3 (8 bytes)
# BPB at offset 0x0B

# Copy the FreeDOS boot code (everything except BPB)
result = bytearray(part_boot)

# Copy jump instruction from floppy
result[0:3] = floppy[0:3]

# Copy OEM name
result[3:11] = floppy[3:11]

# BPB stays from mkfs.fat (offsets 0x0B through 0x3D)
# But we need to patch the hidden sectors field (offset 0x1C, 4 bytes)
# to match the partition offset
struct.pack_into('<I', result, 0x1C, $PART_START)

# Copy boot code from floppy (offset 0x3E onwards)
result[0x3E:510] = floppy[0x3E:510]

# Signature
result[510] = 0x55
result[511] = 0xAA

sys.stdout.buffer.write(bytes(result))
" > /tmp/new_boot.bin

dd if=/tmp/new_boot.bin of="$OUTIMG" bs=512 seek=$PART_START count=1 conv=notrunc 2>/dev/null

# 10. Patch boot sector FAT12 cluster chain code to FAT16
# The FreeDOS floppy boot sector uses FAT12 logic (cluster*3/2, 12-bit entries).
# FAT16 needs cluster*2, 16-bit entries, 0xFFF8 end marker.
echo "Patching boot sector for FAT16..."
python3 -c "
import sys
data = bytearray(open('$OUTIMG', 'rb').read())
boot_off = $PART_START * 512

# FAT12 cluster chain code at boot sector offset 0xFC (24 bytes):
#   STOSW; MOV SI,AX; ADD SI,SI; ADD SI,AX; SHR SI,1; LODSW;
#   JNC+4; MOV CL,4; SHR AX,CL; AND AH,0xF; CMP AX,0xFF8; JC back
fat12 = bytes([0xAB,0x89,0xC6,0x01,0xF6,0x01,0xC6,0xD1,0xEE,0xAD,
               0x73,0x04,0xB1,0x04,0xD3,0xE8,0x80,0xE4,0x0F,0x3D,
               0xF8,0x0F,0x72,0xE8])
actual = bytes(data[boot_off+0xFC : boot_off+0xFC+24])
if actual != fat12:
    print('WARNING: Boot sector bytes differ, skipping FAT16 patch')
    sys.exit(0)

# FAT16 cluster chain code (11 bytes + 13 NOPs):
#   STOSW; MOV SI,AX; ADD SI,SI; LODSW; CMP AX,0xFFF8; JC back; NOPs
fat16 = bytes([0xAB,0x89,0xC6,0x01,0xF6,0xAD,0x3D,0xF8,0xFF,
               0x72,0xF5] + [0x90]*13)
data[boot_off+0xFC : boot_off+0xFC+24] = fat16
open('$OUTIMG', 'wb').write(data)
print('  FAT16 cluster chain patch applied')
"

echo ""
echo "Disk image created: $OUTIMG"
echo "Size: $(ls -lh "$OUTIMG" | awk '{print $5}')"
echo ""
echo "Contents:"
mdir c: 2>/dev/null || echo "(mdir failed - check mtoolsrc)"
echo ""
echo "Free space:"
minfo c: 2>/dev/null | grep -i "free\|total" || true

# 11. Create dev copy for testing (won't clobber release data if app writes to it)
DEVIMG="$IMGDIR/fd/freedos_hd_dev.img"
cp "$OUTIMG" "$DEVIMG"
echo ""
echo "Dev copy: $DEVIMG"
