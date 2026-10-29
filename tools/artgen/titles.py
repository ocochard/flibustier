#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Compose the title screen's logo from its lettering and a painted coin.

The game reveals its title over the moon picture from two sprites, stacked
at the screen's top right (spr/title, 368x135, then spr/title1, 368x134,
in 640x480 pixels).  The pack's title is its own name: the word set in a
script, lacquered red with a gilt rim, over the painted coin, the whole
cut in two at the sprites' seam.

Needs ImageMagick 7 ("magick").
"""

import os

from boards import magick, px

TOP, BOTTOM, WIDTH = 135, 134, 368


def compose(src, t, font, k, work, out):
    """The two halves of the logo, "out" + "0.png" and "1.png"; t holds
    the word, its box and the coin's.  Returns their paths."""
    os.makedirs(work, exist_ok=True)
    W, H = px(WIDTH, k), px(TOP + BOTTOM, k)
    x, y, w, h = t["word_box"]
    mask = os.path.join(work, "mask.png")
    magick("-background", "black", "-fill", "white", "-font", font,
           "-pointsize", px(h * 1.4, k), "label:" + t["word"], "-trim",
           "+repage", "-resize", "%dx%d" % (px(w, k), px(h, k)),
           "-gravity", "center", "-background", "black", "-extent",
           "%dx%d" % (px(w, k), px(h, k)), "-alpha", "off", mask)
    rim = max(2, round(1.4 * k))
    # The rim: the word grown, in gilt; the face: lacquer red, lit from
    # the top left by its own relief.
    gilt = os.path.join(work, "rim.png")
    magick("(", src["gilt"], "-resize", "%dx%d^" % (px(w, k), px(h, k)),
           "-gravity", "center", "-extent", "%dx%d" % (px(w, k), px(h, k)),
           ")", "(", mask, "-morphology", "dilate", "disk:%d" % rim, ")",
           "-alpha", "off", "-compose", "copy-opacity", "-composite", gilt)
    face = os.path.join(work, "face.png")
    magick("-size", "%dx%d" % (px(w, k), px(h, k)),
           "gradient:%s-%s" % (t.get("top", "#e01a12"),
                               t.get("bottom", "#6e0605")),
           "(", mask, "-blur", "0x%g" % (k * 1.2), "-shade", "120x45",
           "-auto-level", ")", "-compose", "softlight", "-composite",
           "(", mask, ")", "-alpha", "off", "-compose", "copy-opacity",
           "-composite", face)
    glow = os.path.join(work, "glow.png")
    # A dark glow round it, so it reads on the night sky.
    magick(mask, "-morphology", "dilate", "disk:%d" % (rim * 3), "-blur",
           "0x%g" % (k * 3), "-alpha", "copy", "-fill", "black",
           "-colorize", "100", "-channel", "A", "-evaluate", "multiply",
           "0.7", "+channel", glow)
    word = os.path.join(work, "word.png")
    # On a colour canvas: the glow alone is grey, and would make it all so.
    magick("-size", "%dx%d" % (px(w, k), px(h, k)), "xc:none",
           glow, "-composite", gilt, "-composite", face, "-composite",
           "-type", "TrueColorAlpha", word)
    cx, cy, d = t["coin"]
    coin = os.path.join(work, "coin.png")
    magick(src["coin"], "-trim", "+repage", "-resize",
           "%dx%d" % (px(d, k), px(d, k)), coin)
    logo = os.path.join(work, "logo.png")
    # The coin under the word, as the original's lies under its tail.
    magick("-size", "%dx%d" % (W, H), "xc:none",
           coin, "-gravity", "northwest", "-geometry",
           "+%d+%d" % (px(cx - d / 2, k), px(cy - d / 2, k)), "-composite",
           word, "-geometry", "+%d+%d" % (px(x, k), px(y, k)),
           "-composite", logo)
    halves = [out + "0.png", out + "1.png"]
    magick(logo, "-crop", "%dx%d+0+0" % (W, px(TOP, k)), "+repage",
           "-depth", 8, halves[0])
    magick(logo, "-crop", "%dx%d+0+%d" % (W, H - px(TOP, k), px(TOP, k)),
           "+repage", "-depth", 8, halves[1])
    return halves
