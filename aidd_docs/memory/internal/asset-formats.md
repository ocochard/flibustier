# Pirates! Gold asset formats

Clean-room notes from analysis of the GOG release (2026-10-25). This file
describes formats only; it must never contain original game data.

All integers are little-endian `u16` unless stated otherwise. "Proven"
means a decoder consumed every file of that type exactly to EOF and the
rendered output looked correct.

Decoders: `src/assets/`. Each one is checked against every file of its
kind on the GOG data by `tests/test_original.c`.

## Distribution layout

- GOG ships `DATA.DAT`, an ISO 9660 image (volume `PIRATESGOLD`). The game
  lives in `PIRATESG.CD/`, with art in `PCX/` and `SPR/`.
- `PIRATES.001`-`.006` are one PKZIP archive spanned over floppies holding
  the same files as the CD. Ignore them.
- Text comes in three languages (`.ENG`, `.FRE`, `.GER`). The executable
  also references `.SPA`, which is not shipped.
- There are two art sets: 256 colours (`PCX/`, `SPR/`) and 16 colours
  (`PCX16/`, `SPR16/`, `TMAP16.IMB`, `16MAP*.IMB`). The 16-colour set is
  produced at install time by `CONV.BAT`/`PCXDITH.EXE`. Only target the
  256-colour set.
- The developer scripts `SPRS.DEF`, `SPRS.FL`, `SPRS.EX` and `CONV.*` map
  each sprite file to the PCX whose palette it uses (third column).

## Screen

640x480, 8-bit indexed. Sprites are stored pre-compiled for a 640-pixel
stride.

## PCX/*.PCX: full-screen art (proven)

- Standard ZSoft PCX: version 5, RLE, 8 bpp, 1 plane, with a 256-colour
  palette at EOF after the `0x0C` marker.
- Sizes: 640x480 (70 files), 800x400 (24, flags and Jolly Roger), 604x403
  (9, ship portraits `S_*`), 200x200 (`BASEFLAG`).
- Indices 0..39 in `MAP1560`, `SBAT` and `SBTL.BMP` are sea pixels. The game
  overwrites or cycles these palette entries at runtime. The ramp is not
  found yet; it is probably in the executable.

## PCX/MAP16x0.RSD: era patches of the overview map (proven)

A byte-stream patch over the decoded 640x480 pixels of `MAP1560.PCX`. The
cursor starts at 0:

- `80 lo hi`: skip a `u16` count of pixels (`80 00 00` ends the stream).
- `81..FF`: skip `b & 0x7F` pixels.
- `01..7F`: copy the next `b` bytes as pixels.

Open: whether each era patches 1560 or the previous era. The evidence
points to 1560.

## Sprite bank (shared by SPR, CRS, CUR, BMP)

- `u16 first`: offset of frame 0. It also equals 2 x frame count.
- `u16 offset[count-1]`: offsets of frames 1..n-1.

### SPR/*.SPR, *.CRS: run-coded frames (proven)

- Frame: `u16 w, u16 h, u16 K`, then repeated `u16 skip, u16 count,
  count bytes`, ended by `u16 0xFFFF`.
- Position counts in 640-pixel screen stride: `y = pos / 640`,
  `x = pos % 640` inside the w x h box. Skipped pixels are transparent.
- `K` is constant per file and not needed to draw. Open: its meaning, and
  the 1-5 padding bytes after `FFFF`.
- `MISC*.CRS` uses this format for UI buttons.

### SPR/*.CUR: raw frames (proven)

- Frame: `u16 w, u16 h, u16 K`, then `w*h` raw bytes, then 4 trailing
  bytes (5 when `w*h` is odd).
- In `*BTL.CUR`, `K = 0xFE` is the colour key (sky-blue fill). Elsewhere
  the background is 0.
- Examples: `SHIPnA/B.CUR` hold 8 headings; `*BTL.CUR` hold damage stages.

### LBTL.BMP, SBTL.BMP: battle tiles (proven, not Windows BMP)

