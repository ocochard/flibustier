# stmalo roadmap

Status: draft, 2026-10-25.

The formats are described in
[asset-formats](../memory/internal/asset-formats.md).

## Goal

Play Pirates! Gold on modern systems with faithful gameplay, written in C99
with raylib, under the BSD-2-Clause licence. No original data ever enters
the repository.

The engine runs from asset packs:

1. Today, it runs from the user's own GOG `DATA.DAT`.
2. Replacement packs can override any part of it, for example remixed
   music.
3. Later, a fully open pack regenerated under an open licence will let it
   run with no original data at all.

## Where we stand

- Every graphics format is decoded and proven: PCX, RSD, SPR, CRS, CUR,
  GRP, IMG, IMB, CHR, and the BMP tiles.
- The text tables are decoded.
- The town tables in the executable are decoded for all six eras. The
  field meanings come from the game code.
- The executable is uncompressed and decompiles with Ghidra 12.2, both
  headless and through REA.
- Rules that only exist as code are unknown: sailing, wind, combat,
  fencing, economy, events, AI, and saves. The sea palette and the
  sailing world grid are now known.
- The music is a per-driver bytecode, not yet decoded.

## Key design decisions

1. **Layered asset packs, addressed by logical IDs.**
   - Game code asks for `music/title`, `pic/town`, `spr/ship0a` or
     `text/eng/cstr`, never for an original file name.
   - Each pack is a directory (later also a zip) with a manifest that maps
     IDs to files.
   - Packs stack in priority order: user overrides, then optional packs,
     then the open base pack, then the original data. The first pack that
     provides an ID wins, so a pack may override a single asset.
   - The original data is one pack among others. It has a built-in
     manifest and is read straight from `DATA.DAT` with a small ISO 9660
     reader, so the user doesn't have to extract it. Loose files also work.
   - Each asset kind has two kinds of loaders:
     - native formats for new packs: PNG, plus OGG, FLAC, MP3, WAV or QOA
       for audio (all supported by raylib), and plain-text data files;
     - original formats: PCX, SPR, IMB and so on.

     Both produce the same in-memory types.
   - Music IDs follow the 20 tunes the original jukebox lists (A-T), so a
     remix pack maps one file per tune and the engine streams it with
     raylib `Music`.
   - Game data (towns per era, ship stats, scenarios, strings) has its own
     documented open text format. The original-data pack fills it from the
     executable and the `CSTR`/`CSTN` files at load time. An open pack ships
     the same format directly.
   - A pack declares its licence in its manifest, and the viewer shows it.
     Third-party packs, such as a fan remix, must carry their own
     redistribution rights.
2. **Identify the executable by its SHA-256.** Data offsets in
   `PIRATESG.EXE` are per build, so they live in a per-hash table. Refuse
   unknown builds with a clear message.
3. **Logical 640x480 screen, rendered natively up to 4K on the CPU.**
   - Game code draws in 640x480 coordinates, so layouts and hotspots
     follow the original. The output is 4:3, pillarboxed on wide screens
     (maintainer, 2026-10-26).
   - The screen (`src/gfx/fb.c`) keeps the 640x480 palette indices that
     game code reads, plus a canvas at the output resolution (1440x1080
     for 1080p, 2880x2160 for 4K). Each canvas pixel holds a palette
     index or a true colour:
     - original assets are enlarged as indices, so palette cycling and
       fades still work on them;
     - replacement art is sampled at the canvas resolution.
   - The output height is the largest `art_height` of the packs
     (`asset-packs.md`).
   - `fb_to_rgb` converts on the CPU, so the same frame goes to a window,
     a PNG or a test without a GPU (maintainer's choice over a GPU
     renderer, 2026-10-26).
     - Cost per frame (Ryzen 7 7735HS, Release): 0.5 ms at 640x480,
       3.7 ms at 1080p, 14.7 ms at 4K. 4K needs dirty regions or threads
       before it holds 60 fps with uploads.
   - Effects that depend on the palette, such as sea animation, are
     expressed as an engine effect (palette cycle or animated texture), so
     a new pack can supply its own version.
   - Sprites have a replacement format, sprite sheets, drawn at canvas
     resolution with alpha (`asset-packs.md`).
   - Still original resolution: fonts, tiles and `img/`. They need
     replacement formats (scalable fonts, tile sheets).
