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
    # 0 continue travels: the boots on the floor, to walk out of the
    # cabin, as players expect of them.
    [490, 362, 562, 470, 1],
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
    # 5 search: the brass telescope on its tripod, by the porthole, to
    # scan the land.
    [248, 170, 318, 292, 1],
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

# The fleet panel, on pic/shipsel: 8 framed windows in two rows of four,
# each with two plaques below it.  Each window's picture area is exactly
# a battle picture's frame (326x194 art pixels, 145x86 on screen), so a
# ship is drawn at that area's top left.  The columns are not evenly
# spaced on the art, so each is measured.  The type (title font, 25
# high in the open pack) and the damage (body font, 15 high) are
# centred on their plaques; their y is their top, as the engine draws
# them.  The prompt is drawn at y 400 in x 5-420, on the wood between
# the second row and the button plaque (y 430-474, x 440-578, with its
# arrow to x 630), which the engine's button label at (510, 436) fits.
PANEL_COLUMNS = [	# [ship x, spot x0, spot x1, plaques' centre x]
    [12, 10, 158, 84],
    [169, 167, 316, 241],
    [325, 323, 472, 397],
    [482, 480, 628, 554],
]
PANEL_ROWS = [		# [ship y, spot y0, spot y1, type top, damage top]
    [33, 30, 192, 130, 171],
    [224, 221, 384, 321, 362],
]
PANEL_BUTTON = [440, 430, 630, 474, 1]

# The trading board (merchant and loot board): pieces of the board's
# first sprite bank (spr/buysell, spr/take-dis and their French and
# German twins, which number their frames alike), each [frame,
# highlighted frame, x, y] by its top left.  The engine draws the board
# itself at (203, 17), the Sell/Buy sign, the O.K. sign and every number
# at fixed places (src/ui/screens.c, merchant_draw); these pieces are
# laid out around them.  Record 0 is the name tag at the top left, whose
# frames the engine picks by mode (88 merchant, 89 overboard, 90
# captured, 91 plundered); the top corners are the sell and buy clicks.
BOARD_STATIC = [
    [88, 88, 0, 0, 1],		# 0 the name tag, top left (82x69)
    [87, 87, 559, 0, 1],	# 1 the player's colours, top right (81x69)
    # 2-3 the purses' parchment boxes (77x29), centred on the engine's
    # six small digits at (20, 84) and (576, 84).
    [84, 84, 6, 74, 1],
    [84, 84, 561, 74, 1],
    [78, 78, 0, 0, 0],		# 4 no piece: an empty frame
]
# The fifth value: whether the loot board shows the piece too.
# The rows: food, goods, sugar on both boards, cannon on the loot board
# only (the merchant draws records 0-22).  Loot has no price, so the
# loot board leaves out the "pcs" after it.  Their goods in the bank's
# order (food, goods, sugar, tobacco, hides, cannon), and each good's
# picture size on screen (its art over 2.25).
BOARD_GOODS = [0, 1, 2, 5]
BOARD_ICONS = {0: (22, 36), 1: (22, 32), 2: (22, 28), 5: (47, 26)}
BOARD_ROW_H = 50
# A row reads as a ledger line, centred on the engine's large digits
# (y 117-133 in row 0): the good's name at the outer edge, the price
# (small digits, x 71-91 and 534-554) with "pcs" after it, the stock or
# the cargo (large digits from x 106 and 475), and the good's picture
# on the row's click area (x 169-190 and 444-465, y 107-147), centred
# on it, a wider one held to the area's outer side.
BOARD_WORD_Y = 116		# 18 high, the digits' middle
BOARD_PCS_Y = 123		# 6 high, on the small digits' foot (119-128)


def board_rows():
    rows = []
    for i, g in enumerate(BOARD_GOODS):
        dy = i * BOARD_ROW_H
        w, h = BOARD_ICONS[g]
        icon_y = 125 - h // 2 + dy
        rows += [
            [53 + g, 59 + g, 6, BOARD_WORD_Y + dy, 1],	# name, merchant's
            [65 + g, 71 + g, 169, icon_y, 1],		# picture
            [85, 86, 92, BOARD_PCS_Y + dy, 0],		# "pcs" after the price
            [53 + g, 59 + g, 572, BOARD_WORD_Y + dy, 1],	# name, player's
            [65 + g, 71 + g, 466 - w, icon_y, 1],	# picture
            [85, 86, 556, BOARD_PCS_Y + dy, 0],		# "pcs"
        ]
    return rows


def cabin():
    return ({"spots": CABIN},
            "Measured on the open pack's art/cabin.png scaled to 640x480 "
            "over a 20-pixel grid: each spot covers the object that "
            "stands for it (boots, chart, chest, book, globe, telescope, "
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
    ship_x, ship_y, spots, type_x, type_y, dmg_x, dmg_y = ([] for _ in
                                                           range(7))
    for sy, y0, y1, ty, dy in PANEL_ROWS:
        for sx, x0, x1, cx in PANEL_COLUMNS:
            ship_x.append(sx)
            ship_y.append(sy)
            spots.append([x0, y0, x1, y1, 1])
            type_x.append(cx)
            type_y.append(ty)
            dmg_x.append(cx)
            dmg_y.append(dy)
    spots.append(PANEL_BUTTON)
    return ({"ship_x": ship_x, "ship_y": ship_y, "spots": spots,
             "type_x": type_x, "type_y": type_y,
             "damage_x": dmg_x, "damage_y": dmg_y},
            "Measured on the open pack's art/shipsel.png scaled to "
            "640x480: each ship's battle picture (145x86 on screen) at "
            "the top left of its window's picture area, which is its "
            "size; the type and damage labels centred on the window's "
            "two plaques; the ship's spot around the window and both "
            "plaques; the button's spot around the button plaque and "
            "its arrow, below the prompt.")


def board():
    return ({"pieces": BOARD_STATIC + board_rows()},
            "Designed on the open pack's art/buysell.png (its frames "
            "scaled by 480/1080) around the engine's fixed board, signs "
            "and numbers: the name tag and the player's colours in the "
            "top corners (the sell and buy clicks), parchment boxes "
            "centred under the purses, and each row a ledger line "
            "centred on its large digits: name at the outer edge, price "
            "with \"pcs\" (left out on the loot board), count, and the good's picture on the row's "
            "click area. Checked by rendering the merchant and the loot "
            "board headless with the open pack.")


TABLES = [
    ("data/layout/cabin", "layout-cabin.json", cabin),
    ("data/layout/town", "layout-town.json", town),
    ("data/layout/tavern", "layout-tavern.json", tavern),
    ("data/layout/weapons", "layout-weapons.json", weapons),
    ("data/layout/cave", "layout-cave.json", cave),
    ("data/layout/fleet-panel", "layout-fleet-panel.json", fleet_panel),
    ("data/layout/board", "layout-board.json", board),
]
