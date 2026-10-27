#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Compose the sailing map's replacement tiles from photographic
textures, each tile keeping the coastline of the tile it replaces.

The game tests sea and land pixel by pixel in the map tiles, so a
replacement tile must keep each tile's coast where it is.  The shapes
come from the terrain of the installed original tiles (flibustier
--export-world writes it, locally); the pictures are this pack's own
textures, laid through those shapes:

- land is jungle, with a band of beach sand along the water;
- water is open sea, turning to turquoise shallows near land;
- the town tiles get a cluster of houses on their land.

Every tile fades, near its edges, to one shared pattern of each
texture, so neighbouring tiles always meet without a seam while their
middles vary.  Pure Python, no image library.
"""

import math
import random
import struct
import zlib


# --- PNG, enough for ComfyUI's files and the atlas ------------------

def png_read(data):
    """(w, h, channels, rows of bytes) of an 8-bit, non-interlaced PNG."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, w = 8, b"", None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, lace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and lace == 0, "8-bit, non-interlaced only"
            ch = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    raw = zlib.decompress(idat)
    stride = w * ch
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - ch] if i >= ch else 0
            b = prev[i]
            c = prev[i - ch] if i >= ch else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else b if pb <= pc else c
                line[i] = (line[i] + pr) & 255
        rows.append(line)
        prev = line
    return w, h, ch, rows


