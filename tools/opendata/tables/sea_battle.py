#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""The sea battle's hit boxes, shot arc and bow cross (data/sea-battle).

How the engine reads the table (src/sim/battle.c, src/ui/screens.c):

- A ship's position is the centre of its sprite, in 640x480 pixels; a
  shot starts at the firing ship's position and lands where it is when
  its flight ends.  It hits when it lands within half_w / half_h pixels
  of the target's position on each axis.
- The box grows by hit_steps[level] quarters of itself (level 0 the
  easiest), so a step adds a quarter of the half size.
- The shot's arc has 16 steps, 0 at the guns, 15 when it lands (a short
  shot stays on step 0).  The balls are drawn at the shot's position and
  their shadow shot_rise pixels lower (the value is 0 or less), so the
  arc must end on 0, where the splash is drawn.
- The bow cross's centre is drawn at the ship plus bow_mark minus
  (29, 21), from the drawing's offsets; headings go clockwise from
  north, heading / 16 indexing the table.

Every number below was measured on the open pack's art or designed
here; nothing comes from the original game.
"""
import math

# The open pack's battle ships (art/ship0.png .. ship2.png, 16 headings
# of 176x130 art pixels at 1080 lines, so x 480/1080 on screen).  The
# mean over the 16 headings of half the opaque extent (alpha >= 128),
# hull and sails, in screen pixels, measured once with a PNG reader:
# small (types 0-2), medium (3-5), large (6-8), as the engine picks the
# sprite by type.
MEAN_HALF = [(10.50, 12.11), (13.17, 15.49), (18.93, 20.96)]

# Within a size the three types share a sprite; the engine lists them
# smaller to larger, so the box grows a tenth a type.
TYPE_FACTOR = [0.9, 1.0, 1.1]

# The fort.  The engine sizes the fort's box itself (battle.c, box()),
# so this entry is not read; it is a square a battle tile and a half
# wide (48 pixels), the walls of a fort on its tile.
FORT_HALF = 24

# The hit box's widening in quarters, by level: the easiest level
# doubles the box, every level takes a quarter off, the hardest asks for
# a shot on the hull itself.
LEVELS = 5

# The arc: a parabola over the 16 steps, peaking at ARC_HEIGHT pixels
# above the water halfway.  About half the height of a medium ship's
# sprite, so the balls read as lobbed rather than skimming.
STEPS = 16
ARC_HEIGHT = 12

# The bow cross sits on an ellipse round the ship: the open art is seen
# from above at an angle, so the hull looks shorter north-south than
# east-west.  Measured on the medium ship (the commonest the player
# sails), its bow reaches 20.9 pixels east or west of the centre
# (heading 4: +20.0, heading 12: -21.8) and 16.4 south (heading 8); a
# gap of 5 pixels keeps the cross off the bowsprit.  A small ship's
# cross stands a little further off, a large one's on its bow.
BOW_EW = 20.9
BOW_NS = 16.4
BOW_GAP = 5
# From the drawing: the cross's centre is the ship plus the entry minus
# this.
MARK_ORIGIN = (29, 21)


def boxes():
    w, h = [], []
    for size in range(3):
        mw, mh = MEAN_HALF[size]
        for f in TYPE_FACTOR:
            w.append(round(mw * f))
            h.append(round(mh * f))
    return w + [FORT_HALF], h + [FORT_HALF]


def arc():
    last = STEPS - 1
    rise = [-round(ARC_HEIGHT * 4 * s * (last - s) / (last * last))
            for s in range(STEPS)]
    # Rounding gives -0 for the ends; JSON wants plain 0.
    rise = [r if r != 0 else 0 for r in rise]
    # The spread opens a frame every 4 steps: tight at the guns, widest
    # as it comes down.
    frame = [min(3, s * 4 // STEPS) for s in range(STEPS)]
    return rise, frame


def bow():
    rx, ry = BOW_EW + BOW_GAP, BOW_NS + BOW_GAP
    out = []
    for i in range(16):
        a = 2 * math.pi * i / 16
        out.append(MARK_ORIGIN[0] + round(rx * math.sin(a)))
        out.append(MARK_ORIGIN[1] - round(ry * math.cos(a)))
    return out


def build():
    w, h = boxes()
    rise, frame = arc()
    value = {
        "half_w": w,
        "half_h": h,
        "hit_steps": [LEVELS - 1 - lvl for lvl in range(LEVELS)],
        "shot_rise": rise,
        "shot_frame": frame,
        "bow_mark": bow(),
    }
    notes = ("Designed for the open pack; nothing from the original game. "
             "Hit boxes: the mean half extent over 16 headings of the open "
             "battle ship sprites (opaque hull and sails, 1080-line art "
             "scaled to 640x480), one sprite per size of three types, the "
             "three types of a size at 0.9, 1.0 and 1.1 of it so a bigger "
             "ship is easier to hit; the fort's entry, not read by the "
             "engine, is a 48-pixel square. Widening: 4 quarters at the "
             "easiest level down to none at the hardest. Shot arc: a "
             "parabola 12 pixels high over the 16 steps, 0 at both ends; "
             "the spread opens one frame every 4 steps. Bow cross: an "
             "ellipse measured on the medium ship's bow (20.9 pixels east "
             "and west, 16.4 north and south) plus a 5-pixel gap, at the "
             "16 headings clockwise from north, offset by the drawing's "
             "origin (29, 21).")
    return value, notes


TABLES = [("data/sea-battle", "sea-battle.json", build)]
