# Open game data

Scope (2026-10-29) for running the game with no `PIRATESG.EXE`: every
table the engine reads from the executable gets an open format that a
pack can give, as roadmap decision 1 planned ("Game data ... has its own
documented open text format"). It follows
[open-terrain.md](open-terrain.md), which did the same for the maps,
towns, treasure spots and battle sets ("Beyond terrain" there).

## Where it stands

- The engine refuses to start without the executable: `game_init()`
  (`src/ui/game.c`), `flibustier-sim` (`src/sim_main.c`), the viewer
  (`src/view/view.c`) and `--export-world` (`src/assets/worldfiles.c`)
  all load `program/main` and identify it by SHA-256 first.
  `flibustier --pack <open pack>` alone stops at "missing or unknown
  PIRATESG.EXE".
- Already open, with the executable as fallback: the era maps and
  battle screens (Tiled), `data/towns/<year>` (with the arrival cells),
  `data/treasure`, `data/battle-sets`, the mouse pointer (`ui/pointer`),
  the credits (`text/<lang>/credits`), the text (`text/<lang>/*`), and
  the terrain classes of a replacement tile sheet (open-terrain T2).
- Even then, `world_load()` reads the executable's land and colour
  classes, battle sets, treasure spots and coast before any pack
  replaces them, so the executable is needed anyway.

## The tables left

From `struct exe_layout` (`src/data/exe.h`), by who reads them and the
open home proposed for each.

| Table | Read by | Open home |
|---|---|---|
| era block: `speed`, `encounter`, `treasure_ship`, `battle_ai`, diplomacy range, shipwright tolerance | `world_load()` | `data/era/<year>` |
| `half_w`, `half_h`, `hit_steps`, `shot_rise`, `shot_frame`, `bow_mark` | `world_load()` | `data/sea-battle` |
| `sword_moves`, `sword` (`struct sword_tables`) | `sword_load()` | `data/sword` |
| `scenario` (30 start scenarios, and `raw[0x58]`, the months, read by `sim.c`) | `scenario_load()` | `data/scenarios` |
| `story` (career stories, byte scripts) | `story_script()`, screens.c | `data/stories` |
| `jolly` (a named captain's Jolly Roger) | `encounter_flag()`, screens.c | `data/captain-flags` |
| `board`, `tavern_spots`, `town_spots`, `cave`, `cabin`, `weapon_spots`, `fleet_panel` | screens.c | `data/layout/<screen>` |
| `coast` | `world_load()` (prison escape) | the map tile sheet (open-terrain "Remaining map data") |
| `colour_class`, `land_class`, `tile_class` | `world_load()`, view.c | the tile sheets (open-terrain T2, done); only the read order changes |
| `sine` | `world_load()` | derived by the engine (G1) |
| `shade` (a colour's shadow, the clouds) | `clouds_draw()` | derived from the palette by the engine (G1) |
| `cursor`, `credits` | game.c, screens.c | done: `ui/pointer`, `text/<lang>/credits` |
| `town_va`, `arrival_va` | towns.c | done: `data/towns/<year>` |
| `treasure`, `battle_sets` | `world_load()` | done: `data/treasure`, `data/battle-sets` |

`struct world` already holds every table `world_load()` reads, so the
sim needs no new structure: only where the values come from changes.
The UI tables are the opposite case: screens.c calls `exe_at()` at the
point of use and decodes the bytes there, so they need a structure first.

## Rules shared by every format

The same as the open world data (asset-packs.md):

- JSON, read with `sim/json.c`, one ID per group above, named fields
  that say what a value means (the names in `world.h` and
  game-rules.md), never offsets or raw byte blocks.
- A pack that gives an ID gives it whole and valid: a broken or
  incomplete file is an error naming the file and the field, never a
  silent fallback to the original.
- An ID no pack gives comes from the executable, as today.
- Coordinates in layouts are in the 640x480 logical screen, as the
  engine already uses them.
- Values in the open pack are written anew, never copied from the
  original (roadmap, "Licensing"). The repository holds the formats,
  the loaders and the tests, never the original's numbers.

## Plan

### G1: one source for the tables (refactor, no behaviour change)

- Move every `exe_at()` out of `src/ui/screens.c` and `src/ui/game.c`
  into a loader in `src/assets/` that fills plain structures once, at
  game start: `struct layouts` (the seven screen layouts), the story
  scripts as a list of steps, the captain flags. Screens read those
  structures.
- Give `struct scenario` a named `months` field and drop `raw[]`, which
  only `sim.c` reads for that byte.
- `world_load()`: let the pack's tables be read first and the
  executable's only for what is missing, instead of always reading the
  executable and overwriting.
- `sine`: compute it in the engine if a formula reproduces the
  original's 128 bytes exactly (`test_original` checks it); otherwise it
  becomes a field of `data/sea-battle`. A sine is a mathematical
  function, not game data.
- `shade`: derive it from the palette (the nearest darker colour) for a
  pack palette; the original keeps its table. To check: whether a rule
  reproduces the original's table, which would remove the special case.
- Check: every replay identical, `test_original` and `test_ui` pass.
- Size: medium; screens.c is the bulk.

### G2: formats, loaders and export

For each ID in the table above:

- The format, documented in asset-packs.md ("Open game data", next to
  "Open world data").
- The loader, filling the structures of G1.
- `flibustier --export-data DIR`: writes the installed tables in these
  formats with a `pack.ini` fragment, as `--export-world` and
  `--export-text` do. Exported from the original data, it is for local
  checks and as a starting point only: never distribute it.
- Verification, as for T3: `test_original` exports the original's
  tables, loads them back, and compares the structures; the ten-year
  replay through `flibustier-sim --pack <export>` gives byte-identical
  output.

Format notes:

- **`data/stories`:** one script per scenario, a list of steps:
  `{"text": <CSTR index>}`, `"page"`, `"wait"`, `"fight"`, `"ask"`,
  `"end"`. The original's copy-protection question (`)` and `*`, and the
  fail branch) is not part of the format: the engine keeps only the
  pass branch (game-rules.md, "New career: the story").
- **`data/scenarios`:** the fields of `struct scenario` by name, the
  ships as type names, the start cells as `[x, y]`.
- **`data/layout/<screen>`:** rectangles as `[x0, y0, x1, y1]`, with
  the flags the original keeps beside them (`on`) as named booleans. A
  pack that replaces a screen's picture with a different composition
  gives its layout too; otherwise the original's hot spots apply to the
  new picture.
- **`data/era/<year>`:** speeds as 8 ship classes x 16 wind sectors;
  encounters per nation as records with named fields (type, nation,
  crew, guns, role, gold and goods factors).

Size: medium to large; it can land one ID at a time, each with its test.

### G3: start without the executable

- `program/main` becomes optional: game.c, sim_main.c, view.c and
  worldfiles.c load it only when an ID it backs is missing from the
  packs.
- When neither a pack nor the executable gives a table, the error
  names the missing IDs ("no data/sword: give the original DATA.DAT or a
  pack that has it"), instead of "missing or unknown PIRATESG.EXE".
- `flibustier-sim` and `flibustier` accept `--pack` with no `--data`.
- Check: a test runs `flibustier-sim` on the G2 export with no original
  data, and the replay matches.
- Size: small.

### G4: the content (open pack repository)

- Write the values anew for every ID, in the open pack, with a
  provenance record each, as for the art.
- Layouts follow the pack's own pictures.
- Size: large, and mostly design and play-testing, not code.

## Not in this plan, still blocking the no-original milestone

- The sound drivers: the music plays from `driver/psound` bytecode
  where a tune has no recording, and the FM effects need the driver or
  a `sound/` recording each (7 to do). The voices of `driver/digital`
  have no open replacement.
- The open world content (open-terrain T4) and the art left to do
  (open pack `COVERAGE.md`).
- The open pack's licence: still undecided, and the music belongs to
  its author.

## Decisions for the maintainer

1. Should the open values match the original's balance, or are they
   free to differ? This is open-terrain decision 2, and the same answer
   covers both plans.
2. ~~Format of the tables?~~ Proposed: JSON, one ID per group, as the
   open world data. Open to change before G2.