Sprite bank of 15x15 frames: `u16 15, u16 15, u16 2`, then 225 pixels and
5 padding bytes. LBTL holds 61 tiles (land battle), SBTL 108 (sea battle).

## SPR/*.GRP: sword-fight body parts (proven)

- `u32 offset[16]`, where count = `offset[0] / 4`. Each entry is a complete
  embedded SPR bank: 16 moves of 4-10 frames each, 141x90 or 141x114
  pixels, all sharing one origin.
- Draw order that renders correctly: legs, body, chest, head, weapon.
- Palette: `ALLEY.PCX`. `MY*` files draw the player, `HE*` the opponent.
- Open: which group is which move (attack, parry or retreat; high, middle
  or low, judging by the source names in `CONV.FL`).

## *.IMG (proven, format confirmed by disassembling PCXDITH)

- Header: `u16 w, u16 h, u16 mode`.
- `mode == 0`: raw `w*h` bytes.
- Otherwise, RLE: control byte `b`. If `b & 0x80` is set, copy `b & 0x7F`
  literal bytes; otherwise repeat the next byte `b` times.
- Files: `CLOUD.IMG` (147x88, raw), `SPR/WPNSEL.IMG` (205x386, RLE,
  `ALLEY` palette).
- Quirk: `WPNSEL.IMG` starts with a zero-length run (`0x80`), and two of
  its runs overflow their row by 1-2 pixels. The decoder skips zero runs
  and drops the overflow. Carrying the overflow into the next row instead
  shifts the picture by up to 2 pixels; the data alone cannot tell which
  reading is right.

## *.IMB: tilesets (proven)

- `0x00` header: `u16 3, u16 1, u16 0x172, u16 256 (tile count),
  u32 0x56`. Open: the meaning of `0x172` and `0x56`.
- `0x0C`: 768-byte VGA palette, 6 bits per channel. It is all zeros in the
  16-colour variants.
- `0x30C`: 12 zero bytes.
- `0x318`: 256 tile records: `u16 w*h+6, u16 w, u16 h, u16 w-1, u16 h-1`,
  then `w*h` pixels and 2 trailing bytes.
- Tile sizes: `MAP` and `TMAP` are 16x16 (sailing); `BATTLE` is 32x32
  (sea battle).
- Open: where the tile layout of the sailing world lives. Hypothesis: the
  640x480 `MAP1560` grid (1 pixel = 1 cell), auto-tiled from a land/sea
  mask. `CSTR` contains a `TileX/TileY/InternalX/InternalY` debug string.

## *.CHR: 1-bit fonts (proven)

- `PIRATE1`-`4.CHR`: 144 fixed-size entries of `filesize / 144` bytes.
  - Entry 0 is a header (`01 FF hh 00`; `hh` may be the space width).
  - Entry k is character `0x20 + k`, verified on `!`, `0`, `B` and `a`:
    `u16 ink, u16 advance`, then rows of 4-byte big-endian bitmaps, MSB
    leftmost.
  - Most glyphs start at column 8 of their 32-pixel cell, and script
    swashes overhang to the left of it. The pen origin is therefore
    probably x = 8. `ink` is roughly the last column measured from there.
    This is inferred from the bitmaps, not from the game code.
  - Heights are 25, 15, 14 and 30. Ink width can exceed advance, so glyphs
    overlap. Entries above 0x7F are accented Latin letters.
- `CAPS.CHR`: 33 drop capitals, 208 bytes each: `u16 w, u16 46`, then 34
  rows of 6 bytes.

## *.FIX

The first 512 bytes of the matching PCX, presumably a backup for the
16-colour conversion. Ignore them.

## CSTR.* / CSTN.*: strings (proven)

- Both start with `u16 offset[N]`, where `N = offset[0] / 2`.
- `CSTR`: 322 NUL-terminated entries of prose and dialogue.
- `CSTN`: 108 groups, each `u16 count` followed by NUL-terminated
  strings. The stored count is wrong in some groups: ENG/FRE group 74
  declares 5 strings and holds 6, group 86 declares 13 and holds 11.
  Decode the strings present in each span and ignore the stored count.
  The last group is empty.
