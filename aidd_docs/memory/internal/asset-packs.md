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

[assets]
music/title = music/01-title.ogg
pic/town = art/town.png
```

- File paths are relative to the pack directory and may not contain `..`.
- Unknown `[pack]` keys are ignored, for forward compatibility.
- Planned `[pack]` keys:
  - `scale`: art resolution relative to 640x480.
  - `format`: version of the manifest format.
