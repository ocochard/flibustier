# Open terrain and world data

Scope (2026-10-26) for running the sailing world, sea battles and land
battles from an open pack, without the original data. Art already has
open formats (`memory/internal/asset-packs.md`); this plan covers what
the game logic reads.

## Where terrain comes from today

| Use | Lookup | Original inputs |
|---|---|---|
| Sailing, landing parties, battle set choice (`world_terrain`) | `colour_class[tile pixel]` of the map tile under a world pixel | `MAP1560.PCX` tile grid, `MAP16x0.RSD` era patches, `MAP.IMB` pixels, executable table (256 classes by palette colour) |
| Sea battle shallows and coast (`field_terrain`) | `colour_class[battle tile pixel]` | `SBTL.BMP` screens, `BATTLE.IMB` pixels, the same colour table, executable battle sets |
| Land battle ground (`land.c terrain`) | `land_class[tile number]` | `LBTL.BMP` screens, executable table (256 classes by tile) |

- Classes: 1 shallows, 2 land, 3 sea (`TERRAIN_*`). Land battles reduce
  their table to 4 ground kinds.
- Terrain has pixel resolution: coastlines run inside tiles, and the
  sim tests terrain pixel by pixel.
- The sailing and sea battle terrain depend on the tiles' palette
  indices. A tile sheet without its original gets fixed-palette indices
  (tile sheets, `asset-packs.md`), and the colour table does not
  describe those, so terrain breaks. This is the blocker.

## Coupled data

Positions elsewhere assume the original map's geometry, so a new map
needs them too:

- towns per era: coordinates, approach points, nation and the other
  fields (`towns.c`, executable era blocks);
- 256 treasure spots, in cells;
- the battle screens (108 sea, 61 land) and the 5 x 5 battle sets.

## Plan

### T1: terrain per tile pixel (refactor, no behaviour change)

Status (2026-10-26): done. `test_original` checks every tile pixel of
every era against the executable's colour classes, and every world
pixel of 1660.

- The world precomputes a class for every pixel of every map tile
  (256 x 16 x 16) and every battle tile (256 x 32 x 32) at load, from
  `colour_class` and the indices.
- `world_terrain` and `field_terrain` read those classes. Palette
  indices no longer reach game logic.
- Proof: `test_sim`, `test_ui` and the ten-year replay give the same
  results.
- Size: small, about 60 lines in `world.c` and `battle.c`.

### T2: terrain masks in tile sheets

Status (2026-10-26): done (`asset-packs.md`, tile sheets). The world
keeps the land battle ground reduced to four kinds (`GROUND_*`), and
`test_original` loads full-size sheets with their own terrain over the
original data.

- `.tiles` gains `terrain = map-terrain.png`: the same grid as the art,
  at 640x480 resolution (16 pixels per map tile, 32 per battle tile).
  Each pixel is one of three exact colours (sea, shallows, land); any
  other colour is an error.
- Land battle tiles also need their ground kind per tile: a `ground`
  list of 256 numbers in the battle tile sheet.
- When present, these replace the colour table and `land_class`.
- Size: small to medium.

### T3: open world data

Status (2026-10-26): done (`asset-packs.md`, open world data). Towns
gained named fields for everything the sim reads. `test_original`
exports the original world and towns and loads them back identical in
every era; the ten-year replay through `flibustier-sim --pack` with the
export gives byte-identical output.

- **Tile grids:** the map per era and the battle screens are tile-number
  grids.
  - Tiled maps (JSON, integer layer data, read by `sim/json.c`), with
    the tile sheet as their tileset.
  - Six full era maps replace the base map plus `.RSD` patches.
- **Structured data:** towns per era, treasure spots and battle sets go
  in documented text, probably JSON like the sim protocol.
- **Original pack:** it fills the same structures from the original
  files, as today, so one code path consumes both.
- **Verification:** a local-only export of the original data to the
  open formats, loaded back, must give an identical world and replay.
  The export is never distributed.
- Size: medium to large; the formats, loaders and tests come first, the
  content later.

### T4: the content

- **Geography:** generated from public-domain coastlines (Natural Earth,
  1:10m), projected like the original map. That gives a terrain mask of
  10240 x 7680 pixels, then auto-tiled into tile numbers for the open
  tile set.
- **Towns:** placed from real coordinates, with the same projection.
- **Values:** gameplay values in town and other tables must be written
  anew (roadmap, licensing risk), tuned against the original rules.
- Size: large; tooling outside the engine (scripts under `tools/`).

## Remaining map data

- `coast` (executable, VA 0x33b18): a byte per map tile, read by the
  prison escape (`sim/prison.c`). Like the land battle ground, it
  belongs in the map tile sheet for open packs; not done yet.

## Beyond terrain

The other tables read from the executable are not map data but still
block the no-original-data milestone: sine, ship speeds, encounters,
battle AI and hit boxes, sword fight tables, start scenarios and
stories, UI hotspots and layouts (board, tavern, town, cabin, weapons,
cave, fleet panel, credits: the original's picture blocks only, open
packs give their credits as text), the shade table, the mouse pointer, and the
strings (`CSTR`/`CSTN`). They belong to the "open game data" format of
roadmap decision 1: [open-game-data.md](open-game-data.md).

## Decisions for the maintainer

1. ~~Tile grid format?~~ Tiled JSON maps (maintainer, 2026-10-26): the
   era maps and battle screens are authored in Tiled, read through
   `sim/json.c`.
2. Should open data values be tuned to match the original's balance, or
   are they free to differ?
