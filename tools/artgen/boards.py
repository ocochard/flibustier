#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Compose the trade boards' sprite banks from rendered materials.

The merchant's and the shipwright's boards are three banks a language
(spr/buysell, spr/buyboard, spr/buysell1 and their French and German
twins): digits, the goods' names and pictures, signs and plaques, and
the board itself.  Their words are set here in the pack's own font; the
image model only paints materials and objects (wood, gilt, the goods),
which compose() lays out at the frames' sizes, each frame its 640x480
size times the pack's scale.

Needs ImageMagick 7 ("magick").
"""

import os
import subprocess

# The frames of spr/buysell, in 640x480 pixels, by kind; their sizes are
# those the game's layout places (its glyph advances, its boxes).
DIGIT_W = [13, 10, 16, 12, 15, 15, 12, 13, 15, 14]	# 0-9, 17 high
SMALL = (7, 10)					# "/", 0-9
WORD = (61, 18)
ICONS = [(22, 36), (22, 32), (22, 28), (22, 43), (34, 45), (47, 26)]
GEM = (12, 12)
SIGN = (77, 24)
GOLD = (77, 29)
PCS = (14, 6)
COLOURS = (81, 69)
PLAQUE = (82, 69)
BOARD = (229, 156)
OK = (140, 48)
GUN = (101, 52)

INK = {"black": "#1c140c", "red": "#b0180e", "cream": "#f2e2b4"}
# Behind dark ink on a photograph: a soft cream glow, so the words read
# on dark wood and shadow as on the original's pale pictures.
HALO = "#f2e2b4e0"
# The small digits and "pcs" are a few pixels high, too small for a
# glow inside their frames: they sit on a cream plate filling the frame,
# the plates of a number's digits meeting in one label.
PLATE = "#efdcaaf0"
GOODS = ["food", "goods", "sugar", "tobacco", "hides", "cannon"]


def magick(*args):
    subprocess.run(["magick"] + [str(a) for a in args], check=True)


def px(v, k):
    return max(1, round(v * k))


def text(path, words, ink, w, h, font, k, halo=None, lines=1, plate=None):
    """Words in "ink", as large as fit w x h (640x480 pixels) with a
    little margin, centred, on transparency, or on a "plate" of a colour
    filling the frame; a soft "halo" behind them keeps dark ink readable
    on a photograph."""
    W, H = px(w, k), px(h, k)
    tmp = path + ".t.png"
    magick("-background", "none", "-fill", ink, "-font", font,
           "-density", 72, "-pointsize", px(h * 1.6 / lines, k) * 2,
           "-gravity", "center", "label:" + words, "-trim", "+repage",
           "-resize", "%dx%d" % (round(W * 0.92), round(H * 0.9)), tmp)
    if halo:
        magick("-size", "%dx%d" % (W, H), "xc:none",
               "(", tmp, "-alpha", "extract", "-morphology", "dilate",
               "disk:%d" % max(1, round(k * 1.5)), "-blur", "0x%g" % k,
               "-background", halo, "-alpha", "shape", ")",
               "-gravity", "center", "-composite",
               tmp, "-gravity", "center", "-composite", path)
    else:
        magick("-size", "%dx%d" % (W, H), "xc:" + (plate or "none"), tmp,
               "-gravity", "center", "-composite", path)
    os.remove(tmp)


def fit(path, src, w, h, k, bright=None):
    """A cut-out object fitted into w x h, its bottom on the frame's."""
    W, H = px(w, k), px(h, k)
    extra = []
    if bright:
        extra = ["-modulate", "%d,%d" % (100 + bright, 100 + bright // 2)]
    magick("-size", "%dx%d" % (W, H), "xc:none",
           "(", src, "-trim", "+repage", "-resize", "%dx%d" % (W, H),
           *extra, ")", "-gravity", "south", "-composite", path)


def panel(path, tex, w, h, k, rim=None, inset=False):
    """A rectangle of a texture, with a rim of another, raised or set
    in."""
    W, H = px(w, k), px(h, k)
    r = max(2, round(1.6 * k))
    cmd = ["(", tex, "-resize", "%dx%d^" % (W, H), "-gravity", "center",
           "-extent", "%dx%d" % (W, H), ")"]
    if rim:
        cmd += ["(", rim, "-resize", "%dx%d^" % (W, H), "-gravity",
                "center", "-extent", "%dx%d" % (W, H),
                "(", "-size", "%dx%d" % (W, H), "xc:white", "-fill",
                "black", "-draw", "rectangle %d,%d %d,%d" % (
                    r, r, W - 1 - r, H - 1 - r), ")",
                "-alpha", "off", "-compose", "copy-opacity", "-composite",
                ")", "-compose", "over", "-composite"]
    # Light from the top left: a raised rim, or a sunken box.
    shade = "-75,-75" if inset else "75,75"
    magick(*cmd, "(", "-size", "%dx%d" % (W, H), "xc:gray50", "-fill",
           "gray%d" % (30 if inset else 75), "-draw",
           "rectangle 0,0 %d,%d" % (W - 1, r - 1), "-draw",
           "rectangle 0,0 %d,%d" % (r - 1, H - 1), "-fill",
           "gray%d" % (75 if inset else 30), "-draw",
           "rectangle 0,%d %d,%d" % (H - r, W - 1, H - 1), "-draw",
           "rectangle %d,0 %d,%d" % (W - r, W - 1, H - 1), ")",
           "-compose", "hardlight", "-composite", path)
    del shade


def lettered(path, base, words, ink, w, h, font, k, lines=1, shadow=True):
    """A plaque with its words on it, cut a little in (a dark edge
    under each letter)."""
    t = path + ".w.png"
    text(t, words, ink, w * 0.86, h * 0.8, font, k, lines=lines)
    if shadow:
        magick(base, "(", t, "-background", "#000000c0", "-shadow",
               "70x%gx%d+%d" % (k * 0.5, round(k * 0.6), round(k * 0.6)),
               ")", "-gravity", "center", "-geometry",
               "+%d+%d" % (round(k * 0.5), round(k * 0.5)), "-composite",
               t, "-gravity", "center", "-composite", path)
    else:
        magick(base, t, "-gravity", "center", "-composite", path)
    os.remove(t)


def board(path, src, words, font, k):
    """The board: dark wood in a gilt frame, the carving at its top, the
    slider's groove between gilt arrows, and its three boxes ("Take
    all", and the space in the hold), in the original's places."""
    W, H = px(BOARD[0], k), px(BOARD[1], k)
    tmp = path + ".b.png"
    panel(tmp, src["wood"], *BOARD, k, rim=src["gilt"])

    def box(x0, y0, x1, y1, name):
        p = path + ".%s.png" % name
        panel(p, src["darkwood"], x1 - x0, y1 - y0, k, rim=src["gilt"],
              inset=True)
        magick(tmp, p, "-geometry", "+%d+%d" % (px(x0, k), px(y0, k)),
               "-composite", tmp)
        os.remove(p)

    # The carving, across the top.
    magick(tmp, "(", src["carving"], "-trim", "+repage", "-resize",
           "%dx%d" % (px(170, k), px(36, k)), ")", "-gravity", "north",
           "-geometry", "+0+%d" % px(8, k), "-composite", tmp)
    # The slider's groove, x 44-186 at y 54-69, and its arrows.
    box(44, 54, 186, 69, "groove")
    for x0, x1 in ((28, 44), (200, 186)):
        tri = "%d,%d %d,%d %d,%d" % (px(x0, k), px(61.5, k), px(x1, k),
                                     px(49, k), px(x1, k), px(74, k))
        magick(tmp, "(", src["gilt"], "-resize", "%dx%d^" % (W, H),
               "-gravity", "center", "-extent", "%dx%d" % (W, H),
               "(", "-size", "%dx%d" % (W, H), "xc:black", "-fill",
               "white", "-draw", "polygon " + tri, ")", "-alpha", "off",
               "-compose", "copy-opacity", "-composite", ")",
               "-compose", "over", "-composite", tmp)
    box(26, 81, 112, 110, "sign")		# the Sell / Buy sign goes here
    box(116, 81, 206, 110, "take")
    box(26, 113, 206, 141, "hold")
    t = path + ".t.png"
    for words_, x0, y0, w, h in ((words["take_all"], 118, 83, 86, 25),
                                 (words["space"], 30, 116, 100, 22),
                                 (words["tons"], 166, 116, 38, 22)):
        text(t, words_, INK["cream"], w, h, font, k,
             lines=words_.count("\n") + 1)
        magick(tmp, t, "-geometry", "+%d+%d" % (px(x0, k), px(y0, k)),
               "-composite", tmp)
    os.remove(t)
    os.replace(tmp, path)


def compose(src, words, font, k, work, out, take=False):
    """The three banks of one language: their frames composed into
    "work", then laid side by side, each with its sizes.  Returns
    {bank: (atlas path, [(w, h), ...])}.  The loot board's set ("take")
    has its sign read Discard and Take, a fourth name tag with a ship
    (frame 92, dividing the squadron) and only the O.K. sign in its
    third bank.  Both sets end with the cannon mirrored (93, lit 94)."""
    os.makedirs(work, exist_ok=True)
    frames = []

    def f(name):
        p = os.path.join(work, name + ".png")
        frames.append(p)
        return p

    # 0-19: the large digits, in black, then in red (the selected row).
    for ink in ("black", "red"):
        for d in range(10):
            text(f("big-%s-%d" % (ink, d)), str(d), INK[ink],
                 DIGIT_W[d], 17, font, k, halo=HALO
                 if ink == "black" else "#00000060")
    # 20-52: "/" and the small digits, in black, red and cream.
    for ink in ("black", "red", "cream"):
        for n, c in enumerate("/0123456789"):
            text(f("small-%s-%d" % (ink, n)), c, INK[ink], *SMALL, font,
                 k, plate=None if ink == "cream" else PLATE)
    # 53-64: the goods' names, in black, then in red.
    for ink in ("black", "red"):
        for g in GOODS:
            text(f("word-%s-%s" % (ink, g)), words[g], INK[ink], *WORD,
                 font, k, halo=HALO if ink == "black" else
                 "#00000060")
    # 65-76: the goods, then lit for the selected row.
    for bright in (0, 18):
        for g, size in zip(GOODS, ICONS):
            fit(f("good-%d-%s" % (bright, g)), src[g], *size, k,
                bright=bright)
    fit(f("gem"), src["gem"], *GEM, k)				# 77
    for n in range(78, 82):					# 78-81
        magick("-size", "%dx%d" % (px(21, k), px(27, k)), "xc:none",
               f("none-%d" % n))
    signs = ("discard", "take") if take else ("sell", "buy")
    for n, key in zip((82, 83), signs):				# the signs
        base = os.path.join(work, "sign-%s-base.png" % key)
        panel(base, src["darkwood"], *SIGN, k)
        lettered(f(key), base, words[key], INK["cream"], *SIGN, font, k)
        os.remove(base)
    panel(f("gold"), src["parchment"], *GOLD, k, rim=src["gilt"])	# 84
    for ink in ("black", "red"):					# 85-86
        text(f("pcs-%s" % ink), words["pcs"], INK[ink], *PCS, font, k,
             plate=PLATE)
    fit(f("colours"), src["colours"], *COLOURS, k)		# 87
    for key in ("merchant", "overboard", "captured", "plundered"):
        base = os.path.join(work, "plaque-%s-base.png" % key)
        panel(base, src["oak"], *PLAQUE, k, rim=src["oak"])
        lettered(f(key), base, words[key], INK["cream"], *PLAQUE, font,
                 k, lines=words[key].count("\n") + 1)
        os.remove(base)
    if take:		# 92: the squadron's tag, a ship on oak
        base = os.path.join(work, "plaque-ship-base.png")
        panel(base, src["oak"], *PLAQUE, k, rim=src["oak"])
        ship = os.path.join(work, "plaque-ship.png")
        fit(ship, src["ship"], PLAQUE[0] * 0.8, PLAQUE[1] * 0.8, k)
        magick(base, ship, "-gravity", "center", "-composite", f("ship"))
        os.remove(base)
        os.remove(ship)
    else:
        magick("-size", "%dx%d" % (px(1, k), px(2, k)), "xc:none",
               f("dot"))
    # 93-94: the cannon (70, lit 76) mirrored, for the board's other side.
    for bright in (0, 18):
        magick(os.path.join(work, "good-%d-cannon.png" % bright), "-flop",
               f("good-%d-cannon-flop" % bright))

    banks = {}
    sizes = [identify(p) for p in frames]
    banks["buysell"] = (strip(frames, os.path.join(out, "buysell")), sizes)
    b = os.path.join(work, "board.png")
    board(b, src, words, font, k)
    banks["buyboard"] = (strip([b], os.path.join(out, "buyboard")),
                         [identify(b)])
    ok = os.path.join(work, "ok-base.png")
    fit(ok, src["scroll"], *OK, k)
    okw = os.path.join(work, "ok.png")
    lettered(okw, ok, words["ok"], INK["black"], OK[0] * 0.55, OK[1] * 0.6,
             font, k, shadow=False)
    guns = [os.path.join(work, "gun-%d.png" % n) for n in (0, 1)]
    fit(guns[1], src["gun"], *GUN, k)
    magick(guns[1], "-flop", guns[0])		# left of the board, facing in
    one = [okw] if take else [okw] + guns
    banks["buysell1"] = (strip(one, os.path.join(out, "buysell1")),
                         [identify(p) for p in one])
    return banks


def identify(p):
    r = subprocess.run(["magick", "identify", "-format", "%w %h", p],
                       capture_output=True, text=True, check=True)
    return tuple(int(v) for v in r.stdout.split())


def strip(frames, out):
    """The frames side by side, top aligned, transparent between."""
    magick("-background", "none", *frames, "-gravity", "north",
           "+append", "-depth", 8, out + ".png")
    return out + ".png"
