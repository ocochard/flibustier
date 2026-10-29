#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Compose the battles' small sprites (cur/sbtlmisc, cur/lbtlmisc).

The sea battle's bank: a broadside's shadow on the water and its balls
in flight (three, spreading), the splashes where they land, the bursts
where they strike, the bow's aiming mark, and the wind's compass in
sixteen directions.  The land battle's: the player's party (chosen, and
not), each nation's soldiers (a shield) and horsemen, eight arrows and a
musket's flash.

Where each piece stands in its frame is read from the original frame of
the player's own game data at make time (never stored): its balls' and
splashes' centres and sizes, the compass needle's direction.  The pieces
are this pack's paintings (cut out) or drawn here.

Needs ImageMagick 7 ("magick").
"""

import math
import os
import subprocess

import tilemaker
from boards import magick, px

CLEAR = ((60, 60, 60), (90, 90, 90))	# the viewer's checkerboard


def export(binary, data, ident, n, out):
    subprocess.run([binary, "--data", data, "--export", ident, out,
                    "--frame", str(n)], check=True,
                   stdout=subprocess.DEVNULL)


def blobs(path):
    """(w, h, [(cx, cy, r, colour)]) of an exported frame's pieces: its
    opaque pixels grouped by touching, each a centre, a radius and its
    mean colour."""
    w, h, ch, rows = tilemaker.png_read(open(path, "rb").read())
    # The original's sea frames have a black field round their dots:
    # black is clear here too.
    on = [[tuple(rows[y][x * ch:x * ch + 3]) not in CLEAR and
           sum(rows[y][x * ch:x * ch + 3]) > 36 and
           (ch < 4 or rows[y][x * ch + 3] > 128) for x in range(w)]
          for y in range(h)]
    seen = [[False] * w for _ in range(h)]
    out = []
    for y in range(h):
        for x in range(w):
            if not on[y][x] or seen[y][x]:
                continue
            todo, pts = [(x, y)], []
            seen[y][x] = True
            while todo:
                a, b = todo.pop()
                pts.append((a, b))
                for da in (-1, 0, 1):
                    for db in (-1, 0, 1):
                        c, d = a + da, b + db
                        if 0 <= c < w and 0 <= d < h and on[d][c] and \
                                not seen[d][c]:
                            seen[d][c] = True
                            todo.append((c, d))
            cx = sum(p[0] for p in pts) / len(pts) + 0.5
            cy = sum(p[1] for p in pts) / len(pts) + 0.5
            r = max(0.8, math.sqrt(len(pts) / math.pi))
            col = [sum(rows[b][a * ch + i] for a, b in pts) / len(pts)
                   for i in range(3)]
            out.append((cx, cy, r, col))
    return w, h, out


def needle(path):
    """The compass needle's direction in an exported frame: the angle,
    clockwise from up, from the dial's middle to its red's far end."""
    w, h, ch, rows = tilemaker.png_read(open(path, "rb").read())
    red = [(x, y) for y in range(h) for x in range(w)
           if rows[y][x * ch] > 150 and rows[y][x * ch + 1] < 80]
    cx, cy = w / 2, h / 2
    # The dial is an ellipse twice as wide as tall: undo it.
    far = max(red, key=lambda p: ((p[0] - cx) / 2) ** 2 + (p[1] - cy) ** 2)
    return math.atan2((far[0] - cx) / 2, -(far[1] - cy))


def piece(src, size, out):
    """A cut-out painting fitted into "size" pixels, centred."""
    magick("-size", "%dx%d" % size, "xc:none", "(", src, "-trim", "+repage",
           "-resize", "%dx%d" % size, ")", "-gravity", "center",
           "-composite", out)


def scatter(path, w, h, k, parts, what, scale=1.0):
    """A frame of w x h with "what" (a cut-out) placed at each part's
    centre, its size the part's times "scale"."""
    W, H = px(w, k), px(h, k)
    cmd = ["-size", "%dx%d" % (W, H), "xc:none"]
    for cx, cy, r, _ in parts:
        d = max(2, round(2 * r * scale * k))
        cmd += ["(", what, "-resize", "%dx%d" % (d, d), ")", "-geometry",
                "+%d+%d" % (round(cx * k - d / 2), round(cy * k - d / 2)),
                "-compose", "over", "-composite"]
    magick(*cmd, "-depth", 8, path)


