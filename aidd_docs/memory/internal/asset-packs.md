# Asset packs and IDs

Implemented in `src/data/pack.c`. The design rationale is decision 1 of
the [roadmap](../../plans/roadmap.md).

## IDs

- Game code loads assets by logical ID, never by file name.
- An ID is lowercase words separated by `/`: `[a-z0-9_-]` plus `/`, with
  no empty component. This also rules out `..`.
- The kind prefix tells the engine which decoder to use for original data.
  For native files, the decoder is chosen from the file extension.

| Prefix | Content | Original file |
|---|---|---|
| `pic/<name>` | full-screen picture | `PCX/<NAME>.PCX` |
| `patch/<name>` | era patch of the overview map | `PCX/<NAME>.RSD` |
| `spr/<name>` | run-coded sprite bank | `SPR/<NAME>.SPR` |
| `cur/<name>` | raw sprite bank | `SPR/<NAME>.CUR` |
| `grp/<name>` | fencing animation groups | `SPR/<NAME>.GRP` |
| `img/<name>` | standalone picture | `<NAME>.IMG`, then `SPR/<NAME>.IMG` |
| `tiles/<name>` | 16x16 or 32x32 tileset | `<NAME>.IMB` |
| `tiles15/<name>` | 15x15 battle tiles | `<NAME>.BMP` |
| `font/<name>` | 1-bit font | `<NAME>.CHR` |
| `ui/<name>` | UI sprite bank | `<NAME>.CRS` |
| `ui/pointer` | the mouse pointer, frame 0, its tip at the top left (packs only; the original's is a picture in the executable) | none |
| `driver/<name>` | sound driver (music, effects) | `<NAME>.PIR` |
| `text/<lang>/<name>` | string table (packs: JSON, see "Open text") | `<NAME>.<LANG>`, e.g. `CSTR.ENG` |
| `program/main` | original executable (data tables) | `PIRATESG.EXE` |
| `music/<tune>` | a recording that replaces a tune (packs only) | none |
| `sound/<name>` | a recording that replaces a sound effect (packs only) | none |

## Replacement sounds (`sound/`)

A pack may replace any of the game's sound effects with a recording, in
any format raylib loads (WAV, OGG, MP3, FLAC, QOA), loaded once at the
start. See "Sound effects" and "FM effects" in game-rules.md for when
each plays.

- **The digitised effects** (`DIGITAL.PIR`): `sound/hail` (a pirate's
  "Arrgh!"), `sound/broadside`, `sound/land-battle` (a voice: "En
  garde!"), `sound/last-man` (the beaten opponent's voice, surrendering),
  `sound/harbour` (a voice), `sound/sail-ho` (a voice: "Sail ho!"),
  `sound/parry`,
  `sound/hit-foe` and `sound/hit-you` (cries of the man hit). A recording plays even without the original driver,
  and wins over the driver's where the game prefers a digitised effect
  to an FM one.
- **The FM effects** (sent to the music driver): `sound/shot-hit`,
  `sound/shot-miss`, `sound/crash` (hulls meeting, a reef),
  `sound/swing`, `sound/blow`, `sound/miss`, `sound/their-men`,
  `sound/our-men`, `sound/scrape`, `sound/trade` (a unit traded),
  `sound/row` (the trade board's row). A recording plays instead of the
  driver's sound.
- An effect without a recording plays as before.
- **The open set:** `tools/sfxgen/sfxgen.py` renders those that are not
  voices (the model cannot speak; the voices stay the original's) with
  Stable Audio Open 1.0 on a ComfyUI server, from the text prompts of
  `tools/sfxgen/sounds.toml` alone (never an original sound as input),
  trims and normalises them with ffmpeg, and writes them to the open
  pack with a provenance record each and the `pack.d/sound.ini`
  fragment. The model needs a Hugging Face account that accepted
  Stability AI's licence; its text encoder is `t5_base.safetensors`.

## Replacement music (`music/`)

A pack may replace any of the game's tunes with a recording, in any
format raylib streams (OGG, MP3, WAV, FLAC, QOA, XM, MOD), detected by
its file extension. The IDs, by driver command (see "Music" in
game-rules.md):

| ID | When | ID | When |
|---|---|---|---|
| `music/title` | intro, welcome menu (9) | `music/trade` | merchant, shipwright (20) |
| `music/lost-treasure` | lost treasure found (10) | `music/plunder` | a capture's or sack's loot (21) |
| `music/family` | dinner with the merchant's family (11) | `music/rescue` | a relative found (22) |
| `music/niece` | the governor's niece (12) | `music/tavern` | the tavern (23) |
| `music/daughter` | the governor's daughter (13) | `music/wedding` | a wedding (24) |
| `music/bank` | the bank (14) | `music/sail-ho` | a sail sighted (25) |
| `music/barmaid` | the barmaid talking (15) | `music/verdict` | a career's end (27) |
| `music/treasure` | a pirate's treasure found (16) | `music/prize` | "Shall we keep her?" (28) |
| `music/news` | the governor's news (17) | `music/prison` | prison, marooned (43) |
| `music/news-titled` | the same, titled by his nation (18) | `music/harbour` | arriving in a town (44) |
| `music/promotion` | a promotion (19) | | |

- **Variants:** `music/<tune>-2` to `music/<tune>-9` are other
  recordings of the same tune; each time the tune starts, one of those
  present (with `music/<tune>`) is picked at random.
- **The shipwright's own:** the game plays the trade tune for the
  merchant and the shipwright alike; a pack may give the shipwright
  `music/shipwright` (with its variants), preferred there over
  `music/trade`.
- The recording plays once, as the driver's tunes do, in place of the FM
  arrangement; asked again while it plays, it goes on.
- A stop fades it out over 3 seconds (the driver's fade); opening the
  cabin at sea cuts it at once.
- The FM sound effects (battles, swords, trade boards) still play over
  it from the driver.
- A tune without a recording plays from the FM driver as usual; a file
  raylib cannot read is reported on stderr and the FM tune plays.

## Pack stack

- The last pack added has the highest priority. The first pack that
  provides an ID wins, so a pack can override a single asset.
- `packs_list()` returns the union of all IDs, sorted and unique.

## Original data pack

`packs_add_original()` accepts any of:

- the GOG `DATA.DAT` ISO 9660 image, read in place;
- the GOG install directory that contains it;
- a directory holding the extracted CD contents, or `PIRATESG.CD` itself.

File names are matched case-insensitively.

## Directory packs: `pack.ini`

```ini
# Comments start with '#' or ';'.
[pack]
name = Pirates! Gold remixed music
license = CC-BY-4.0
art_height = 1080

[assets]
music/title = music/01-title.ogg
pic/town = art/town.png
```

- File paths are relative to the pack directory and may not contain `..`.
- **Fragments:** after `pack.ini`, every `pack.d/*.ini` is read in name
  order, with the same syntax, so that each tool keeps its own list (the
  art generator writes `pack.ini`, `tools/music_pack.py` writes
  `pack.d/music.ini`). A fragment's `[pack]` keys are ignored: the
  pack's name and licence are `pack.ini`'s. The first entry for an ID
  wins, `pack.ini`'s first.
- Unknown `[pack]` keys are ignored, for forward compatibility.
- `art_height`: the screen height the pack's art is drawn for, 240 to
  4320 (default 480, the original). A full-screen picture for 1080p is
  1440x1080; other pictures cover their 640x480 size times
  `art_height / 480`, rounded.
  - The engine renders at the largest `art_height` of all packs, rounded
    down to a multiple of 3 for a whole 4:3 width.
- Planned `[pack]` keys:
  - `format`: version of the manifest format.

## Replacement pictures (`pic/` as PNG)

Loaded by `pic_load()` (`src/assets/pic.c`):

- The true colours are kept at the art's resolution.
- Palette indices are derived too, for code that works on indices
  (colour keys, palette effects, sprites drawn in the picture's palette):
  - the original PCX's palette and pixels when it is installed with the
    same size;
  - otherwise the PNG reduced to the original's palette, or to a fixed
    palette (6x6x6 cube plus 40 greys) without original data.
- Limits:
  - Palette cycling and ink fades do not change true colours.
  - `fb_image_part` draws indices only, so the credits text blocks stay
    in original resolution.
  - The treasure map is composed on indices, so it drops the true
    colours of `pic/wood`.

## Replacement sprites: sprite sheets

Any bank ID (`spr/`, `ui/`, `cur/`, `tiles15/`, `grp/`) may map to a
`.sheet` file, loaded by `bank_load()` and `group_load()`
(`src/assets/sheet.c`):

```ini
# sprites/tavern.sheet
[sheet]
image = tavern.png

[frames]
# frame = x y w h, in art pixels of the image
0 = 0 0 300 200
1 = 300 0 120 80
```

- `image` is relative to the sheet's directory, in the same pack, and
  may not contain `..`.
- Frames are numbered from 0 with no gaps, in any order. Fencing groups
  (`grp/`) number them `bank.frame`, for example `3.0`.
- A frame covers its art size times `480 / art_height` in 640x480
  pixels, rounded, so game code places it as the original frame.
- Frames are drawn on the canvas by their alpha.
- Palette indices and a mask are derived for code that reads them:
  - the original frame's, when it is installed with the same size;
  - otherwise the art reduced to the fixed palette, opaque where alpha is
    at least half, never index 0 when opaque (raw frames use 0 for
    transparency).
- Limits: the title is composed on indices (intro), and so are sprites
  drawn into the treasure map, so those show at original resolution.
  `img/` has no replacement format yet.

## Replacement tiles: tile sheets

Any `tiles/` ID may map to a `.tiles` file, loaded by `tileset_load()`
(`src/assets/tiles.c`):

```ini
# tiles/map.tiles
[tiles]
image = map.png
# tile side in art pixels: 36 for the 16 pixel map tiles at 1080p
tile = 36
# optional: tile count (default 256)
count = 256
# optional: animation frames (default 1), each a full set of tiles
# below the previous one, and sailing passes per frame (default 4)
frames = 8
period = 4
# optional: terrain, the same grid at 640x480 resolution in three
# colours: sea #0000FF, shallows #00FFFF, land #00FF00
terrain = map-terrain.png

# optional, battle tiles: land battle ground per tile, clear, cover,
# swamp or water; unlisted tiles are clear
[ground]
12 = cover
```

- Tiles fill the image in rows, left to right, top to bottom; the image
  width sets the columns.
- A tile covers its art size times `480 / art_height` in 640x480 pixels,
  rounded. The game needs 16 pixel map tiles (`tiles/map`) and 32 pixel
  battle tiles (`tiles/battle`), 256 of each.
- Terrain: the world keeps a class per tile pixel. With `terrain`, the
  sheet gives it; without, it comes from the tiles' palette indices
  through the executable's colour classes, which only describe the
  original's indices. A sheet for an open pack therefore needs
  `terrain`, and the battle tiles a `[ground]` section.
- Palette indices and a palette still come with the tiles, for code that
  reads them: the original tileset's when it is installed with the same
  tiles, otherwise the first frame reduced to the fixed palette.
- Animation stands in for the original's palette shimmer of the sea:
  frame `pass / period % frames`, counted in sailing passes (12.2 a
  second). Battles draw frame 0.
- Limits:
  - The treasure map's tiles are composed on indices (original
    resolution).
  - The shimmer pause while the ship touches land is not reproduced.

## Replacement fonts

Any `font/` ID may map to a `.font` file, loaded by `font_load()`
(`src/assets/fonts.c`) through FreeType:

```ini
# fonts/pirate1.font
[font]
file = PirataOne-Regular.ttf
# line height in 640x480 pixels, as the original font's
height = 25
# optional: glyph size (default: height) and baseline below the line top
# (default: the font's ascender), in 640x480 pixels
size = 22.5
baseline = 19
```

- `file` (TrueType or OpenType) is relative to the descriptor, in the
  same pack.
- Original line heights: `pirate1` 25, `pirate2` 15, `pirate3` 14,
  `pirate4` 30, `caps` 34.
- `font/caps` holds the drop capitals, looked up as A-Z then the
  accented capitals (`caps_char()`); the other fonts hold characters
  0x21-0xFF, mapped from Mac Roman to Unicode.
- Text layout stays in 640x480 pixels but moves the pen in 1/16 pixel,
  so the font's own spacing survives; the original fonts advance by
  whole pixels as before.
- Glyphs are rasterised once for the canvas and blended by coverage in
  the text colour. The 640x480 screen gets 1-bit glyphs (coverage at
  least half), so code reading it still works.
- No kerning yet.
- The open pack's fonts (IM Fell English for `pirate1`-`pirate3`, Pirata
  One for `pirate4` and `caps`, both SIL OFL) are fetched by
  `tools/font_pack.py`, which also writes their descriptors and
  `pack.d/fonts.ini`. Their sizes and baselines match the original's
  x-height and baseline in each line.

## Open world data

The world's layout and towns, when a pack gives them; otherwise the
original files and executable.

| ID | Format | Content |
|---|---|---|
| `map/<year>` | Tiled map, 640 x 480 cells | the era's map (1560 ... 1680), one tile number per cell |
| `screens/sea` | Tiled map, 15 x 15 cells, 108 tile layers | sea battle screens, in layer order |
| `screens/land` | Tiled map, 15 x 15 cells, 61 tile layers | land battle screens |
| `data/towns/<year>` | JSON (`towns.h`) | the era's towns: position, nation, wealth, garrison, harbour, fort and battle cells, dot on the Map of the Caribbean, `arrival` (optional: 1 to 16 `[x, y]` map cells where the town is reached, also the cells the nearest-town search measures to; absent, its cell and the eight around); `treasure_fleet` and `silver_train`, each up to 15 stops as town indices (-1 unused), the convoys' routes (absent: no convoys) |
| `data/treasure` | JSON (`world.h`) | 256 treasure spots, world cells |
| `data/battle-sets` | JSON (`world.h`) | sea battle screens offered by a coast north, east, south, west, or in open sea |

- Tiled maps (`src/assets/tiled.c`): orthogonal, fixed size, one
  tileset, tile layers stored as CSV ("Tile Layer Format: CSV"), every
  cell an unflipped tile 0-255. Object and image layers are ignored,
  group layers refused. Files end in `.tmj` (or `.json`).
- A pack that gives one of these must give it whole and valid: a broken
  file is an error, never a silent fallback to the original.
- `flibustier --export-world DIR` writes the installed world (the original's
  unless a pack replaces it) in these formats, with Tiled tilesets
  `map.tsj` and `battle.tsj` (images `map.png`, `battle.png` to supply)
  and a `pack.ini`. Exported from the original data, it is for local
  checks and as a starting point only: never distribute it.

## Open text

The game's text, when a pack gives it; otherwise the original `CSTR` and
`CSTN` files (see asset-formats.md). Implemented in `src/assets/text.c`
(`strtab_from_blob()`, `strgroups_from_blob()`) and
`src/assets/textfiles.c`.

| ID | Format | Content |
|---|---|---|
| `text/<lang>/cstr` | JSON, `.json` | prose and dialogue: tavern talk, governors, missions, battles, menus, the era and expedition texts |
| `text/<lang>/cstn` | JSON, `.json` | word lists: captain names, months, ship types, nations, compass points, the log book's sentences |

- The decoder is chosen by the file extension: `.json` is this format,
  and the original data pack's files stay binary. Both decode to the
  same `struct strtab` and `struct strgroups` (`src/assets/text.h`), so
  the rest of the engine does not change.
- `text/<lang>/cstr`:

  ```json
  {
    "strings": {
      "62": "Pick your voyage:\n~First choice\n~Second choice",
      "119": "These are %s waters, Captain."
    }
  }
  ```

  `strings` maps an index to its text. Keys are decimal indices with no
  leading zero, 0 to 1023. The engine looks entries up by the original's
  index (named `S_*` in `src/ui/screens.c`). Index 322 and above are
  allowed but unused.
- `text/<lang>/cstn`:

  ```json
  {
    "groups": {
      "6": ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"],
      "71": ["Spanish", "English", "French", "Dutch"]
    }
  }
  ```

  `groups` maps a group index (0 to 1023, decimal) to its words, in
  order. The order carries meaning: group 8 lists the ship types in the
  engine's order, group 20 the log's sentences by news code, and group
  13 the compass points clockwise from south (`W_*` in
  `src/ui/screens.c`).
- **Words of the engine's own.** A pack may add words the original
  does not have, after a group's last one; without them the engine
  behaves as with the original. So far: CSTN 74.6, the pattern of a
  crew fighter's label, filled with the nation (CSTN 71) and the title
  (CSTN 74), for example `%2$s %1$s` for "Mutin espagnol".
- **Whole tables.** A pack that gives `text/<lang>/cstr` replaces the
  whole table. An index it leaves out is an empty string, never the
  original's entry; a group it leaves out has no words, and a short group
  has empty words past its end. So a pack can run without the original
  text.
- **Errors.** A file that is not valid JSON, has a key that is not an
  index in range, or has a value of the wrong type is an error when the
  game loads it, with the file and the reason on stderr. It never falls
  back silently to the original.
- **Text.** UTF-8. The fonts draw the Mac Roman repertoire (ASCII plus
  Western European accents and some symbols). A character outside it is
  drawn as `?`, even with a replacement font.
- **Markup**, as in the original (the text renderer, `src/gfx/text.c`):
  - `%s`: an argument the engine supplies (a name, a town, a sum). The
    engine fills the `%s` in order, so an entry keeps the order of its
    arguments; it may leave out trailing ones. `%1$s` to `%9$s` take
    that argument instead, so a translation can reorder them (an
    engine extension: the original has none). `%%` is not a
    conversion: a literal `%` is written alone.
  - `\n` (or `\r`): a line break. The decoder turns each `\n` into
    `\r`, the original's, since some screens test for it.
  - `~` at the start of a line: a menu option the player can select.
  - `` ` `` at the start of a paragraph: the next letter is drawn as a
    drop capital. Elsewhere it is ignored.
  - `^`: an apostrophe. A plain `'` draws the same, and a pack may use
    either.
  - The original's control bytes (0x01-0x1B) and its `0xCA` placeholder
    in `CSTN` are not used by the engine: the renderer skips control
    bytes. A pack does not need them, and may write them as `\u0001`
    and so on.
- **Languages.** `<lang>` is three lowercase letters. The original has
  `eng`, `fre` and `ger`. A pack may add a language (for example
  `text/spa/cstr` and `text/spa/cstn`), and `--lang` accepts any code
  for which the pack stack gives both tables.
- **Writing it.** The open pack's text must be written anew, never
  copied or paraphrased line by line from the original (see
  "Licensing" in the roadmap); [text-catalogue.md](text-catalogue.md)
  says what each entry is for. `flibustier --export-text DIR` writes the
  installed tables of every language in this format, as
  `<lang>/cstr.json` and `<lang>/cstn.json` with a `pack.ini`; line
  breaks are written `\n`. Exported from the original data, it is for
  checking the format and the indices locally: never distribute it.
- **Coverage.** `tools/artgen/coverage.py` counts the text IDs with the
  other assets, and lists per text ID the pack gives the strings the
  engine reads that the original has (not empty) and the pack lacks,
  and the groups it gives fewer words than the engine reads. What the
  engine reads comes from text-catalogue.md.

## Per-town assets

Town screens look up their assets per town (`game_town_asset()`,
`src/ui/game.c`), so a pack can give any town its own:

1. `<base>/<town>`: the town's name in lowercase, each run of
   characters other than a-z and 0-9 a `-` (`pic/tavern/port-royale`,
   `pic/governor/st-augustine`);
2. `<base>/<nation>`: the town's current nation, `spain`, `england`,
   `france` or `holland` (`pic/governor/england`), so a change of
   owner changes the picture;
3. `<base>`: the shared asset, as in the original.

| Screen | Assets looked up |
|---|---|
| Village | `pic/town`, `spr/town` |
| Tavern | `pic/tavern`, `spr/tavern` |
| Bartender | `pic/bartenda` |
| Barmaid | `pic/barmaid` |
| Governor | `pic/governor`, `spr/gov` |

- The clickable spots of each screen come from the executable and do
  not change per town: a town's picture must keep the shared layout.
- Town names come from the town data, so an open pack's own names give
  its keys.