4. **Separate simulation from presentation.** Game rules live in a module
   with no raylib dependency (pure C99, deterministic, seeded RNG), so they
   can be unit tested and compared against traces of the original.
5. **Headless and drivable by bots.** Decision 4 makes this possible, but
   these pieces are needed on top of it:
   - Game logic advances only through explicit commands, such as "set
     sail heading N", "choose menu item 2" or "attack high", and a fixed
     tick. It never polls the keyboard or the clock directly. The raylib
     front end turns input into commands; a bot sends the same commands.
   - `stmalo-sim`: a separate binary with no raylib and no window. It reads
     commands and writes state over stdin/stdout as line-based JSON. Bots,
     AI agents and CI tests drive it.
   - State snapshots (dump and load the whole game state as text) and
     replays (seed plus command log, reproduced exactly) make bugs
     reproducible. Comparing a state against an expected state gives
     regression tests.
   - Optional headless screenshots: the screen is drawn on the CPU at
     any resolution, so it can be written to PNG without a GPU.
   - Debug cheats exist only in the sim API: teleport, set gold, force an
     encounter.
6. **Audio through licence-compatible emulators only.** Use ymfm
   (BSD-3-Clause) or Opal (public domain) for OPL. Nuked-OPL3 and munt are
   LGPL, so they would only be optional, separately linked backends. GPL
   code (DOSBox OPL) is excluded. Check each licence from its repository
   before adopting it.

## Phases

Each phase ends with something visible and a test.

### Phase 1: data access and asset viewer

Status (2026-10-26): done, apart from native-format loaders for packs.
PNG pictures (`pic/`) and sprite sheets load; OGG does not yet.


- `src/data/`: the pack system (manifest parser, priority stack, logical
  ID lookup), the ISO 9660 reader, executable identification, and pack
  path settings (command line or config file).
- Define the open game-data format and the manifest format. Document both
  in `aidd_docs/`.
- `src/assets/`: loaders for PCX, RSD, sprite banks (SPR, CRS, CUR, BMP),
  GRP, IMG, IMB, CHR, CSTR/CSTN (converted from Mac Roman to UTF-8), and the
  town tables.
- `stmalo --viewer`: browse every asset with its palette, frames and
  strings.
- Tests: run against the user's own data and skip when it is absent.
  Checks are structural (frame counts, dimensions, parse to EOF) plus
  hashes of the decoded output.

### Phase 1b: simulation core and headless harness

Status (2026-10-26): harness done. `stmalo-sim` runs the monthly world
update: town growth, conquest, diplomacy and random town events, as
specified in `game-rules.md`. Treasure fleets and anything involving the
player are still to come.


- `src/sim/`: game state structure, command type, a `step` function, a
  seeded RNG, and text serialisation of the state.
- `stmalo-sim`: the stdin/stdout JSON protocol, replay files, and a first
  CTest suite that replays scripted sessions.
- Every later gameplay phase adds its commands and tests here first, and
  its screens second.

### Phase 2: presentation shell

Status (2026-10-26): start flow done, both in a window and headless:
- intro, welcome menu, then era, nationality, name, difficulty and
  ability for a new career, or one of the famous expeditions;
- the choices create the simulation.

Saved games work (2026-10-26): 10 slots, each a JSON state file, saved
from a stand-in in-game menu and loaded from the welcome menu. The
original's save layout is documented at block level, so an importer is
possible later.

The Hall of Fame works (2026-10-26): "Exit game" asks the original
question, then records the career with a stand-in score, until the real
score's inputs exist.

Still to do: the title sprites, and the portrait positions, which are a
guess (see `src/ui/screens.c`).


