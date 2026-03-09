# Bundled Games - Hardware Notes

All games tested with DOSEmu (386 real-mode CPU, VGA adapter, 507K conventional memory).
The 386 CPU emulation is **real mode only** -- no protected mode, DPMI, or DOS extenders.

## Working Games (23)

### Duke Nukem 1 (Apogee, 1991)
- **Directory:** `\GAMES\DUKE1`
- **Run:** `DN1`
- **Video:** EGA/VGA (mode 13h)
- **Memory:** Needs ~520K. Works with optimized FDCONFIG.SYS (507K free).
- **Notes:** Shareware Episode 1. Side-scrolling platformer.

### Dark Ages (Apogee, 1991)
- **Directory:** `\GAMES\DARKAGES\1`
- **Run:** `DA1`
- **Video:** EGA
- **Notes:** Shareware Episode 1. Side-scrolling platformer.

### Kingdom of Kroz (Apogee, 1990)
- **Directory:** `\GAMES\KROZ`
- **Run:** `KINGDOM`
- **Video:** Text mode (CGA/MDA)
- **Notes:** Freeware. Asks "Color or Monochrome?" at startup. ASCII dungeon crawler.

### SkyRoads (Bluemoon Interactive, 1993)
- **Directory:** `\GAMES\SKYROADS`
- **Run:** `SKYROADS`
- **Video:** EGA/VGA
- **Notes:** Freeware. 3D road racing/jumping game.

### Supaplex (Digital Integration, 1991)
- **Directory:** `\GAMES\SUPAPLEX`
- **Run:** `SPFIX63`
- **Video:** VGA (mode 13h)
- **Notes:** Freeware. Boulder Dash clone with circuit board theme.

### Bio Menace (Apogee, 1993)
- **Directory:** `\GAMES\BIOMENAC`
- **Run:** `BMENACE1`
- **Video:** CGA/EGA/VGA (auto-detected)
- **Sound:** PC Speaker, Sound Blaster/AdLib detected
- **Notes:** Freeware. Side-scrolling shooter by Jim Norwood.

### God of Thunder (Adept Software/Software Creations, 1993)
- **Directory:** `\GAMES\GOT`
- **Run:** `GOT`
- **Video:** VGA required (mode 13h)
- **Notes:** Freeware. Top-down puzzle/action adventure.

### Jill of the Jungle (Epic MegaGames, 1992)
- **Directory:** `\GAMES\JILL`
- **Run:** `JILL1`
- **Video:** VGA 256-color (auto-detected)
- **Notes:** Shareware Episode 1. Side-scrolling platformer by Tim Sweeney.

### Xargon (Epic MegaGames, 1994)
- **Directory:** `\GAMES\XARGON`
- **Run:** `XARGON`
- **Video:** VGA (40-column mode)
- **Notes:** Freeware. Side-scrolling platformer, successor to Jill of the Jungle.

### Major Stryker (Apogee, 1993)
- **Directory:** `\GAMES\STRYKER`
- **Run:** `STRYKER`
- **Video:** EGA/VGA
- **Notes:** Freeware. Vertical scrolling shoot-em-up.

### Alien Carnage / Halloween Harry (SubZero Software, 1994)
- **Directory:** `\GAMES\ALIENCAR`
- **Run:** `CARNAGE`
- **Video:** VGA (40-column mode)
- **Notes:** Freeware. Side-scrolling platformer/shooter.

### Commander Keen 1: Marooned on Mars (id Software, 1990)
- **Directory:** `\GAMES\KEEN1`
- **Run:** `KEEN1`
- **Video:** EGA
- **Memory:** ~300K
- **Notes:** Shareware Episode 1. Classic side-scrolling platformer by id Software.

### Commander Keen 4: Secret of the Oracle (id Software, 1991)
- **Directory:** `\GAMES\KEEN4`
- **Run:** `KEEN4E`
- **Video:** EGA
- **Memory:** ~350K
- **Notes:** Shareware Episode 1. Widely regarded as the best Keen game.

