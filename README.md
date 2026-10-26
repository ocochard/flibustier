# stmalo

An open source engine re-implementation of **Pirates! Gold**, MicroProse's
1993 remake of Sid Meier's *Pirates!*, built on [raylib](https://www.raylib.com/).

The goal is to play the original game on modern systems (FreeBSD, Linux,
macOS, Windows) with faithful gameplay, while allowing fixes and quality of
life improvements the original never had.

## Status

Early stage: no playable code yet.

## Game data

This project ships **no** original game assets. Graphics, music, text and
other data of Pirates! Gold remain the property of their copyright holders.
To play, you will need a legally obtained copy of the original game; the
engine will load its data files at runtime.

Assets are organized in layered packs: any original asset can be replaced
by a pack using modern formats (PNG, OGG, ...), for example remixed music.
The long-term goal is a complete set of assets released under an open
license, so the game can run without any original data.

Do not open issues or pull requests containing original game files.

## Building

Requirements:

- A C99 compiler (clang or gcc)
- CMake 3.16 or later
- [raylib](https://github.com/raysan5/raylib) 5.5 or later (optional: if
  not installed, CMake downloads and builds raylib 5.5). On FreeBSD:
  `pkg install cmake raylib`

```sh
cmake -B build
cmake --build build
./build/stmalo
```

Run the tests with `ctest --test-dir build`. Tests that need the original
game data are skipped unless `STMALO_DATA` points to the GOG `DATA.DAT`
(or its install directory, or an extracted copy of the CD):

```sh
STMALO_DATA=~/games/pirates_gold ctest --test-dir build
```

## Running

```sh
./build/stmalo --data ~/games/pirates_gold [--lang eng|fre|ger]
```

So far this runs the start of the game: the intro, the welcome menu, and
the choices that create a new career or a famous expedition, then
sailing the Caribbean: steer with the arrow keys, sail into a town's
harbour, meet ships at sea and fight them (Left/Right steer,
Enter fires, Up/Down set full or battle sails), board them and fight
their captain (Left attacks with a lunge, Shift+Left with a slash,
Up/Down pick the line, Right steps back), and Escape opens the menu
(save game, exit game). "Exit game" offers the Hall of Fame. Saves (one
JSON file per slot) and the Hall of Fame (a tab-separated text file) go
to `~/.local/share/stmalo/saves`, or wherever `--saves DIR` says.
`--script "enter down enter ..." out.png` plays the same screens without
a window and saves the last one, for tests and bots.

## Asset viewer

A viewer for the original assets:

```sh
./build/stmalo --data ~/games/pirates_gold --viewer
```

Up/Down picks an asset, Left/Right steps through its frames, PgUp/PgDn
pages (in the sailing world views, they scroll vertically), Tab changes
the palette of sprites, and typing filters the list. Besides the
original files, `world/<year>` shows the sailing world of an era with
its animated sea, and `towns/<year>` the era's towns on a terrain map.

`--list` prints every asset ID. `--export ID out.png` renders one asset
without opening a window. Use `--frame N` to pick the frame and
`--tick N` the animation step. `--pack DIR` adds an asset pack on top of
the original data.

## Headless simulation

`stmalo-sim` runs the game rules without graphics, driven by JSON
commands on stdin, for bots, AI agents and tests:

```sh
echo '{"cmd":"new_game","era":1660,"seed":7}
{"cmd":"advance","days":360}
{"cmd":"state"}' | ./build/stmalo-sim --data ~/games/pirates_gold
```

The protocol is described in
`aidd_docs/memory/internal/sim-protocol.md`.

## Methodology and legal

stmalo is an interoperable re-implementation. It does not contain the
original game; it reads the files of a copy the player owns.

How the original is studied:

- **File formats** are worked out by examining the game's data files and
  checking each decoder against every file of its kind.
- **Behaviour that only exists as code** (game rules, data tables inside
  the executable, palette animation) is worked out by disassembling and
  decompiling our own copy of the game's executable with
  [Ghidra](https://ghidra-sre.org/). This is done for interoperability:
  to load the original data and reproduce the game's behaviour.

How findings become code:

- Every finding is written down as a specification in plain prose:
  file formats, data table layouts and game rules, with evidence (see
  `aidd_docs/memory/internal/`).
- The engine is written from those specifications. It is not a
  translation of the original program, and it does not reuse its code.
- Numbers that are game data (town tables, start scenarios) are not
  copied into the source. The engine reads them at runtime from the
  player's executable, which it recognises by checksum.

What the repository never contains:

- original graphics, music, sound or text;
- original program code, whether disassembled, decompiled or
  transcribed;
- the output of the analysis tools. `tools/ghidra/` writes it outside
  the repository, and it must stay there.

This is how we read the rules on interoperability, not legal advice. If
you hold rights in the original game and have a concern, please open an
issue.

## Contributing

Contributions are welcome. Please follow the method above: document
behaviour as a specification, then implement from it, and never submit
original assets or decompiled code.

## License

The engine source code is released under the BSD 2-Clause License. See
[LICENSE](LICENSE).

"Pirates!" and "Pirates! Gold" are trademarks of their respective owners.
This project is not affiliated with or endorsed by them.
