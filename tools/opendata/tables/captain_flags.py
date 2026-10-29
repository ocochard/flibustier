#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""data/captain-flags: the Jolly Roger each named enemy captain flies.

A row per nation (Spain, England, France, Holland), a column per surname
slot 0-7; a value 1-20 picks pic/fjolly01 ... pic/fjolly20.  Designed
from the open pack's own 20 pictures (art/fjolly*.png), nothing else.
"""

# What each open picture shows, to reason about the assignment below.
LOOK = {
    1: "skeleton with a spear beside a figure",
    2: "swordsman standing on two skulls",
    3: "three skulls over crossbones",
    4: "plain skull and crossbones, large",
    5: "plain skull and crossbones, small",
    6: "skull, crossbones and hourglass",
    7: "skull in a bandana over crossbones",
    8: "skull over two crossed cutlasses",
    9: "plain skull and crossbones, large",
    10: "skull, bone and cutlass crossed",
    11: "skull in a broad hat, bone and cutlass",
    12: "skull and crossbones, dagger, red heart",
    13: "arm raising a cutlass, white bar",
    14: "gentleman with a sword beside a skull",
    15: "horned skeleton, spear, bleeding heart, hourglass (red)",
    16: "large skull beside a big red heart",
    17: "skull and crossbones with a small red heart",
    18: "red skeleton on black",
    19: "red field, yellow skull, crossed swords",
    20: "skull with flaming red hair, crossed sabres",
}

# Slots 0-4 are the captains a player meets again and again: across the
# four nations that is exactly 20 captains, so each gets a picture of
# his own and all 20 pictures are seen.  The near twins are kept apart
# so no nation has two captains that look alike at a glance: the plain
# skulls 4, 5, 9; the skeletons 1, 15, 18; the red hearts 16 and 17.
# Each row leans to a style for its nation:
COMMON = [
    # Spain: the red-and-gold and the most baroque, devilish designs.
    [19, 15, 20, 6, 4],
    # England: figures of men, the duellist and the raised arm.
    [2, 1, 14, 13, 5],
    # France: the buccaneer's hat, the bandana, the cutlasses.
    [11, 7, 8, 17, 9],
    # Holland: the three skulls, the red skeleton, the hearts.
    [3, 18, 16, 12, 10],
]

# Slot 5 is met rarely: with no picture left, it borrows another
# nation's flag, one that does not resemble anything in its own row.
RARE = [13, 8, 2, 20]

# Slots 6-7 are practically never met; they repeat the row's first two,
# the nation's signature flags, so every value stays a valid picture.


def build():
    flags = [COMMON[n] + [RARE[n]] + COMMON[n][:2] for n in range(4)]
    assert all(len(r) == 8 and all(1 <= v <= 20 for v in r)
               for r in flags)
    assert sorted(v for r in COMMON for v in r) == list(range(1, 21))
    assert all(RARE[n] not in COMMON[n] for n in range(4))
    notes = ("Designed from the open pack's 20 Jolly Roger pictures. "
             "Slots 0-4, the commonly met captains, use each picture "
             "exactly once over the four nations, so every common "
             "captain is told apart by his flag; look-alikes (the plain "
             "skulls 4/5/9, the skeletons 1/15/18, the hearts 16/17) "
             "sit in different nations. Rows lean to a style: Spain the "
             "red-and-gold and baroque designs, England the human "
             "figures, France the hat, bandana and cutlasses, Holland "
             "the three skulls, red skeleton and hearts. Slot 5 (rare) "
             "borrows a flag from another nation not resembling its own "
             "row; slots 6-7 (practically never met) repeat the row's "
             "first two.")
    return {"flags": flags}, notes


TABLES = [("data/captain-flags", "captain-flags.json", build)]
