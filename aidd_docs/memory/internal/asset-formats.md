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
- The mouse pointer is not a file. It is a raw picture inside the
  executable at VA `0x37080`: `u16 w, u16 h` (24 x 27), then `w*h`
  pixels, colour 0 transparent, with the tip at the top left. It uses
  palette entries 0x1a-0x1f, the same greys in the game's pictures. The
  16-colour mode has its own copy at VA `0x3730c`, which the engine
  ignores.
- Fonts are drawn one colour, pixel for pixel, as in the game (VA
  `0x1900`, `0x19af`). The softer look under dosbox-staging comes from
  its default CRT shader (`shader = crt-auto`), not from the game.
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
- `MAP1560.PCX` and the era maps are not pictures. Each of their pixels
  is a tile number in `MAP.IMB` (see "Sailing world" below). Their own
  palette is meaningless, which is why the sea looked like noise.
- Indices 0..39 in `SBAT` and `SBTL.BMP` may be tile or colour codes of
  the same kind. Not checked yet.

## PCX/MAP16x0.RSD: era patches of the overview map (proven)

A byte-stream patch over the decoded 640x480 pixels of `MAP1560.PCX`. The
cursor starts at 0:

- `80 lo hi`: skip a `u16` count of pixels (`80 00 00` ends the stream).
- `81..FF`: skip `b & 0x7F` pixels.
- `01..7F`: copy the next `b` bytes as pixels.

Each era patches the 1560 map directly. Proof: when asked for
`map16x0.pcx`, the game's picture loader (VA `0x1cbb0`) loads
`map1560.pcx` instead, then applies `map16x0.RSD` to it (VA `0x1d6e3`).

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

### LBTL.BMP, SBTL.BMP: battle screens (proven, not Windows BMP)

- `u16 offset[n]` (n = `offset[0] / 2`), then at each offset a record:
  `u16 15, u16 15, u16 2`, then 15 x 15 tile numbers, row by row, and
  padding.
- The numbers index the 32 x 32 tiles of `BATTLE.IMB` (VA `0x2a9e8`):
  each record is a 480 x 480 battle screen.
- SBTL holds 108 sea battle screens; open sea uses tiles 0-39. LBTL
  holds 61, presumably for land battles.

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

- `0x00` header: `u16 3, u16 1, u16 0x172, u16 256 (tile count), u8
  0x56`. Open: the meaning of `0x172` and `0x56`.
- `0x09`: 768-byte VGA palette, 6 bits per channel. It is all zeros in the
  16-colour variants. It matches `SBAT.PCX`'s palette (254 of 256
  entries, within rounding). An earlier reading at `0x0C` shifted every
  colour by one entry, which put white into the sea's shimmer range.
- `0x309`: 15 zero bytes.
- `0x318`: 256 tile records: `u16 w*h+6, u16 w, u16 h, u16 w-1, u16 h-1`,
  then `w*h` pixels and 2 trailing bytes.
- Tile sizes: `MAP` and `TMAP` are 16x16 (sailing); `BATTLE` is 32x32
  (sea battle).
- The tile layout of the sailing world is the era map: see "Sailing
  world" below.

## Sailing world and sea animation (traced in code, rendering proven)

- **The world grid.** The sailing setup (VA `0x289da`) loads the era map
  PCX as a raw 640x480 buffer. Each byte is a tile number in `MAP.IMB`, so
  the world is 640x480 cells of 16x16 pixels.
  - Rendering the cells around Havana with `MAP.IMB` gives a coherent
    coastline, with town, beach and shallow-water tiles.
  - The random sea values 1-38 are random variants of the sea tile.
- **Picture table.** Pictures are loaded by index through the pointer
  table at VA `0x31dd0` (130 file names; loader at VA `0x2320`).
  - The map is picture `table[era]` with `table` = `72, 73, 73, 74, 75,
    76, 77` at VA `0x41418`. Picture 72 is `map1560.pcx` and 73-77 are
    `map1600.pcx`-`map1680.pcx`.
  - Those later files are not on the CD; only the `.RSD` patches are.
    How the patches get applied, and the meaning of the era index, are
    still open.
