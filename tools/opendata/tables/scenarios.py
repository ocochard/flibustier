#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""data/scenarios: the 30 start scenarios, careers and famous expeditions.

Written for the open pack from real history; nothing comes from the
original game.  The order and fields are asset-packs.md's ("Open game
data", data/scenarios): 24 careers, era by era (1560 ... 1680), each
English, French, Dutch, Spanish; then the 6 famous expeditions in the
order of the open text's expedition menu, in the eras the engine fixes
for them (expedition_era in src/ui/screens.c: 1560, 1560, 1620, 1660,
1660, 1680).

Relations.  Per era, the four nations' state in the Caribbean during the
era's first decade, the one a career mostly plays: "no peace beyond the
line" meant the Spanish treated intruders as enemies whatever Europe's
treaties said, so the war that defined the decade there wins over a
peace that held only in Europe.  An expedition takes the relations of
its own year.  -1 war, 0 peace, 1 alliance; symmetric.

Careers.  The player starts in a home port of his nation in the era,
one of towns.py's towns of that nation, chosen to fit the career's
story (stories.py) and its history.  The four start cells are that
town's own arrival cells within a cell of it, the most open water
first: the engine (sim_new_game, start_town in src/sim/sim.c) puts the
player in the town owning the arrival cell nearest the drawn start cell
when that town is within 4 cells of it, then on the water outside its
harbour.  A career starts with one small ship, a few dozen men, a few
guns, a modest purse and food for some months; the hold is the ship's
(src/sim/internal.h, sim_ship_room: type x 2 + 2 tens of tons).  The
player's standing is 1 with his own nation, rank 1 (Ensign) there; with
the others, their relation to his nation (-1 at war, 0 at peace, 1
allied), rank 0; pirates -1.