def compose(binary, data, src, k, work, art):
    os.makedirs(work, exist_ok=True)
    o = os.path.join(work, "orig.png")
    # The sea battle.
    frames = []

    def f(n):
        p = os.path.join(work, "s%d.png" % n)
        frames.append(p)
        return p

    shadow = os.path.join(work, "shadow.png")
    magick("-size", "64x64", "xc:none", "-fill", "#00000070", "-draw",
           "circle 32,32 32,4", "-blur", "0x6", shadow)
    for n in range(48):
        export(binary, data, "cur/sbtlmisc", n, o)
        w, h, parts = blobs(o)
        W, H = px(w, k), px(h, k)
        p = f(n)
        if n < 4:			# the shot's shadow on the water
            scatter(p, w, h, k, parts, shadow, 1.6)
        elif n < 8:			# the balls in flight
            scatter(p, w, h, k, parts, src["ball"], 2.2)
        elif 8 <= n < 13:		# the splashes, growing then falling
            scatter(p, w, h, k, parts, src["splash"], 1.8)
        elif 24 <= n < 29:		# the bursts, smoke and fire
            scatter(p, w, h, k, parts, src["burst"], 1.6)
        elif n == 20:			# the bow's aiming mark
            c = W / 2
            magick("-size", "%dx%d" % (W, H), "xc:none", "-stroke",
                   "#3a2008", "-strokewidth", str(max(2, round(k * 1.6))),
                   "-fill", "none", "-draw", "circle %g,%g %g,%g" % (
                       c, c, c, c * 0.2), "-draw", "line %g,%g %g,%g" % (
                       c, c * 0.1, c, c * 1.9), "-draw", "line %g,%g %g,%g" % (
                       c * 0.1, c, c * 1.9, c), "-stroke", "#f0c050",
                   "-strokewidth", str(max(1, round(k * 0.8))), "-draw",
                   "circle %g,%g %g,%g" % (c, c, c, c * 0.2), "-draw",
                   "line %g,%g %g,%g" % (c, c * 0.1, c, c * 1.9), "-draw",
                   "line %g,%g %g,%g" % (c * 0.1, c, c * 1.9, c), p)
        elif n >= 32:			# the wind's compass
            a = needle(o)
            magick("-size", "%dx%d" % (W, H), "xc:none", "(", src["compass"],
                   "-trim", "+repage", "-resize", "%dx%d!" % (W, H), ")",
                   "-composite", p)
            cx, cy = W / 2, H / 2
            rx, ry = W * 0.42, H * 0.40
            tip = (cx + rx * math.sin(a), cy - ry * math.cos(a))
            base = (cx - rx * 0.35 * math.sin(a), cy + ry * 0.35 * math.cos(a))
            nx, ny = -math.cos(a) * W * 0.04, -math.sin(a) * H * 0.08
            magick(p, "-fill", "#b01810", "-stroke", "#4a0804", "-strokewidth",
                   "1", "-draw", "polygon %g,%g %g,%g %g,%g" % (
                       tip[0], tip[1], base[0] + nx, base[1] + ny,
                       base[0] - nx, base[1] - ny), p)
        else:				# frames the battle does not draw
            magick("-size", "%dx%d" % (W, H), "xc:none", p)
    _strip(frames, os.path.join(art, "sbtlmisc.png"))
    sea = [_size(p) for p in frames]
    # The land battle.
    frames = []
    for n in range(19):
        export(binary, data, "cur/lbtlmisc", n, o)
        w, h, _ = blobs(o)
        W, H = px(w, k), px(h, k)
        p = os.path.join(work, "l%d.png" % n)
        frames.append(p)
        if n < 2:			# the party, chosen and not
            piece(src["party%d" % n], (W, H), p)
        elif n < 6:			# each nation's soldiers
            piece(src["shield%d" % (n - 2)], (W, H), p)
        elif n < 10:			# each nation's horsemen
            piece(src["horse%d" % (n - 6)], (W, H), p)
        elif n < 18:			# the arrows, clockwise from up
            a = 45 * (n - 10)
            magick("-size", "%dx%d" % (W * 4, H * 4), "xc:none", "-fill",
                   "#f4f0e0", "-stroke", "#202020", "-strokewidth",
                   str(max(2, round(k * 2))), "-draw",
                   "polygon %d,%d %d,%d %d,%d %d,%d %d,%d %d,%d %d,%d" % (
                       W * 2, H * 0.3, W * 3.5, H * 1.9, W * 2.6, H * 1.9,
                       W * 2.6, H * 3.7, W * 1.4, H * 3.7, W * 1.4, H * 1.9,
                       W * 0.5, H * 1.9), "-rotate", str(a), "-gravity",
                   "center", "-extent", "%dx%d" % (W * 4, H * 4), "-resize",
                   "%dx%d!" % (W, H), p)
        else:				# a musket's flash
            piece(src["flash"], (W, H), p)
    _strip(frames, os.path.join(art, "lbtlmisc.png"))
    land = [_size(p) for p in frames]
    return sea, land


def _strip(frames, out):
    magick("-background", "none", *frames, "-gravity", "north", "+append",
           "-depth", 8, out)


def _size(p):
    r = subprocess.run(["magick", "identify", "-format", "%w %h", p],
                       capture_output=True, text=True, check=True)
    return tuple(int(v) for v in r.stdout.split())