- Indexed framebuffer, palette shader, fades.
- Text renderer: PIRATE1-4 fonts, drop capitals, the markup (`~` menus,
  `` ` ``, `^`, `%s`).
- Screen and state manager; mouse and keyboard input.
- Title screen, language choice, the career and expedition start flow
  (requires decoding the 90-byte scenario records), and the Hall of Fame
  shell.

### Phase 3: reverse-engineering groundwork (in parallel with phases 1-2)

- Ghidra 12.2 works headless: `tools/ghidra/analyze.sh` carves the flat
  image, analyses it as raw i386 at base 0, and decompiles all 639
  functions it finds, in about 25 seconds. Keep its output outside the
  repository, since it is derived from the original program.
- REA (MCP) also works on the same flat image, opened as `raw-binary`,
  `x86`, entry `0x0`. Use it for interactive work (xrefs, decompiling one
  function, annotations) and `analyze.sh` for whole-program greppable
  dumps.
- Next steps: label the C runtime, name the file-loading functions (from
  the filename table), and find functions Ghidra missed. Raw-binary
  analysis only follows calls reachable from the entry point.
- Capture reference behaviour in DOSBox-staging: screenshots, save files,
  and tile coordinates through the `TileX/TileY` debug string if it can be
  enabled.
- Priority targets:
  1. ~~Sea palette ramp and cycling.~~ Done: tileset palette rotation.
  2. ~~How the sailing world grid is built.~~ Done: the era map is a
     tile grid.
  3. Ship stats.
  4. Save file format.
  5. Remaining town record fields.
- Write each finding into `asset-formats.md` or a new
  `aidd_docs/memory/internal/game-rules.md`. Contributors never copy code;
  they document behaviour.

### Phase 4: sailing the Caribbean

Status (2026-10-26): first slice done.
- **Traced and specified:** steering, speed by point of sail and ship
  class, the wind, collisions through the terrain classes, the world
  edges and the view.
- **Implemented:** in the simulation (`sail` command) and on screen.
  The sailing screen shows the tiles, the animated sea, the ship and the
  wind, and arriving at a town opens the stand-in town menu.

Encounters done (2026-10-26): sightings as the game rolls them, the
ship picked from the era's records, and the "Sail ho!" dialogs over the
lookout picture, the ship's portrait and her flag, with sailing away,
hailing for news, or closing for battle.

Still to do: shoals and running aground, storms (the speed effects of
VA `0x4d292`), food, night, the overview map and landing parties.


- World map, town placement per era, wind and points of sail per ship
  type, shoals and running aground, day and month progression, fleet
  encounters ("Sail ho!"), landing, and the overview map with RSD era
  patches.

### Phase 5: towns

Status (2026-10-26): first slice.
- **Done:** the town screen (buildings, labels, keyboard, which are
  open), city information, leaving, and the captain's cabin (our travel
  menu for now). In the simulation: entering and leaving a town, and the
  visit flags.
- **Shipwright:** repairs, ship sales and cannon trading, with his
  gold per stay.
- **Bank:** dividing the plunder, the treasure cave, another expedition
  (a refit, optionally at a higher difficulty) or retirement.
- **Merchant:** cargo (food, goods, sugar, hold capacity) and trading
  on the original board. The daughter's subplot and "Take all" wait.
- **Tavern:** the people, recruiting crew, the bartender's rumours, and
  the reputation. The map and information sellers, the barmaid and the
  stranger wait.
- **Governor:** the political news, standing changes from captures,
  promotions with land, Letters of Marque and pardons. Missions, the
  family quest, prisoners and his daughter wait.
- **Arrival:** the menu, sailing in under the fort's guns, losing a
  ship with its cargo share, sneaking in and the guard's duel in the
  alley, and the disgrace.
- **Battle field:** the original's prebuilt screens chosen by the
  nearest coast, scrolling, the shallows and beaching.
- **Fort battle:** attacking a fortified town from the sea.
- **Assault and sack:** storming a town, the plunder, installing a
  governor, prison after a defeat.
- **Landing parties:** going ashore, walking, inland towns, back aboard.
- **Tavern:** the traveller, the map seller and the barmaid.
- **Land battle:** attacking on foot.
- **Treasure maps:** pieces, the map page, digging; the relative's
  quest waits for the mystery man.
- **Mystery man:** the relatives' villains and the governors' missions.
- **Crew's mood:** warnings, mutiny, desertion; dividing parties is dead
  in the original.
- **Prison:** the stay, health, marooning, the career's ends.
- **Map of the Caribbean.**
- **Log book:** its menu and status pages.
- **Named captains:** the hail, hostages, prisoners, ransoms.
- **Next:** the treasure fleet and silver train, the governor's
  daughter and marriage.

- Governor (titles, missions, daughter), tavern (rumours, crew, maps),
  merchant (trade economy), shipwright, bank and ship log screens.
- Town wealth, population and nation changes, and town attacks.

### Phase 6: combat

Status (2026-10-26): sea battles done.
- **Traced:** setup, speed, steering, sails, gunnery, hit boxes,
  damage, the enemy's decisions and the outcomes (see game-rules).
- **Implemented:** in the simulation (`battle` and `finish` commands)
  and on the battle screen: the encounter's corner of the map in battle
  tiles, both ships, shots, and the side panel with men, guns, sails,
  damage pictures and the wind.
- **Stand-ins:** boarding is decided by crews, and prizes pay a fixed
  sum, until sword fights and cargo exist.

Sword fights done (2026-10-26):
- **Traced:** the shared routine for boarding, duels and practice:
  moves, reach, hit rolls, the strength bar, the crew melee and the
  opponent's scripts (see game-rules).
- **Implemented:** boarding in the simulation (`board` and `sword`
  commands) and on screen: the boarding picture, the weapon choice, and
  the fight on the ship's deck with the fighters, flags and wounds.
- **Not yet:** duels, practice, prison after a lost boarding.

- Sea battle: BATTLE.IMB tiles, the `*BTL.CUR` ships, broadsides, damage.
- Boarding, leading into sword fighting: GRP composition and the 16 moves.
- Land battle (LBTL tiles).
- Fort assault.

### Phase 7: career and narrative

- Fame and titles, the family-member quest, treasure maps, the evil
  Spaniard, crew morale and division of plunder, aging and health,
  retirement and its ranking, and save and load (ideally compatible with
  original saves).

### Phase 8: audio

- Streamed music from packs (raylib `Music`), keyed by tune ID. This
  comes first, because a remix pack then provides music without any
  reverse engineering.
- Sound effects from DIGITAL.PIR (PCM through raylib `Wave`), or from
  packs.
- Decode the ASOUND bytecode (instruments, voice streams, F0-FF commands).
  Then write a clean C player that feeds OPL register writes to ymfm or
  Opal. PSOUND (OPL3 stereo) can be a second target.
- Fallback if decoding stalls: a minimal 8086 interpreter that runs the
  original real-mode driver with trapped I/O. It is faithful, but it is a
  few thousand lines of code to own.
- Optional: MT-32 through munt with user-supplied ROMs.

### Phase 9: open asset pack

- Regenerate every asset under an open licence, following the original's
  asset list: art, fonts, music, sound effects and game data.
- Tooling: a pack validator that lists IDs that are missing or unused.
- Milestone: the game runs with no original data installed.

### Phase 10: polish

- Configurable scaling and filters, widescreen-safe UI, remappable keys,
  and gamepad.
- Packaging for FreeBSD ports, Linux, macOS and Windows; CI builds.

## Risks

- **Game rules depend on disassembly.** This is the critical path for
  phases 4-7 and is blocked on tooling. Mitigation: start phase 3 early,
  and prioritise the targets that unblock sailing.
- **Fidelity cannot be checked without references.** Mitigation: use
  DOSBox captures and comparisons against original saves.
- **Only one executable build is known.** Other releases (floppy version,
  other languages) need their own offset tables.
- **Licensing.** No original assets, no decompiled code and no original
  strings in the repository. Game data and text in the open pack must be
  rewritten, not copied. Licences of third-party libraries are checked before
  adoption.

## Open questions for the maintainer

- ~~Must saves be compatible with the original game?~~ No (maintainer,
  2026-10-26). Saves use a simple text format of their own.
- ~~Should the 16-colour art mode be supported?~~ No (maintainer,
  2026-10-26). The engine uses only the 256-colour art and code paths.
- Should the open pack live in this repository, or in a separate
  repository with its own licence (for example CC BY-SA for art and
  music)?
- What is the minimum raylib version and the platform list for CI?
