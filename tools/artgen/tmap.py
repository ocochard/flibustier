#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Compose the treasure map's frame, torn edges and marks.

The game draws a treasure map as a parchment sheet on wood (pic/mapframe
over pic/wood), the world showing through the sheet's hole, the missing
quarters torn off (spr/treasmap frames 5-16 along the tears) and a mark
on the spot (frames 0-2).  The original paints the hole in its colour 6.

These pieces follow the original's shapes, read from the player's own
game data where the pack is made (never stored): its sheet and hole,
each torn edge's paper and hole, each mark's strokes.  They are painted
with the pack's paper and ink, the holes in the key colour (pure
magenta) the engine fills with the world.

Needs ImageMagick 7 ("magick").
"""

import os
import subprocess

import tilemaker
from boards import magick

KEY = "#ff00ff"
HOLE = (225, 13, 2)		# the original's colour 6, as exported
# An exported frame's transparent pixels: the viewer's checkerboard.
CLEAR = ((60, 60, 60), (90, 90, 90))


def export(binary, data, ident, out, frame=0):
    subprocess.run([binary, "--data", data, "--export", ident, out,
                    "--frame", str(frame)], check=True,
                   stdout=subprocess.DEVNULL)


def classes(path):
    """(w, h, hole, solid) of an exported picture: the hole's pixels and
    the other opaque ones, as lists of rows of 0/255."""
    w, h, ch, rows = tilemaker.png_read(open(path, "rb").read())
    hole, solid = [], []
    for r in rows:
        hr, sr = bytearray(w), bytearray(w)
        for x in range(w):
            p = r[x * ch:x * ch + ch]
            if (ch == 4 and p[3] < 128) or tuple(p[:3]) in CLEAR:
                continue
            if tuple(p[:3]) == HOLE:
                hr[x] = 255
            else:
                sr[x] = 255
        hole.append(hr)
        solid.append(sr)
    return w, h, hole, solid


def sheet_of(path):
    """The parchment sheet of the original frame: all but the wood round
    it, found by flooding from the edges through the colours of the
    picture's outer band."""
    w, h, ch, rows = tilemaker.png_read(open(path, "rb").read())
    px = [[tuple(r[x * ch:x * ch + 3]) for x in range(w)] for r in rows]
    band = set()
    for y in range(h):
        for x in range(w):
            if x < 6 or y < 6 or x >= w - 6 or y >= h - 6:
                band.add(px[y][x])
    out = [[False] * w for _ in range(h)]
    todo = [(x, y) for x in range(w) for y in (0, h - 1)] + \
        [(x, y) for y in range(h) for x in (0, w - 1)]
    while todo:
        x, y = todo.pop()
        if x < 0 or y < 0 or x >= w or y >= h or out[y][x] or \
                px[y][x] not in band:
            continue
        out[y][x] = True
        todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return w, h, [bytearray(0 if out[y][x] else 255 for x in range(w))
                  for y in range(h)]


def save_mask(path, w, h, rows):
    tilemaker.png_write(path, w, h, [bytearray(
        b for v in r for b in (v, v, v, 255)) for r in rows])


def paint(out, paper, solid, hole, W, H, k, burn=8):
    """Paper where "solid", soft at its rim and burnt towards it; the
    key, hard, where "hole"; clear elsewhere."""
    s = out + ".s.png"
    magick(solid, "-resize", "%dx%d!" % (W, H), "-blur",
           "0x%g" % (k * 0.6), "-colorspace", "gray", s)
    magick("(", paper, "-resize", "%dx%d^" % (W, H), "-gravity", "center",
           "-extent", "%dx%d" % (W, H), ")",
           "(", s, "-blur", "0x%g" % (k * burn), "-level", "20%,100%",
           ")", "-compose", "multiply", "-composite",
           "(", s, ")", "-alpha", "off", "-compose", "copy-opacity",
           "-composite",
           "(", hole, "-resize", "%dx%d!" % (W, H), "-threshold", "50%",
           "-alpha", "copy", "-fill", KEY, "-colorize", "100", ")",
           "-compose", "over", "-composite", "-depth", 8, out)
    os.remove(s)


def ink(out, strokes, colour, W, H, k):
    """A mark's strokes in ink, smooth at their edges."""
    magick(strokes, "-resize", "%dx%d!" % (W, H), "-blur",
           "0x%g" % (k * 0.5), "-level", "25%,75%", "-alpha", "copy",
           "-fill", colour, "-colorize", "100", "-depth", 8, out)


def compose(binary, data, src, k, work, art, frames=17, marks=3,
            ink_colour="#8a1a0c"):
    """The frame (art/mapframe.png) and the bank (art/treasmap.png), the
    wood (art/wood.png).  Returns the bank's frame sizes."""
    os.makedirs(work, exist_ok=True)
    W, H = round(640 * k), round(480 * k)
    # The wood.
    magick(src["wood"], "-resize", "%dx%d^" % (W, H), "-gravity", "center",
           "-extent", "%dx%d" % (W, H), "-depth", 8,
           os.path.join(art, "wood.png"))
    # The frame: the sheet, its hole.
    o = os.path.join(work, "mapframe.png")
    export(binary, data, "pic/mapframe", o)
    w, h, sheet = sheet_of(o)
    _, _, hole, _ = classes(o)
    sm, hm = os.path.join(work, "sheet.png"), os.path.join(work, "hole.png")
    save_mask(sm, w, h, sheet)
    save_mask(hm, w, h, hole)
    paint(os.path.join(art, "mapframe.png"), src["paper"], sm, hm, W, H, k)
    # The bank: the marks, then the torn edges.
    paths, sizes = [], []
    for n in range(frames):
        o = os.path.join(work, "f%d.png" % n)
        export(binary, data, "spr/treasmap", o, n)
        w, h, hole, solid = classes(o)
        fw, fh = max(1, round(w * k)), max(1, round(h * k))
        fm, fh_ = (os.path.join(work, "f%d-%s.png" % (n, s))
                   for s in ("solid", "hole"))
        save_mask(fm, w, h, [bytearray(max(a, b) for a, b in zip(r, q))
                             for r, q in zip(solid, hole)]
                  if n < marks else solid)
        save_mask(fh_, w, h, hole)
        out = os.path.join(work, "out%d.png" % n)
        if n < marks:
            ink(out, fm, ink_colour, fw, fh, k)
        else:
            paint(out, src["paper"], fm, fh_, fw, fh, k, burn=3)
        paths.append(out)
        sizes.append((fw, fh))
    magick("-background", "none", *paths, "-gravity", "north", "+append",
           "-depth", 8, os.path.join(art, "treasmap.png"))
    return sizes
