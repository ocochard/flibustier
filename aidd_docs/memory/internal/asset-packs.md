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
| `driver/<name>` | sound driver (music, effects) | `<NAME>.PIR` |
| `text/<lang>/<name>` | string table | `<NAME>.<LANG>`, e.g. `CSTR.ENG` |
| `program/main` | original executable (data tables) | `PIRATESG.EXE` |

Planned: `music/<tune>`, keyed by the 20 tunes of the original jukebox.

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
```

- Tiles fill the image in rows, left to right, top to bottom; the image
  width sets the columns.
- A tile covers its art size times `480 / art_height` in 640x480 pixels,
  rounded. The game needs 16 pixel map tiles (`tiles/map`) and 32 pixel
  battle tiles (`tiles/battle`), 256 of each.
- Palette indices and a palette come with the tiles, because game logic
  reads terrain from tile colours: the original tileset's when it is
  installed with the same tiles, otherwise the first frame reduced to the
  fixed palette.
- Animation stands in for the original's palette shimmer of the sea:
  frame `pass / period % frames`, counted in sailing passes (12.2 a
  second). Battles draw frame 0.
- Limits:
  - Without original data, terrain comes from the fixed-palette indices,
    which the executable's colour classes do not describe. A fully open
    pack needs its own terrain data.
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