- French `CSTR` #106 lacks its terminating NUL. Groups include captain names per nation, months, ship types,
  wealth levels, wind and compass points, damage levels and log formats.
- `CSTN` group 8 order (probably the internal ship-type enum): Pinnace,
  Sloop, Barque, Cargo fluyt, Merchantman, Frigate, War galleon, Galleon,
  Fast galleon, Fort, then the suffix " (damaged)".
- Encoding: ASCII plus Mac Roman for accented letters (not CP437).
- Control bytes 0x01-0x0C, 0x10, 0x14, 0x18 and 0x1B also occur in the
  text. Their meaning (colour, formatting or variables) is unknown.
- Markup:
  - `%s`: substitution, the only printf conversion used.
  - `\r`: newline.
  - `~`: selectable menu line.
  - `` ` ``: paragraph or dialogue lead-in.
  - `^`: apostrophe.
  - `0xCA`: digit placeholder in `CSTN`.

## PIRATESG.EXE: data tables

- FlashTek X-32 DOS extender, flat 32-bit image, not compressed. Virtual
  address 0 = file offset `0x47a7`, image size `0x42338`.
- Compiler: probably Zortech/Symantec C (medium confidence).
- Town tables, one per era. Record = 80 bytes: `u16 f[32]`, then
  `char name[16]`.
  - Era start offsets (file offset, record count):

    | Era | Start | Records |
    |---|---|---|
    | 1560 | `0x3d523` | 36 |
    | 1600 | `0x3e447` | 36 |
    | 1620 | `0x3f36b` | 42 |
    | 1640 | `0x4028f` | 45 |
    | 1660 | `0x411b3` | 45 |
    | 1680 | `0x420d7` | 48 |

  - Verified fields: f1,f2 = map x,y in 640x480 space (spot-checked Havana,
    Panama, San Juan); f3 = nation (0 Spain, 1 England, 2 France,
    3 Holland).
  - Likely fields: f14 = stable town ID; f24-f27 = harbour points;
    f29,f30 = x,y x 0.75.
  - Non-town entries are mixed in, such as treasure-fleet waypoints.
- Career start and famous-expedition scenarios: 90-byte records at
  `0x3a337`. Fields not decoded.
- Coordinate triplet lists at `0x43000`-`0x45800`. Meaning unverified.
- Ship stats: not found as a data table. They are probably code
  immediates. The manual gives max guns, crew, cargo and speed per ship.
- Hardcode the offsets per executable hash; only the GOG build is known.

## Sound drivers (*.PIR)

- Real-mode MZ overlays. Image header (after the 0x200 MZ header):
  - `+0x10`: banner string.
  - `+0x30`: entry count.
  - `+0x32`: entry offset table.
- Drivers:

  | File | Device |
  |---|---|
  | `ASOUND` | AdLib / OPL2 |
  | `PSOUND` | Pro Audio / SB Pro stereo FM, OPL3 path |
  | `RSOUND` | Roland MT-32 via MPU-401 |
  | `ISOUND` | PC speaker |
  | `NSOUND` | none |
  | `DIGITAL` | digitised effects |

- Music is embedded in each FM, MT-32 and speaker driver as its own
  per-device arrangement.
  - `ASOUND` data starts with 2-operator x 13-parameter AdLib instruments,
    then per-voice streams of (note, duration) bytes; `F0`-`FF` are
    commands.
  - A 46-entry command dispatcher (`ASOUND` entry 1) covers both music and
    effects.
  - `PGJUKE` lists 20 tunes (A-T).
- `DIGITAL.PIR`: 9 effects in 8-bit unsigned PCM at about 7330 Hz
  (`mov cx,1CA2h`). Start table at image offset `0x9f`, end table at
  `0xb1`.

## Config

- `CONFIG.PIR` (34 bytes): sound card, port, IRQ and DMA. Field order not
  confirmed.
- `MEMORY.X32`: text written by `X32MEM` (free memory, SVGA chipset).
