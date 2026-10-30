#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""The towns of each era (data/towns/1560 ... data/towns/1680), and the
map tiles that draw them on the sailing map (world_map.era_grid()).

Written for the open pack from real history and geography; nothing comes
from the original game.  Sources: the ports' foundation, abandonment and
change of hands as in the general histories of the Spanish Caribbean
and of the buccaneers (Haring, "The Buccaneers in the West Indies in the
XVII Century", 1910; Andrews, "The Spanish Caribbean: Trade and
Plunder 1530-1630", 1978; Exquemelin, "De Americaensche Zee-Roovers",
1678), and the places' modern coordinates (degrees, the town's old
centre or harbour), given in LINEAGE below.

Which towns.  An era shows the settlements that stood at some time in
its first decade, [year, year + 10), with the nation that held them at
the era's year: a career starts then and plays mostly that decade.  So
Curacao is Dutch from 1640 (taken 1634), Barbados English from 1620
(settled 1627), Jamaica English from 1660 (1655), Tortuga French from
1620 (the boucaniers, about 1630; Le Vasseur's fort, 1640).  A town
moved to a new site keeps its record (Nombre de Dios to Portobelo in
1597, Puerto Caballos to Santo Tomas in 1604, La Yaguana abandoned in
1605 and resettled by the French as Leogane, Jamaica's capital to Port
Royal), so the same town keeps its "id" across eras.

Stretches, for play.  Every nation gets at least three home ports in
every era, so each career has a governor to report to; where history
has fewer, the era takes the real attempts and footholds of the time
and, failing those, a colony a little earlier than it was.  Each is
marked "STRETCH" in LINEAGE.  1560: Fort Caroline (1564-65) beside San
Agustin (1565), Charlesfort (1562-63); for England Drake's hidden Port
Pheasant (1572), the Isla de Pinos where the English rovers watered
(the 1570s-90s) and Bermuda (1612); for France Tortuga (settled about
1630) and for Holland Punta de Araya (the salt fleets, 1599), Curacao
(1634) and St Eustatius (1636).  1600: Bermuda (the Sea Venture's
castaways, 1609-10), the real English attempts on St Lucia (1605) and
Grenada (1609), the Isla de Pinos again; Tortuga, Martinique and
Guadeloupe French (1635); Punta de Araya (Dutch to 1605), Curacao and
St Eustatius Dutch.  1620: Martinique and Guadeloupe French, St Maarten
(1631) and Curacao Dutch.  Where islands lie closer than three map cells, one
town stands for them (no French Basseterre beside English St
Christopher, no Saba beside St Eustatius).  48 at most an era; the
least important (Tolu, San German, Baracoa, Coro, Tampico, Santo
Tomas, Eleuthera, Montserrat in 1680) leave the crowded later eras.

The fields, as the engine reads them (src/sim/sim.c, town.c, land.c,
battle.c, prison.c, src/ui/screens.c):

- x, y: the town's map cell (640 x 480 cells, 16 world pixels each).
  flags: 0x08 food, 0x04 goods, 0x02 sugar are a local product (half
  price at the merchant), 0x10 sugar wanted (twice its price), 0x40
  TOWN_INLAND (reached on foot; its arrival picture is the land one);
  0x20 and 0x80 are the player's information, clear at the start.
- nation: who holds it at the era's year.
- forts: 0-5, 12 guns each in the fort battle; 0 lets any ship in.
  soldiers: tens of men (the city information shows x 10); the monthly
  growth moves them a step toward (income + citizens) / 2.  citizens:
  hundreds of inhabitants (x 100 in the city information); under 6
  there is no governor; they size the merchant's purse and stock and
  the recruits.  gold: the town's treasure, about a thousand pieces of
  eight each when sacked by surprise; wealth: 0-255, 64 a level (poor,
  modest, prosperous, wealthy), the merchant's prices and the
  shipwright's purse.  Forts never change in play, while the
  garrison drifts toward the town's size, so the forts are what keeps
  a small town on the treasure road from being taken every year:
  Chagres, a hamlet of a few hundred under the castle San Lorenzo and
  its batteries, counts two from 1600.  The Spanish treasure ports
  are rich and strong, the young colonies and the buccaneer havens
  small and poor; the table below gives every value.
- standing, garrison, family, barmaid: 0 (the player's history with
  the town, and the mark of a villain hiding there); governor: a
  repeatable draw 0-15, as a new governor's (bit 0 a niece rather than
  a daughter; the bits 0x20-0x80 are the player's doings).
- id: the town's battle screens (screens/sea layer id, the land
  battle's screens/land layer id, both indexed by it), the same in every
  era: 0-60 for the towns that last, 61 and up (screens to 107) for the
  early footholds only.  Inland towns use the ids the engine nudges toward the right
  coast when the player leaves their prison (src/sim/prison.c): Panama
  43 (20 cells north, to the Caribbean side of the isthmus), Granada 45
  (50 cells east, past Lake Nicaragua); the other inland towns avoid
  those ids.
- land [x, y], party [x, y]: the land battle (15 x 15 cells of 32
  pixels): the town, and where the attackers come ashore, on the side
  of the town's sea.  fort [x, y], ship [x, y, heading]: the fort
  battle on the same 15 x 15 grid: the fort by the shore, the attacker
  coming in from the open water, heading for it (0-255, 0 north,
  clockwise).  These follow one convention from the direction of the
  town's water (see battle_cells()), for the open battle screens to
  be drawn to.
- sea [x, y, heading]: the map cell where a ship leaving the town
  starts, on open sea (no shallows, which wreck large ships), outside
  the town's arrival cells, heading 0-7 (x 32) to the open water: of
  the nearest such cells, the first with 16 cells of open sea straight
  ahead (else the longest such run), so a ship sailing straight out
  meets no shallows; 0, 0 for inland towns.  Where a town's arrival
  cells lie across a strait (Maracaibo, between its lake and the Gulf),
  only the cells joined to the open sea without crossing any town's
  arrival cells are taken, so a ship leaving for the open sea does not
  sail straight back into the town it left.
  Every port must be reached from the open sea without crossing
  another town's arrival cells (a ship entering them stops at that
  town), or the build stops.
- citymap [x, y]: the town's dot on the Map of the Caribbean, which
  shows the whole world at 0.75 from the screen's top-left: the cell
  times 0.75, rounded.
- arrival: the cells where a ship or a party reaches the town: the
  3 x 3 cells around each of its map tiles, and for a coastal town,
  when those hold no open sea joined to the main water, the cells on
  toward it (at most 16).  No cell belongs to two towns.  No shallows
  (the map's reefs) lie within HARBOUR (8) cells of a town's tiles or
  arrival cells, so its approaches are safe for every ship; the build
  stops otherwise.

Placement.  Each town's real coordinates go through
_geo.lonlat_to_world() and snap to the nearest fitting cell of the
bare map (world_map.base_grid()'s tile terrain): a coastal town on a
cell with some land (an eighth, preferably half) within four cells of a
cell with open sea of the main water (one 4-connected body of sea
pixels holding the open Caribbean, so a ship can sail in without
crossing shallows), the nearer the better; where shallows lie between,
its arrival cells run out across them to that open sea.  An islet the
map's tiles drew as shallows (Providence) keeps its town there, the
house tile giving it land.  An inland town sits on land with no such
sea within a cell.  The towns are placed most important first,
each at least three cells (Chebyshev) from every town it shares an era
with, so arrival cells never touch.

Map tiles.  The open tile sheet's tiles with a house (world_map's
BUILDINGS): 68-79 a house on a shore, the twelve of them the coast on
different sides, and 123-127 and 255 a house inland.  Each town cell
takes the building tile whose terrain best matches the geography there
and its four neighbours' edges (the costs of world_map's seams), so the
shore runs on.  A town of 4000 people or more in the era (citizens 40)
gets a second house on the neighbouring land cell that fits one best,
so the big ports read as cities.

