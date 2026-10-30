#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""The open tile sheets' terrain: art/map-terrain.png,
art/battle-terrain.png, art/map-sections.ini and art/battle-sections.ini.

Derived from the open pack's own tile art (art/map.png, art/battle.png)
by colour, nothing else.  The art is read from the directory in the
TILE_ART environment variable, or else from the art/ of the pack
opendata.py writes (OPENDATA_PACK).

Each art pixel of frame 0 gets a class (the sea's animation never moves
the coastline, so frame 0 stands for all):

  - water: blue over red by more than 15 and over green by at least 15.
    This keeps the white wave crests (blue still leads) as water, and
    the sand, foliage, roofs (b <= r) and the grey-blue shade of walls
    and stone (b about g) as land.
  - shallows: teal water: green at least 0.62 of blue and 45 over red.
    The open sea's green is about 0.48 of its blue; the lighter halo the
    art paints along the shores rises to about 0.8 at the beach, and
    0.62 takes its inner half.  The halo's green is 55 to 75 over its
    red, the soft edges of the open sea's wave crests only 25 to 37
    (they whiten all three channels), so they stay sea.
  - sea: the other water; land: everything that is not water.

The battle sheet's water is all sea: its shore halo is where ships come
in to land, and shallows there would wreck them in front of every fort.

A terrain pixel (640x480) covers 2.25 x 2.25 art pixels at 1080p; it
takes the class with the largest covered area, ties going to land, then
shallows.

Map shores ([coast]): a tile with at least 5% land whose east, south or
west edge (its outer three terrain pixels) is at least 75% sea or
shallows; of those, the most open edge, ties in the order south, east,
west.  The rules step one tile that way from the shore and put a new
ship there (src/sim/prison.c).

Battle ground ([ground]), for the land battles (src/sim/land.c):

  - water: tiles at least half water.
  - cover: tiles with a building or a rock: at least 3% roof red (red
    over green and blue by 30) or 10% grey stone (channels within 20).
  - swamp: other land tiles with 2% to 50% water in pools that keep off
    the tile's edge (a marsh, not a coast).
  - clear: the rest, including the plain jungle that fills most land
    tiles: it is the sheet's base ground and reads the same on every
    tile, so it cannot mark cover in some places and not others.
  - Woods and marsh: the tiles that tools/artgen/assets.toml places the
    "woods" and "marsh" cut-outs on (tiles/battle, "place") are cover and
    swamp, whatever their colours say: the art was painted to show them.
