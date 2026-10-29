#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""The per-era tables (data/era/<year>): ship speeds, the ships met at
sea, the enemy captain's decisions, the politics and the Spanish
shipwrights' tolerance.

Every value comes from the model below, written for the open pack from
history and sailing physics; nothing comes from the original game.  The
six eras share the model and differ only by the parameters in ERAS.

How the engine reads them (src/sim/sim.c, battle.c, town.c):

- speed[class][sector]: the class is the ship type's low 3 bits (the
  slowest class in the fleet at sea), the sector the heading relative
  to the wind in 16ths, 0 running before it, 8 head to wind.  At sea the
  ship heads for (v + 0..2 by difficulty) x 3, clamped to 4..12, so 0 to
  4 is the useful span there; in battle v x 4 (x 1.5 under full sail)
  is the speed and a negative value drives the ship astern.
- encounter[region][k]: the region is the nation of the town nearest
  the sail (0 Spain, 1 England, 2 France, 3 Holland); k grows with the
  player's crew and the region's alert, so the five records run from
  the least to the most dangerous.  A record is type (0 pinnace, 1
  sloop, 2 barque, 3 fluyt, 4 merchantman, 5 frigate, shown as a fast
  galleon before 1640, 6 war galleon, 7 galleon), flag (0-3, 4
  pirate), crew in fours of men, guns, role, gold factor, goods factor.
  The role is the crew's fighting worth (x/8 in the captain's
  comparison, the sword fight's factor against the player's 7, the
  reload time 144 - 8 x role) and above 5 marks a warship or a raider.
  The gold is rand((town wealth + 64) / 16 x gold factor) x 10; the
  goods and the sugar each about goods factor / 6 tons, up to half of
  it.
- battle_ai[u]: u = 9 a + 3 b + c, where a, b, c (0-2) say how far the
  enemy is ahead in hull damage, guns and crew; 0 steers at the player,
  1 turns broadside under battle sails, 2 runs.
