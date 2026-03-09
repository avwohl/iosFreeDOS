#!/bin/bash
# Install games onto the FreeDOS hard disk image
# Run after build_hdd.sh

set -e

IMGDIR="$(cd "$(dirname "$0")/.." && pwd)"
OUTIMG="$IMGDIR/fd/freedos_hd.img"
GAMEDIR="$IMGDIR/fd/games"

if [ ! -f "$OUTIMG" ]; then
  echo "Error: $OUTIMG not found. Run build_hdd.sh first."
  exit 1
fi

# Set up mtools for the partition (sector 63)
export MTOOLS_SKIP_CHECK=1
PART_OFFSET=$((63 * 512))
cat > /tmp/mtoolsrc_hd << EOF
mtools_skip_check=1
drive c: file="$OUTIMG" offset=$PART_OFFSET
EOF
export MTOOLSRC=/tmp/mtoolsrc_hd

# Helper: recursively copy a directory to the image
copy_game() {
  local src="$1" dest="$2"
  mmd "c:/GAMES/$dest" 2>/dev/null || true

  # Copy files in the source directory
  for f in "$src"/*; do
    if [ -f "$f" ]; then
      local bn=$(basename "$f")
      # Skip non-game files
      case "$bn" in
        *.txt|*.TXT|*.md|*.nfo|*.NFO|*.diz|*.DIZ|*.1st|*.doc|*.DOC) continue ;;
        run.bat|dosbox*.conf|*.ba1) continue ;;
      esac
      mcopy -D o "$f" "c:/GAMES/$dest/$bn" 2>/dev/null || true
    elif [ -d "$f" ]; then
      local subdir=$(basename "$f")
      # Skip DOSBox/metadata dirs
      case "$subdir" in
        Documentation|__MACOSX|.git) continue ;;
      esac
      mmd "c:/GAMES/$dest/$subdir" 2>/dev/null || true
      for sf in "$f"/*; do
        [ -f "$sf" ] && mcopy -D o "$sf" "c:/GAMES/$dest/$subdir/$(basename "$sf")" 2>/dev/null || true
      done
    fi
  done
}

echo "Installing games to $OUTIMG..."

# Games with nested archive.org directory structures
[ -d "$GAMEDIR/biomenace/BioMenac" ] && {
  echo "  Bio Menace..."
  copy_game "$GAMEDIR/biomenace/BioMenac" "BIOMENAC"
}

[ -d "$GAMEDIR/skyroads" ] && {
  echo "  SkyRoads..."
  copy_game "$GAMEDIR/skyroads" "SKYROADS"
}

[ -d "$GAMEDIR/godofthunder/godthund" ] && {
  echo "  God of Thunder..."
  copy_game "$GAMEDIR/godofthunder/godthund" "GOT"
}

[ -d "$GAMEDIR/darkages/DarkAges" ] && {
  echo "  Dark Ages..."
  copy_game "$GAMEDIR/darkages/DarkAges" "DARKAGES"
}

[ -d "$GAMEDIR/jill/JillJung" ] && {
  echo "  Jill of the Jungle..."
  copy_game "$GAMEDIR/jill/JillJung" "JILL"
}

[ -d "$GAMEDIR/xargon/Xargon" ] && {
  echo "  Xargon..."
  copy_game "$GAMEDIR/xargon/Xargon" "XARGON"
}

[ -d "$GAMEDIR/kiloblaster/Kiloblas" ] && {
  echo "  Kiloblaster..."
  copy_game "$GAMEDIR/kiloblaster/Kiloblas" "KILOBLST"
}

[ -d "$GAMEDIR/majorstryker/MajorStr/MAJOR" ] && {
  echo "  Major Stryker..."
  copy_game "$GAMEDIR/majorstryker/MajorStr/MAJOR" "STRYKER"
}

[ -d "$GAMEDIR/aliencarnage/AlienCar" ] && {
  echo "  Alien Carnage..."
  copy_game "$GAMEDIR/aliencarnage/AlienCar" "ALIENCAR"
}

[ -d "$GAMEDIR/duke1" ] && {
  echo "  Duke Nukem 1..."
  # Duke has nested DUKE/ subdir
  if [ -d "$GAMEDIR/duke1/DUKE" ]; then
    copy_game "$GAMEDIR/duke1/DUKE" "DUKE1"
  else
    copy_game "$GAMEDIR/duke1" "DUKE1"
  fi
}

[ -d "$GAMEDIR/kroz" ] && {
  echo "  Kingdom of Kroz..."
  copy_game "$GAMEDIR/kroz" "KROZ"
}

[ -d "$GAMEDIR/liero" ] && {
  echo "  Liero..."
  copy_game "$GAMEDIR/liero" "LIERO"
}

[ -d "$GAMEDIR/supaplex" ] && {
  echo "  Supaplex..."
  copy_game "$GAMEDIR/supaplex" "SUPAPLEX"
}

echo ""
echo "Games installed. Contents of C:\\GAMES:"
mdir c:/GAMES 2>/dev/null || true
echo ""
echo "Free space:"
minfo c: 2>/dev/null | grep -i "free\|total" || true
