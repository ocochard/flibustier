#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""The screens' layouts (data/layout/<screen>): hot spots and piece
positions for the open pack's own pictures.

Every rectangle was measured on the open pack's art (1440x1080, scaled
by 480/1080 to the 640x480 logical screen) over a 20-pixel grid, or
designed where the pack has no picture; nothing comes from the original
game.  A rectangle is [x0, y0, x1, y1], inclusive.
"""

# The captain's cabin, on pic/cabin.  The fifth word is the spot's "on":
# the engine opens save, search and treasure maps from the rules, and
# every other spot from this word (src/ui/screens.c, cabin_open).  The
# engine has no party to divide or join, so those two stay off.
CABIN = [
    # 0 continue travels: the brass telescope on its tripod, by the
    # porthole.
    [248, 170, 318, 292, 1],
    # 1 treasure maps: the chart spread on the table (the table sprite,
    # without the chart, is drawn over it while there are none).
    [332, 258, 492, 304, 1],
    # 2 treasure cave: the iron-bound sea chest on the left, below the
    # book lying on it, left of the globe standing in front of it.
    [24, 283, 139, 460, 1],
    # 3 log book: the leather book lying on the chest.
    [44, 250, 150, 282, 1],
    # 4 map of the Caribbean: the globe on its stand, in front.
    [140, 318, 330, 479, 1],
    # 5 search: the boots on the floor, for going ashore.
    [490, 362, 562, 470, 1],
    # 6 divide party: the gilt mirror on the right wall (never open).
    [528, 90, 610, 158, 0],
    # 7 join party: the book on the window sill (never open).
    [140, 199, 198, 216, 0],
    # 8 save game: the chest of drawers on the right.
    [505, 196, 618, 358, 1],
    # 9 exit game: the curtained bed at the back.
    [320, 100, 485, 244, 1],
    # 10 practice fencing: the crossed swords on the wall.
    [80, 62, 138, 195, 1],
]

# A town, on pic/town.  The name plaque spans x 212-436, y 0-75: every
# spot stays below or beside it.  The harbour and the shipwright's shed
# at the bottom left are the town sprite's (0, 148)-(176, 479), drawn
# while the ship is in port.
TOWN = [
    # 0 governor's residence: the walled house with the bell tower and
    # the flag, at the back on the right.
    [462, 68, 612, 232],
    # 1 merchant: the thatched stall with the awning and its goods.
    [362, 235, 472, 385],
    # 2 bank: the stone house with the heavy door, on the right.
    [474, 238, 620, 385],
    # 3 tavern: the large white house with the balcony, in the middle.
    [160, 200, 358, 400],
    # 4 shipwright: the shed with the boat, bottom left (in port only).
    [0, 323, 150, 479],
    # 5 city information: the tall narrow house with the small balcony,
    # left of the tavern.
    [120, 168, 159, 395],
    # 6 leave town: the road down to the shore and the ship at anchor.
    [0, 150, 117, 322],
    # 7 not used by the engine.
    [0, 0, 0, 0],
]

# The tavern, on pic/tavern.  The map seller, the information seller
# and the stranger are the tavern sprite's frames 0-2, drawn at (0, 255),
# (142, 259) and (349, 199) plus each frame's offset, only while they
# are there; the others are in the picture.
TAVERN = [
    # 0 map seller: the old bearded man in yellow, at the left table
    # (frame 0).
    [6, 262, 120, 444],
    # 1 information seller: the man in the turban, arms crossed (frame
    # 1).
    [150, 262, 214, 440],
    # 2 seaman: the young man with the red scarf, at the front table.
    [216, 296, 318, 479],
    # 3 bartender: the man behind the bar, on the left.
    [80, 178, 138, 265],
    # 4 barmaid: the woman in the red skirt, by the back wall.
    [420, 198, 492, 372],
    # 5 stranger: the figure in the dark doorway at the back (frame 2).
    [350, 198, 418, 300],
    # 6 door: the open plank door on the right.
    [512, 138, 612, 296],
    # 7 seaman: the old sailor in the green head scarf.
    [280, 262, 372, 378],
    # 8 seaman: the broad man seated on the right.
    [506, 297, 612, 479],
    # 9 seaman: the seamen's round table, with the mugs and the coins.
    [320, 380, 504, 479],
]

# The weapon choice, img/wpnsel drawn at (369, 56) (the case of three
# blades): spot i is weapon i.
WEAPONS = [
    # 0 rapier: the thin straight blade with the swept hilt, left.
    [398, 78, 436, 405],
    # 1 longsword: the long blade with the gilt cross guard, right.
    [487, 74, 548, 420],
    # 2 cutlass: the short curved blade, in the middle.
    [440, 96, 485, 345],
]

# The treasure cave, on pic/trescave: x, y of each pile (spr/tcave0-7,
# drawn by its top left) and the fortune, in gold pieces, past which it
# shows.  Small piles at the back on the dark floor, the three large
# ones in front; the last, the fullest chest, at the skeleton's feet.
# A career starts with 200 and gains 5 to 20 % of each divided plunder,
# so the marks grow about twofold, from the first good share to a
# famous fortune.
CAVE = [
    [130, 280, 300],	# 0 a small heap of coins
    [270, 335, 1000],	# 1 a bed of coins spread on the floor
    [40, 250, 2500],	# 2 a stack of gold bars
    [0, 340, 5000],	# 3 a heap of coins, pearls and jewels
    [220, 260, 10000],	# 4 a small chest of jewels
    [20, 362, 20000],	# 5 an open chest full of coins
    [230, 338, 35000],	# 6 a heap of plate, swords and goblets
    [427, 282, 60000],	# 7 a crowned chest, overflowing
]

# The fleet panel (no open picture yet): 8 ships in two rows of four
# columns 160 wide.  A battle picture is 326x194 art pixels, 145x86 on
# screen; under each ship its type (title font, 25 high in the open
# pack) then its damage (body font, 15 high), centred on the column.
# The labels' y is their top, as the engine draws them.  The prompt
# is drawn at y 400 in x 5-420 and the button's label centred on
# x 510 at y 436, so the button spot sits right of the prompt.
PANEL_COLUMN = 160
PANEL_SHIP_W = 145
PANEL_ROWS = (28, 212)		# each row's ship top
PANEL_TYPE_DY = 90		# type label's top, below the ship's top
PANEL_DAMAGE_DY = 116		# damage label's top
PANEL_BUTTON = [430, 428, 590, 468, 1]


def cabin():
    return ({"spots": CABIN},
            "Measured on the open pack's art/cabin.png scaled to 640x480 "
            "over a 20-pixel grid: each spot covers the object that "
            "stands for it (telescope, chart, chest, book, globe, boots, "
            "mirror, sill book, drawers, bed, crossed swords). Divide and "
            "join party, which the engine never offers, are off.")


def town():
    return ({"spots": TOWN},
            "Measured on the open pack's art/town.png and its in-port "
            "sprite (art/town-sprites.png) scaled to 640x480: one "
            "rectangle per building, clear of the name plaque; leave "
            "town covers the road to the shore and the anchored ship.")


def tavern():
    return ({"spots": TAVERN},
            "Measured on the open pack's art/tavern.png and the people "
            "of art/tavern-sprites.png at the engine's drawing places, "
            "scaled to 640x480: one rectangle per person, the door, and "
            "the seamen's table as the fourth crew spot.")


def weapons():
    return ({"spots": WEAPONS},
            "Measured on the open pack's art/wpnsel.png, drawn at "
            "(369, 56) and scaled to 640x480: each rectangle covers a "
            "blade, told by its look (swept-hilt rapier, curved cutlass, "
            "cross-guarded longsword).")


def cave():
    return ({"piles": CAVE},
            "Designed on the open pack's art/trescave.png with the "
            "piles art/tcave0-7: positions chosen by composing them on "
            "the cave floor so each stays visible; fortune marks "
            "designed to roughly double, from 300 (a career starts "
            "with 200) to 60000.")


def fleet_panel():
    xs = [c * PANEL_COLUMN + (PANEL_COLUMN - PANEL_SHIP_W) // 2
          for c in range(4)]
    ship_x, ship_y, spots, type_x, type_y, dmg_x, dmg_y = ([] for _ in
                                                           range(7))
    for top in PANEL_ROWS:
        for c, x in enumerate(xs):
            ship_x.append(x)
            ship_y.append(top)
            spots.append([c * PANEL_COLUMN + 4, top - 4,
                          (c + 1) * PANEL_COLUMN - 4,
                          top + PANEL_DAMAGE_DY + 18, 1])
            type_x.append(c * PANEL_COLUMN + PANEL_COLUMN // 2)
            type_y.append(top + PANEL_TYPE_DY)
            dmg_x.append(c * PANEL_COLUMN + PANEL_COLUMN // 2)
            dmg_y.append(top + PANEL_DAMAGE_DY)
    spots.append(PANEL_BUTTON)
    return ({"ship_x": ship_x, "ship_y": ship_y, "spots": spots,
             "type_x": type_x, "type_y": type_y,
             "damage_x": dmg_x, "damage_y": dmg_y},
            "Designed: two rows of four columns 160 wide, each ship's "
            "battle picture (145x86 on screen) centred in its column, "
            "its type and damage labels below it, the ship's spot "
            "around all three; the button's spot around its label at "
            "(510, 436), right of the prompt.")


TABLES = [
    ("data/layout/cabin", "layout-cabin.json", cabin),
    ("data/layout/town", "layout-town.json", town),
    ("data/layout/tavern", "layout-tavern.json", tavern),
    ("data/layout/weapons", "layout-weapons.json", weapons),
    ("data/layout/cave", "layout-cave.json", cave),
    ("data/layout/fleet-panel", "layout-fleet-panel.json", fleet_panel),
]