Routes.  The treasure fleet follows the Spanish fleets' circuit, as one
convoy: in at Puerto Rico and Santo Domingo, the galeones at
Cartagena, the Isthmus fair (Nombre de Dios, Portobelo from 1597) and
Cartagena again, the flota at Veracruz and Campeche, all meeting at
Havana and leaving by the Florida channel past San Agustin.  The silver
train follows the treasure by land: Peru's silver over the Camino Real
from Panama (by Chagres) to the Isthmus port, then the Main's treasure
roads east, Cartagena (New Granada's gold), Santa Marta, Rio de la
Hacha (pearls), Maracaibo and Gibraltar (cacao), Coro, Caracas and
Cumana.  A stop missing from an era is left out; the engine passes over
the towns not Spanish.
"""
import math

import numpy as np

import _geo
import world_map

W, H, SIDE = world_map.W, world_map.H, world_map.SIDE
YEARS = world_map.YEARS
SEA, SHALLOWS, LAND = world_map.SEA, world_map.SHALLOWS, world_map.LAND

INLAND = 0x40
TRADE = {"F": 0x08, "G": 0x04, "S": 0x02, "W": 0x10, "-": 0}
SPACING = 3             # Chebyshev cells between towns of one era
CITY = 40               # citizens from which a town gets a second house
ARRIVALS = 16
REACH = 4               # cells from a coastal town to open sea, at most
RUN_OK = 16             # open sea cells straight out of a harbour mouth
HARBOUR = 8             # cells around a town and its arrival cells kept
                        # free of shallows
COAST_TILES = tuple(range(68, 80))
LAND_TILES = (123, 124, 125, 126, 127, 255)
# A point of the open Caribbean, in the main water.
OPEN_SEA = (-75.0, 15.0)
# The ids prison.c nudges from (src/sim/prison.c, leave_prison()).
NUDGED = {0x2d: (50, 0), 0x33: (20, 0), 0x2b: (0, -20), 7: (0, 16),
          0x1e: (0, -20), 0x31: (0, -10)}

# --- The towns ---------------------------------------------------------
#
# key: id, longitude, latitude (east and north positive), inland, and
# the history behind its eras.
LINEAGE = {
    # Spanish Cuba
    "havana": (0, -82.36, 23.14, False,
               "San Cristobal de la Habana, the fleets' meeting place; "
               "La Fuerza 1558-77, the Morro and the Punta 1589-1630"),
    "santiago": (1, -75.87, 20.00, False,
                 "Santiago de Cuba, the east's capital; El Morro from "
                 "1638; sacked by Myngs 1662"),
    "trinidad": (2, -79.98, 21.77, False,
                 "Trinidad, south Cuba, and its landing at Casilda"),
    "baracoa": (3, -74.50, 20.35, False, "Baracoa, Cuba's first town"),
    "principe": (4, -77.92, 21.38, True,
                 "Puerto Principe (Camaguey), inland cattle town, sacked "
                 "by Morgan from the coast in 1668"),
    # Hispaniola
    "domingo": (5, -69.89, 18.47, False,
                "Santo Domingo, seat of the Audiencia; Drake 1586, "
                "Penn and Venables repulsed 1655"),
    "plata": (6, -70.69, 19.80, False,
              "Puerto Plata, abandoned in the devastaciones of 1605-06"),
    "cristi": (7, -71.65, 19.85, False,
               "Monte Cristi, abandoned 1605-06"),
    "yaguana": (8, -72.63, 18.51, False,
                "La Yaguana, abandoned 1605-06; the French Leogane on its "
                "plain from the 1660s"),
    "tortuga": (9, -72.78, 20.03, False,
                "Tortuga's harbour (Cayonne, Basse-Terre): boucaniers about "
                "1630, French under Le Vasseur 1640, d'Ogeron 1665; "
                "STRETCH: French from 1560 (the corsairs' coast)"),
    "goave": (10, -72.86, 18.43, False,
              "Petit-Goave, French buccaneer port from the 1660s, the "
              "flibustiers' base under de Graaf and Grammont"),
    "cap": (11, -72.20, 19.76, False, "Cap-Francois, founded 1670"),
    # Puerto Rico
    "sanjuan": (12, -66.11, 18.47, False,
                "San Juan de Puerto Rico; El Morro from 1539; Drake "
                "repulsed 1595, Cumberland 1598, the Dutch 1625"),
    "german": (13, -67.15, 18.08, False,
               "San German, the island's second town, by its west coast"),
    # The Main: New Granada
    "cartagena": (14, -75.55, 10.42, False,
                  "Cartagena de Indias, the galeones' port; walls after "
                  "Drake's raid of 1586"),
    "marta": (15, -74.21, 11.24, False, "Santa Marta, founded 1525"),
    "hacha": (16, -72.91, 11.54, False,
              "Rio de la Hacha, the pearl fisheries"),
    "tolu": (17, -75.58, 9.52, False, "Santiago de Tolu"),
    "isthmus": (18, -79.47, 9.58, False,
                "Nombre de Dios, the Isthmus fair, moved in 1597 to "
                "Portobelo (-79.65, 9.55; Morgan 1668)"),
    "chagres": (19, -79.99, 9.32, False,
                "Chagres and its castle San Lorenzo (1597-1601), the river "
                "road to Panama, on its cliff over the river mouth with the "
                "batteries below (two forts from 1600); Morgan took it in "
                "1670 at the cost of a hundred men"),
    "panama": (43, -79.53, 8.95, True,
               "Panama, on the Pacific: the Camino Real's end, reached "
               "overland (Morgan 1671), rebuilt nearby 1673"),
    # Venezuela and Trinidad
    "coro": (20, -69.62, 11.43, False,
             "Santa Ana de Coro and its landing La Vela"),
    # Set a little west of the city, on the shore beside the strait's
    # lane (tables/_geo.py, CHANNELS), so its arrival cells leave the
    # lane open to ships bound for the lake.
    "maracaibo": (21, -71.68, 10.65, False,
                  "Nueva Zamora de Maracaibo (1569, 1574); sacked by "
                  "l'Olonnais 1666, Morgan 1669, Grammont 1678"),
    "gibraltar": (22, -71.05, 9.45, False,
                  "San Antonio de Gibraltar, the lake's cacao port (1592)"),
    "caracas": (23, -66.92, 10.50, True,
                "Santiago de Leon de Caracas (1567), over the mountains "
                "from La Guaira"),
    "cumana": (24, -64.18, 10.46, False,
               "Cumana, and the Araya salt pans the Dutch worked"),
    "margarita": (25, -63.85, 11.00, False,
                  "Margarita's pearls, the port of Pampatar"),
    "espana": (26, -61.51, 10.65, False,
               "Trinidad's Spanish settlement (San Jose de Oruna 1592) "
               "and its harbour, Puerto de Espana"),
    # New Spain and Central America
    "veracruz": (27, -96.13, 19.20, False,
                 "Veracruz and San Juan de Ulua, the flota's port; "
                 "Hawkins 1568, Lorencillo 1683"),
    "campeche": (28, -90.53, 19.85, False,
                 "San Francisco de Campeche; sacked 1663, 1678, 1685"),
    "merida": (29, -89.62, 20.97, True, "Merida, Yucatan's capital"),
    "tampico": (31, -97.86, 22.22, False, "Tampico (1554)"),
    "trujillo": (32, -85.95, 15.92, False,
                 "Trujillo, Honduras; sacked repeatedly"),
    "honduras": (33, -87.94, 15.84, False,
                 "Puerto Caballos, Honduras' port, moved in 1604 to "
                 "Santo Tomas de Castilla (-88.62, 15.70)"),
    "granada": (45, -85.96, 11.93, True,
                "Granada on Lake Nicaragua, reached up the San Juan "
                "river; sacked by Mansvelt and Morgan's men 1665"),
    # Florida and the Carolinas
    "agustin": (34, -81.31, 29.90, False,
                "San Agustin (1565); Drake 1586, Searle 1668; the "
                "Castillo de San Marcos 1672-95"),
    "caroline": (35, -81.50, 30.38, False,
                 "Fort Caroline, the Huguenot colony on the St Johns "
                 "river, 1564-1565"),
    "charles": (36, -79.93, 32.78, False, "Charles Town, Carolina (1670)"),
    # Jamaica and the western Caribbean
    "jamaica": (37, -76.84, 17.94, False,
                "Jamaica's capital: the Spanish Villa de la Vega by its "
                "landing at Caguaya, English from 1655, Port Royal at the "
                "harbour's mouth"),
    "providence": (38, -81.37, 13.35, False,
                   "Providence Island (Santa Catalina): the Puritans "
                   "1629-1641, Spanish 1641, Mansvelt 1666-1670"),
    "belize": (39, -88.19, 17.50, False,
               "The Baymen's logwood settlement at the Belize river"),
    "trist": (40, -91.83, 18.65, False,
              "Trist (Isla del Carmen), the logwood cutters of the "
              "Laguna de Terminos, driven out in 1680"),
    "blewfields": (41, -83.77, 12.01, False,
                   "Blewfields, after the Dutch rover Blauvelt (1630s); "
                   "the Mosquito Shore's English and Miskito"),
    # The Bahamas and Bermuda
    "bermuda": (42, -64.68, 32.38, False,
                "St George's, Bermuda (1612); STRETCH: in 1560 and 1600"),
    "eleuthera": (44, -76.24, 25.20, False,
                  "The Eleutheran Adventurers (1648)"),
    "nassau": (46, -77.34, 25.08, False,
               "Charles Town on New Providence (1666), Nassau from 1695"),
    # The Leeward Islands
    "kitts": (47, -62.80, 17.32, False,
              "St Christopher: Old Road, Warner's colony (1624), the "
              "English mother colony"),
    "nevis": (48, -62.62, 17.14, False, "Nevis (1628)"),
    "antigua": (49, -61.85, 17.12, False, "Antigua, St John's (1632)"),
    "montserrat": (50, -62.21, 16.71, False, "Montserrat (1632)"),
    "eustatius": (51, -62.99, 17.48, False,
                  "St Eustatius, Dutch from 1636, the great free port; "
                  "STRETCH: Dutch in 1560 and 1600"),
    "maarten": (52, -63.05, 18.02, False,
                "Sint Maarten, Dutch from 1631, Spanish 1633-48, then "
                "shared with France; STRETCH: in 1620"),
    "tortola": (53, -64.62, 18.42, False,
                "Tortola, Dutch 1648-1672, English from 1672"),
    "guadeloupe": (30, -61.73, 15.99, False,
                   "Guadeloupe, Basse-Terre (1635); STRETCH: in 1600 and "
                   "1620"),
    # The Windward Islands and Barbados
    "martinique": (54, -61.18, 14.74, False,
                   "Martinique, Saint-Pierre (1635); STRETCH: in 1600 "
                   "and 1620"),
    "lucia": (55, -60.99, 14.01, False,
              "Sainte-Lucie, French from 1650 (English 1664-67); the "
              "Olive Branch's English colonists of 1605 in 1600"),
    "barbados": (56, -59.62, 13.10, False,
                 "Barbados (1627), Bridgetown; sugar from the 1640s"),
    "grenada": (57, -61.75, 12.05, False,
                "Grenade, French from 1649 (Fort Royal); the London "
                "merchants' English attempt of 1609 in 1600"),
    "tobago": (58, -60.73, 11.18, False,
               "Tobago: the Dutch Nieuw Walcheren (1628-37), "
               "Lampsinsburg (1654-66, 1667-77)"),
    # The Dutch Leewards
    "curacao": (59, -68.93, 12.11, False,
                "Curacao, Dutch from 1634, Willemstad and Fort Amsterdam; "
                "STRETCH: Dutch from 1560"),
    "bonaire": (60, -68.27, 12.15, False,
                "Bonaire, Dutch from 1636: salt and goats"),
    # The early footholds (ids past the original's 61 towns)
    "chfort": (61, -80.68, 32.32, False,
               "Charlesfort, Ribault's Huguenot post on Port Royal Sound, "
               "1562-63"),
    "pheasant": (62, -78.95, 9.55, False,
                 "Port Pheasant, Drake's hidden harbour on the Darien "
                 "coast east of Nombre de Dios (1572; its exact cove is "
                 "not known); STRETCH: English in 1560"),
    "pinos": (63, -82.80, 21.88, False,
              "Isla de Pinos, where the English rovers watered and "
              "careened; STRETCH: an English post, 1560 and 1600"),
    "araya": (64, -64.24, 10.64, False,
              "Punta de Araya, the salt pans the Dutch fleets worked "
              "1599-1605; STRETCH: in 1560"),
}

# Per era: key, name, nation, forts, soldiers (tens of men), citizens
# (hundreds), gold, wealth (0-255), local trade (F food, G goods, S
# sugar, W sugar wanted).
ERAS = {}
ERAS[1560] = """
havana     Havana            spain    2  25  30   80 170 G
santiago   Santiago de Cuba  spain    1  10  15   25 110 F
trinidad   Trinidad          spain    0   5   9   15  90 F
baracoa    Baracoa           spain    0   4   8    8  70 F
principe   Puerto Principe   spain    0   6  12   20 100 F
domingo    Santo Domingo     spain    2  20  35   70 180 S
plata      Puerto Plata      spain    0   6  12   20 110 F
cristi     Monte Cristi      spain    0   4   8   15  90 F
yaguana    La Yaguana        spain    0   5  10   18 100 F
sanjuan    San Juan          spain    2  15  18   35 130 S
german     San German        spain    0   4   8   10  80 F
cartagena  Cartagena         spain    2  20  25   90 190 G
marta      Santa Marta       spain    1   8  12   30 120 F
hacha      Rio de la Hacha   spain    0   6  10   40 150 -
tolu       Tolu              spain    0   3   7   10  80 F
isthmus    Nombre de Dios    spain    1  10  12  120 220 G
panama     Panama            spain    1  20  30  140 220 G
coro       Coro              spain    0   6  10   15  80 F
maracaibo  Maracaibo         spain    0   5   9   20 100 -
caracas    Caracas           spain    0   6   8   15  90 F
cumana     Cumana            spain    1   6  10   25 110 -
margarita  Margarita         spain    1   8  12   50 170 -
veracruz   Veracruz          spain    1  18  22  110 210 G
campeche   Campeche          spain    0   8  14   30 120 -
merida     Merida            spain    0  10  20   35 130 F
tampico    Tampico           spain    0   4   7   10  70 F
trujillo   Trujillo          spain    1   6  10   20 100 F
honduras   Puerto Caballos   spain    0   4   7   20 100 -
granada    Granada           spain    0  10  18   40 140 F
agustin    San Agustin       spain    1  15   8   10  50 W
jamaica    Villa de la Vega  spain    0   6  12   15  80 F
caroline   Fort Caroline     france   1  15   8    5  40 W
chfort     Charlesfort       france   1  10   6    5  40 -
tortuga    Tortuga           france   0   8   6   10  40 F
pheasant   Port Pheasant     england  0   8   6   15  40 -
pinos      Isla de Pinos     england  0   6   6   10  40 F
bermuda    Bermuda           england  0   3   6    5  40 F
araya      Punta de Araya    holland  0   6   6   10  50 -
curacao    Curacao           holland  1  10   8   15  60 G
eustatius  St Eustatius      holland  0   5   6   10  50 G
"""
ERAS[1600] = """
havana     Havana            spain    4  45  40  110 200 G
santiago   Santiago de Cuba  spain    1  12  16   30 110 F
trinidad   Trinidad          spain    0   5  10   15  90 F
baracoa    Baracoa           spain    0   4   8    8  70 F
principe   Puerto Principe   spain    0   7  14   25 110 F
domingo    Santo Domingo     spain    2  22  35   70 170 S
plata      Puerto Plata      spain    0   6  12   25 120 F
cristi     Monte Cristi      spain    0   4   9   20 100 F
yaguana    La Yaguana        spain    0   6  11   22 110 F
sanjuan    San Juan          spain    3  25  20   40 140 S
german     San German        spain    0   4   8   10  80 F
cartagena  Cartagena         spain    4  40  35  120 210 G
marta      Santa Marta       spain    1   8  12   30 110 F
hacha      Rio de la Hacha   spain    1   7  10   45 150 -
tolu       Tolu              spain    0   3   7   10  80 F
isthmus    Portobelo         spain    3  25  12  140 230 G
chagres    Chagres           spain    2   8   6   10  80 -
panama     Panama            spain    1  25  40  160 230 G
coro       Coro              spain    0   6  10   15  80 F
maracaibo  Maracaibo         spain    1   8  12   30 120 -
gibraltar  Gibraltar         spain    0   5   9   30 140 -
caracas    Caracas           spain    0   8  14   25 110 F
cumana     Cumana            spain    1   8  11   25 110 -
margarita  Margarita         spain    1   8  12   50 160 -
espana     Puerto de Espana  spain    0   5   7   10  70 F
veracruz   Veracruz          spain    2  25  28  130 220 G
campeche   Campeche          spain    1  10  16   35 130 -
merida     Merida            spain    0  12  24   40 140 F
tampico    Tampico           spain    0   4   7   10  70 F
trujillo   Trujillo          spain    1   7  10   20 100 F
honduras   Puerto Caballos   spain    0   4   7   20  90 -
granada    Granada           spain    0  10  20   45 150 F
agustin    San Agustin       spain    1  15   9   10  50 W
jamaica    Villa de la Vega  spain    0   6  12   15  80 F
bermuda    Bermuda           england  0   3   6    5  40 F
lucia      St Lucia          england  0   5   6    5  40 -
grenada    Grenada           england  0   5   6    5  40 -
pinos      Isla de Pinos     england  0   6   6   10  40 F
tortuga    Tortuga           france   0   8   6   10  50 F
martinique Martinique        france   0   5   6    8  40 -
guadeloupe Guadeloupe        france   0   5   6    8  40 -
araya      Punta de Araya    holland  0   8   6   15  60 -
curacao    Curacao           holland  1  10   8   15  60 G
eustatius  St Eustatius      holland  0   5   6   10  50 G
"""
ERAS[1620] = """
havana     Havana            spain    5  50  45  120 210 G
santiago   Santiago de Cuba  spain    1  12  16   30 110 F
trinidad   Trinidad          spain    0   5  10   15  90 F
baracoa    Baracoa           spain    0   4   8    8  70 F
principe   Puerto Principe   spain    0   8  15   25 110 F
domingo    Santo Domingo     spain    2  22  32   60 150 S
sanjuan    San Juan          spain    3  28  20   40 140 S
german     San German        spain    0   4   8   10  80 F
cartagena  Cartagena         spain    4  45  40  130 220 G
marta      Santa Marta       spain    1   8  12   25 100 F
hacha      Rio de la Hacha   spain    1   7  10   40 140 -
tolu       Tolu              spain    0   3   7   10  80 F
isthmus    Portobelo         spain    3  28  14  150 230 G
chagres    Chagres           spain    2   8   6   10  80 -
panama     Panama            spain    1  28  45  170 230 G
coro       Coro              spain    0   6  10   15  80 F
maracaibo  Maracaibo         spain    1  10  14   40 140 -
gibraltar  Gibraltar         spain    0   6  10   40 160 -
caracas    Caracas           spain    0  10  16   30 120 F
cumana     Cumana            spain    1  10  12   25 110 -
margarita  Margarita         spain    1   8  12   40 150 -
espana     Puerto de Espana  spain    0   5   7   12  80 F
veracruz   Veracruz          spain    2  28  30  140 220 G
campeche   Campeche          spain    1  10  16   35 130 -
merida     Merida            spain    0  12  25   40 140 F
tampico    Tampico           spain    0   4   7   10  70 F
trujillo   Trujillo          spain    1   7  10   20 100 F
honduras   Santo Tomas       spain    1   6   7   20  90 -
granada    Granada           spain    0  10  20   50 160 F
agustin    San Agustin       spain    1  18  10   10  50 W
jamaica    Villa de la Vega  spain    0   6  12   15  80 F
bermuda    Bermuda           england  2  12  15   20  90 F
kitts      St Christopher    england  0   8   8   10  60 -
barbados   Barbados          england  0   6   8   10  60 -
nevis      Nevis             england  0   4   6    8  50 -
providence Providence        england  1   6   6    8  60 -
tortuga    Tortuga           france   0   8   6   10  50 F
tobago     Nieuw Walcheren   holland  0   5   6   10  50 -
martinique Martinique        france   0   6   6    8  40 -
guadeloupe Guadeloupe        france   0   6   6    8  40 -
maarten    St Maarten        holland  0   5   6   10  50 -
curacao    Curacao           holland  1  10   8   15  70 G
"""
ERAS[1640] = """
havana     Havana            spain    5  55  50  130 220 G
santiago   Santiago de Cuba  spain    2  15  18   30 110 F
trinidad   Trinidad          spain    0   5  10   15  90 F
baracoa    Baracoa           spain    0   4   8    8  60 F
principe   Puerto Principe   spain    0   8  16   30 120 F
domingo    Santo Domingo     spain    2  20  30   55 140 S
sanjuan    San Juan          spain    4  30  22   45 140 S
german     San German        spain    0   4   8   10  70 F
cartagena  Cartagena         spain    5  50  45  140 220 G
marta      Santa Marta       spain    1   8  12   25 100 F
hacha      Rio de la Hacha   spain    1   7  10   35 130 -
tolu       Tolu              spain    0   3   7   10  80 F
isthmus    Portobelo         spain    3  30  14  160 230 G
chagres    Chagres           spain    2   8   6   10  80 -
panama     Panama            spain    1  30  50  180 240 G
coro       Coro              spain    0   6  10   15  80 F
maracaibo  Maracaibo         spain    1  12  16   50 160 -
gibraltar  Gibraltar         spain    0   6  12   50 170 -
caracas    Caracas           spain    0  10  18   35 130 F
cumana     Cumana            spain    1  10  12   25 110 -
margarita  Margarita         spain    1   8  12   35 140 -
espana     Puerto de Espana  spain    0   5   7   12  80 F
veracruz   Veracruz          spain    3  30  32  150 220 G
campeche   Campeche          spain    1  12  18   40 140 -
merida     Merida            spain    0  12  26   40 140 F
tampico    Tampico           spain    0   4   7   10  70 F
trujillo   Trujillo          spain    1   6   9   15  80 F
honduras   Santo Tomas       spain    1   6   7   15  80 -
granada    Granada           spain    0  10  22   55 170 F
agustin    San Agustin       spain    1  18  10   10  50 W
jamaica    Villa de la Vega  spain    0   8  14   15  80 F
bermuda    Bermuda           england  2  12  18   25 100 F
kitts      St Christopher    england  1  15  25   35 130 S
barbados   Barbados          england  1  20  40   50 170 S
nevis      Nevis             england  1   8  15   25 120 S
antigua    Antigua           england  0   5   9   12  70 S
montserrat Montserrat        england  0   4   8   10  70 S
providence Providence        england  1   8  10   15  80 -
eleuthera  Eleuthera         england  0   3   6    5  30 F
tortuga    Tortuga           france   1  15  10   30  80 F
martinique Martinique        france   1  10  12   15  80 S
guadeloupe Guadeloupe        france   1  10  12   15  80 S
grenada    Grenade           france   0   5   6    8  50 -
curacao    Curacao           holland  2  20  15   40 140 GW
bonaire    Bonaire           holland  0   3   6    5  40 F
eustatius  St Eustatius      holland  1   6   8   20 110 GW
blewfields Blewfields        holland  0   8   6   15  60 F
"""
ERAS[1660] = """
havana     Havana            spain    5  60  60  140 230 G
santiago   Santiago de Cuba  spain    2  15  18   25 100 F
trinidad   Trinidad          spain    0   5  10   15  90 F
principe   Puerto Principe   spain    0   8  16   30 120 F
domingo    Santo Domingo     spain    2  25  30   50 130 S
sanjuan    San Juan          spain    4  30  24   45 140 S
cartagena  Cartagena         spain    5  55  50  150 230 G
marta      Santa Marta       spain    1   8  12   20  90 F
hacha      Rio de la Hacha   spain    1   7  10   30 120 -
isthmus    Portobelo         spain    3  30  14  170 240 G
chagres    Chagres           spain    2  10   6   10  80 -
panama     Panama            spain    1  30  60  200 245 G
maracaibo  Maracaibo         spain    1  12  18   60 170 -
gibraltar  Gibraltar         spain    0   6  12   50 170 -
caracas    Caracas           spain    0  12  20   40 140 F
cumana     Cumana            spain    1  10  12   25 110 -
margarita  Margarita         spain    1   8  12   30 130 -
espana     Puerto de Espana  spain    0   4   7   10  70 F
veracruz   Veracruz          spain    3  35  35  160 230 G
campeche   Campeche          spain    1  12  18   40 140 -
merida     Merida            spain    0  12  28   45 150 F
trujillo   Trujillo          spain    1   6   9   15  80 F
granada    Granada           spain    0  10  22   55 170 F
agustin    San Agustin       spain    1  20  12   15  60 W
providence Santa Catalina    spain    2  15   6   10  60 -
jamaica    Port Royal        england  2  30  30   60 170 G
bermuda    Bermuda           england  2  12  20   25 100 F
kitts      St Christopher    england  1  15  25   40 140 S
barbados   Barbados          england  2  30  60   80 200 S
nevis      Nevis             england  1  10  18   30 140 S
antigua    Antigua           england  1   8  14   20 110 S
montserrat Montserrat        england  0   6  12   15 100 S
nassau     New Providence    england  0   4   6    5  40 F
belize     Belize            england  0   4   6   10  50 -
trist      Trist             england  0   5   6   15  60 -
blewfields Blewfields        england  0   6   6   10  50 F
tortuga    Tortuga           france   1  20  15   50 110 F
martinique Martinique        france   1  15  20   30 130 S
guadeloupe Guadeloupe        france   1  15  20   30 130 S
grenada    Grenade           france   1   6   8   10  70 -
lucia      Ste Lucie         france   0   4   6    5  40 -
yaguana    Leogane           france   0   8  10   20  90 F
goave      Petit-Goave       france   0  12  10   30  90 F
curacao    Curacao           holland  3  30  25   80 200 GW
bonaire    Bonaire           holland  0   3   6    5  40 F
eustatius  St Eustatius      holland  1   8  12   40 150 GW
maarten    St Maarten        holland  1   5   7   15  90 -
tobago     Tobago            holland  1   8   8   15  80 -
"""
ERAS[1680] = """
havana     Havana            spain    5  65  70  150 230 G
santiago   Santiago de Cuba  spain    2  18  20   30 110 F
trinidad   Trinidad          spain    0   6  11   20 100 F
principe   Puerto Principe   spain    0   8  18   30 120 F
domingo    Santo Domingo     spain    2  25  30   50 130 S
sanjuan    San Juan          spain    4  32  25   50 150 S
cartagena  Cartagena         spain    5  60  55  160 230 G
marta      Santa Marta       spain    1   8  12   20  90 F
hacha      Rio de la Hacha   spain    1   7  10   30 120 -
isthmus    Portobelo         spain    4  30  14  150 220 G
chagres    Chagres           spain    2  12   6   10  80 -
panama     Panama            spain    2  35  50  180 230 G
maracaibo  Maracaibo         spain    1  12  16   40 140 -
gibraltar  Gibraltar         spain    0   5  10   30 130 -
caracas    Caracas           spain    0  12  22   45 150 F
cumana     Cumana            spain    1  10  12   25 110 -
margarita  Margarita         spain    1   8  12   30 120 -
espana     Puerto de Espana  spain    0   4   7   10  70 F
veracruz   Veracruz          spain    3  40  40  170 240 G
campeche   Campeche          spain    2  15  20   40 140 -
merida     Merida            spain    0  12  30   45 150 F
trujillo   Trujillo          spain    1   6   9   15  80 F
granada    Granada           spain    0  10  22   50 160 F
agustin    San Agustin       spain    2  25  14   15  60 W
providence Santa Catalina    spain    2  15   6   10  60 -
jamaica    Port Royal        england  4  40  65  150 230 G
bermuda    Bermuda           england  2  12  22   25 100 F
kitts      St Christopher    england  1  15  25   40 140 S
barbados   Barbados          england  3  35  70  100 220 S
nevis      Nevis             england  2  12  20   40 150 S
antigua    Antigua           england  1  10  16   25 120 S
nassau     New Providence    england  1   6   8   10  60 F
charles    Charles Town      england  1  10  10   15  80 FW
belize     Belize            england  0   5   6   10  60 -
blewfields Blewfields        england  0   6   6   10  50 F
tortola    Tortola           england  0   4   6    8  50 -
tortuga    Tortuga           france   1  10   8   20  60 F
martinique Martinique        france   2  20  30   50 160 S
guadeloupe Guadeloupe        france   1  15  25   40 150 S
grenada    Grenade           france   1   8  10   15  90 -
lucia      Ste Lucie         france   0   4   6    5  50 -
yaguana    Leogane           france   1  12  15   35 120 F
goave      Petit-Goave       france   1  20  15   60 120 F
cap        Cap-Francois      france   1  10  12   25 100 F
curacao    Curacao           holland  3  35  30  100 220 GW
eustatius  St Eustatius      holland  1  10  14   50 160 GW
maarten    St Maarten        holland  1   5   7   15  90 -
bonaire    Bonaire           holland  0   3   6    5  40 F
"""

FLEET = ["sanjuan", "domingo", "cartagena", "isthmus", "cartagena",
         "veracruz", "campeche", "havana", "agustin"]
TRAIN = ["panama", "chagres", "isthmus", "tolu", "cartagena", "marta",
         "hacha", "maracaibo", "gibraltar", "coro", "caracas", "cumana"]

NATIONS = ("spain", "england", "france", "holland")


def era_rows(year):
    """The era's towns: dicts in table order."""
    out = []
    for line in ERAS[year].strip("\n").split("\n"):
        key = line[:11].split()[0]
        name = line[11:29].strip()
        f = line[29:].split()
        flags = 0
        for c in f[6]:
            flags |= TRADE[c]
        out.append({"key": key, "name": name, "nation": f[0],
                    "forts": int(f[1]), "soldiers": int(f[2]),
                    "citizens": int(f[3]), "gold": int(f[4]),
                    "wealth": int(f[5]), "trade": flags})
    return out


def check_tables():
    ids = {}
    for key, v in LINEAGE.items():
        tid, inland = v[0], v[3]
        assert 0 <= tid < 108, key
        assert tid not in ids, (key, ids.get(tid))
        ids[tid] = key
        # A coastal town never leaves its prison by a nudge; an inland
        # one only by its own.
        assert not inland or tid not in NUDGED or key in ("panama",
                                                          "granada"), key
    for year in YEARS:
        rows = era_rows(year)
        assert 1 <= len(rows) <= 48, year
        seen = set()
        for r in rows:
            assert r["key"] in LINEAGE and r["key"] not in seen, r
            seen.add(r["key"])
            assert r["nation"] in NATIONS, r
            assert 1 <= len(r["name"].encode()) <= 16, r
            assert r["name"].isascii(), r
            assert 0 <= r["forts"] <= 5 and r["citizens"] >= 6, r
            assert 0 <= r["wealth"] <= 255 and 0 <= r["gold"] <= 255, r
            assert 0 <= r["soldiers"] <= 255, r


# --- Geometry ----------------------------------------------------------

DIRS = ((0, -1), (1, -1), (1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0),
        (-1, -1))


def _runs(mask):
    """Row runs of a boolean mask: (rows, starts, ends), sorted."""
    h, w = mask.shape
    pad = np.zeros((h, w + 2), dtype=np.int8)
    pad[:, 1:-1] = mask
    d = np.diff(pad, axis=1)
    rs, cs = np.nonzero(d == 1)
    _, ce = np.nonzero(d == -1)
    return rs, cs, ce


def main_water(grid):
    """The pixels of the main body of open sea, 4-connected, on the
    tiles of "grid": a boolean (H * SIDE, W * SIDE) array."""
    sea = world_map.terrain_of(grid) == SEA
    h = sea.shape[0]
    rs, cs, ce = _runs(sea)
    rowstart = np.searchsorted(rs, np.arange(h + 1))
    pa, pb = [], []
    for y in range(h - 1):
        a0, a1 = rowstart[y], rowstart[y + 1]
        b0, b1 = rowstart[y + 1], rowstart[y + 2]
        if a0 == a1 or b0 == b1:
            continue
        lo = np.searchsorted(ce[b0:b1], cs[a0:a1], side="right")
        hi = np.searchsorted(cs[b0:b1], ce[a0:a1], side="left")
        cnt = hi - lo
        m = cnt > 0
        if not m.any():
            continue
        c = cnt[m]
        ia = np.repeat(np.arange(a0, a1)[m], c)
        off = np.arange(c.sum()) - np.repeat(np.cumsum(c) - c, c)
        pa.append(ia)
        pb.append(np.repeat(lo[m], c) + off + b0)
    a = np.concatenate(pa)
    b = np.concatenate(pb)
    lab = np.arange(len(rs))
    while True:
        m = np.minimum(lab[a], lab[b])
        new = lab.copy()
        np.minimum.at(new, a, m)
        np.minimum.at(new, b, m)
        new = new[new]
        new = new[new]
        if (new == lab).all():
            break
        lab = new
    ox, oy = _geo.lonlat_to_world(*OPEN_SEA)
    ox, oy = int(ox), int(oy)
    k = np.nonzero((rs == oy) & (cs <= ox) & (ce > ox))[0]
    if len(k) != 1:
        raise SystemExit("towns: the open sea point is not sea")
    keep = lab == lab[k[0]]
    out = np.zeros((h, sea.shape[1] + 1), dtype=np.int32)
    np.add.at(out, (rs[keep], cs[keep]), 1)
    np.add.at(out, (rs[keep], ce[keep]), -1)
    return np.cumsum(out, axis=1)[:, :-1] > 0


def per_cell(mask):
    """Pixels set per map cell, (H, W)."""
    return mask.reshape(H, SIDE, W, SIDE).sum(axis=(1, 3))


_memo = {}


def _base():
    if "base" not in _memo:
        grid = world_map.base_grid()
        land = per_cell(world_map.terrain_of(grid) == LAND)
        water = per_cell(main_water(grid))
        _memo["base"] = (grid, land, water)
    return _memo["base"]


def _coexist():
    """Pairs of keys that share an era."""
    pairs = set()
    for year in YEARS:
        keys = [r["key"] for r in era_rows(year)]
        for a in keys:
            for b in keys:
                pairs.add((a, b))
    return pairs


def _importance(key):
    return max((r["citizens"] + r["gold"] / 4.0 for y in YEARS
                for r in era_rows(y) if r["key"] == key), default=0)


def placement():
    """Each town's map cell and how far it moved: {key: (x, y, d)}."""
    if "place" in _memo:
        return _memo["place"]
    _, land, water = _base()
    near = np.zeros_like(water)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dx or dy:
                near = np.maximum(near, np.roll(np.roll(water, dy, 0),
                                                dx, 1))
    wet3 = np.zeros_like(water)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            wet3 = wet3 + np.roll(np.roll(water, dy, 0), dx, 1)
    # How far the open sea is: 1 next to the cell, up to REACH across
    # a band of shallows (the arrival cells then run out to it).
    reach = np.where(near >= 64, 1, 0)
    open_ = water >= 64
    grow = open_.copy()
    for r in range(2, REACH + 1):
        g = grow.copy()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                g |= np.roll(np.roll(grow, dy, 0), dx, 1)
        grow = g
        reach = np.where((reach == 0) & grow, r, reach)
    # Small islands are a few cells with some land each: any cell an
    # eighth land will do, one at least half land is preferred.
    coastal = (land >= SIDE * SIDE // 8) & (reach > 0)
    small = land < SIDE * SIDE // 2
    inland = (land == SIDE * SIDE) & (wet3 == 0)
    for m in (coastal, inland):
        m[:2, :] = m[-2:, :] = False
        m[:, :2] = m[:, -2:] = False
    co = _coexist()
    keys = sorted(LINEAGE, key=lambda k: (-_importance(k), LINEAGE[k][0]))
    ys, xs = np.mgrid[0:H, 0:W]
    out = {}
    for key in keys:
        tid, lon, lat, inl, _ = LINEAGE[key]
        px, py = _geo.lonlat_to_world(lon, lat)
        fx, fy = px / SIDE - 0.5, py / SIDE - 0.5
        ok = (inland if inl else coastal).copy()
        if not inl:
            # An islet the map's tiles drew as shallows keeps its town
            # there: the house tile gives it land again.
            cx, cy = int(fx + 0.5), int(fy + 0.5)
            win = (slice(cy - 1, cy + 2), slice(cx - 1, cx + 2))
            if not (land[win] >= SIDE * SIDE // 8).any():
                ok[win] |= (water[win] < SIDE * SIDE) & (reach[win] > 0)
        for other, (ox, oy, _) in out.items():
            if (key, other) in co:
                ok[max(oy - SPACING + 1, 0):oy + SPACING,
                   max(ox - SPACING + 1, 0):ox + SPACING] = False
        d = np.hypot(xs - fx, ys - fy)
        if not inl:
            d = d + np.where(small, 1.0, 0.0) + 0.5 * (reach - 1)
        d[~ok] = np.inf
        i = int(np.argmin(d))
        if not np.isfinite(d.flat[i]):
            raise SystemExit("towns: no cell for %s" % key)
        out[key] = (i % W, i // W, float(d.flat[i]))
    _memo["place"] = out
    return out


def _tile_cost(grid, x, y, tiles):
    """The cost of each of "tiles" at cell (x, y) of "grid": its match
    with the geography plus its seams with the four neighbours."""
    s = world_map.solved()
    m = s["masks"]
    block = s["raster"][y * SIDE:(y + 1) * SIDE, x * SIDE:(x + 1) * SIDE]
    t = np.asarray(tiles)
    cost = world_map.COST[m[t], block[None]].sum(axis=(1, 2))
    seam = np.zeros(len(t), dtype=np.float64)
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        nb = m[grid[y + dy, x + dx]]
        if dx == 1:
            e = world_map.COST[m[t][:, :, -1], nb[None, :, 0]]
        elif dx == -1:
            e = world_map.COST[m[t][:, :, 0], nb[None, :, -1]]
        elif dy == 1:
            e = world_map.COST[m[t][:, -1, :], nb[None, 0, :]]
        else:
            e = world_map.COST[m[t][:, 0, :], nb[None, -1, :]]
        seam += e.sum(axis=1)
    jitter = world_map.hash_jitter([y * W + x], t)[0]
    return cost + world_map.SEAM * seam + jitter


def _best_tile(grid, x, y, inland):
    tiles = LAND_TILES if inland else COAST_TILES + LAND_TILES
    c = _tile_cost(grid, x, y, tiles)
    return int(tiles[int(np.argmin(c))]), float(c.min())


def _ring(x, y, r):
    """The cells at Chebyshev distance r from (x, y), in a fixed order."""
    return sorted(((x + dx, y + dy) for dy in range(-r, r + 1)
                   for dx in range(-r, r + 1)
                   if max(abs(dx), abs(dy)) == r),
                  key=lambda c: (math.hypot(c[0] - x, c[1] - y), c[1],
                                 c[0]))


def heading_of(dx, dy):
    """0-255, 0 north, clockwise, of a vector with y south."""
    return int(round(math.degrees(math.atan2(dx, -dy)) * 256 / 360)) % 256


def battle_cells(d):
    """The land and fort battle cells from the direction "d" (8-way,
    x east, y south) of the town's water: the town and its fort on the
    land side, the attackers coming from the water side."""
    dx, dy = d
    c = 7

    def at(k):
        return [min(max(c + k * dx, 1), 13), min(max(c + k * dy, 1), 13)]
    fort = at(-3)
    ship = at(5)
    return {"land": at(-4), "party": at(5), "fort": fort,
            "ship": ship + [heading_of(fort[0] - ship[0],
                                       fort[1] - ship[1])]}


def _era(year):
    """The era's town records (JSON), map tiles and checks."""
    if ("era", year) in _memo:
        return _memo[("era", year)]
    grid, land, _ = _base()
    grid = grid.copy()
    place = placement()
    rows = era_rows(year)
    tiles = {}
    # First houses, then the cities' second houses.
    for r in rows:
        x, y, _ = place[r["key"]]
        inl = LINEAGE[r["key"]][3]
        t, _ = _best_tile(grid, x, y, inl)
        grid[y, x] = t
        tiles[r["key"]] = [(x, y, t)]
    taken = {}
    for r in rows:
        x, y, _ = place[r["key"]]
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                taken[(x + dx, y + dy)] = r["key"]
    for r in rows:
        if r["citizens"] < CITY:
            continue
        x, y, _ = place[r["key"]]
        inl = LINEAGE[r["key"]][3]
        best = None
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if land[ny, nx] < SIDE * SIDE * 3 // 4:
                continue
            # Its 3 x 3 must not reach another town's cells.
            clash = any(taken.get((nx + a, ny + b), r["key"]) != r["key"]
                        for a in (-1, 0, 1) for b in (-1, 0, 1))
            if clash:
                continue
            t, c = _best_tile(grid, nx, ny, inl)
            if best is None or c < best[0]:
                best = (c, nx, ny, t)
        if best is not None:
            _, nx, ny, t = best
            grid[ny, nx] = t
            tiles[r["key"]].append((nx, ny, t))
            for a in (-1, 0, 1):
                for b in (-1, 0, 1):
                    taken[(nx + a, ny + b)] = r["key"]
    water = per_cell(main_water(grid))
    owner = {}
    arrival = {}
    for r in rows:
        cells = []
        for x, y, _ in tiles[r["key"]]:
            for c in [(x, y)] + _ring(x, y, 1):
                if c not in cells:
                    cells.append(c)
        arrival[r["key"]] = cells
        for c in cells:
            assert owner.get(c, r["key"]) == r["key"], (year, r["key"], c)
            owner[c] = r["key"]
    problems = []
    for r in rows:
        key = r["key"]
        if LINEAGE[key][3]:
            continue
        cells = arrival[key]
        if not any(water[cy, cx] >= 64 for cx, cy in cells):
            # Across the shallows: the shortest run of free cells to one
            # with open sea joins the arrival cells.
            for c in _path_to_sea(water, owner, key, cells,
                                  ARRIVALS - len(cells)):
                cells.append(c)
                owner[c] = key
        if not any(water[cy, cx] >= 64 for cx, cy in cells):
            problems.append("%s: no open sea at its arrival cells" % key)
    shoal = per_cell(world_map.terrain_of(grid) == SHALLOWS) > 0
    for r in rows:
        near = [(x, y) for x, y, _ in tiles[r["key"]]] + \
            arrival[r["key"]]
        for cx, cy in near:
            win = shoal[max(cy - HARBOUR, 0):cy + HARBOUR + 1,
                        max(cx - HARBOUR, 0):cx + HARBOUR + 1]
            if win.any():
                problems.append("%s: shallows within %d cells of its "
                                "cell %d,%d" % (r["key"], HARBOUR, cx, cy))
                break
    outside = _outside(water, owner)
    # Every port is reached from the open sea without sailing through
    # another town's arrival cells: a ship that enters them stops at
    # that town and leaves by its sea cell, so they would bar the way
    # (Gibraltar's, past Maracaibo on the strait).
    for r in rows:
        key = r["key"]
        if LINEAGE[key][3]:
            continue
        if not any(0 <= cx + dx < W and 0 <= cy + dy < H and
                   outside[cy + dy, cx + dx]
                   for cx, cy in arrival[key] for dx, dy in DIRS):
            problems.append("%s: only reached through another town's "
                            "arrival cells" % key)
    records = []
    sea_cells = {}
    for r in rows:
        key = r["key"]
        tid, _, _, inl, _ = LINEAGE[key]
        x, y, _ = place[key]
        if inl:
            sea, d = [0, 0, 0], _inland_dir(water, x, y)
        else:
            sea, d = _sea_cell(water, owner, x, y, arrival[key],
                               outside)
            if sea is None:
                problems.append("%s: no open sea to leave by" % key)
                sea, d = [0, 0, 0], (0, 1)
            sea_cells[key] = sea
        cells = battle_cells(d)
        flags = r["trade"] | (INLAND if inl else 0)
        rec = {"name": r["name"], "id": tid, "nation": r["nation"],
               "x": x, "y": y, "flags": flags, "forts": r["forts"],
               "soldiers": r["soldiers"], "citizens": r["citizens"],
               "gold": r["gold"], "wealth": r["wealth"], "standing": 0,
               "garrison": 0, "governor": _governor(tid, year),
               "family": 0, "barmaid": 0,
               "land": cells["land"], "fort": cells["fort"],
               "ship": cells["ship"], "party": cells["party"],
               "sea": sea,
               "citymap": [int(x * 0.75 + 0.5), int(y * 0.75 + 0.5)],
               "arrival": [list(c) for c in arrival[key]]}
        records.append(rec)
    index = {r["key"]: i for i, r in enumerate(rows)}
    fleet = [index[k] for k in FLEET if k in index]
    train = [index[k] for k in TRAIN if k in index]
    out = {"records": records, "fleet": fleet, "train": train,
           "grid": grid, "tiles": tiles, "problems": problems,
           "water": water, "arrival": arrival, "rows": rows}
    _memo[("era", year)] = out
    return out


def _path_to_sea(water, owner, key, cells, most):
    """The cells from "cells" to the nearest cell with open sea, by
    8-way steps through cells no other town owns: at most "most"."""
    from collections import deque
    prev = dict.fromkeys(cells)
    q = deque(cells)
    while q:
        c = q.popleft()
        if water[c[1], c[0]] >= 64:
            path = []
            while c not in cells:
                path.append(c)
                c = prev[c]
            path.reverse()
            return path if len(path) <= most else []
        for dx, dy in DIRS:
            n = (c[0] + dx, c[1] + dy)
            if n in prev or not (0 <= n[0] < W and 0 <= n[1] < H):
                continue
            if owner.get(n, key) != key:
                continue
            prev[n] = c
            q.append(n)
    return []


def _governor(tid, year):
    h = (tid * 0x9E3779B1 + year * 0x85EBCA77) & 0xFFFFFFFF
    h ^= h >> 15
    h = (h * 0x2C1B3C6D) & 0xFFFFFFFF
    h ^= h >> 12
    return h % 16


def _full_sea(water, x, y):
    return 0 <= x < W and 0 <= y < H and water[y, x] == SIDE * SIDE


def _outside(water, owner):
    """The cells with open sea (64 main water pixels or more, as the
    sea routes) joined to the open sea, 8-way, without crossing any
    town's arrival cells."""
    from collections import deque
    ox, oy = _geo.lonlat_to_world(*OPEN_SEA)
    start = (int(ox) // SIDE, int(oy) // SIDE)
    out = np.zeros((H, W), dtype=bool)
    out[start[1], start[0]] = True
    q = deque([start])
    while q:
        cx, cy = q.popleft()
        for dx, dy in DIRS:
            nx, ny = cx + dx, cy + dy
            if not (0 <= nx < W and 0 <= ny < H) or out[ny, nx]:
                continue
            if water[ny, nx] < 64 or (nx, ny) in owner:
                continue
            out[ny, nx] = True
            q.append((nx, ny))
    return out


def _sea_cell(water, owner, x, y, mine, outside):
    """The nearest cell of open sea outside every arrival cell, reached
    from the town's arrival cells ("mine") through cells with main
    water, and the 8-way heading out from it that keeps longest to open
    sea, away from the town."""
    from collections import deque
    seen = dict.fromkeys(mine, 0)
    q = deque(mine)
    found = []
    while q:
        c = q.popleft()
        d = seen[c]
        if d > 8:
            break
        for dx, dy in DIRS:
            n = (c[0] + dx, c[1] + dy)
            if n in seen or not (0 <= n[0] < W and 0 <= n[1] < H):
                continue
            if water[n[1], n[0]] == 0:
                continue
            seen[n] = d + 1
            q.append(n)
            if _full_sea(water, *n) and n not in owner:
                found.append((d + 1, math.hypot(n[0] - x, n[1] - y),
                              n[1], n[0]))
    if not found:
        return None, None
    # Toward the open sea, not into a lake behind the town.
    if any(outside[sy, sx] for _, _, sy, sx in found):
        found = [f for f in found if outside[f[2], f[3]]]
    # Of the nearest few, the first with a clear run out to sea (a
    # straight course of RUN_OK cells of open sea); else the longest.
    best = None
    for rank, (_, _, sy, sx) in enumerate(sorted(found)[:24]):
        ax, ay = sx - x, sy - y
        for k, (dx, dy) in enumerate(DIRS):
            run = _run(water, sx, sy, dx, dy)
            away = (dx * ax + dy * ay) / (math.hypot(dx, dy) *
                                          max(math.hypot(ax, ay), 1e-9))
            score = (min(run, RUN_OK), -rank, away)
            if best is None or score > best[0]:
                best = (score, sx, sy, k)
    _, sx, sy, k = best
    ax, ay = sx - x, sy - y
    d = (int(np.sign(ax)), int(np.sign(ay)))
    if d == (0, 0):
        d = DIRS[k]
    return [sx, sy, k], d


def _run(water, x, y, dx, dy):
    """Cells of open sea straight on from (x, y): a diagonal step also
    needs the two cells beside it open."""
    run = 0
    while run < RUN_OK:
        nx, ny = x + dx * (run + 1), y + dy * (run + 1)
        if not _full_sea(water, nx, ny):
            break
        if dx and dy and not (_full_sea(water, nx - dx, ny) and
                              _full_sea(water, nx, ny - dy)):
            break
        run += 1
    return run


def _inland_dir(water, x, y):
    """The 8-way direction from an inland town to the nearest open sea."""
    ys, xs = np.nonzero(water >= 64)
    i = int(np.argmin((xs - x) ** 2 + (ys - y) ** 2))
    dx, dy = xs[i] - x, ys[i] - y
    k = int(round(math.atan2(dx, -dy) / (math.pi / 4))) % 8
    return DIRS[k]


# --- Checks ------------------------------------------------------------

def sea_steps(year, limit=800):
    """Sea route steps (8-way cells with open sea of the main water)
    between the era's coastal towns: {(a, b): steps}, table keys, from
    any arrival cell of a to the first arrival cell of b.  At the top
    speed (6 cells per 64 ticks) and data/calendar's day of 64 ticks,
    6 steps are a day."""
    from collections import deque
    e = _era(year)
    passable = e["water"] >= 64
    goal = {}
    for k, cells in e["arrival"].items():
        if not LINEAGE[k][3]:
            for c in cells:
                goal[c] = k
    out = {}
    for k, cells in e["arrival"].items():
        if LINEAGE[k][3]:
            continue
        seen = dict.fromkeys(cells, 0)
        q = deque(cells)
        while q:
            c = q.popleft()
            d = seen[c]
            if d >= limit:
                break
            for dx, dy in DIRS:
                n = (c[0] + dx, c[1] + dy)
                if n in seen or not (0 <= n[0] < W and 0 <= n[1] < H):
                    continue
                g = goal.get(n)
                if g is not None and g != k:
                    out.setdefault((k, g), d + 1)
                # A ship sails on through other towns' cells (into the
                # Lake of Maracaibo past Maracaibo, say).
                if passable[n[1], n[0]] or g is not None:
                    seen[n] = d + 1
                    q.append(n)
    return out


def report(year):
    e = _era(year)
    place = placement()
    lines = []
    count = {}
    for r in e["rows"]:
        count[r["nation"]] = count.get(r["nation"], 0) + 1
    lines.append("%d: %d towns %s" % (year, len(e["rows"]), count))
    for r, rec in zip(e["rows"], e["records"]):
        lines.append("  %-17s %-8s cell %3d,%3d moved %.1f tiles %s "
                     "sea %s" % (rec["name"], rec["nation"], rec["x"],
                                 rec["y"], place[r["key"]][2],
                                 [t for _, _, t in e["tiles"][r["key"]]],
                                 rec["sea"]))
    lines += ["  PROBLEM " + p for p in e["problems"]]
    return "\n".join(lines)


# --- Output ------------------------------------------------------------

def era_tiles(year):
    """The era's town tiles: [(x, y, tile)]."""
    e = _era(year)
    return [t for r in e["rows"] for t in e["tiles"][r["key"]]]


def build(year):
    check_tables()
    e = _era(year)
    if e["problems"]:
        raise SystemExit("towns %d: %s" % (year, "; ".join(e["problems"])))
    data = {"towns": e["records"], "treasure_fleet": e["fleet"],
            "silver_train": e["train"]}
    parts = __doc__.split("\n\n")[1:]
    text = " ".join(" ".join(p.split()) for p in parts)
    count = {}
    for r in e["rows"]:
        count[r["nation"]] = count.get(r["nation"], 0) + 1
    notes = "%s This era: %d towns (%s)." % (
        text, len(e["rows"]), ", ".join("%s %d" % (n, count[n])
                                        for n in NATIONS if n in count))
    return data, notes


def builder(year):
    return lambda: build(year)


TABLES = [("data/towns/%d" % y, "towns-%d.json" % y, builder(y))
          for y in YEARS]


if __name__ == "__main__":
    check_tables()
    for y in YEARS:
        print(report(y))