def png_write(path, w, h, rgba):
    """Write rows of RGBA bytes (a list of bytearrays) as a PNG."""
    raw = b"".join(b"\x00" + bytes(r) for r in rgba)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(
            ">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def pgm_read(path):
    """(w, h, bytes) of a binary PGM."""
    data = open(path, "rb").read()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    assert fields[0] == b"P5", "not a binary PGM"
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[pos + 1:pos + 1 + w * h]


# --- Textures ------------------------------------------------------

class Texture:
    """A picture to sample colours from, by wrapping coordinates.  With
    "flatten", its local average tone is replaced by its overall one,
    leaving only its fine detail (times "detail"), so any two parts of
    it match where they meet."""

    def __init__(self, png, flatten=0, detail=1.0, colour=None):
        self.w, self.h, self.ch, self.rows = png_read(png)
        if flatten:
            self.flatten(flatten, detail, colour)

    def flatten(self, r, detail, colour=None):
        w, h, ch = self.w, self.h, self.ch
        for c in range(min(3, ch)):
            v = [[row[x * ch + c] for x in range(w)] for row in self.rows]
            mean = sum(map(sum, v)) / (w * h) if colour is None \
                else colour[c]
            # A box blur of radius r, wrapping, by running sums.
            rows = []
            for y in range(h):
                acc = sum(v[y][x % w] for x in range(-r, r + 1))
                out = []
                for x in range(w):
                    out.append(acc)
                    acc += v[y][(x + r + 1) % w] - v[y][(x - r) % w]
                rows.append(out)
            n = (2 * r + 1) ** 2
            for x in range(w):
                acc = sum(rows[y % h][x] for y in range(-r, r + 1))
                for y in range(h):
                    local = acc / n
                    val = mean + (v[y][x] - local) * detail
                    self.rows[y][x * ch + c] = max(0, min(255, round(val)))
                    acc += rows[(y + r + 1) % h][x] - rows[(y - r) % h][x]

    def at(self, x, y):
        r = self.rows[y % self.h]
        i = (x % self.w) * self.ch
        if self.ch >= 3:
            return r[i], r[i + 1], r[i + 2]
        return r[i], r[i], r[i]

    def alpha(self, x, y):
        if self.ch != 4:
            return 255
        return self.rows[y % self.h][(x % self.w) * 4 + 3]


def periodic(tex, side):
    """A tile-sized patch of a texture that repeats without a seam: a
    crop cross-faded with itself shifted by half a tile, the shifted copy
    taking over at the edges, where it runs on from the other side."""
    h = side // 2
    o = (tex.w - side) // 2		# the texture's middle
    crop = [[tex.at(o + x, o + y) for x in range(side)] for y in range(side)]

    def w(v):			# 1 in the middle, 0 at the edges
        return 1 - abs(2 * (v + 0.5) / side - 1)

    out = []
    for y in range(side):
        row = []
        for x in range(side):
            wx, wy = w(x), w(y)
            parts = ((crop[y][x], wx * wy),
                     (crop[y][(x + h) % side], (1 - wx) * wy),
                     (crop[(y + h) % side][x], wx * (1 - wy)),
                     (crop[(y + h) % side][(x + h) % side],
                      (1 - wx) * (1 - wy)))
            row.append(tuple(round(sum(c[k] * f for c, f in parts))
                             for k in range(3)))
        out.append(row)
    return out


def mix(a, b, t):
    return tuple(round(p + (q - p) * t) for p, q in zip(a, b))


def edge_weight(x, y, side, band):
    """1 at a tile's edge, 0 from "band" pixels inside: how much of the
    shared pattern a pixel takes."""
    d = min(x, y, side - 1 - x, side - 1 - y)
    return max(0.0, 1.0 - d / band)


# --- Masks ---------------------------------------------------------

LAND = 170		# the export's greys: TERRAIN_LAND x 85
SHALLOWS = 85


def tile_land(terrain, tw, tile, n, side):
    """Tile "tile"'s land at "side" pixels: a float per pixel, from its
    n x n terrain upscaled bilinearly (edges held), so a coast runs
    smoothly where the game's pixels step."""
    tx, ty = tile % (tw // n) * n, tile // (tw // n) * n
    cls = [[terrain[(ty + y) * tw + tx + x] for x in range(n)]
           for y in range(n)]
    # "Shallows" pixels are the open sea's wave crests, but also the
    # houses drawn on some land tiles: in a tile mostly of land, land.
    mostly = sum(r.count(LAND) for r in cls) * 2 > n * n
    src = [[1.0 if v == LAND or (v == SHALLOWS and mostly) else 0.0
            for v in row] for row in cls]
    out = []
    for y in range(side):
        fy = (y + 0.5) * n / side - 0.5
        y0 = min(n - 1, max(0, int(fy // 1)))
        y1 = min(n - 1, y0 + 1)
        ty_ = min(1.0, max(0.0, fy - y0))
        row = []
        for x in range(side):
            fx = (x + 0.5) * n / side - 0.5
            x0 = min(n - 1, max(0, int(fx // 1)))
            x1 = min(n - 1, x0 + 1)
            tx_ = min(1.0, max(0.0, fx - x0))
            top = src[y0][x0] + (src[y0][x1] - src[y0][x0]) * tx_
            bot = src[y1][x0] + (src[y1][x1] - src[y1][x0]) * tx_
            row.append(top + (bot - top) * ty_)
        out.append(row)
    return out


def distance(inside, side):
    """Chamfer distance, in pixels, from each pixel to the nearest pixel
    where inside is False (0 where it is False)."""
    big = side * 4
    d = [[0 if not inside[y][x] else big for x in range(side)]
         for y in range(side)]
    for y in range(side):
        for x in range(side):
            if d[y][x]:
                v = d[y][x]
                if x:
                    v = min(v, d[y][x - 1] + 3)
                if y:
                    v = min(v, d[y - 1][x] + 3)
                    if x:
                        v = min(v, d[y - 1][x - 1] + 4)
                    if x + 1 < side:
                        v = min(v, d[y - 1][x + 1] + 4)
                d[y][x] = v
    for y in range(side - 1, -1, -1):
        for x in range(side - 1, -1, -1):
            if d[y][x]:
                v = d[y][x]
                if x + 1 < side:
                    v = min(v, d[y][x + 1] + 3)
                if y + 1 < side:
                    v = min(v, d[y + 1][x] + 3)
                    if x + 1 < side:
                        v = min(v, d[y + 1][x + 1] + 4)
                    if x:
                        v = min(v, d[y + 1][x - 1] + 4)
                d[y][x] = v
    # With no "outside" in the tile, all is far.
    return [[v / 3 for v in row] for row in d]


# --- The tiles -----------------------------------------------------

def tile_crests(terrain, tw, tile, n, side):
    """Tile "tile"'s wave crests, as tile_land() gives its land: the
    "shallows" pixels of a tile mostly of water, which the original
    draws in the colours it rotates, as whitecaps."""
    tx, ty = tile % (tw // n) * n, tile // (tw // n) * n
    cls = [[terrain[(ty + y) * tw + tx + x] for x in range(n)]
           for y in range(n)]
    if sum(r.count(LAND) for r in cls) * 2 > n * n:
        return None
    if not any(SHALLOWS in r for r in cls):
        return None
    # Upscaled smoothly, as the coasts are, not in the original's blocks.
    src = [[1.0 if v == SHALLOWS else 0.0 for v in row] for row in cls]
    out = []
    for y in range(side):
        fy = min(n - 1.0, max(0.0, (y + 0.5) * n / side - 0.5))
        y0 = int(fy)
        y1 = min(n - 1, y0 + 1)
        row = []
        for x in range(side):
            fx = min(n - 1.0, max(0.0, (x + 0.5) * n / side - 0.5))
            x0 = int(fx)
            x1 = min(n - 1, x0 + 1)
            top = src[y0][x0] + (src[y0][x1] - src[y0][x0]) * (fx - x0)
            bot = src[y1][x0] + (src[y1][x1] - src[y1][x0]) * (fx - x0)
            row.append(top + (bot - top) * (fy - y0))
        out.append(row)
    return out


def compose(terrain_pgm, textures, side, towns, seed, looks=None,
            beach=0.06, shallows=0.22, band=0.25, town_size=0.95,
            frames=1, drift=2):
    """The 256 tiles at "side" pixels as rows of RGBA bytes (16 tiles a
    row), "frames" sets of them one below the other: the sea's ripples
    drift round a circle of "drift" pixels, its glints flash and its
    wave crests roll over the frames, which loop.  "textures" maps sea,
    land, sand and town to PNG bytes; "looks" maps them to their
    (colour, detail): the average colour each takes and how much of its
    detail it keeps.  "beach" and "shallows" are widths, "band" the edge
    fade and "town_size" the houses, in tiles."""
    tw, th, terrain = pgm_read(terrain_pgm)
    n = tw // 16
    looks = looks or {}
    tex = {k: Texture(v, 0, 1.0) if k == "town" else
           Texture(v, 12, *looks.get(k, (None, 0.8))[::-1])
           for k, v in textures.items()}
    shared = {k: periodic(tex[k], side) for k in ("sea", "land", "sand")}
    rng = random.Random(seed)
    beach_px, shallow_px, band_px = beach * side, shallows * side, \
        band * side
    turquoise = (64, 170, 170)
    foam = (225, 235, 235)
    rows = 16 * side
    atlas = [bytearray(16 * side * 4) for _ in range(rows * frames)]
    # The sea's motion, the same for every tile so their edges still
    # meet: a whole number of pixels each frame, back where it began.
    shifts = [(round(drift * math.cos(2 * math.pi * f / frames)),
               round(drift * math.sin(2 * math.pi * f / frames)))
              for f in range(frames)]
    for t in range(256):
        land = tile_land(terrain, tw, t, n, side)
        is_land = [[v >= 0.5 for v in row] for row in land]
        to_water = distance(is_land, side)
        to_land = distance([[not v for v in row] for row in is_land], side)
        any_land = any(any(r) for r in is_land)
        any_water = not all(all(r) for r in is_land)
        crests = tile_crests(terrain, tw, t, n, side)
        # Where this tile's middle samples each texture.
        # (inside it: a texture's own edges may be darker)
        ox = {k: rng.randrange(4, max(5, tex[k].w - side - 4)) for k in tex}
        oy = {k: rng.randrange(4, max(5, tex[k].h - side - 4)) for k in tex}
        # A few glints in the tile's middle, each flashing in its frame.
        glints = {}
        for _ in range(side * side // 250):
            gx = rng.randrange(round(band_px), side - round(band_px))
            gy = rng.randrange(round(band_px), side - round(band_px))
            glints[(gx, gy)] = rng.randrange(frames)
        roll = rng.random() * 2 * math.pi
        for f in range(frames):
            dx, dy = shifts[f]
            for y in range(side):
                out = atlas[f * rows + t // 16 * side + y]
                for x in range(side):
                    e = edge_weight(x, y, side, band_px)

                    def sample(k, mx=0, my=0):
                        mid = tex[k].at(x + mx + ox[k], y + my + oy[k])
                        return mix(mid, shared[k][(y + my) % side]
                                   [(x + mx) % side], e)

                    if is_land[y][x]:
                        c = sample("land")
                        if any_water and to_water[y][x] < beach_px:
                            c = mix(sample("sand"), c,
                                    to_water[y][x] / beach_px)
                    else:
                        c = sample("sea", dx, dy)
                        if any_land and to_land[y][x] < shallow_px:
                            c = mix(c, turquoise,
                                    0.6 * (1 - to_land[y][x] / shallow_px))
                        if crests and crests[y][x]:
                            # Rolling along: brighter where the wave's
                            # phase is.
                            a = 0.55 + 0.4 * math.cos(
                                2 * math.pi * f / frames - x * 0.35 + roll)
                            c = mix(c, foam, max(0.0, a) * 0.6 *
                                    crests[y][x])
                        g = glints.get((x, y))
                        if g is not None:
                            d = min((f - g) % frames, (g - f) % frames)
                            if d < 2:
                                c = mix(c, foam, 0.55 if d == 0 else 0.2)
                    i = (t % 16 * side + x) * 4
                    out[i:i + 4] = bytes((*c, 255))
            if t in towns and any_land:
                place_town(atlas, t, side, is_land, tex["town"], town_size,
                           f * rows)
    return atlas


def place_town(atlas, t, side, is_land, town, scale, top=0):
    """A house on a town tile: the town picture, scaled to "scale" of the
    tile, centred on its land, in the frame from atlas row "top"."""
    pts = [(x, y) for y in range(side) for x in range(side) if is_land[y][x]]
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)
    size = round(side * scale)
    s = min(town.w, town.h)
    # The corner rounded once: rounding each pixel (halves to even) would
    # land two of them on one and skip the next.
    x0, y0 = int(cx - size / 2 + 0.5), int(cy - size / 2 + 0.5)
    for y in range(size):
        for x in range(size):
            px, py = x0 + x, y0 + y
            if not (0 <= px < side and 0 <= py < side) or \
                    not is_land[py][px]:
                continue
            sx, sy = x * s // size, y * s // size
            a = town.alpha(sx, sy) / 255
            if a <= 0:
                continue
            row = atlas[top + t // 16 * side + py]
            i = (t % 16 * side + px) * 4
            under = tuple(row[i:i + 3])
            row[i:i + 3] = bytes(mix(under, town.at(sx, sy), a))
