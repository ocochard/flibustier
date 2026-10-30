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
  4 is the useful span there; in battle v x 4 (x 1.5 under full sail,
  at most 40) is the speed, a fort battle halves it, and a negative
  value drives the ship astern.
- encounter[region][k]: the region is the nation of the town nearest
  the sail (0 Spain, 1 England, 2 France, 3 Holland); k grows with the
  player's crew and the region's alert, so the five records run from
  the least to the most dangerous.  Far from towns (over 12 cells) and
  one time in four near them, any of the twenty records comes up
  instead (see "Ships met at sea").  A record is type (0 pinnace, 1
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
# of the ship's best speed; a sector is two points (22.5 degrees), so
# sector 6 spans 34 to 56 degrees off the wind and sector 7 11 to 34.  A
# sector's value is the way made good over a watch on that heading,
# tacks and leeway included, so a rig that beats to windward makes some
# way even in sector 8: the engine has no tacking of its own.
#
# A square rig drives hardest with the wind on the quarter, where every
# sail draws, holds most of it on a broad reach, and falls off fast once
# the yards are braced hard round: six points off (sector 5) is about
# the best it can lie, so sector 6 is the little it makes by long,
# leeward-sagging boards, sector 7 next to nothing, and head to wind it
# is taken aback and drifts astern.  A fore-and-aft rig (gaff, sprit,
# lateen, with headsails) is best from a beam to a close reach, loses
# a third dead before the wind (the sails blanket each other, no
# studding sails), lies four points off the wind at most of its speed,
# and short tacks make real way into the wind's own sector.
SQUARE = [0.85, 1.00, 1.00, 0.90, 0.75, 0.55, 0.35, 0.05, -0.20]
FORE_AFT = [0.65, 0.80, 0.95, 1.00, 1.00, 0.95, 0.85, 0.55, 0.20]

# Per class: best speed and how fore-and-aft the rig is (0 square,
# 1 fore-and-aft).  Class 0 also carries the fast galleons, which the
# engine keeps as type 8 (8 & 7 = 0), hence its mixed rig.
#
# The scale: at sea the engine sails at (v + 0..2) x 3 clamped to 4..12,
# so from about 2 up a good sector makes full speed on the map and the
# classes differ by how near the wind they keep it; in battle v x 4
# (x 1.5 under full sail, capped at 40) is the speed, so 7 is a full
# sail's top.  The light fore-and-aft craft are the fastest, as the
# Jamaica and Bermuda sloops and the dispatch pinnaces were; the great
# galleons the slowest at five sevenths of a sloop's best and about a
# third of her way to windward, slow enough to be caught, fast enough
# to run down on a fort or run before the wind.
CLASSES = [
    (7, 0.6),   # 0 pinnace: light, lateen and sprit sails, sweeps
    (7, 1.0),   # 1 sloop: gaff main and headsails, the weatherliest
    (6, 0.5),   # 2 barque: small three-master, square fore, lateen mizzen
    (6, 0.2),   # 3 fluyt: long and lightly sparred, built to carry
    (5, 0.0),   # 4 merchantman: deep laden square rigger
    (7, 0.25),  # 5 frigate: fine run, square rigged, good staysails
    (5, 0.0),   # 6 war galleon: high castles, heavy guns
    (5, 0.0),   # 7 galleon: deep, broad, castle-built carrier
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
#
# How the engine draws them (src/sim/sim.c, pick_ship): within 12 cells
# of a town, eleven times in fifteen, the record is the region's k-th,
# k = rand(crew / 50 + alert / 6 - the town's goodwill) / 3, mostly 0
# or 1 unless sails left alone have alerted the coast or the player has
# wronged the town; otherwise, and always farther out, it is any of the
# era's twenty records, all equally likely.  On the open map most
# sails are met far from a town, so an era's sea traffic is the census
# of its twenty records: one record is a twentieth of the ships met in
# open water.  The first record of a region is the commonest ship
# off its towns, the next ones those a hunted coast sends out.

SPAIN, ENGLAND, FRANCE, HOLLAND, PIRATE = range(5)

# Archetypes: type, crew (fours of men), guns, role, gold, goods.  The
# gold rewards risk: raiders carry their takings, a galleon the King's
# silver; warships carry a pay chest and powder, traders their cargo.
#
# The role is both the gunnery (the reload) and the crew's worth.  A
# navy's drilled gun crews set the top of it: a frigate 9; a war galleon
# or ship of the line 8, her heavy guns slower to serve than a
# frigate's, her worth aboard in her crowd of soldiers (at 10 she fired
# her 30-odd guns every 64 ticks and, with the frigates, sank most of
# the career bot's ships).  Rovers and privateers fought by boarding,
# with crowded decks and few, ill-served guns: their strength is their
# numbers (the crew) and the era's boldness (+0..2), not their gunnery,
# so their base is 6-8 and at their boldest they are a frigate's equal,
# the buccaneers a little more.  At 7-9 base (up to 11 with the
# boldness), the pirate sloops outgunned frigates, sank the career
# bot's ships twice as often as the game intends, and seldom struck
# their colours.
#
# Traders sailed lean, their owners paying every hand: a coasting
# balandra or an aviso a dozen or two men, an island barque two dozen,
# a Dutch fluyt famously a dozen to twenty, an armed merchantman of
# 200-300 tons thirty to fifty, a great nao of the flota a hundred and
# more with her passengers.  The crews below are those.  Manned half
# again as heavily, the traders swelled the career bot's crews past 300
# men through the volunteers a capture brings (a share of the taken
# crew), beyond any town's garrison, and stood to fight it more often.
SHIPS = {
    "courier":    (0, 4, 4, 4, 4, 20),      # aviso, dispatch pinnace
    "coaster":    (1, 4, 4, 3, 5, 50),      # coasting sloop or balandra
    "trader":     (2, 6, 8, 4, 8, 70),      # barque in the island trade
    "fluyt":      (3, 5, 6, 3, 10, 120),    # Dutch carrier, small crew
    "merchant":   (4, 10, 14, 4, 12, 100),  # armed merchantman
    "carrack":    (7, 30, 20, 5, 30, 150),  # trade galleon of the flota
    "privateer":  (1, 18, 8, 6, 15, 35),    # sloop under letters of marque
    "corsair":    (2, 22, 12, 6, 18, 40),   # privateer barque, commissioned
    "flibustier": (2, 26, 12, 8, 25, 40),   # buccaneers' barque
    "raider":     (5, 30, 18, 7, 20, 30),   # race-built galleon / frigate
    "pirate":     (1, 16, 8, 7, 22, 30),    # pirate sloop
    "rover":      (2, 22, 10, 7, 24, 35),   # pirate barque or brigantine
    "freebooter": (5, 34, 20, 8, 28, 40),   # pirate in a taken frigate
    "frigate":    (5, 40, 24, 9, 8, 15),    # navy frigate
    "warship":    (6, 60, 32, 8, 10, 20),  # war galleon / ship of the line
}

# The raiders whose daring grows with the era's "bold".
RAIDERS = ("privateer", "corsair", "flibustier")

# Per era and region (nation of the town near), the five sails from the
# least to the most dangerous.  Each region has exactly one pirate, so
# that a governor's pirate hunt finds one and pirates are a fifth of
# the open sea's sails, never more; the other sixteen records share the
# sea by flag as history had it (the census under each era), and the
# pirates' hulls vary so that no era is all sloops.  A "flibustier"
# under the French flag is a buccaneer with a commission from Tortuga
# or Saint-Domingue, as most of them carried; under no flag, a pirate.
SEAS = {
    # The flota years (Spain 10, France 3, England 2, Holland 1): Spain
    # alone holds the Main; Huguenot corsairs raid it every year and are
    # the commonest sail off French landfalls, Hawkins' slavers come to
    # trade by force, the Sea Beggars barely reach the Indies.
    1560: [
        [("courier", SPAIN), ("merchant", SPAIN), ("rover", PIRATE),
         ("carrack", SPAIN), ("warship", SPAIN)],
        [("trader", ENGLAND), ("coaster", SPAIN), ("pirate", PIRATE),
         ("raider", ENGLAND), ("warship", SPAIN)],
        [("corsair", FRANCE), ("trader", FRANCE), ("rover", PIRATE),
         ("merchant", SPAIN), ("carrack", SPAIN)],
        [("trader", HOLLAND), ("coaster", SPAIN), ("pirate", PIRATE),
         ("corsair", FRANCE), ("warship", SPAIN)],
    ],
    # After the Armada war (Spain 8, England 3, Holland 3, France 2):
    # Spanish silver at its height; English privateers and interlopers,
    # Dutch salt fleets at Araya, French traders in hides and tobacco.
    1600: [
        [("coaster", SPAIN), ("merchant", SPAIN), ("pirate", PIRATE),
         ("carrack", SPAIN), ("warship", SPAIN)],
        [("trader", ENGLAND), ("merchant", SPAIN), ("rover", PIRATE),
         ("privateer", ENGLAND), ("raider", ENGLAND)],
        [("trader", FRANCE), ("corsair", FRANCE), ("pirate", PIRATE),
         ("merchant", SPAIN), ("carrack", SPAIN)],
        [("fluyt", HOLLAND), ("trader", HOLLAND), ("rover", PIRATE),
         ("corsair", HOLLAND), ("warship", SPAIN)],
    ],
    # The West India Company's war on Spain (Spain 7, Holland 5,
    # England 2, France 2): Dutch carriers and raiders everywhere, the
    # first English and French settlers on St Christopher.
    1620: [
        [("coaster", SPAIN), ("fluyt", HOLLAND), ("pirate", PIRATE),
         ("carrack", SPAIN), ("warship", SPAIN)],
        [("trader", ENGLAND), ("merchant", SPAIN), ("rover", PIRATE),
         ("privateer", ENGLAND), ("raider", HOLLAND)],
        [("trader", FRANCE), ("merchant", SPAIN), ("rover", PIRATE),
         ("corsair", FRANCE), ("frigate", SPAIN)],
        [("fluyt", HOLLAND), ("merchant", HOLLAND), ("pirate", PIRATE),
         ("raider", HOLLAND), ("warship", SPAIN)],
    ],
    # Tortuga and the Lesser Antilles (Spain 6, Holland 4, England 3,
    # France 3): France at war with Spain from 1635, its flibustiers
    # the commonest sail off French coasts; Barbados sugar in Dutch
    # hulls; Spain stretched thin.
    1640: [
        [("coaster", SPAIN), ("merchant", SPAIN), ("flibustier", PIRATE),
         ("carrack", SPAIN), ("warship", SPAIN)],
        [("merchant", ENGLAND), ("fluyt", HOLLAND), ("pirate", PIRATE),
         ("privateer", ENGLAND), ("frigate", ENGLAND)],
        [("flibustier", FRANCE), ("trader", FRANCE), ("rover", PIRATE),
         ("merchant", SPAIN), ("frigate", FRANCE)],
        [("fluyt", HOLLAND), ("merchant", HOLLAND), ("pirate", PIRATE),
         ("raider", HOLLAND), ("frigate", SPAIN)],
    ],
    # Port Royal and Tortuga at their height (Spain 5, England 4,
    # France 4, Holland 3): Jamaica's privateers and the flibustiers of
    # Saint-Domingue sack the Main, the West India Company of 1664
    # brings French merchantmen, navies send frigates out.
    1660: [
        [("coaster", SPAIN), ("merchant", SPAIN), ("flibustier", PIRATE),
         ("frigate", SPAIN), ("warship", SPAIN)],
        [("merchant", ENGLAND), ("privateer", ENGLAND), ("pirate", PIRATE),
         ("flibustier", FRANCE), ("frigate", ENGLAND)],
        [("flibustier", FRANCE), ("merchant", FRANCE), ("rover", PIRATE),
         ("frigate", FRANCE), ("warship", ENGLAND)],
        [("fluyt", HOLLAND), ("merchant", HOLLAND), ("rover", PIRATE),
         ("carrack", SPAIN), ("frigate", HOLLAND)],
    ],
    # Sugar and the great navies (England 5, France 5, Spain 4,
    # Holland 2): French squadrons and Saint-Domingue's flibustiers,
    # English sloop traders and frigates on station, pirates cast out of
    # the treaties and taking bigger ships.
    1680: [
        [("coaster", SPAIN), ("merchant", SPAIN), ("flibustier", FRANCE),
         ("freebooter", PIRATE), ("frigate", SPAIN)],
        [("merchant", ENGLAND), ("coaster", ENGLAND), ("pirate", PIRATE),
         ("frigate", ENGLAND), ("warship", ENGLAND)],
        [("merchant", FRANCE), ("flibustier", FRANCE), ("rover", PIRATE),
         ("frigate", FRANCE), ("warship", FRANCE)],
        [("fluyt", HOLLAND), ("merchant", ENGLAND), ("pirate", PIRATE),
         ("carrack", SPAIN), ("frigate", HOLLAND)],
    ],
}

NAVY = ("frigate", "warship", "raider")


def census(year):
    """Records per flag in an era: its share of the open sea's sails."""
    out = [0] * 5
    for region in SEAS[year]:
        for _, flag in region:
            out[flag] += 1
    return out


def record(name, flag, era):
    """An archetype as met in an era: navies grow, pirates grow bolder,
    Spanish silver swells and ebbs, the carrying trade fattens."""
    t, crew, guns, role, gold, goods = SHIPS[name]
    if name in NAVY:
        crew = round(crew * era["navy"])
        guns = round(guns * era["navy"])
    if flag == PIRATE or name in RAIDERS:
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
    for region in SEAS[year]:
        assert len(region) == 5
        assert sum(flag == PIRATE for _, flag in region) == 1
    return [[record(n, f, era) for n, f in region] for region in SEAS[year]]


def treasure_ship(era):
    """The treasure galleon: a deep galleon with soldiers aboard, the
    King's silver as its gold factor (it also sizes a convoy's sack)."""
    return [7, SPAIN, 55, 30, 7, era["treasure"], 60]


# --- The enemy captain -------------------------------------------------

def battle_ai(bold):
    """Per comparison u = 9 a + 3 b + c.  A captain's courage is in his
    men and his guns: holed (a = 0) he runs to save the ship; else he
    stands to fight only when ahead in crew or guns (b + c + "bold" of
    2 or more), closing to board when the crew is his strength and
    lying broadside when the guns are; ahead in neither he runs, however
    the hulls compare.  A merchant master's duty was to his owners'
    cargo and his sailors had no share in a fight's spoils, so a trader
    outmanned and outgunned put on sail whatever the state of the
    attacker's hull.  An earlier rule counted the hull double and let a
    lead in it alone hold a trader to fight: any damage the attacker
    carried from an earlier fight kept traders from running, so they
    were boarded where they lay (two captures in three), seldom fled
    (16 escapes on eight careers, 64 wanted) and seldom struck (231
    surrenders, 350 wanted); with this rule the same careers see about
    50 escapes and 300 to 400 surrenders."""
    out = []
    for u in range(27):
        a, b, c = u // 9, u // 3 % 3, u % 3
        strength = b + c + bold
        if a == 0:
            d = 2                       # holed: save the ship
        elif c == 2 and strength >= 3:
            d = 0                       # far more men: close and board
        elif strength >= 2:
            d = 0 if c > b else 1       # board with men, fight with guns
        else:
            d = 2                       # outmanned and outgunned: run
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
        "nothing from the original game.  Speeds: a square-rig polar "
        "(best on the quarter, lying six points off the wind, taken "
        "aback head to wind) and a fore-and-aft one (best from the beam "
        "to a close reach, making some way into the wind by short "
        "tacks) blended per class by its rig and scaled by its best "
        "speed, from 7 for the sloops and pinnaces (8 for the Jamaica "
        "sloops from 1640) to 5 for the galleons, so the fastest class "
        "outsails the slowest by about half again off the wind and two "
        "to four times close-hauled; square rigs gain weatherliness by "
        "era (%.1f here).  Ships met: per region five archetypes from "
        "the least to the most dangerous, chosen for the era's history "
        "(%s); since most sails are met far from a town, where all "
        "twenty records are equally likely, the era's sea traffic is "
        "their census by flag (Spain, England, France, Holland, "
        "pirates: %s), one pirate per region; navies scaled x%.2f, "
        "raiders' role +%d, Spanish gold x%.2f, goods x%.2f; the "
        "treasure galleon's gold factor %d.  Enemy captain: holed, he "
        "runs; else strength = guns + crew comparison + nerve %d, and "
        "at 2 or more he fights, boarding when ahead in men, lying "
        "broadside when ahead in guns; below it he runs whatever the "
        "hulls.  Diplomacy range %d, Spanish "
        "shipwrights' tolerance %d (loosening as Spain's monopoly "
        "weakens)." % (
            era["upwind"], era["why"],
            ", ".join(str(n) for n in census(year)), era["navy"], era["bold"],
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