Expeditions.  The real voyage's force, cut to the engine's eight ships
(the largest ones, or the eight a captain sailed in with), its men and
guns in proportion; the date (years past the era's first x 360 + day of
a 30-day-month year) and the months already at sea since the fleet's
last port of call, when its Caribbean part began; the start at sea
where it did, at least five cells from every town of the era, so the
engine leaves the fleet on the open water (start_town finds no town).
"""
import _geo
import towns

SPAIN, ENGLAND, FRANCE, HOLLAND = 0, 1, 2, 3
NATION_NAMES = ("spain", "england", "france", "holland")
CELL = _geo.CELL
FULL_SEA = CELL * CELL
TOWN_REACH = 4          # src/sim/sim.c START_CELLS
EXPEDITION_CLEAR = 5    # cells from any town, for a start at sea
EXPEDITION_ERAS = (1560, 1560, 1620, 1660, 1660, 1680)

# Ship types, in CSTN 8 order.
PINNACE, SLOOP, BARQUE, FLUYT, MERCHANTMAN, FRIGATE, WAR_GALLEON, \
    GALLEON, FAST_GALLEON = range(9)

# --- Relations per era -----------------------------------------------
#
# The pairs at war (-1) or allied (1); every other pair is at peace.
RELATIONS = {
    # Cateau-Cambresis (1559) left "no peace beyond the line": Menendez
    # massacres the Huguenots of Fort Caroline (1565), Gourgues avenges
    # them (1568), French corsairs haunt Cuba and Hispaniola.  The Dutch
    # rise against Spain (1568) and the Sea Beggars take to the sea.
    # England is formally at peace with Spain (Hawkins' trading voyages,
    # 1562-68), and with France after Troyes (1564).
    1560: {(SPAIN, FRANCE): -1, (SPAIN, HOLLAND): -1},
    # The Anglo-Spanish war (1585-1604) and the Dutch revolt; England
    # and the States General allied since Nonsuch (1585).  France made
    # her peace with Spain at Vervins (1598).
    1600: {(SPAIN, ENGLAND): -1, (SPAIN, HOLLAND): -1,
           (ENGLAND, HOLLAND): 1},
    # The Twelve Years' Truce ends (1621) and the West India Company
    # goes to war; England fights Spain again (1625-30, Cadiz, Toledo's
    # fleet razes St Christopher and Nevis in 1629), allied with the
    # Dutch by the treaty of Southampton (1625).
    1620: {(SPAIN, ENGLAND): -1, (SPAIN, HOLLAND): -1,
           (ENGLAND, HOLLAND): 1},
    # France at war with Spain (1635-59) beside the Dutch (alliance of
    # 1635), the Dutch war with Spain until Munster (1648); England,
    # torn by her civil war, at peace with all.
    1640: {(SPAIN, FRANCE): -1, (SPAIN, HOLLAND): -1,
           (FRANCE, HOLLAND): 1},
    # Cromwell's Western Design took Jamaica (1655); in the Indies the
    # war goes on until the treaty of Madrid (1670): Myngs, Mansvelt,
    # Morgan.  The Second Anglo-Dutch war (Holmes 1664, 1665-67), France
    # bound to the Dutch by the alliance of 1662 and at war with England
    # beside them (1666-67: the French take English St Christopher,
    # Antigua and Montserrat, Harman burns their fleet off Martinique).
    # Spain at peace with France (the Pyrenees, 1659) and Holland
    # (Munster).
    1660: {(SPAIN, ENGLAND): -1, (ENGLAND, HOLLAND): -1,
           (ENGLAND, FRANCE): -1, (FRANCE, HOLLAND): 1},
    # Louis XIV against Spain (the Reunions 1683-84, the Nine Years'
    # War from 1689): Grammont and de Graaf sack Veracruz (1683) and
    # Campeche (1685).  The Dutch, Spain's allies since 1673, allied
    # with England since 1678.  England keeps the treaty of Madrid and
    # hangs its buccaneers.
    1680: {(SPAIN, FRANCE): -1, (SPAIN, HOLLAND): 1,
           (ENGLAND, HOLLAND): 1},
}

# --- Careers ----------------------------------------------------------
#
# Per era, England, France, Holland, Spain: (name, home port (towns.py
# name), ship, crew, guns, gold, food, goods, sugar), with the story
# (stories.py) and the history.
CAREERS = {
    1560: [
        # A sea dog's son.  Hawkins and Drake's generation; the English
        # rovers watered at the Isla de Pinos off Cuba.
        ("Elizabethan Sea Dog", "Isla de Pinos", PINNACE,
         40, 6, 400, 12, 0, 0),
        # The rebel of a failing colony: Laudonniere's Huguenots at Fort
        # Caroline (1564-65), who stole ships to go raiding.
        ("Huguenot Corsair", "Fort Caroline", BARQUE,
         45, 8, 300, 20, 0, 0),
        # The Sea Beggars (Watergeuzen), commissioned by Orange in 1568;
        # Curacao as a Dutch harbour is a stretch (towns.py).
        ("Sea Beggar", "Curacao", PINNACE,
         35, 6, 250, 12, 0, 0),
        # A man of rank backed at court, sailing from the Audiencia's
        # seat in the years of Menendez de Aviles' Indies fleets.
        ("Hidalgo Adventurer", "Santo Domingo", BARQUE,
         40, 8, 600, 20, 0, 0),
    ],
    1600: [
        # A privateer of the war's last years, from the English attempt
        # on St Lucia (1605).
        ("Queen's Privateer", "St Lucia", SLOOP,
         45, 8, 500, 20, 0, 0),
        # A wrecked emigrant turned smuggler: the Norman and Breton
        # interlopers trading hides and tobacco with the islands.
        ("Dieppe Interloper", "Martinique", SLOOP,
         35, 6, 600, 20, 5, 0),
        # A ship owner shut out by the Spanish monopoly: the Dutch salt
        # fleets at Punta de Araya (1599-1605), in a fluyt.
        ("Salt Fleet Skipper", "Punta de Araya", FLUYT,
         30, 6, 400, 20, 10, 0),
        # A merchant's son backed at court, an officer of the fleets
        # that meet at Havana.
        ("Indies Fleet Ensign", "Havana", SLOOP,
         45, 8, 500, 20, 0, 0),
    ],
    1620: [
        # An indentured servant who slips away to the buccaneers, from
        # Warner's colony on St Christopher (1624).
        ("Runaway Servant", "St Christopher", PINNACE,
         35, 4, 200, 12, 0, 0),
        # A dispossessed noble turned privateer: d'Esnambuc's
        # gentlemen of the islands (1625-35).
        ("Gentleman Adventurer", "Guadeloupe", SLOOP,
         40, 6, 500, 20, 0, 0),
        # A captain of the new West India Company (1621), from the
        # Zeeland colony of Nieuw Walcheren on Tobago (1628).
        ("Company Skipper", "Nieuw Walcheren", SLOOP,
         40, 8, 600, 20, 0, 0),
        # A coast guard captain of the armadilla that patrolled the
        # Main from Cartagena.
        ("Armadilla Captain", "Cartagena", BARQUE,
         50, 10, 500, 20, 0, 0),
    ],
    1640: [
        # A convict transported to the cane fields of Barbados, the
        # sugar revolution of the 1640s.
        ("Barbados Convict", "Barbados", PINNACE,
         35, 4, 200, 12, 0, 0),
        # Raised among the boucaniers of Tortuga, Le Vasseur's fort
        # (1640).
        ("Tortuga Boucanier", "Tortuga", SLOOP,
         45, 6, 250, 25, 0, 0),
        # A sailor's son in the carrying trade the Dutch ruled, from
        # Curacao (Dutch from 1634).
        ("Zeeland Free Trader", "Curacao", FLUYT,
         30, 6, 800, 20, 10, 0),
        # A fortune seeker after a lost silver galleon, from San Juan,
        # in the years of the Armada de Barlovento (1641).
        ("Hidalgo of Fortune", "San Juan", BARQUE,
         45, 8, 600, 20, 0, 0),
    ],
    1660: [
        # A buccaneer with a letter of marque from Port Royal, Myngs'
        # and Morgan's town.
        ("Port Royal Buccaneer", "Port Royal", SLOOP,
         50, 8, 300, 20, 0, 0),
        # An engage who ran to the flibustiers of Petit-Goave, d'Ogeron's
        # coast of Saint-Domingue (1665).
        ("Coast Flibustier", "Petit-Goave", BARQUE,
         50, 8, 250, 20, 0, 0),
        # A ship owner shut out by monopolies, trading from the free
        # port of St Eustatius.
        ("Statia Merchant", "St Eustatius", FLUYT,
         30, 6, 900, 20, 10, 0),
        # A seaman's son turned corsair against the heretics, from
        # Santiago de Cuba (sacked by Myngs, 1662).
        ("Santiago Corsair", "Santiago de Cuba", SLOOP,
         50, 10, 400, 20, 0, 0),
    ],
    1680: [
        # A convict transported to Jamaica who escapes to sea, in the
        # years Port Royal hangs its pirates.
        ("Port Royal Runaway", "Port Royal", PINNACE,
         35, 4, 200, 12, 0, 0),
        # A Huguenot exile of the Revocation (1685), at Cap-Francois
        # (1670).
        ("Huguenot Exile", "Cap-Francois", SLOOP,
         40, 6, 300, 20, 0, 0),
        # A merchant backed at court, interloping from Curacao, the
        # Dutch entrepot of the Spanish trade.
        ("Curacao Interloper", "Curacao", SLOOP,
         40, 8, 900, 20, 8, 0),
        # A guarda costa captain, the Biscayan privateers Spain licensed
        # against the smugglers, from Havana.
        ("Biscayan Guardacosta", "Havana", BARQUE,
         60, 10, 600, 20, 0, 0),
    ],
}

CAREER_NATIONS = (ENGLAND, FRANCE, HOLLAND, SPAIN)

# --- Famous expeditions -------------------------------------------------
#
# name (the captain, as the open text names him), nation, rank (CSTN
# 78), standing with Spain, the fleet's ships, crew, guns, gold, food,
# goods, sugar, (year, month 1-12, day), months at sea, relations
# (wars and alliances of the year), and the start (longitude, latitude).
EXPEDITIONS = [
    # John Hawkins' third slaving and trading voyage: the Jesus of
    # Lubeck, the Minion, the William and John, the Swallow, the Judith
    # and the Angel, some 400 men; out of Plymouth in October 1567, the
    # last African port in February 1568, landfall at Dominica on 27
    # March 1568, then the forced trade at Margarita, Borburata and Rio
    # de la Hacha, before San Juan de Ulua.  Spain at peace with England,
    # wary of his trade.
    {"name": "John Hawkins", "nation": ENGLAND, "rank": 2, "spain": 0,
     "ships": [GALLEON, MERCHANTMAN, MERCHANTMAN, BARQUE, BARQUE,
               PINNACE],
     "crew": 370, "guns": 60, "gold": 1500, "food": 60, "goods": 100,
     "sugar": 0, "date": (1568, 3, 27), "months": 2,
     "relations": RELATIONS[1560], "start": (-61.15, 15.30)},
    # Francis Drake's Nombre de Dios raid: the Pascha (70 tons) and the
    # Swan (25), 73 men, three pinnaces stowed in pieces; out of
    # Plymouth on 24 May 1572, landfall at Guadeloupe on 29 June, then
    # Port Pheasant.  England had bound herself to France at Blois
    # (April 1572); Drake would raid with Le Testu's Huguenots.
    {"name": "Francis Drake", "nation": ENGLAND, "rank": 1, "spain": -2,
     "ships": [BARQUE, PINNACE], "crew": 73, "guns": 10, "gold": 200,
     "food": 30, "goods": 0, "sugar": 0, "date": (1572, 6, 29),
     "months": 1,
     "relations": {(SPAIN, FRANCE): -1, (SPAIN, HOLLAND): -1,
                   (ENGLAND, FRANCE): 1},
     "start": (-61.95, 16.10)},
    # Piet Heyn's West India Company fleet: 31 ships, some 700 guns and
    # 4000 men out of the Netherlands in the spring of 1628, waiting off
    # Cabo San Antonio for the flota in August, taking it in Matanzas
    # Bay on 8 September.  The eight largest: two great ships, four
    # warships, two yachts, and their share of the men and guns.
    # England at war with Spain and France (La Rochelle), allied with
    # the Dutch.
    {"name": "Piet Heyn", "nation": HOLLAND, "rank": 5, "spain": -2,
     "ships": [WAR_GALLEON, WAR_GALLEON, FRIGATE, FRIGATE, FRIGATE,
               FRIGATE, SLOOP, SLOOP],
     "crew": 1000, "guns": 180, "gold": 2000, "food": 200, "goods": 0,
     "sugar": 0, "date": (1628, 8, 1), "months": 3,
     "relations": {(SPAIN, HOLLAND): -1, (SPAIN, ENGLAND): -1,
                   (ENGLAND, FRANCE): -1, (ENGLAND, HOLLAND): 1},
     "start": (-85.20, 21.70)},
    # Francois L'Olonnais and Michel le Basque: eight ships and 660 men
    # out of Tortuga in 1666 (the flagship of 10 guns), prizes off
    # Hispaniola's eastern point, then Maracaibo and Gibraltar.  France
    # and the Dutch at war with England (1666); in the Indies England
    # still at war with Spain.
    {"name": "L'Olonnais", "nation": FRANCE, "rank": 0, "spain": -3,
     "ships": [BARQUE, SLOOP, SLOOP, SLOOP, SLOOP, PINNACE, PINNACE,
               PINNACE],
     "crew": 660, "guns": 40, "gold": 100, "food": 80, "goods": 0,
     "sugar": 0, "date": (1666, 4, 30), "months": 0,
     "relations": {(SPAIN, ENGLAND): -1, (ENGLAND, FRANCE): -1,
                   (ENGLAND, HOLLAND): -1, (FRANCE, HOLLAND): 1},
     "start": (-72.75, 20.35)},
    # Henry Morgan, admiral of Jamaica's privateers under Modyford's
    # commission: out of Port Royal in August 1670, 36 ships and some
    # 1800 men gathered at the Ile-a-Vache, sailing on 16 December for
    # Providence, Chagres and Panama; the Satisfaction (22 guns) his
    # flagship.  The treaty of Madrid (July 1670) not yet known; the
    # Triple Alliance (1668) binds England and the Dutch.
    {"name": "Henry Morgan", "nation": ENGLAND, "rank": 5, "spain": -2,
     "ships": [FRIGATE, MERCHANTMAN, BARQUE, BARQUE, SLOOP, SLOOP,
               SLOOP, SLOOP],
     "crew": 1200, "guns": 100, "gold": 300, "food": 120, "goods": 0,
     "sugar": 0, "date": (1670, 12, 16), "months": 4,
     "relations": {(SPAIN, ENGLAND): -1, (ENGLAND, HOLLAND): 1},
     "start": (-73.75, 17.95)},
    # The Baron de Pointis: the King's squadron out of Brest in January
    # 1697 (the Sceptre of 84 guns, six more of the line, frigates),
    # at Saint-Domingue in March, where Du Casse's buccaneers joined;
    # Cartagena fell on 3 May.  The Nine Years' War: France against
    # the Grand Alliance of Spain, England and the Dutch.
    {"name": "De Pointis", "nation": FRANCE, "rank": 6, "spain": -2,
     "ships": [WAR_GALLEON, WAR_GALLEON, WAR_GALLEON, FRIGATE, FRIGATE,
               FRIGATE, SLOOP, SLOOP],
     "crew": 1500, "guns": 250, "gold": 2000, "food": 250, "goods": 0,
     "sugar": 0, "date": (1697, 3, 1), "months": 2,
     "relations": {(SPAIN, FRANCE): -1, (ENGLAND, FRANCE): -1,
                   (FRANCE, HOLLAND): -1, (SPAIN, ENGLAND): 1,
                   (SPAIN, HOLLAND): 1, (ENGLAND, HOLLAND): 1},
     "start": (-71.70, 20.20)},
]

DAYS_PER_MONTH, DAYS_PER_YEAR = 30, 360
FOOD_FRAC_AT_SEA = 128  # half a ton of the current one eaten


def matrix(pairs):
    m = [[0] * 4 for _ in range(4)]
    for (a, b), v in pairs.items():
        m[a][b] = m[b][a] = v
    return m


def room(ships):
    return sum(t * 2 + 2 for t in ships)


def start_town(year, x, y):
    """The engine's start town for a start cell, or None (start_town in
    src/sim/sim.c: the town of the nearest arrival cell, by |dx| + |dy|,
    the first on a tie, if within TOWN_REACH cells)."""
    recs = towns._era(year)["records"]
    best, bestd = None, None
    for r in recs:
        for cx, cy in r["arrival"]:
            d = abs(cx - x) + abs(cy - y)
            if best is None or d < bestd:
                best, bestd = r, d
    if (abs(best["x"] - x) > TOWN_REACH or
            abs(best["y"] - y) > TOWN_REACH):
        return None
    return best


def career_starts(year, town):
    """Four of the town's arrival cells within a cell of it, the most
    open water first."""
    e = towns._era(year)
    rec = [r for r in e["records"] if r["name"] == town]
    assert len(rec) == 1, (year, town)
    rec = rec[0]
    water = e["water"]
    cells = [(cx, cy) for cx, cy in rec["arrival"]
             if abs(cx - rec["x"]) <= 1 and abs(cy - rec["y"]) <= 1]
    cells.sort(key=lambda c: (-int(water[c[1], c[0]]),
                              abs(c[0] - rec["x"]) + abs(c[1] - rec["y"]),
                              c[1], c[0]))
    assert len(cells) >= 4, (year, town)
    return rec, [list(c) for c in cells[:4]]


def expedition_starts(year, lon, lat):
    """Four open-sea cells near the place, every one at least
    EXPEDITION_CLEAR cells from the era's towns and two from each
    other, the nearest first."""
    e = towns._era(year)
    water = e["water"]
    px, py = _geo.lonlat_to_world(lon, lat)
    tx, ty = int(px // CELL), int(py // CELL)
    recs = e["records"]
    cand = []
    for r in range(0, 40):
        for y in range(ty - r, ty + r + 1):
            for x in range(tx - r, tx + r + 1):
                if max(abs(x - tx), abs(y - ty)) != r:
                    continue
                if not (0 <= x < towns.W and 0 <= y < towns.H):
                    continue
                if water[y, x] != FULL_SEA:
                    continue
                if any(max(abs(t["x"] - x), abs(t["y"] - y)) <
                       EXPEDITION_CLEAR for t in recs):
                    continue
                cand.append((abs(x - tx) + abs(y - ty), y, x))
        if len(cand) >= 40:
            break
    cand.sort()
    out = []
    for _, y, x in cand:
        if all(max(abs(x - a), abs(y - b)) >= 2 for a, b in out):
            out.append((x, y))
        if len(out) == 4:
            break
    assert len(out) == 4, (lon, lat)
    for x, y in out:
        assert start_town(year, x, y) is None, (x, y)
    return [list(c) for c in out]


def scenario(name, nation, rel, standing, rank, ships, crew, guns, gold,
             food, food_frac, goods, sugar, start, date, months):
    assert len(name.encode("utf-8")) <= 20, name
    assert 1 <= len(ships) <= 8
    cap = room(ships)
    assert cap * 10 - guns // 2 - food - goods - sugar >= 0, name
    return {"name": name, "nation": nation, "relations": rel,
            "standing": standing, "rank": rank, "crew": crew,
            "guns": guns, "gold": gold, "food": food,
            "food_frac": food_frac, "goods": goods, "sugar": sugar,
            "capacity": cap, "nships": len(ships),
            "ships": ships + [0] * (8 - len(ships)), "start": start,
            "date": date, "months": months}


def standing_of(nation, rel, own):
    st = [rel[nation][k] for k in range(4)] + [-1]
    st[nation] = own
    return st


def build():
    out = []
    homes = []
    for year in towns.YEARS:
        rel = matrix(RELATIONS[year])
        for nation, row in zip(CAREER_NATIONS, CAREERS[year]):
            name, town, ship, crew, guns, gold, food, goods, sugar = row
            rec, start = career_starts(year, town)
            assert rec["nation"] == NATION_NAMES[nation], (year, town)
            for x, y in start:
                t = start_town(year, x, y)
                assert t is not None and t["name"] == town, (year, x, y)
            rank = [0] * 5
            rank[nation] = 1
            out.append(scenario(name, nation, rel,
                                standing_of(nation, rel, 1), rank, [ship],
                                crew, guns, gold, food, 0, goods, sugar,
                                start, 0, 0))
            homes.append("%d %s: %s" % (year, NATION_NAMES[nation], town))
    for year, ex in zip(EXPEDITION_ERAS, EXPEDITIONS):
        rel = matrix(ex["relations"])
        n = ex["nation"]
        st = standing_of(n, rel, 3)
        if n != SPAIN:
            st[SPAIN] = ex["spain"]
        rank = [0] * 5
        rank[n] = ex["rank"]
        y, m, d = ex["date"]
        assert year <= y < year + 20
        date = (y - year) * DAYS_PER_YEAR + (m - 1) * DAYS_PER_MONTH + d
        start = expedition_starts(year, *ex["start"])
        out.append(scenario(ex["name"], n, rel, st, rank, ex["ships"],
                            ex["crew"], ex["guns"], ex["gold"], ex["food"],
                            FOOD_FRAC_AT_SEA if ex["months"] else 0,
                            ex["goods"], ex["sugar"], start, date,
                            ex["months"]))
    assert len(out) == 30
    notes = ("%s Home ports: %s." % (
        " ".join(" ".join(p.split()) for p in __doc__.split("\n\n")[1:]),
        "; ".join(homes)))
    return {"scenarios": out}, notes


TABLES = [("data/scenarios", "scenarios.json", build)]


if __name__ == "__main__":
    import json
    data, _ = build()
    for s in data["scenarios"]:
        print(json.dumps(s))