- **Tile classes.** A 256-byte table at VA `0x4103c` gives each tile a
  class:

  | Class | Tiles | Cells in 1560 | Meaning |
  |---|---|---|---|
  | 3 | 0-38 | 68% | open sea |
  | 1 | 39-58 | | probably shallows |
  | 2 | 59-66, 79-121 | | probably land |
  | 4 | 67-78, 122-255 | | probably coast and towns |

  Class 3 is confirmed: the game scatters 40 random effects (probably
  whitecaps) over class-3 cells in view.
- **Sea colours.** No ramp is stored. The colours are the tileset's own
  palette entries, rotated in place.
  - At setup (VA `0x2ab09`), the game snapshots palette ranges 208-215,
    64-71, 72-79 and 216-223 into doubled 16-entry tables, so a rotation
    can wrap.
  - Each frame, the animators rewrite the live palette from offset
    `counter & 7` and upload the range (VA `0x26e9f`):
    - In the sailing loop (VA `0x231b8`), 208-215 rotate every 4
      passes (VA `0x2ae26`).
    - 64-71 and 72-79 rotate every 2 passes (VA `0x2aed6`).
    - 216-223 rotate every 2 passes in the opposite direction (VA
      `0x2b009`), except while the ship touches land (VA `0x4d28a`).
    - Another loop (VA `0x7db8`) turns all of them every 8 passes.
  - The rotations are counted in passes of the sailing loop, so they
    follow its pace (12.2 passes a second, or 60.9 in the fast mode).
  - The sea tiles use 208-215, so that range is the water shimmer.
- **Other palette cycling.** A generic "rotate range by one" (VA
  `0x35f8`) cycles 238-254, 100-163, 194-206 and 211-236 in three
  functions near the start of the code, probably title and intro
  screens.

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
- Executable identification: `src/data/exe.c` keys data offsets by the
  file's SHA-256. GOG build: `0f196be3...bce3`, 289503 bytes.
- **Town tables** (decoded in `src/assets/towns.c`, checked against the
  game code with Ghidra):
  - **Era loader.** The function at VA `0x13555` takes an era (1-6 for
    1560-1680; 0 also maps to 1560). It copies a 0xF24-byte block from
    VA `0x38d58`, `0x39c7c`, `0x3aba0`, `0x3bac4`, `0x3c9e8` or `0x3d90c`
    into the live table at VA `0x4605e`. It also copies 0x780 bytes of a
    second per-era table, the arrival cells, to VA `0x4ca58`.
  - **Arrival cells.** Per era at VA `0x3e830`, `0x3ed70`, `0x3f2da`,
    `0x3f922`, `0x40030`, `0x4075c` (1560 to 1680; era codes 0 and 1
    share the first): records of three i16, a map cell's x and y and the
    index of the town reached there, ended by x = -1, within 0x780
    bytes (1344 to 1890 used). Every town has 2 to 13; the two Florida
    Channel records have none. Read by the sailing loop (VA `0x29dd9`,
    exact match) and the nearest-town search (VA `0x1adfc`); see
    "Arriving" in game-rules.md.
  - **Block layout.** A 36-byte header, then 48 records of 80 bytes:
    `u16 f[32]` followed by `char name[16]` (ASCII).
    - Header offset `0x20`: index of the last record in use.
    - The last two records in use are both "Florida Channel", which are
      sea points rather than towns. The game's town count (VA `0x4605a`)
      excludes them.
    - Towns per era: 34, 34, 40, 43, 43, 46.
    - Other header words: unknown.
  - **Fields.** Evidence from the decompiled code at VA `0xca7f` (random
    town events, logged as CSTN group 20 entries 11-15), `0xc81b`
    (conquest of a weaker neighbour, entry 19) and `0xcde9` (treasure
    fleet):
    - f0 flags:
      - `0x40`: inland. Exactly the inland towns; their second approach
        point is 0.
      - `0x80`: a town event moves it to `0x20`.
      - `0x10`: set on the major Spanish ports.
    - f1, f2: x, y on the 640x480 map (conquest uses their Manhattan
      distance, < 150).
    - f3: nation: 0 Spain, 1 England, 2 France, 3 Holland (conquest
      copies it).
    - f4: forts, 0-4 (a fort battle's guns are `f4 x 12`).
    - f5: soldiers.
      - Halved by "Indians attack" and "Malaria".
      - The conquest attacker needs more than twice the defender's value.
    - f6: citizens. Halved by "Malaria".
    - f7: gold.
      - +12 on "New gold mine".
      - Zeroed by "Pirates plunder".
      - Quartered when the treasure fleet leaves.
    - f8: wealth, 0-255.
      - +0x40 on a gold mine; -1/4 when plundered; +10 when the treasure
        fleet visits.
      - Its word in the city information is CSTN 9 by `f8 / 64`.
    - f9: the town's word for the player, its standing (reset to 0 by
      "New governor"; see "Governor", "Missions", sneaking in and the
      governor's daughter in game-rules.md).
    - f10: a villain hides there (VA `0xcba7`); a land battle meets
      `f10 x 128` more men.
    - f11: the governor (re-rolled 0-15 by "New governor" unless bit
      `0x40`): bit 0 a niece, 0x80 the player's informant, 0x40 his
      wife.
    - f12: the merchant's family (`& 0xe0`, set by marrying his
      daughter).
    - f13: the barmaid's fondness, 0-11.
    - f14: town ID, stable across eras; also the town's battle screen
      (LBTL and SBTL index).
    - f15, f16: the town's own cell on its land battle field (the fort).
    - f17, f18: the fort's cell in a fort battle.
    - f19, f20, f21: the player's ship in a fort battle (`x 32 + 16`)
      and its heading.
    - f22, f23: the landing party's place on the land battle field
      (`x 32`).
    - f24, f25: never read by the Gold program.
    - f26, f27, f28: the harbour mouth (a cell on the water outside the
      harbour; 0 for inland towns) and the heading leaving it (`x 32`).
    - f29, f30: the town on the Map of the Caribbean (about 0.75 x its
      cell).
    - f31: never read by the Gold program.
  - **Towns per nation** (Spain, England, France, Holland):
    1560: 27/2/5/0, 1600: 30/2/2/0, 1620: 30/4/3/3, 1640: 28/7/5/3,
    1660: 26/8/6/3, 1680: 25/11/7/3. There are no Dutch towns before
    1620.
