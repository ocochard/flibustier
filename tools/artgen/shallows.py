#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Paint open shallows into the sailing map's tile sheet.

The original sheet's shallows are only the halo along each coast; the
open world has banks and reefs away from the shore.  These tiles are
painted over the sheet's surf tiles (which the open map does not use):

- 4 solid shallows, then the straight edges with shallows on the north,
  east, south and west half, the outer corners with shallows in the NE,
  SE, SW and NW quarter, the inner corners with open sea in those
  quarters (the first index of each group of four, from "first").

Each is drawn on an open-sea tile of the same frame, so its sea sides
meet the open sea as that does (the sheet's sea is periodic at the
tiles' edges); the shallows are that sea lit turquoise over a sandy
bank, the bank's pattern periodic on the tile, so shallows meet
shallows seamlessly; the edge between them runs through the middle of
each side, softly, the same in every tile.  The shallows stay in the
turquoise of the sheet's coastal halo, which tools/opendata classifies
from the art: water (blue > red + 15, blue >= green + 15), then shallows
(green >= 0.62 x blue, green - red >= 45); classify_safe() keeps every
shallows pixel inside both.
"""

import math

import tilemaker

TURQUOISE = (46, 128, 162)


def classify_safe(c):
    """A shallows colour moved, if need be, into tools/opendata's
    shallows class, with margins: water (blue > red + 15, blue >= green
    + 15), then shallows (green >= 0.62 x blue, green - red >= 45)."""
    r, g, b = c
    b = min(255.0, max(b, g + 20, r + 20))
    g = min(g, b - 20)
    g = max(g, 0.65 * b)
    r = min(r, g - 50)
    return (max(0.0, r), g, b)
SAND = (200, 188, 140)


def open_sea_tiles(rows, side, columns, frame_rows, skip):
    """The sheet's tiles of plain open sea in frame 0, most even first:
    blue-dominant, no turquoise, no land."""
    found = []
    for t in range(frame_rows * columns):
        if t in skip:
            continue
        x0, y0 = (t % columns) * side, (t // columns) * side
        n = blue = 0
        ok = True
        tot = [0, 0, 0]
        for y in range(y0, y0 + side, 3):
            r = rows[y]
            for x in range(x0, x0 + side, 3):
                c = r[x * 4:x * 4 + 3]
                if not (c[2] > c[1] + 15 and c[2] > c[0] + 25):
                    ok = False
                    break
                for i in range(3):
                    tot[i] += c[i]
                n += 1
            if not ok:
                break
        if ok:
            found.append((t, tot[2] / n))
    found.sort(key=lambda p: -p[1])
    return [t for t, _ in found]


def smooth(e0, e1, v):
    t = min(1.0, max(0.0, (v - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def wob(u, side, phase=0.0):
    """A wobble along an edge, zero at both its ends."""
    return 1.4 * math.sin(math.pi * u / side) * math.sin(
        2 * math.pi * u / side + phase)


def mask(kind, q, x, y, side):
    """The shallows' share at (x, y) for tile kind ("solid", "edge",
    "outer", "inner") and quarter or side q (0 N/NE, 1 E/SE, 2 S/SW,
    3 W/NW)."""
    h = side / 2
    s = side * 0.07		# the edge's softness
    if kind == "solid":
        return 1.0
    if kind == "edge":
        d = (h - y + wob(x, side), x - h + wob(y, side, 1),
             y - h + wob(x, side, 2), h - x + wob(y, side, 3))[q]
        return smooth(-s, s, d)
    # Corners: the quarter (NE: x > h, y < h ...), its inner corner
    # rounded; both its sides reach the tile's edges at the middle.
    sx = 1 if q in (0, 1) else -1
    sy = -1 if q in (0, 3) else 1
    dx, dy = sx * (x - h), sy * (y - h)
    r = side * 0.22
    if dx >= r and dy >= r:
        d = min(dx, dy) if min(dx, dy) < r else r
        d = min(dx, dy)
    elif dx < r and dy < r and dx > -s and dy > -s:
        d = r - math.hypot(r - dx, r - dy)
    else:
        d = min(dx, dy)
    m = smooth(-s, s, d)
    return m if kind == "outer" else 1.0 - m


def bank(x, y, side, v):
    """The sandy bank's brightness, 0-1: periodic on the tile, so it
    meets itself; a variant's own pattern only inside the tile."""
    a = 2 * math.pi / side
    p = 0.5 + 0.16 * (math.sin(a * x + 0.7) * math.sin(a * y * 2 + 1.9)) \
        + 0.08 * math.sin(a * (x - y) * 2 + 0.4)
    if v:
        w = math.sin(math.pi * x / side) * math.sin(math.pi * y / side)
        p += 0.25 * w * math.sin(a * (x * (1 + v) - y * (2 + v)) + v)
    return min(1.0, max(0.0, p))


def paint(path, side, columns, frames, frame_rows, first, skip):
    w, h, ch, rows = tilemaker.png_read(open(path, "rb").read())
    if ch != 4:
        rows = [bytearray(b for i in range(w) for b in
                          (*r[i * ch:i * ch + 3], 255)) for r in rows]
    seas = open_sea_tiles(rows, side, columns, frame_rows, skip)
    if len(seas) < 4:
        raise RuntimeError("fewer than 4 open-sea tiles to paint on")
    kinds = [("solid", v) for v in range(4)] + \
        [(k, q) for k in ("edge", "outer", "inner") for q in range(4)]
    for n, (kind, q) in enumerate(kinds):
        t = first + n
        base = seas[(q if kind == "solid" else n) % 4]
        bx, by = (base % columns) * side, (base // columns) * side
        tx, ty = (t % columns) * side, (t // columns) * side
        for f in range(frames):
            fo = f * frame_rows * side
            for y in range(side):
                src = rows[fo + by + y]
                dst = rows[fo + ty + y]
                for x in range(side):
                    c = src[(bx + x) * 4:(bx + x) * 4 + 3]
                    m = mask(kind, q, x, y, side)
                    if m > 0:
                        b = bank(x, y, side, q if kind == "solid" else 0)
                        sh = [c[i] + (TURQUOISE[i] - c[i]) * 0.7
                              for i in range(3)]
                        sh = [sh[i] + (SAND[i] - sh[i]) * 0.18 * b
                              for i in range(3)]
                        sh = [v * (0.92 + 0.16 * b) for v in sh]
                        sh = classify_safe(sh)
                        c = [round(c[i] + (sh[i] - c[i]) * m)
                             for i in range(3)]
                    o = (tx + x) * 4
                    dst[o:o + 4] = bytes((*[min(255, max(0, v))
                                            for v in c], 255))
    tilemaker.png_write(path, w, h, rows)
    return seas[:4]
