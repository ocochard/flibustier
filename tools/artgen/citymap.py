#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Compose the Map of the Caribbean (pic/citymap).

The game draws a dot on this map for each town, at its map place, and
the player's place at sea at his cell times 0.75: the map is the world's
640 x 480 cells at 480 x 360.  So its land is the world's own, read from
the player's game data where the pack is made (a mask of land, a pixel a
cell, never stored), and painted: a sea texture, a parchment land, a
raised dark coast.  Beside and below it, the wood panels of the town
list (x 480-640) and of the selected town's figures (y 360-480), with
their dividers and arrows, and the DONE plaque (x 348-479, y 326-359,
lower than the original's 315 so the coast towns' dots stay clear of
it), its word set in the pack's font.

Needs ImageMagick 7 ("magick").
"""

import os

from boards import magick, panel, lettered, px

MAP_W, MAP_H = 480, 360


def compose(land_pgm, src, font, k, work, out):
    os.makedirs(work, exist_ok=True)
    W, H = px(640, k), px(480, k)
    mw, mh = px(MAP_W, k), px(MAP_H, k)
    # The land, smoothed to the map's size; its coast a band round it.
    land = os.path.join(work, "land.png")
    # Sampled at the map's own size: only its edge softened.
    magick(land_pgm, "-resize", "%dx%d!" % (mw, mh), "-blur",
           "0x%g" % (k * 0.25), "-level", "30%,70%", land)
    # Islands of a pixel or two (a town's islet) fade away in that:
    # those are grown to a dot the eye finds, the larger land kept as is.
    seen = os.path.join(work, "any.png")
    big = os.path.join(work, "big.png")
    magick(land_pgm, "-resize", "%dx%d!" % (mw, mh), "-threshold", "20%",
           seen)
    magick(seen, "-define", "connected-components:area-threshold=%d" %
           round((3 * k) ** 2), "-define",
           "connected-components:mean-color=true",
           "-connected-components", "8", big)
    magick(land, "(", seen, big, "-compose", "minus_src", "-composite",
           "-morphology", "dilate", "disk:%g" % (k * 1.1), "-blur",
           "0x%g" % (k * 0.3), ")", "-compose", "lighten", "-composite",
           land)
    coast = os.path.join(work, "coast.png")
    magick(land, "(", "+clone", "-morphology", "dilate",
           "disk:%d" % max(1, round(k * 0.8)), ")", "-compose",
           "difference", "-composite", "-blur", "0x%g" % (k * 0.4), "-level",
           "0%,60%", coast)

    def tex(key, path):
        magick(src[key], "-resize", "%dx%d^" % (mw, mh), "-gravity",
               "center", "-extent", "%dx%d" % (mw, mh), path)
        return path

    # The sea a wash of one teal, faintly mottled: a painted sea brings
    # pale stains that read as islands, and a paper edge.
    sea = os.path.join(work, "sea.png")
    magick("-size", "%dx%d" % (mw, mh), "xc:#1d5560", "(", src["land"],
           "-resize", "%dx%d^" % (mw, mh), "-gravity", "center",
           "-extent", "%dx%d" % (mw, mh), "-colorspace", "gray",
           "-auto-level", "+level", "38%,62%", ")", "-compose",
           "softlight", "-composite", sea)
    paper = tex("land", os.path.join(work, "paper.png"))
    mp = os.path.join(work, "map.png")
    # The sea, the land over it through the mask, a shadow along the
    # coast on the sea's side (raised land), the coast's dark edge.
    shade = os.path.join(work, "shade.png")
    magick(land, "-morphology", "dilate", "disk:%d" % max(1, round(k * 1.5)),
           "-blur", "0x%g" % (k * 1.5), "-level", "0%,170%", "-negate",
           shade)
    magick(sea, shade, "-compose", "multiply", "-composite",
           "(", paper, land, "-alpha", "off", "-compose", "copy-opacity",
           "-composite", ")", "-compose", "over", "-composite",
           "(", "-size", "%dx%d" % (mw, mh), "xc:#3a2410", coast,
           "-alpha", "off", "-compose", "copy-opacity", "-composite", ")",
           "-compose", "over", "-composite", mp)
    if "compass" in src:
        cp = os.path.join(work, "compass.png")
        magick(src["compass"], "-trim", "+repage", "-resize",
               "%dx%d" % (px(64, k), px(64, k)), cp)
        # In the open Atlantic, east of the Bahamas, where no town is.
        magick(mp, cp, "-geometry", "+%d+%d" % (px(392, k), px(92, k)),
               "-composite", mp)
    # The wood: the town list's column and the figures' panel below.
    magick(src["wood"], "-resize", "%dx%d^" % (W, H), "-gravity", "center",
           "-extent", "%dx%d" % (W, H), out)
    for x, y, w, h in ((480, 0, 160, 480), (0, 360, 480, 120)):
        p = os.path.join(work, "panel.png")
        panel(p, src["wood"], w, h, k, rim=src["brass"])
        magick(out, p, "-geometry", "+%d+%d" % (px(x, k), px(y, k)),
               "-composite", out)
    # The figures' six boxes, in two rows of three.
    for col in range(3):
        for row in range(2):
            p = os.path.join(work, "box.png")
            panel(p, src["wood"], 156, 52, k, rim=src["brass"], inset=True)
            magick(out, p, "-geometry", "+%d+%d" % (
                px(4 + 160 * col, k), px(366 + 56 * row, k)), "-composite",
                out)
    # The list's arrows, top and bottom.
    for y, flip in ((4, False), (452, True)):
        p = os.path.join(work, "arrow.png")
        panel(p, src["brass"], 140, 22, k, rim=src["brass"])
        tri = "polygon %d,%d %d,%d %d,%d" % (
            px(70, k), px(4 if not flip else 18, k), px(52, k),
            px(18 if not flip else 4, k), px(88, k),
            px(18 if not flip else 4, k))
        magick(p, "-fill", "#3a2410", "-draw", tri, p)
        magick(out, p, "-geometry", "+%d+%d" % (px(490, k), px(y, k)),
               "-composite", out)
    magick(out, mp, "-geometry", "+0+0", "-composite", out)
    # DONE, on a brass plaque, on the map's corner.
    done = os.path.join(work, "done.png")
    panel(done, src["brass"], 128, 30, k, rim=src["brass"])
    lettered(done, done, "DONE", "#2a1608", 128, 30, font, k, shadow=False)
    magick(out, done, "-geometry", "+%d+%d" % (px(349, k), px(328, k)),
           "-composite", "-depth", 8, out)
