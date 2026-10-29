# AGENTS.md

This file provides guidance to AI agents when working with code in this repository.

## What this is

Clean-room, open source engine re-implementation of Pirates! Gold (C99, raylib, BSD-2-Clause). It ships no original assets or code: it reads the player's own GOG `DATA.DAT` at runtime. Never add original graphics, music, text, decompiled code, Ghidra output, or game-data numbers (town tables, scenarios) to the repo. Rules are written as prose specs first, then implemented from the spec.

## Commands

```sh
cmake -B build && cmake --build build          # raylib/FreeType fetched if not installed
ctest --test-dir build                          # tests needing game data are skipped without FLIBUSTIER_DATA
FLIBUSTIER_DATA=~/games/pirates_gold ctest --test-dir build
ctest --test-dir build -R test_sim              # one test (test_formats, test_original, test_sim, test_ui, replay_ten_years)
cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DMINGW_ROOT=$HOME/opt/llvm-mingw -DMINGW_CLANG_SUFFIX=19   # Windows .exe (README); ctest runs it via wine
FLIBUSTIER_DATA=... ./build/test_sim            # or run a test binary directly
```

Portability: POSIX calls Windows spells differently (`mkdir`, replacing `rename`, `mkdtemp`) go through `src/data/fsys.h`; open files in binary mode (`"rb"`/`"wb"`) so outputs match byte for byte across systems.

No linter; warnings are `-Wall -Wextra -Wpedantic` (C99, no extensions). Default build type is Debug.

Binaries:
- `flibustier --data DIR [--lang eng|fre|ger] [--crt] [--smooth] [--viewer] [--list] [--export ID out.png] [--pack DIR] [--saves DIR]`: game and asset viewer.
- `flibustier --script "enter down enter ..." out.png`: plays the UI headless and saves the last screen (used by tests and bots).
- `flibustier-sim [--data DIR] [--pack DIR]...`: headless rules engine, JSON lines on stdin/stdout; runs from packs alone when they give the open game data.
- `flibustier-seamap`: sea map helper for the career bot.
- `python3 tools/career-bot/career.py --data DIR --seed 3`: plays a full career through `flibustier-sim`, writes `journal-*.txt` (gitignored); `ANOMALY` lines flag sim problems.

## Architecture

Everything except the raylib front end is in the static lib `flibustier_core` (no raylib dependency), so tools and tests link it headless. `src/main.c`, `play.c`, `crt.c`, `viewer.c` are the only raylib code.

Layers (`src/`):
- `data/`: ISO 9660 reader, executable reader (`exe.c`, recognised by SHA-256, supplies game tables at runtime), and the **pack stack** (`pack.c`). Game code loads assets by logical ID (`pic/<name>`, `spr/<name>`, `text/<lang>/<name>`...), never by filename; the last-added pack wins per ID, original data is just one pack.
- `assets/`: one decoder per original format (PCX, RSD, SPR, IMB, CHR, ...) plus world/towns/scenario loaders. Each decoder is checked against every original file of its kind in `tests/test_original.c`.
- `sim/`: the game rules, no graphics. `sim.c` owns the world/state and monthly world update; `battle`, `sword`, `land`, `town`, `treasure`, `mission`, `crew`, `prison`, `hof` are rule modules sharing `internal.h`. `proto.c` is the JSON command protocol, `save.c` JSON saves, `rng.c` deterministic RNG (same seed + commands => same output).
- `gfx/`, `view/`: software framebuffer (`fb`), sea palette animation, text, world views.
- `ui/`: the game shell, driven by `struct ui_event`s. `game.c` holds state; `screens.c` (huge) defines each `struct screen` {enter, event, tick, draw} declared in `screen.h`. `script.c` is the headless script driver.
- `audio/`, `external/ymfm`: OPL3 music emulation (the only C++, with a C wrapper in `opl3.cpp`) and vendored libs, exempt from warnings.

New `.c` files must be added to the `flibustier_core` list in `CMakeLists.txt` by hand (no globbing, except replays).

Replay tests: `tests/replays/*.jsonl` are fed twice to `flibustier-sim` via `tests/replay.cmake`; every command must succeed and both outputs must be identical.

## Specs (read before touching rules or formats)

`aidd_docs/memory/internal/`: `game-rules.md` (rules with VA references into the original EXE, and what could not be traced), `asset-formats.md`, `asset-packs.md` (ID scheme, pack stack), `sim-protocol.md` (JSON commands), `text-catalogue.md` (the role of each text entry, for open pack writers). Plans in `aidd_docs/plans/` (`roadmap.md`, `open-terrain.md`, `open-game-data.md`). Update the matching spec when behaviour changes.

## Conventions

- Source files carry an SPDX BSD-2-Clause header with `Copyright (c) 2026, Olivier Cochard-Labbé`.
- BSD/FreeBSD-style C (tabs, return type on its own line for definitions). Tools are Python 3 (`tools/artgen` renders the open art pack via ComfyUI; `tools/ghidra` writes analysis output outside the repo).
