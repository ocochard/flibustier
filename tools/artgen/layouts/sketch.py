#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Draw the line sketches of the screens with clickable spots.

Each screen's objects are drawn where the game expects clicks (the spot
rectangles of the original executable, in 640x480 pixels), as white
outlines on black, for the Canny ControlNet.  Only the layout comes from
the game; the drawing is ours.  Needs ImageMagick.

    sketch.py            writes town.png, tavern.png and cabin.png here
"""

import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 1472, 1104		# the render size of a 4:3 picture
K = W / 640


def sx(v):
    return round(v * K)


def rect(x0, y0, x1, y1):
    return "rectangle %d,%d %d,%d" % (sx(x0), sx(y0), sx(x1), sx(y1))


def line(*pts):
    return "polyline " + " ".join("%d,%d" % (sx(x), sx(y)) for x, y in pts)


def ellipse(cx, cy, rx, ry):
    return "ellipse %d,%d %d,%d 0,360" % (sx(cx), sx(cy), sx(rx), sx(ry))


def sign(x0, y0, x1, y1):
    """The board for the screen's title (drawn at y 38): a plank fixed
    flush along the top edge, as a hanging one ends up too low."""
    return [rect(x0, 0, x1, y1), line((x0 + 8, 8), (x1 - 8, 8))]


# Town: a sunny square of a Caribbean port, buildings at the spots.
TOWN = sign(212, 0, 436, 66) + [
    line((0, 150), (640, 150)),				# horizon
    # Leave Town: the road to the harbour on the left.
    line((0, 349), (80, 300), (159, 260)), line((0, 300), (110, 262)),
    # Shipwright: a timber shed at the bottom left.
    rect(10, 360, 150, 425), line((10, 360), (80, 330), (150, 360)),
    # Tavern: a two-storey house with a wide door, left of centre.
    rect(165, 250, 355, 428), rect(235, 340, 290, 428),
    line((160, 250), (260, 200), (360, 250)),
    # Merchant: a shop with an awning, centre right.
    rect(362, 285, 467, 368), line((358, 285), (412, 262), (471, 285)),
    rect(395, 315, 435, 368),
    # Bank: a stone building with columns, right.
    rect(472, 270, 617, 388), line((472, 270), (545, 240), (617, 270)),
    rect(500, 300, 512, 388), rect(578, 300, 590, 388),
    # Governor: the mansion's gate on the hill, far right.
    rect(540, 150, 639, 258), rect(575, 195, 610, 258),
    line((530, 150), (590, 120), (640, 145)),
    line((0, 440), (640, 440)),				# the square's edge
]

# Tavern: the room, people and furniture at the spots.
TAVERN = sign(212, 0, 436, 66) + [
    line((0, 80), (640, 80)),				# ceiling beam
    # The bar along the back left, the bartender behind it.
    rect(70, 270, 210, 300), ellipse(110, 225, 18, 22),
    line((92, 248), (85, 290)), line((128, 248), (135, 290)),
    line((60, 150), (220, 150)), line((60, 190), (220, 190)),	# shelves
    # The barmaid standing, centre right.
    ellipse(455, 200, 17, 21), line((440, 222), (425, 355), (485, 355),
                                    (470, 222)),
    # A table with sailors at the bottom centre.
    ellipse(285, 390, 85, 30), ellipse(240, 330, 16, 19),
    ellipse(330, 330, 16, 19), line((225, 350), (215, 470)),
    line((345, 350), (355, 470)),
    # Another table at the bottom right.
    ellipse(530, 420, 50, 22), ellipse(520, 360, 15, 18),
    # The door on the right edge.
    rect(585, 95, 639, 478), line((600, 290), (612, 290)),
    line((0, 478), (640, 478)),
]

# Cabin: the captain's cabin, its objects at the spots.
CABIN = sign(212, 0, 436, 66) + [
    rect(160, 80, 300, 150), rect(330, 80, 470, 150),	# stern windows
    # Practice fencing: two crossed swords on the wall, upper left.
    line((90, 70), (130, 180)), line((130, 70), (90, 180)),
    # Logbook: a big book on a shelf.
    rect(140, 185, 240, 214), line((135, 216), (250, 216)),
    # Treasure cave: an iron-bound sea chest on the left.
    rect(10, 260, 130, 350), line((10, 285), (130, 285)),
    # Search: a telescope on a stand.
    line((255, 150), (310, 190)), line((282, 170), (270, 278)),
    line((282, 170), (295, 278)),
    # Exit game: the curtained bed at the back centre.
    rect(330, 120, 485, 255), line((330, 160), (485, 160)),
    # The table in the middle, maps spread on it.
    rect(320, 255, 490, 305), line((330, 305), (335, 380)),
    line((480, 305), (475, 380)), rect(340, 262, 480, 298),
    # Map of the Caribbean: a large globe at the bottom centre left.
    ellipse(235, 395, 75, 75), line((160, 470), (310, 470)),
    # Save game: a tall chest of drawers with a mirror, right.
    rect(505, 85, 635, 265), rect(530, 95, 610, 160),
    line((505, 200), (635, 200)), line((505, 232), (635, 232)),
    # Continue travels: a pair of sea boots at the bottom right.
    rect(520, 395, 560, 470), rect(575, 395, 615, 470),
    line((0, 478), (640, 478)),
]


def draw(name, ops):
    cmd = ["magick", "-size", "%dx%d" % (W, H), "xc:black", "-fill",
           "none", "-stroke", "white", "-strokewidth", "5"]
    for op in ops:
        cmd += ["-draw", op]
    cmd.append(os.path.join(HERE, name))
    subprocess.run(cmd, check=True)


if __name__ == "__main__":
    draw("town.png", TOWN)
    draw("tavern.png", TAVERN)
    draw("cabin.png", CABIN)
