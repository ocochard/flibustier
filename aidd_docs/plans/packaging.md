# Packaging: a playable game on Windows, FreeBSD, macOS and Linux

Goal: one download per platform that plays the whole game from the open
pack, with no original data, and still reads a player's own GOG
`DATA.DAT` when given.

## Where it stands (2026-10-30)

- The engine builds on all four systems (README); Windows through the
  llvm-mingw cross toolchain, its tests run under Wine.
- The open game data is complete (open-game-data.md, G1-G4): the game
  and `flibustier-sim` run from the open pack alone (`--pack`, no
  `--data`), and the career bot plays full twelve-year careers on it.
- The open pack (`~/flibustier-openpack`, 227 MB: art 155, music 58,
  data 5, provenance 8) is a draft: its pictures are 8-step renders.
- No install rules, no release builds, no CI. The game finds its data
  only through `--data`, `--pack` or `FLIBUSTIER_DATA`; saves default to
  `~/.local/share/flibustier/saves` on every system.

## Blockers outside the code

1. **The art's licence** (maintainer). Nothing may ship before the pack
   has a licence file. Candidates: CC BY-SA 4.0, CC BY 4.0, CC0. The
   pictures are machine-generated from text prompts, so their copyright
   status is uncertain in several countries; CC0 sidesteps that
   question, CC BY-SA keeps derivatives open where rights exist.
2. **The music** belongs to its author (`music/CREDITS.txt`): written
   permission to redistribute it in the packages, or packages without
   it (P1 decides what plays then).
3. **Final renders**: the draft pictures are to be re-rendered at full
   quality before a 1.0 (the art session's work, not this plan's).

## Phases

### P1. Gaps of the open pack alone

Done (2026-10-30) but for one tune:

- The 7 spoken sound effects (`sound/hail`, `sail-ho`, `harbour`,
  `land-battle`, `last-man`, `hit-foe`, `hit-you`) voiced by
  `tools/sfxgen/voicegen.py` (Dia2) from `voices.toml`; still to be
  heard by a person (`last-man`, the surrender, may need another take).
- Text entry 318, the cabin's language menu, in the three languages.
- `tools/artgen/coverage.py` counts the original assets the pack
  replaces in another form as done (`tiles15/sbtl`, `lbtl` by the
  Tiled battle screens, `pic/map1560` by `map/1560`, `pic/baseflag`,
  whose palette trick true-colour flags do not need).
- Left: `music/prize` (the capture's tune) has no remake; the music's
  author would have to record it, or another remade tune stands in.
  Without it the moment is silent with no original data.

### P2. The engine finds its own data

Each package holds the program and the open pack side by side (a zip
or a self-extracting archive, a macOS application bundle), so the
program looks for its pack beside itself (maintainer, 2026-10-30).

- **Done:** with no `--pack`, the game adds the `openpack` directory
  beside its program (`<dir>/openpack/pack.ini`), or in a macOS
  bundle `Contents/Resources/openpack`. The program's directory comes
  from the system (`src/data/progdir.c`: Windows, macOS, FreeBSD,
  Linux), else `argv[0]`. `--data` and `FLIBUSTIER_DATA` still add a
  player's own data under it; `--pack` replaces it.
- **Saves:** still `--saves`, `FLIBUSTIER_SAVES`, else
  `~/.local/share/flibustier/saves`; on Windows and macOS that path is
  to move to `%APPDATA%\Flibustier` and
  `~/Library/Application Support/Flibustier` with the packages (P3).

### P3. Install rules

- CMake `install()` for `flibustier` (and `flibustier-sim` as a tool),
  the open pack under the data place of P2, the licence files (engine
  BSD-2-Clause, the pack's, the fonts' OFL, the music's terms) and a
  short player README.
- Release build: `-DCMAKE_BUILD_TYPE=Release`, raylib and FreeType
  linked statically where the platform allows, so the packages carry
  no library the player must install.

### P4. One package per platform

| System | Package | Built on | Notes |
|---|---|---|---|
| Windows | zip (and later an installer) | FreeBSD, llvm-mingw cross build (as now) | the pack beside the `.exe`; checked under Wine, then on a real Windows |
| macOS | `Flibustier.app` in a `.dmg` | a Mac (or CI macOS runner) | universal binary (arm64 + x86_64); unsigned at first: Gatekeeper asks once; signing and notarisation need a paid Apple developer account (maintainer) |
| Linux | AppImage, and a `.tar.gz` | CI Linux runner, oldest glibc in use | Flatpak later (Flathub) |
| FreeBSD | a port (`games/flibustier`, pack as `games/flibustier-openpack`) | this host | the pack as its own port: it changes less often and is large |

CPack makes the zip, tarball and dmg from the P3 rules; the AppImage
and the FreeBSD port have their own tools.

### P5. Releases

- Versions: the engine and the pack tagged apart (`v0.x`,
  `openpack-0.x`); a package names both.
- CI (GitHub Actions): build and test on Linux, macOS and Windows (the
  mingw cross build), make the packages on a tag, attach them to the
  GitHub release. The pack's art comes from Git LFS: CI must fetch it
  (`git lfs pull`), within the free LFS bandwidth or from a release
  asset instead.
- Each package is checked by starting the game headless from the
  package (`flibustier --script ...`) before it is attached.

## Order

P1 and P2 first (they make the open game whole and self-starting,
whatever the licences); P3-P5 once blockers 1 and 2 are settled, since
nothing can be published before.

## Decisions for the maintainer

1. The art's licence (blocker 1).
2. The music: permission from its author, or packages without it.
3. macOS signing and notarisation: pay for an Apple developer account,
   or ship unsigned.
4. Where releases live: GitHub releases, plus the FreeBSD port.