### Jetpack (Software Creations, 1993)
- **Directory:** `\GAMES\JETPACK`
- **Run:** `JETPACK`
- **Video:** VGA
- **Notes:** Freeware (released free 1998). Puzzle-platformer with level editor.

### ZZT (Epic MegaGames, 1991)
- **Directory:** `\GAMES\ZZT`
- **Run:** `ZZT`
- **Video:** Text mode
- **Notes:** Freeware. Text-mode adventure/puzzle game by Tim Sweeney.

### Scorched Earth (Wendell Hicken, 1991)
- **Directory:** `\GAMES\SCORCHED`
- **Run:** `SCORCH`
- **Video:** VGA
- **Notes:** Shareware. "The Mother of All Games" -- artillery strategy classic.

### Epic Pinball (Epic MegaGames, 1993)
- **Directory:** `\GAMES\EPINBALL`
- **Run:** `PINBALL`
- **Video:** VGA
- **CPU:** 386 required
- **Notes:** Shareware (1 table). VGA pinball with digitized graphics.

### Dangerous Dave (id Software/Softdisk, 1990)
- **Directory:** `\GAMES\DAVE`
- **Run:** `DAVE`
- **Video:** VGA/EGA/CGA
- **Notes:** Freeware (Gamer's Edge sampler). Classic platformer by John Romero.

### Monuments of Mars (Apogee, 1991)
- **Directory:** `\GAMES\MARS`
- **Run:** `MARS1`
- **Video:** CGA
- **Notes:** Freeware (released free 2009). Puzzle-platformer.

### Pharaoh's Tomb (Apogee, 1990)
- **Directory:** `\GAMES\PTOMB`
- **Run:** `PTOMB1`
- **Video:** CGA
- **Notes:** Freeware (released free 2009). Platformer by George Broussard.

### Arctic Adventure (Apogee, 1991)
- **Directory:** `\GAMES\ARCTIC`
- **Run:** `AA1`
- **Video:** CGA
- **Notes:** Freeware (released free 2009). Sequel to Pharaoh's Tomb.

### Crystal Caves (Apogee, 1991)
- **Directory:** `\GAMES\CRYSTALC`
- **Run:** `CC1`
- **Video:** EGA
- **Memory:** ~450K
- **Notes:** Shareware Episode 1. Puzzle-platformer collecting gems.

### Secret Agent (Apogee, 1992)
- **Directory:** `\GAMES\SECAGENT`
- **Run:** `SAM1`
- **Video:** EGA
- **Memory:** ~450K
- **Notes:** Shareware Episode 1. Same engine as Crystal Caves.

## Removed Games

### Liero (1998-1999) -- REMOVED
- **Reason:** Requires XMS extended memory (HIMEM.SYS).

### Kiloblaster (Epic MegaGames, 1992) -- REMOVED
- **Reason:** Silently exits at startup. Turbo Pascal runtime fails hardware detection.

### Cosmo's Cosmic Adventure (Apogee, 1992) -- REMOVED
- **Reason:** Requires 528K-536K conventional memory (507K available).

### Traffic Department 2192 (Safari Software, 1994) -- REMOVED
- **Reason:** Requires more conventional memory than available.

## Emulator Requirements

- **CPU:** 386 real mode only (no protected mode, no DPMI, no DOS extenders)
- **Display:** Set to VGA in config (games auto-detect CGA/EGA/VGA via INT 10h)
- **Memory:** 507K conventional with optimized FDCONFIG.SYS
- **FDCONFIG.SYS:** Reduced FILES=20, BUFFERS=10, ENV=256 for maximum free memory
- **No XMS/EMS:** HIMEM.SYS and EMM386 cannot run (require protected mode)

## Games That Cannot Run on This Emulator

- **DOOM, Quake, Duke Nukem 3D, Descent, etc.:** Require DOS4GW/DPMI (32-bit protected mode)
- **Any game needing HIMEM.SYS/EMM386:** Requires protected mode transitions
- **Windows 3.x games:** Require protected mode
- **Any game showing "DOS/4GW Protected Mode Runtime":** Needs DPMI