"""
import os
import struct
import subprocess
import tomllib
import zlib

import numpy as np

SEA, SHALLOWS, LAND = 0, 1, 2
COLOURS = {SEA: (0, 0, 255), SHALLOWS: (0, 255, 255), LAND: (0, 255, 0)}
SHALLOW_RATIO = 0.62
TEAL = 45


def art_dir():
    if os.environ.get("TILE_ART"):
        return os.environ["TILE_ART"]
    if os.environ.get("OPENDATA_PACK"):
        return os.path.join(os.environ["OPENDATA_PACK"], "art")
    raise SystemExit("tile_terrain: set TILE_ART or run from opendata.py")


def load(path):
    """The PNG at path as an (h, w, 3) int array, through ImageMagick."""
    size = subprocess.run(["magick", "identify", "-format", "%w %h",
                           path + "[0]"], capture_output=True, text=True,
                          check=True).stdout.split()
    w, h = int(size[0]), int(size[1])
    raw = subprocess.run(["magick", path + "[0]", "-depth", "8", "rgb:-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.uint8).reshape(h, w, 3).astype(np.int32)


def png(rgb):
    """An 8 bit RGB PNG of the (h, w, 3) array, with no dated chunks."""
    h, w = rgb.shape[:2]
    rows = b"".join(b"\0" + rgb[y].astype(np.uint8).tobytes()
                    for y in range(h))

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def water_of(f):
    r, g, b = f[..., 0], f[..., 1], f[..., 2]
    return (b > r + 15) & (b >= g + 15)


def classes(f):
    """Per art pixel: SEA, SHALLOWS or LAND."""
    r, g, b = f[..., 0], f[..., 1], f[..., 2]
    water = water_of(f)
    shallow = water & (g - r >= TEAL) & (g >= SHALLOW_RATIO * b)
    c = np.full(f.shape[:2], LAND, np.int32)
    c[water] = SEA
    c[shallow] = SHALLOWS
    return c


def coverage(art, out):
    """(out, art) matrix: how much of art pixel k output pixel i covers."""
    m = np.zeros((out, art))
    s = art / out
    for i in range(out):
        lo, hi = i * s, (i + 1) * s
        for k in range(int(lo), min(art, int(np.ceil(hi)))):
            m[i, k] = max(0.0, min(hi, k + 1) - max(lo, k))
    return m


def sheet(name, out_side):
    """The sheet's tiles: art (frame 0) and terrain classes per tile."""
    a = load(os.path.join(art_dir(), name))
    side = a.shape[1] // 64
    m = coverage(side, out_side)
    tiles = []
    for t in range(256):
        f = a[(t // 64) * side:(t // 64 + 1) * side,
              (t % 64) * side:(t % 64 + 1) * side]
        c = classes(f)
        score = np.stack([m @ (c == k).astype(float) @ m.T
                          for k in (SEA, SHALLOWS, LAND)])
        # Ties go to land, then shallows.
        score += np.array([0, 1e-9, 2e-9])[:, None, None]
        tiles.append((f, score.argmax(0)))
    return tiles


def mask_png(tiles, out_side):
    img = np.zeros((4 * out_side, 64 * out_side, 3), np.int32)
    for t, (_, cls) in enumerate(tiles):
        y, x = (t // 64) * out_side, (t % 64) * out_side
        for k, col in COLOURS.items():
            img[y:y + out_side, x:x + out_side][cls == k] = col
    return png(img)


def counts(tiles):
    n = [0, 0, 0]
    pure = {"sea": 0, "land": 0, "mixed": 0}
    for _, cls in tiles:
        for k in range(3):
            n[k] += int((cls == k).sum())
        if (cls == LAND).all():
            pure["land"] += 1
        elif not (cls == LAND).any() and not (cls == SHALLOWS).any():
            pure["sea"] += 1
        else:
            pure["mixed"] += 1
    return ("pixels sea %d, shallows %d, land %d; tiles all sea %d, all "
            "land %d, mixed %d" % (n[0], n[1], n[2], pure["sea"],
                                   pure["land"], pure["mixed"]))


_cache = {}


def notes(*paragraphs):
    """The docstring's paragraphs, on one line, for the provenance."""
    parts = __doc__.split("\n\n")
    return " ".join(" ".join(parts[i].split()) for i in paragraphs)


def tiles_of(name, side):
    if name not in _cache:
        _cache[name] = sheet(name, side)
    return _cache[name]


def map_terrain():
    tiles = tiles_of("map.png", 16)
    return mask_png(tiles, 16), notes(2, 3, 5) + " Map: " + counts(tiles) + "."


def battle_terrain():
    # The battle sheet has no shallows: the halo the art paints along its
    # shores is sea, so a ship sails straight up to the beach (it lands
    # only once its centre is on land, and a band of shallows on the way
    # would scrape it, slowed before a fort, until it sinks).
    tiles = [(f, np.where(cls == SHALLOWS, SEA, cls))
             for f, cls in tiles_of("battle.png", 32)]
    return mask_png(tiles, 32), notes(2, 3, 4, 5) + " Battle: " + counts(tiles) + "."


def shore_side(cls):
    """The open side of a map tile's terrain, or None."""
    if (cls == LAND).mean() < 0.05:
        return None
    wet = cls != LAND
    edges = {"south": wet[-3:, :], "east": wet[:, -3:], "west": wet[:, :3]}
    best, side = 0.75, None
    for name in ("south", "east", "west"):
        v = edges[name].mean()
        if v > best or (side is None and v == best):
            best, side = v, name
    return side


def map_sections():
    tiles = tiles_of("map.png", 16)
    lines = ["[coast]"]
    n = {"east": 0, "south": 0, "west": 0}
    for t, (_, cls) in enumerate(tiles):
        side = shore_side(cls)
        if side:
            lines.append("%d = %s" % (t, side))
            n[side] += 1
    text = notes(6) + " %d east, %d south, %d west." % (
        n["east"], n["south"], n["west"])
    return "\n".join(lines) + "\n", text


def pools_inside(water):
    """Whether the tile's water stays off its edge (2 art pixels)."""
    edge = np.ones_like(water)
    edge[2:-2, 2:-2] = False
    return not (water & edge).any()


def ground_of(f):
    r, g, b = f[..., 0], f[..., 1], f[..., 2]
    water = water_of(f)
    wet = water.mean()
    if wet >= 0.5:
        return "water"
    red = ((r > g + 30) & (r > b + 30)).mean()
    grey = ((abs(r - g) < 20) & (abs(g - b) < 20)).mean()
    if red >= 0.03 or grey >= 0.10:
        return "cover"
    if wet >= 0.02 and pools_inside(water):
        return "swamp"
    return None


ART_SPEC = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "..", "artgen", "assets.toml")


def painted_ground():
    """The battle tiles the art paints as woods or marsh."""
    with open(ART_SPEC, "rb") as f:
        spec = tomllib.load(f)
    place = next(a for a in spec["asset"]
                 if a["id"] == "tiles/battle")["tiles"].get("place", {})
    ground = {t: "cover" for t in place.get("woods", [])}
    ground.update({t: "swamp" for t in place.get("marsh", [])})
    return ground


def battle_sections():
    tiles = tiles_of("battle.png", 32)
    painted = painted_ground()
    lines = ["[ground]"]
    n = {"cover": 0, "swamp": 0, "water": 0}
    for t, (f, _) in enumerate(tiles):
        gr = painted.get(t) or ground_of(f)
        if gr:
            lines.append("%d = %s" % (t, gr))
            n[gr] += 1
    text = notes(7, 8) + " %d water, %d swamp, %d cover, %d clear." % (
        n["water"], n["swamp"], n["cover"], 256 - sum(n.values()))
    return "\n".join(lines) + "\n", text


FILES = [
    ("art/map-terrain.png", map_terrain),
    ("art/battle-terrain.png", battle_terrain),
    ("art/map-sections.ini", map_sections),
    ("art/battle-sections.ini", battle_sections),
]