- Career start and famous-expedition scenarios: 90-byte records at
  `0x3a337`, decoded in game-rules.md ("Start scenarios").
- Coordinate triplet lists at `0x43000`-`0x45800`. Meaning unverified.
- Ship stats: not found as a data table. They are probably code
  immediates. The manual gives max guns, crew, cargo and speed per ship.
- Hardcode the offsets per executable hash; only the GOG build is known.

## Sound drivers (*.PIR)

- Real-mode MZ overlays. Image header (after the 0x200 MZ header):
  - `+0x10`: banner string.
  - `+0x30`: entry count.
  - `+0x32`: entry offset table.
- **The game clock:** entry 5 returns a tick count, which the game
  reads through the DOS extender's real-mode call (VA `0x7c8d`). The
  driver's set-up programs PIT channel 0 with divisor `0x4C90` (60.9
  Hz) and counts its own interrupts, chaining to the BIOS every third
  one (`NSOUND` image offsets `0xb1`-`0xd7`). Sailing, sea battles and
  sword fights all wait on this clock.
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
- **The FM music engine (`ASOUND`, traced; `PSOUND` is the same engine
  recompiled):** image offsets below are `ASOUND`'s.
  - Entry 0 loads the data segment (`mov ax,0x22c; mov ds,ax`: image
    offset `0x22c0`; `PSOUND` `0x2fd0`). Entry 1 dispatches driver
    command `n` (`n` up to the word at `ds:0x85e`, 45) through the word
    table at `cs:0x1d54` (`PSOUND` `cs:0x9ab`). The game calls it with
    the tune number (VA `0x7c1f`, by VA `0x1b8cb`).
  - A tune's handler stops the music, then starts its streams: `mov cx,
    stream; call 0x1875` (`PSOUND`: `lea cx,[stream]; call 0x541`).
    `0x1875` takes the first free voice of 0-5, then 6-8; `0x18c6` (the
    FM effects) takes 6-8. Tunes are commands 9-26 and 43-45 (20, as
    `PGJUKE` lists), with 4 to 9 streams; 0-8 are effects; 1 stops.
  - The timer (PIT `0x4C90`, 60.9 Hz, interrupt 8 at `0x1829`) steps the
    nine voices once per tick (`0x1624` -> `0x13b0` -> `0x6aa` per voice,
    channel = voice number); the BIOS clock is chained every third tick.
  - **Voice record (46 bytes at `ds:0xc852 + 46 x n`):** `+0` ticks left
    (0 = idle), `+1` pitch slide per tick, `+2` volume ramp step, `+3`
    pan ramp step, `+4` note, `+5` instrument, `+6` velocity, `+7` gate,
    `+8` ticks to key-off, `+9/+0xa` volume ramp count/period, `+0xb/
    +0xc` pan ramp count/period, `+0xd` pan (0x40 centre), `+0x10`
    stream pointer, `+0x12` inner loop start, `+0x14` outer loop start,
    `+0x16` stream start, `+0x18` return address, `+0x1a/+0x1c` loop
    counts, `+0x24` percussion countdown, `+0x26` detune, `+0x27` volume
    (0-127), `+0x2a` transpose, `+0x2b` fading, `+0x2c` fixed release.
  - **Stream:** a byte below `0xbe` (signed compare with `0xbd`) is a
    note: note, then duration in ticks (note 0 rests; duration 0 ends
    the voice). Key-off comes `duration - gate` ticks later, or after
    the fixed release. `0xbe`-`0xff` are commands (table at `cs:0x123a`),
    lengths in bytes including the opcode: `be`, `c0`, `c1` 2 (globals
    `0x856`, `0x852`, `0x6dc`); `bf` 3 (global word `0x854`); `c2` 4
    (nothing); `c3` 2; `c4` 3 (calls driver code); `c5`-`d1`, `d3`, `d4`
    5: compare two script variables (bytes at `ds:0xc830 + v`) or a
    variable and a constant, and on success call the word at +3 (`c5`
    var b < var a, `c6` >, `c7` !=, `c8` ==, `c9` const < var, `d4` var
    == const; the others not traced); `d2` 3 jump; `e5`/`e6` 2
    decrement/increment a variable; `e7 v n` 3 writes the variable into
    the stream at +3+n; `e8 a b` 3 copies; `e9 v x` 3 sets; `ea v n
    table off` (n+4) and `ec n table off` (n+3) write a table entry
    (by a variable, or at random) into the stream; `eb a b n` 4 writes
    a random value in a..b at +4+n (self-modifying patterns); `ee t` 2
    transpose; `ef period step` 3 pan ramp; `f0 p` 2 pan; `f1 v` 2
    volume; `f2 d` 2 detune; `f3 period step` 3 volume ramp; `f4 v` 2
    velocity; `f5 s` 2 pitch slide; `f6 t` 2 fixed release; `f7 g` 2
    gate; `f8 i` 2 instrument; `f9` return; `fa w` call; `fb w` goto;
    `fc w` new start; `fd` restart (state reset); `fe n`/`ff n` 2
    repeat the outer/inner section n more times (0: mark the start).
    The random generator is `seed = ror3(seed + 0x9248)`, taken & 0x7fff.
  - **Per tick:** key-off countdown; then events until a note; then the
    slide, and every period the volume ramp (`+0x27` clamped 0-127) and
    the pan ramp; any change rewrites the levels.
  - **Pitch:** `n = note + transpose - 1`; block `n / 12`; F-number =
    word table at `ds:0x7e0` [`n % 12`] + detune; `A0+ch`, `B0+ch` with
    key-on `0x20`.
  - **Level (OPL2 path, card type below `0x18`):** `L = t[velocity] +
    t[volume] - 1` (byte table at `ds:0x75e`), clamped 0-63; carrier
    total level `63 - L`, keeping KSL; with the additive connection the
    modulator gets `63 - clamp(L - (63 - its level))`. Stereo cards
    (type `0x18` up) subtract pan tables (`ds:0x6de` by pan, `ds:0x75d -
    pan`) per side and write two chips.
  - **Instrument `i`:** 44 bytes at `ds:0x50 + 44 i`, modulator then
    carrier (22 bytes each): attack, decay, sustain level, release, EG
    type, KSR, level (63 loud), KSL, waveform, multiplier, feedback, AM,
    vibrato, connection (0 additive), percussion countdown, then words
    for the percussion noise. Operators: modulator slot `ds:0x7fc +
    2ch`, carrier `ds:0x7fd + 2ch`, register offset `ds:0x80e + slot`.
    The `C0` register is written last from the carrier block.
  - Register writes go to port `0x388` (shadow at `ds:0xc9f4 + reg`).
  - **`PSOUND` ("Pirates! Pro 7-13-93", what the engine plays):** the
    same script engine for an OPL3, with its own arrangement. Data
    segment image offset `0x2fd0`; command table `cs:0x9ab`; starters
    `0x541` (first free voice of 0-5), `0x510` (of 6-8: commands 25 and
    28-42, short stingers), `0x596 + 5k` voice k, `0x5be` voice 8, called
    or jumped to after `lea cx,[stream]`; `0x72d` stops. Tunes: commands
    9-24, 27, 43-45 (26 is empty).
    - Start-up: bank 2 register `0x05` = 1 (OPL3), `0x04` = `0x3f` (the six
      4-operator pairs), `0xBD` = `0xC0` on both banks.
    - Register writes take a bank mask (VA `0x29e`): bit 0 the first bank
      (base port, shadow `ds:0xba6c`), bit 1 the second (base + 2, shadow
      `ds:0xbb6c`); reads come from the second's when bit 1 is set.
    - **Voices:** nine records of 50 bytes at `ds:0xb8a4`, stepped once a
      tick (60.9 Hz) in order; voice n uses bank mask `ds:0xae82[n]` (1, 1,
      1, 2, 2, 2, 3, 3, 3) and channel `ds:0xae8c[n]` (0-2, 0-2, 6-8):
      voices 0-5 are 4-operator voices on either bank, 6-8 2-operator
      voices written to both banks. Operator offsets `ds:0xae96 + 4n`.
    - **Record:** as `ASOUND`'s up to `+0xd` (pan); then `+0xe` volume
      (`f1 v`: `v / 2 - 50`, signed), `+0xf` connection (0-3), `+0x10`-
      `+0x13` the operators' levels, `+0x14` (start copy), `+0x16` stream
      pointer, `+0x18`/`+0x1a` inner/outer loop starts, `+0x1c`/`+0x1e`
      their counts, `+0x20` start, `+0x22` return address, `+0x2a`
      percussion, `+0x2c` detune, `+0x2d` transpose, `+0x2e` fading,
      `+0x30` fixed release. Velocity (`f4 v`) is `v / 2`. Variables at
      `ds:0xb882`.
    - **Opcodes** as `ASOUND`'s, and: `db a b` 3 (var a modulo var b),
      `ed n` (n+3) skips a data block; `d4` is a goto. `f0` applies the
      pan at once; `f3`, `f1` are held off while fading. The volume ramp
      is not clamped.
    - **Pitch:** `n = note + transpose` (no `- 1`), block `n / 12`,
      F-number `ds:0xae66[n % 12]` + detune.
    - **Levels (VA `0xe26`):** `A = 0x7e - velocity - volume`. 4-operator
      voices: +6 when the pan is 0x2b-0x54 (both sides); each carrier
      gets `63 - clamp(A - its level)` with its KSL; carriers by
      connection: 0 op 3; 1 ops 1, 3; 2 ops 0, 3; 3 ops 0, 2, 3.
      2-operator voices: op 1, and op 0 with connection 0, per bank with
      the pan table added (`ds:0xade6[pan]` on the first, `[0x7f - pan]`
      on the second).
    - **Pan (VA `0x12ba`):** 4-operator voices set the OPL3 output bits of
      both channels of the pair (`C0`, `C3`...): `0x30` for pan 0x2b-0x54,
      `0x10` below, `0x20` above.
    - **Instrument `i` (VA `0x1616`):** 64 bytes at `ds:0xaeba + 64 i`:
      four 14-byte operator blocks (two for voices 6-8): attack, decay,
      sustain level, release, EG type, KSR, level (63 loud), KSL,
      waveform (0-7), multiplier, feedback, AM, vibrato, connection; the
      first block's feedback and connection are the voice's (connection
      bit 1 to the first channel's `C0`, bit 0 to the paired one's; 2-
      operator voices: bit 0, with `0x10` on the first bank and `0x20` on
      the second). Modulators keep `63 - level`, carriers start silent.
      Then `+0x38` percussion flag, words `+0x3a`-`+0x3e` its noise
      (VA `0x484`, not traced).
- `DIGITAL.PIR`: 9 effects in 8-bit unsigned PCM at about 7330 Hz
  (`mov cx,1CA2h`). Start table at image offset `0x9f`, end table at
  `0xb1` (words, image offsets; the image follows the MZ header, its
  size in paragraphs at file offset 8). Lengths in the GOG build: 6852,
  10716, 6276, 7307, 7800, 8904, 3300, 4159, 2964 bytes. The game loads
  it at start (VA `0x2a0f`) and plays effect n through VA `0x7d82`,
  only when it loaded; see "Sound effects" in game-rules.md.

## Original save files (layout traced, fields mostly not decoded)

- **Files:** `PIRATES-<n>.SAV`, with 10 slots, `n` from 0 to 9. The slot
  picker is at VA `0x1c658`.
- **Recognition:** a file counts as a save when it starts with the
  14-byte magic `PIRATES! GOLD\0`. The 80 bytes after it are the
  description shown in the slot list.
- **Body:** a straight dump of the game's memory, written block by block
  (writer at VA `0x1c281`, reader at VA `0x1b9b0`, in the same order):

| Bytes | Memory (VA) | Known content |
|---|---|---|
| 0x124 | `0x45e6e` | player standing per nation at `+0x0c`; relations 4x4 at `+0x44` |
| 0x30 | `0x45f92` | player values, initialised from the start scenario (VA `0x1386c`) |
| 0x90 | `0x45fc2` | |
| 0x24 | `0x47186` | 3 treasure maps of 12 bytes: pieces, spot x, y, people, town |
| 0x0c | `0x471aa` | `+0`: player in town flag; `+2`: town index |
| 0x24 | `0x471b6` | |
| 0x30 | `0x471da` | 4 fleets of 12 bytes (targeting at VA `0xcba7`) |
| 0x20 | `0x4720a` | |
| 2 | `0x4723e` | log entry count `N` |
| 8 x N | `0x47242` | log entries |
| 0xF24 | `0x4605e` | live town table, in the era block format |
| 0xA0 | `0x4d280` | |
| 0x54 | `0x47a76` | |
| 0x780 | `0x4ca58` | the era's arrival cells (copied at era load) |
| 2 | `0x46058` | current town index |
| 2 | (current town) | the first field of the current town record |
| 1 | `0x47aca` | |
| 2 + 2 | `0x4c852`, `0x4c854` | |
| 100 | `0x330f0` | a table in the initialised data |

- **Log entry:** when saving, the game first logs an entry with news
  code 27, writes the body, then drops the entry again (VA `0x1c110`),
  so the saved log includes it.
- **Calendar:** the calendar words (VA `0x46052`-`0x46056`) sit right
  after the 0x90 block but are not in the list. The date must be kept
  elsewhere, maybe in the 0x30 player block. Open.
- **Hall of Fame:** kept in `pirates.hof` (read and written at VA
  `0x529a`).
  - Layout: 10 records of 70 bytes, best first:

    | Bytes | Content |
    |---|---|
    | 4 | score, signed |
    | 2 | rating |
    | 32 | name |
    | 32 | a string of `+` characters, as many as the rating |

  - The new career is inserted by score, highest first, and the top 10
    are kept.
  - The screen shows rank, name, the `+` string and score at x = 150,
    180, 300 and 480. Rows sit at y = 200 + 25 x row (table at VA
    `0x322d4`). Empty records are not shown.

The engine uses its own save format (see `sim-protocol.md`). With this
layout, original saves could be imported later.

## Config

- `CONFIG.PIR` (34 bytes): sound card, port, IRQ and DMA. Field order not
  confirmed.
- `MEMORY.X32`: text written by `X32MEM` (free memory, SVGA chipset).