"""

# --- Sailing -----------------------------------------------------------

# A polar per rig, by sector 0 (running) .. 8 (head to wind), as a share
# of the ship's best speed.  A square rig drives hardest with the wind on
# the quarter, where every sail draws, and cannot point higher than
# about six points off the wind; a fore-and-aft rig is best on a beam
# reach, loses a little dead before the wind (the sails blanket each
# other) and still makes way four points off it.  Head to wind both are
# taken aback and drift astern.
SQUARE = [0.85, 0.95, 1.00, 0.95, 0.80, 0.55, 0.15, -0.20, -0.45]
FORE_AFT = [0.60, 0.75, 0.90, 1.00, 1.00, 0.90, 0.60, 0.05, -0.30]

# Per class: best speed and how fore-and-aft the rig is (0 square,
# 1 fore-and-aft).  Class 0 also carries the fast galleons, which the
# engine keeps as type 8 (8 & 7 = 0).
CLASSES = [
    (5, 0.6),   # 0 pinnace: light, lateen and sprit sails, sweeps
    (5, 1.0),   # 1 sloop: gaff main and headsails, the weatherliest
    (5, 0.4),   # 2 barque: small three-master, square fore, lateen mizzen
    (4, 0.2),   # 3 fluyt: long and lightly sparred, built to carry
    (4, 0.0),   # 4 merchantman: deep laden square rigger
    (6, 0.2),   # 5 frigate: fine run, square rigged, good staysails
    (3, 0.0),   # 6 war galleon: high castles, heavy guns
    (3, 0.0),   # 7 galleon: deep, broad, castle-built carrier
]


def polar(top, rig, upwind, sector):
    """The speed at a sector for a rig, rounded half away from zero.
    "upwind" is the era's share of square-rig weatherliness gained
    through staysails and better cut sails."""
    k = sector if sector <= 8 else 16 - sector
    r = rig + (1 - rig) * upwind
    f = SQUARE[k] * (1 - r) + FORE_AFT[k] * r
    v = top * f
    return int(v + 0.5) if v >= 0 else -int(-v + 0.5)


def speeds(era):
    table = []
    for c, (top, rig) in enumerate(CLASSES):
        top += era["top"].get(c, 0)
        table.append([polar(top, rig, era["upwind"], s)
                      for s in range(16)])
    return table


# --- Ships met at sea --------------------------------------------------

SPAIN, ENGLAND, FRANCE, HOLLAND, PIRATE = range(5)

# Archetypes: type, crew (fours of men), guns, role, gold, goods.  The
# gold rewards risk: raiders carry their takings, a galleon the King's
# silver; warships carry a pay chest and powder, traders their cargo.
SHIPS = {
    "courier":   (0, 6, 4, 4, 4, 20),      # dispatch pinnace
    "coaster":   (1, 6, 4, 3, 5, 50),      # coasting sloop
    "trader":    (2, 10, 8, 4, 8, 70),     # barque in the island trade
    "fluyt":     (3, 8, 6, 3, 10, 120),    # Dutch carrier, small crew
    "merchant":  (4, 16, 14, 4, 12, 100),  # armed merchantman
    "carrack":   (7, 40, 20, 5, 30, 150),  # trade galleon of the flota
    "corsair":   (2, 22, 12, 7, 18, 40),   # privateer under commission
    "raider":    (5, 30, 18, 8, 20, 30),   # race-built galleon / frigate
    "pirate":    (1, 16, 8, 8, 22, 30),   # pirate sloop
    "buccaneer": (2, 26, 12, 9, 25, 40),   # buccaneer's prize barque
    "frigate":   (5, 40, 24, 9, 8, 15),    # navy frigate
    "warship":   (6, 60, 32, 10, 10, 20),  # war galleon / ship of the line
}

# Per era and region (nation of the town near), the five sails from the
# least to the most dangerous; each list has a pirate so that a
# governor's pirate hunt finds one.
SEAS = {
    # The flota years: Spain alone holds the Main; Huguenot corsairs and
    # English slavers poach on it, the Sea Beggars are just rising.
    1560: [
        [("courier", SPAIN), ("merchant", SPAIN), ("corsair", FRANCE),
         ("carrack", SPAIN), ("warship", SPAIN)],
        [("trader", ENGLAND), ("coaster", SPAIN), ("pirate", PIRATE),
         ("raider", ENGLAND), ("warship", SPAIN)],
        [("trader", FRANCE), ("courier", FRANCE), ("corsair", FRANCE),
         ("pirate", PIRATE), ("carrack", SPAIN)],
        [("trader", HOLLAND), ("merchant", SPAIN), ("pirate", PIRATE),
         ("corsair", HOLLAND), ("warship", SPAIN)],
    ],
    # After the Armada war: Spanish silver at its height, English and
    # Dutch interlopers trading salt and hides, the Twelve Years' Truce.
    1600: [
        [("coaster", SPAIN), ("merchant", SPAIN), ("raider", ENGLAND),
         ("carrack", SPAIN), ("warship", SPAIN)],
        [("trader", ENGLAND), ("merchant", ENGLAND), ("pirate", PIRATE),
         ("raider", ENGLAND), ("warship", SPAIN)],
        [("trader", FRANCE), ("merchant", SPAIN), ("corsair", FRANCE),
         ("pirate", PIRATE), ("carrack", SPAIN)],
        [("fluyt", HOLLAND), ("trader", HOLLAND), ("pirate", PIRATE),
         ("corsair", HOLLAND), ("warship", SPAIN)],
    ],
    # The West India Company's war on Spain, the first English and
    # French islands; Dutch carriers everywhere.
    1620: [
        [("coaster", SPAIN), ("fluyt", HOLLAND), ("corsair", HOLLAND),
         ("carrack", SPAIN), ("warship", SPAIN)],
        [("trader", ENGLAND), ("fluyt", HOLLAND), ("pirate", PIRATE),
         ("merchant", ENGLAND), ("raider", HOLLAND)],
        [("trader", FRANCE), ("fluyt", HOLLAND), ("pirate", PIRATE),
         ("corsair", FRANCE), ("frigate", SPAIN)],
        [("fluyt", HOLLAND), ("merchant", HOLLAND), ("pirate", PIRATE),
         ("raider", HOLLAND), ("warship", SPAIN)],
    ],
    # Tortuga's buccaneers, sugar on Barbados, Spain stretched thin.
    1640: [
        [("coaster", SPAIN), ("merchant", SPAIN), ("buccaneer", PIRATE),
         ("carrack", SPAIN), ("warship", SPAIN)],
        [("trader", ENGLAND), ("fluyt", HOLLAND), ("pirate", PIRATE),
         ("merchant", ENGLAND), ("frigate", ENGLAND)],
        [("trader", FRANCE), ("fluyt", HOLLAND), ("buccaneer", PIRATE),
         ("corsair", FRANCE), ("frigate", FRANCE)],
        [("fluyt", HOLLAND), ("merchant", HOLLAND), ("pirate", PIRATE),
         ("raider", HOLLAND), ("frigate", HOLLAND)],
    ],
    # Port Royal's heyday: Anglo-Dutch wars, buccaneers sacking the Main,
    # navies sending frigates out.
    1660: [
        [("coaster", SPAIN), ("buccaneer", PIRATE), ("merchant", SPAIN),
         ("frigate", SPAIN), ("warship", SPAIN)],
        [("merchant", ENGLAND), ("pirate", PIRATE), ("corsair", ENGLAND),
         ("frigate", ENGLAND), ("warship", ENGLAND)],
        [("trader", FRANCE), ("buccaneer", PIRATE), ("corsair", FRANCE),
         ("frigate", FRANCE), ("warship", FRANCE)],
        [("fluyt", HOLLAND), ("pirate", PIRATE), ("merchant", HOLLAND),
         ("frigate", HOLLAND), ("warship", ENGLAND)],
    ],
    # Pirates cast out of the treaties, the great navies on station,
    # sugar and slave ships crowding the lanes.
    1680: [
        [("coaster", SPAIN), ("pirate", PIRATE), ("merchant", SPAIN),
         ("buccaneer", PIRATE), ("frigate", SPAIN)],
        [("merchant", ENGLAND), ("trader", ENGLAND), ("pirate", PIRATE),
         ("frigate", ENGLAND), ("warship", ENGLAND)],
        [("merchant", FRANCE), ("pirate", PIRATE), ("buccaneer", PIRATE),
         ("frigate", FRANCE), ("warship", FRANCE)],
        [("fluyt", HOLLAND), ("merchant", HOLLAND), ("pirate", PIRATE),
         ("frigate", HOLLAND), ("warship", HOLLAND)],
    ],
}

NAVY = ("frigate", "warship", "raider")


def record(name, flag, era):
    """An archetype as met in an era: navies grow, pirates grow bolder,
    Spanish silver swells and ebbs, the carrying trade fattens."""
    t, crew, guns, role, gold, goods = SHIPS[name]
    if name in NAVY:
        crew = round(crew * era["navy"])
        guns = round(guns * era["navy"])
    if flag == PIRATE or name in ("corsair", "buccaneer"):
        role += era["bold"]
    if flag == SPAIN:
        gold = round(gold * era["silver"])
    goods = round(goods * era["trade"])
    # A type 5 before 1640 is shown as a fast galleon, and an era's
    # guns never exceed what such a hull carries.
    guns = min(guns, (t + 2) * 4 + 4)
    crew = min(crew, (t + 2) * 8 + 8)
    return [t, flag, crew, guns, role, min(gold, 255), min(goods, 255)]


def encounters(year, era):
    return [[record(n, f, era) for n, f in region] for region in SEAS[year]]


def treasure_ship(era):
    """The treasure galleon: a deep galleon with soldiers aboard, the
    King's silver as its gold factor (it also sizes a convoy's sack)."""
    return [7, SPAIN, 55, 30, 7, era["treasure"], 60]


# --- The enemy captain -------------------------------------------------

def battle_ai(bold):
    """Per comparison u = 9 a + 3 b + c: the hull counts double, since a
    shattered ship cannot win whatever she carries.  A captain ahead
    with a stronger crew closes to board; ahead on guns he lies
    broadside; behind he runs.  "bold" lowers the bar."""
    out = []
    for u in range(27):
        a, b, c = u // 9, u // 3 % 3, u % 3
        strength = 2 * a + b + c + bold
        if a == 0 and strength < 6:
            d = 2                       # holed: save the ship
        elif strength >= 6 and c == 2:
            d = 0                       # close and board
        elif strength >= 4:
            d = 0 if c > b and a >= 1 else 1
        else:
            d = 2
        out.append(d)
    return out


# --- The eras ----------------------------------------------------------

ERAS = {
    # top: best speed change per class (the frigate, still a race-built
    # galleon before 1640, one less; the Bermuda and Jamaica sloops one
    # more from 1640; frigate-built merchantmen from 1660; the lighter,
    # two-decked ships of the line in 1680).
    # upwind: the square rigs' gain in weatherliness (staysails, better
    # cut canvas).
    # navy: the warships' crew and guns scale.
    # bold: the raiders' role bonus (pirates, corsairs, buccaneers).
    # nerve: the enemy captain's boldness, 0 or 1 (more would make even
    # a merchant stand and fight).
    # silver: the Spanish ships' gold scale; trade: the goods scale.
    # treasure: the treasure galleon's gold factor.
    # diplomacy: the range (larger, calmer); wright: the Spanish
    # shipwrights' tolerance, loosening as the monopoly weakens.
    1560: dict(top={5: -1}, upwind=0.0, navy=0.8, bold=0, nerve=0,
               silver=1.0, trade=0.8, treasure=70, diplomacy=110,
               wright=30,
               why="Huguenot raids and Hawkins' voyages under an uneasy "
               "peace; Spain's monopoly strict"),
    1600: dict(top={5: -1}, upwind=0.1, navy=0.85, bold=0, nerve=0,
               silver=1.2, trade=0.9, treasure=90, diplomacy=200,
               wright=40,
               why="the Peace of London and the Twelve Years' Truce: "
               "the calmest era; silver at its height"),
    1620: dict(top={}, upwind=0.2, navy=0.9, bold=1, nerve=0,
               silver=1.0, trade=1.0, treasure=80, diplomacy=90,
               wright=55,
               why="the Dutch war resumes and the Thirty Years' War "
               "spreads to the colonies"),
    1640: dict(top={1: 1}, upwind=0.3, navy=1.0, bold=1, nerve=1,
               silver=0.9, trade=1.1, treasure=65, diplomacy=75,
               wright=75,
               why="Portugal's revolt, the English civil war, Westphalia "
               "at the end; Spain stretched thin"),
    1660: dict(top={1: 1, 4: 1}, upwind=0.4, navy=1.1, bold=2, nerve=1,
               silver=0.8, trade=1.2, treasure=50, diplomacy=60,
               wright=95,
               why="the Anglo-Dutch and Franco-Dutch wars: the stormiest "
               "politics, buccaneers at their boldest"),
    1680: dict(top={1: 1, 4: 1, 6: 1}, upwind=0.5, navy=1.2, bold=1,
               nerve=0, silver=0.75, trade=1.3, treasure=45,
               diplomacy=120, wright=115,
               why="the truce after Nijmegen and Ratisbon before the "
               "Nine Years' War; navies hunt the pirates"),
}


def build(year):
    era = ERAS[year]
    value = {
        "speed": speeds(era),
        "encounter": encounters(year, era),
        "treasure_ship": treasure_ship(era),
        "battle_ai": battle_ai(era["nerve"]),
        "diplomacy_range": era["diplomacy"],
        "wright_tolerance": era["wright"],
    }
    notes = (
        "Designed for the open pack, from history and sailing physics; "
        "nothing from the original game.  Speeds: a square-rig and a "
        "fore-and-aft polar (best on the quarter and on the beam, "
        "astern head to wind) blended per class by its rig and scaled "
        "by its best speed, square rigs gaining weatherliness by era "
        "(%.1f here).  Ships met: per region five archetypes from the "
        "least to the most dangerous, chosen for the era's history (%s), "
        "navies scaled x%.2f, raiders' role +%d, Spanish gold x%.2f, "
        "goods x%.2f; the treasure galleon's gold factor %d.  Enemy "
        "captain: strength = 2 x hull + guns + crew comparison + "
        "nerve %d; he boards when ahead with the stronger crew, lies "
        "broadside when ahead, runs when behind or holed.  Diplomacy "
        "range %d, Spanish shipwrights' tolerance %d (loosening as "
        "Spain's monopoly weakens)." % (
            era["upwind"], era["why"], era["navy"], era["bold"],
            era["silver"], era["trade"], era["treasure"], era["nerve"],
            era["diplomacy"], era["wright"]))
    return value, notes


def _builder(year):
    return lambda: build(year)


build_1560 = _builder(1560)
build_1600 = _builder(1600)
build_1620 = _builder(1620)
build_1640 = _builder(1640)
build_1660 = _builder(1660)
build_1680 = _builder(1680)

TABLES = [("data/era/%d" % y, "era-%d.json" % y, b) for y, b in (
    (1560, build_1560), (1600, build_1600), (1620, build_1620),
    (1640, build_1640), (1660, build_1660), (1680, build_1680))]
