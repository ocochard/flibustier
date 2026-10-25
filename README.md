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

## Asset viewer

The only feature so far is a viewer for the original assets:

```sh
./build/stmalo --data ~/games/pirates_gold
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

## Contributing

Contributions are welcome. Please work only from your own analysis of the
game's behavior and file formats; do not submit decompiled or copied
original code.

## License

The engine source code is released under the BSD 2-Clause License. See
[LICENSE](LICENSE).

"Pirates!" and "Pirates! Gold" are trademarks of their respective owners.
This project is not affiliated with or endorsed by them.
