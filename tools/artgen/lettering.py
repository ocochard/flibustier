#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Compose a picture or a sprite of lettering: words set in a font and
filled with a painted texture or a gradient, lit by their own relief,
with an optional painted emblem whose brightness is its opacity (so it
glows out of the black screen it is shown on).

The intro's studio names use it: the pack's own studios, in place of the
original's publisher logos.

Needs ImageMagick 7 ("magick").
"""

import os

from boards import magick, px


def fill_image(src, fill, w, h, out):
    """A w x h image of "fill": a painted texture by its key in "src",
    or an ImageMagick image such as "gradient:#fff-#888"."""
    if fill in src:
        magick(src[fill], "-resize", "%dx%d^" % (w, h), "-gravity",
               "center", "-extent", "%dx%d" % (w, h), out)
    else:
        magick("-size", "%dx%d" % (w, h), fill, out)


def line(src, ln, k, work, n):
    """One line of words as large as fit its box, centred, filled and
    lit by its relief, on transparency.  Returns its path."""
    x, y, w, h = ln["box"]
    W, H = px(w, k), px(h, k)
    mask = os.path.join(work, "mask-%d.png" % n)
    magick("-background", "black", "-fill", "white", "-font",
           os.path.expanduser(ln["font"]), "-pointsize", px(h * 1.4, k),
           "label:" + ln["text"], "-trim", "+repage", "-resize",
           "%dx%d" % (W, H), "-gravity", "center", "-background", "black",
           "-extent", "%dx%d" % (W, H), "-alpha", "off", mask)
    fill = os.path.join(work, "fill-%d.png" % n)
    fill_image(src, ln["fill"], W, H, fill)
    out = os.path.join(work, "line-%d.png" % n)
    magick(fill, "(", mask, "-blur", "0x%g" % (k * 0.8), "-shade",
           "120x45", "-auto-level", ")", "-compose", "softlight",
           "-composite", "(", mask, ")", "-alpha", "off", "-compose",
           "copy-opacity", "-composite", out)
    return out


def compose(src, t, k, work, out, opaque):
    """The lettering of "t" (its size, lines and emblem, in 640x480
    pixels) into "out": on black when "opaque", else on transparency."""
    os.makedirs(work, exist_ok=True)
    W, H = (px(v, k) for v in t["size"])
    layers = []
    if "emblem" in t:
        cx, cy, d = t["emblem"]
        emblem = os.path.join(work, "emblem.png")
        # Its brightness is its opacity, its colour kept.
        magick(src["emblem"], "-resize", "%dx%d" % (px(d, k), px(d, k)),
               "(", "+clone", "-colorspace", "gray", "-level", "8%,70%",
               ")", "-alpha", "off", "-compose", "copy-opacity",
               "-composite", emblem)
        layers += [emblem, "+%d+%d" % (px(cx - d / 2, k),
                                        px(cy - d / 2, k))]
    for n, ln in enumerate(t["lines"]):
        x, y = ln["box"][:2]
        layers += [line(src, ln, k, work, n), "+%d+%d" % (px(x, k),
                                                         px(y, k))]
    args = ["-size", "%dx%d" % (W, H), "xc:" + ("black" if opaque
                                                else "none")]
    for path, at in zip(layers[::2], layers[1::2]):
        args += [path, "-gravity", "northwest", "-geometry", at,
                 "-composite"]
    if opaque:
        args += ["-alpha", "off"]
    else:
        args += ["-type", "TrueColorAlpha"]
    magick(*args, "-depth", 8, out)
    return out
