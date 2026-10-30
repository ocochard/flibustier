#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""data/treasure: the 256 map cells where a treasure map's spot may lie.

Written for the open pack from real geography; nothing comes from the
original game.  How the engine uses them (src/sim/treasure.c,
src/ui/screens.c): a new map takes a random spot whose nearest town (by
the era's arrival cells) is neither the player's town nor another map's,
and names that town ("Somewhere near ...").  The map shows the 40 x 30
cells around the spot.  A party ashore digs it up when it stands within
one cell of it, both ways (|dx| < 2 and |dy| < 2 cells); the party walks
on land pixels only, from where its ship touched the shore.  One table
serves every era.

Where.  The places the rovers of 1560-1680 and their legends used: small
cays and islands, headlands, river mouths and lagoon shores away from
the settled ports.  ANCHORS below names each one with real coordinates
(degrees, modern) and how many spots it gets: the Florida Keys and
capes, the Gulf coast (Barataria, Sabine Pass, the Tamaulipas and
Veracruz lagoons, Laguna de Terminos), Yucatan's capes and Cozumel, the
Belize cays and the Bay Islands (Roatan, Utila, Guanaja), the Mosquito
coast (Cabo Gracias a Dios, the Pearl Cays, Bluefields' lagoons, Bocas
del Toro), the San Blas islands and the Darien, the Main's capes, the
Dutch islands, the Venezuelan islands and the Paria coast, Trinidad, the
Grenadines and the Windwards, Barbuda, Anegada and St Croix, Puerto
Rico's islets (Mona, Desecheo, Vieques, Caja de Muertos), around
Hispaniola (Saona, Samana, La Isabela, Tortuga's west end, Gonave,
Ile-a-Vache, the Tiburon peninsula), Cuba's cays and bays (Isla de
Pinos, Jardines de la Reina, Cayo Romano, the Colorados), Jamaica's
capes and bays (Negril, Morant Point), the Caymans, the Bahamas and the
Caicos, and the Atlantic shore from Biscayne Bay to Cape Fear.  Bigger or more storied haunts get more spots
(Isla de Pinos 5, Andros 4). The tiniest islets (the Virgin islands but
Anegada and St Croix, Saba, Navassa, the lower Keys and the Dry
Tortugas, Turneffe, Cayo Largo, Los Testigos, San Andres, the Swan
islands) are left out: the open map's 16-pixel cells, about 7 km, draw
them as a few pixels of land that no cell can hold as a dig site, and
their spots went to the larger islands' shores.

Snapping.  Each anchor goes through _geo.lonlat_to_world() and takes the
fitting cells of the map nearest to it, up to 7 cells (50 km) away, as
many as its count; the anchors with the fewest fitting cells per spot
choose first, so a lone cay keeps its cell before a neighbour's coast
spreads over it.  A cell fits when, in every era's map (the bare map,
world_map.base_grid(), with that era's town houses drawn on it,
tables/towns.py): an eighth of its pixels or more are land a landing
party can stand on (land 4-connected to sea that is itself joined to
the open sea without crossing shallows, so any ship reaches the shore
without passing a reef), as the towns' coastal cells (the map's tiles
draw a small cay as a corner of land, never a whole cell), a cell less
than half land counting one cell farther, so a bigger island's solid
ground wins over its fringe; a cell with open sea of the
main water (64 pixels or more, as the towns' reach) lies within 3 cells,
so the spot is a short walk from where a ship can come in and the map's
40 x 30 cells always show a shoreline; it is 2 cells or more (Chebyshev)
from every arrival cell of every era's towns, so the spot is near a
named town but never in it and a party digging there never walks into a
town; it is 3 cells or more from every other spot, so no two dig areas
(the 3 x 3 cells around a spot) overlap; and it keeps 6 cells from the
map's edge.
"""
import math

import numpy as np

import _geo
import towns
import world_map

W, H, SIDE = world_map.W, world_map.H, world_map.SIDE
SPOTS = 256
SNAP = 7            # cells from an anchor to its spots, at most
REACH = 3           # cells from a spot to open sea of the main water
TOWN_GAP = 2        # cells from every era's arrival cells, at least
SPACING = 3         # cells between two spots (Chebyshev), at least
MARGIN = 6          # cells from the map's edge
LAND_MIN = SIDE * SIDE // 8     # standable land pixels in a spot's cell
HALF = SIDE * SIDE // 2
OPEN = 64           # main water pixels that make a cell open sea

# (place, lon, lat, spots)
ANCHORS = [
    # Florida, the Keys and the Gulf coast.
    ("Key Largo", -80.45, 25.10, 2),
    ("Matecumbe Keys", -80.63, 24.92, 1),
    ("Vaca Key", -81.05, 24.72, 1),
    ("Cape Sable", -81.15, 25.15, 1),
    ("Ten Thousand Islands", -81.55, 25.85, 2),
    ("Estero Bay", -81.90, 26.40, 1),
    ("Gasparilla", -82.25, 26.75, 2),
    ("Tampa Bay", -82.75, 27.60, 1),
    ("Cedar Keys", -83.03, 29.13, 1),
    ("Anclote Keys", -82.85, 28.20, 1),
    ("Apalachee Bay", -84.20, 30.05, 1),
    ("St George Island", -84.90, 29.65, 1),
    ("Cape San Blas", -85.37, 29.67, 1),
    ("Santa Rosa Island", -87.00, 30.35, 1),
    ("Dauphin Island", -88.15, 30.25, 1),
    ("Mississippi passes", -89.25, 29.15, 1),
    ("Barataria", -89.95, 29.27, 2),
    ("Marsh Island", -91.90, 29.50, 1),
    ("Galveston Island", -94.80, 29.30, 1),
    ("Sabine Pass", -93.85, 29.70, 1),
    ("Matagorda", -96.35, 28.25, 1),
    ("Padre Island", -97.20, 27.00, 1),
    ("Soto la Marina", -97.75, 23.75, 1),
    ("Laguna Madre", -97.50, 24.50, 1),
    ("Cabo Rojo de Veracruz", -97.35, 21.55, 1),
    ("Alvarado", -95.75, 18.77, 1),
    ("Tuxpan", -97.35, 20.95, 1),
    ("Coatzacoalcos", -94.42, 18.15, 1),
    ("Dos Bocas", -93.20, 18.42, 1),
    ("Isla del Carmen", -91.60, 18.75, 2),
    ("Champoton", -90.72, 19.35, 1),
    ("Celestun", -90.40, 20.85, 1),
    ("Sisal", -90.05, 21.17, 1),
    ("Rio Lagartos", -88.15, 21.60, 1),
    ("Alacranes", -89.70, 22.50, 1),
    # Yucatan, Belize and the Bay of Honduras.
    ("Cabo Catoche", -87.10, 21.60, 1),
    ("Isla Holbox", -87.30, 21.55, 1),
    ("Isla Mujeres", -86.73, 21.23, 1),
    ("Cozumel", -86.95, 20.45, 3),
    ("Tulum", -87.45, 20.20, 1),
    ("Bahia de la Ascension", -87.60, 19.70, 1),
    ("Chetumal Bay", -88.00, 18.50, 1),
    ("Banco Chinchorro", -87.35, 18.60, 1),
    ("Ambergris Caye", -87.95, 18.00, 1),
    ("South Water Caye", -88.08, 16.80, 1),
    ("Punta Gorda", -88.80, 16.10, 1),
    ("Roatan", -86.55, 16.35, 2),
    ("Utila", -86.90, 16.10, 1),
    ("Guanaja", -85.88, 16.45, 2),
    ("Cayos Cochinos", -86.50, 15.95, 1),
    ("Cabo Tres Puntas", -88.60, 15.97, 1),
    ("Rio Aguan", -85.65, 15.95, 1),
    # The Mosquito coast to the Darien.
    ("Cabo Camaron", -85.00, 16.00, 1),
    ("Laguna de Brus", -84.60, 15.85, 1),
    ("Rio Patuca", -84.30, 15.80, 1),
    ("Laguna de Caratasca", -83.80, 15.30, 1),
    ("Cabo Gracias a Dios", -83.20, 15.00, 2),
    ("Miskito Cays", -82.75, 14.40, 1),
    ("Pearl Cays", -83.50, 12.40, 2),
    ("Rio Grande de Matagalpa", -83.55, 12.90, 1),
    ("Monkey Point", -83.65, 11.60, 1),
    ("San Juan del Norte", -83.70, 10.93, 1),
    ("Tortuguero", -83.55, 10.55, 1),
    ("Cahuita", -82.83, 9.73, 1),
    ("Limon", -83.03, 10.00, 1),
    ("Bocas del Toro", -82.25, 9.35, 3),
    ("Chiriqui Lagoon", -81.90, 8.95, 1),
    ("Escudo de Veraguas", -81.55, 9.10, 1),
    ("Rio Belen", -80.85, 8.90, 1),
    ("San Blas islands", -78.60, 9.55, 3),
    ("Caledonia Bay", -77.65, 8.90, 1),
    ("Gulf of Uraba", -76.90, 8.40, 1),
    ("Isla Fuerte", -76.18, 9.38, 1),
    ("San Bernardo islands", -75.85, 9.75, 1),
    # The Main, the Dutch islands and the Venezuelan islands.
    ("Bocas de Ceniza", -74.85, 11.10, 1),
    ("Cienaga Grande", -74.45, 11.00, 1),
    ("Tayrona", -73.95, 11.32, 1),
    ("Cabo de la Vela", -72.15, 12.20, 1),
    ("Punta Gallinas", -71.68, 12.45, 1),
    ("Paraguana", -70.20, 11.95, 1),
    ("Aruba", -70.00, 12.50, 2),
    ("Klein Curacao", -69.10, 12.35, 1),
    ("Bonaire", -68.40, 12.30, 1),
    ("Morrocoy", -68.25, 10.90, 1),
    ("Puerto Cabello coast", -68.00, 10.48, 1),
    ("Los Roques", -66.70, 11.85, 1),
    ("Cabo Codera", -66.05, 10.48, 1),
    ("Piritu islands", -65.00, 10.20, 1),
    ("La Tortuga", -65.30, 10.95, 1),
    ("La Blanquilla", -64.60, 11.85, 1),
    ("Cubagua", -64.20, 10.80, 1),
    ("Margarita", -63.90, 11.15, 2),
    ("Araya", -63.95, 10.55, 1),
    ("Carupano", -63.25, 10.67, 1),
    ("Paria", -62.50, 10.70, 1),
    ("Chacachacare", -61.75, 10.68, 1),
    ("Icacos Point", -61.92, 10.06, 1),
    ("Toco", -60.95, 10.83, 1),
    ("Manzanilla", -61.00, 10.50, 1),
    ("Orinoco mouths", -61.00, 8.70, 1),
    # The Windwards and the Grenadines.
    ("Carriacou", -61.45, 12.48, 1),
    ("Tobago Cays", -61.36, 12.62, 1),
    ("Mustique", -61.19, 12.88, 1),
    ("Bequia", -61.24, 13.00, 1),
    ("Le Diamant", -61.03, 14.45, 1),
    ("Caravelle", -60.88, 14.77, 1),
    ("Prince Rupert Bay", -61.46, 15.58, 1),
    ("Dominica windward", -61.25, 15.40, 1),
    ("Marie-Galante", -61.27, 15.93, 1),
    ("Les Saintes", -61.60, 15.86, 1),
    ("La Desirade", -61.07, 16.32, 1),
    # The Leewards and the Virgin islands.
    ("Barbuda", -61.80, 17.63, 1),
    ("Anegada", -64.33, 18.73, 1),
    ("St Croix", -64.75, 17.72, 1),
    ("Vieques", -65.45, 18.12, 2),
    # Puerto Rico.
    ("Fajardo", -65.63, 18.33, 1),
    ("Arecibo", -66.70, 18.48, 1),
    ("Desecheo", -67.48, 18.38, 1),
    ("Mona", -67.90, 18.08, 1),
    ("Cabo Rojo", -67.18, 17.95, 1),
    ("Guanica", -66.90, 17.95, 1),
    ("Rincon", -67.25, 18.37, 1),
    ("Isabela", -67.00, 18.50, 1),
    ("Maunabo", -65.90, 18.00, 1),
    ("Caja de Muertos", -66.52, 17.90, 1),
    # Hispaniola.
    ("Cabo Engano", -68.33, 18.60, 1),
    ("Saona", -68.70, 18.15, 2),
    ("Catalina", -69.00, 18.36, 1),
    ("Bahia de Ocoa", -70.60, 18.30, 1),
    ("San Pedro de Macoris", -69.30, 18.42, 1),
    ("Barahona", -71.05, 18.20, 1),
    ("Samana", -69.40, 19.20, 2),
    ("Cabrera", -69.90, 19.65, 1),
    ("La Isabela", -71.05, 19.90, 1),
    ("Fort Liberte Bay", -71.85, 19.68, 1),
    ("Tortuga west end", -72.95, 20.07, 1),
    ("Mole Saint-Nicolas", -73.40, 19.80, 1),
    ("Gonave", -73.00, 18.85, 3),
    ("Cayemites", -73.85, 18.62, 1),
    ("Cap Dame-Marie", -74.42, 18.60, 1),
    ("Cap Tiburon", -74.45, 18.32, 1),
    ("Ile-a-Vache", -73.65, 18.07, 2),
    ("Jacmel coast", -72.50, 18.20, 1),
    ("Aquin Bay", -73.40, 18.25, 1),
    ("Isla Beata", -71.55, 17.58, 1),
    # Cuba.
    ("Isla de Pinos", -82.85, 21.70, 5),
    ("Bay of Pigs", -81.15, 22.10, 1),
    ("Zapata coast", -81.60, 22.00, 1),
    ("Jagua Bay", -80.45, 22.05, 1),
    ("Jardines de la Reina", -78.90, 20.80, 2),
    ("Gulf of Guacanayabo", -77.60, 20.60, 1),
    ("Cabo Cruz", -77.73, 19.83, 1),
    ("Manzanillo coast", -77.15, 20.35, 1),
    ("Gulf of Ana Maria", -78.60, 21.30, 1),
    ("Santa Cruz del Sur", -77.98, 20.70, 1),
    ("Cayo Guajaba", -77.75, 21.85, 1),
    ("Guantanamo Bay", -75.15, 19.95, 1),
    ("Punta Maisi", -74.15, 20.25, 1),
    ("Nipe Bay", -75.70, 20.75, 1),
    ("Cayo Moa", -74.90, 20.68, 1),
    ("Cabo Lucrecia", -75.62, 21.07, 1),
    ("Gibara", -76.13, 21.11, 1),
    ("Puerto Padre", -76.60, 21.20, 1),
    ("Cayo Sabinal", -77.30, 21.68, 1),
    ("Cayo Romano", -78.40, 22.50, 2),
    ("Cayo Santa Maria", -79.10, 22.65, 1),
    ("Cayo Cruz del Padre", -80.00, 23.10, 1),
    ("Hicacos", -81.20, 23.15, 1),
    ("Bahia Honda", -83.15, 22.95, 1),
    ("Cayo Levisa", -83.50, 22.90, 1),
    ("Cayo Jutias", -83.95, 22.72, 1),
    ("Cayos de San Felipe", -83.55, 21.95, 1),
    ("Cabo Corrientes", -84.50, 21.75, 1),
    ("Cabo San Antonio", -84.95, 21.87, 1),
    # Jamaica and the islands south of Cuba.
    ("Negril", -78.35, 18.27, 2),
    ("Montego Bay", -77.92, 18.47, 1),
    ("Lucea", -78.17, 18.45, 1),
    ("Falmouth", -77.65, 18.50, 1),
    ("St Ann's Bay", -77.20, 18.44, 1),
    ("Port Antonio", -76.45, 18.18, 1),
    ("Annotto Bay", -76.77, 18.27, 1),
    ("Morant Point", -76.18, 17.92, 1),
    ("Portland Point", -77.18, 17.74, 1),
    ("Pedro Bluff", -77.70, 17.85, 1),
    ("Black River", -77.85, 18.02, 1),
    ("Bluefields Bay", -78.03, 18.17, 1),
    ("Grand Cayman", -81.25, 19.32, 2),
    ("Little Cayman", -80.05, 19.69, 1),
    # The Bahamas and the Turks and Caicos.
    ("Grand Bahama", -78.30, 26.65, 2),
    ("Abaco", -77.10, 26.50, 3),
    ("Bimini", -79.28, 25.73, 1),
    ("Berry Islands", -77.80, 25.60, 1),
    ("Andros", -77.90, 24.70, 4),
    ("Spanish Wells", -76.75, 25.55, 1),
    ("Eleuthera south", -76.20, 24.80, 1),
    ("Cat Island", -75.50, 24.30, 1),
    ("Great Exuma", -75.85, 23.55, 1),
    ("San Salvador", -74.50, 24.05, 1),
    ("Rum Cay", -74.85, 23.67, 1),
    ("Long Island", -75.10, 23.20, 1),
    ("Crooked Island", -74.20, 22.70, 1),
    ("Acklins", -73.95, 22.30, 1),
    ("Mayaguana", -73.00, 22.38, 1),
    ("Great Inagua", -73.50, 21.05, 2),
    ("Little Inagua", -72.95, 21.50, 1),
    ("Caicos", -72.00, 21.80, 2),
    # The Atlantic shore.
    ("Biscayne Bay", -80.20, 25.45, 1),
    ("Jupiter Inlet", -80.07, 26.95, 1),
    ("Cape Canaveral", -80.60, 28.45, 1),
    ("Sebastian Inlet", -80.45, 27.85, 1),
    ("Mosquito Inlet", -80.92, 29.08, 1),
    ("Amelia Island", -81.45, 30.60, 1),
    ("St Catherines Island", -81.15, 31.63, 1),
    ("Edisto", -80.30, 32.50, 1),
    ("Cape Romain", -79.40, 33.00, 1),
    ("Winyah Bay", -79.20, 33.20, 1),
    ("Cape Fear", -77.95, 33.85, 1),
]


# --- Geometry -----------------------------------------------------------

def _components(mask, seeds):
    """The pixels of "mask" 4-connected to a pixel of "seeds"."""
    h = mask.shape[0]
    rs, cs, ce = towns._runs(mask)
    if len(rs) == 0:
        return np.zeros_like(mask)
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
    lab = np.arange(len(rs))
    if pa:
        a = np.concatenate(pa)
        b = np.concatenate(pb)
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
    # Runs holding a seed pixel.
    cum = np.zeros((h, mask.shape[1] + 1), dtype=np.int64)
    cum[:, 1:] = np.cumsum(seeds & mask, axis=1)
    seeded = cum[rs, ce] - cum[rs, cs] > 0
    keep = np.isin(lab, np.unique(lab[seeded]))
    out = np.zeros((h, mask.shape[1] + 1), dtype=np.int32)
    np.add.at(out, (rs[keep], cs[keep]), 1)
    np.add.at(out, (rs[keep], ce[keep]), -1)
    return np.cumsum(out, axis=1)[:, :-1] > 0


def _shift_or(m):
    """Pixels 4-adjacent to a pixel of "m"."""
    o = np.zeros_like(m)
    o[1:, :] |= m[:-1, :]
    o[:-1, :] |= m[1:, :]
    o[:, 1:] |= m[:, :-1]
    o[:, :-1] |= m[:, 1:]
    return o


def _grow(m, r):
    """Cells within Chebyshev distance r of a cell of "m"."""
    out = m.copy()
    for _ in range(r):
        g = out.copy()
        g[1:, :] |= out[:-1, :]
        g[:-1, :] |= out[1:, :]
        out = g.copy()
        out[:, 1:] |= g[:, :-1]
        out[:, :-1] |= g[:, 1:]
    return out


_memo = {}


def era_fit(year):
    """Per cell of the era's map: (standable land pixels, open sea
    within REACH, arrival cells)."""
    if year in _memo:
        return _memo[year]
    e = towns._era(year)
    t = world_map.terrain_of(e["grid"])
    land = t == world_map.LAND
    ox, oy = (int(v) for v in _geo.lonlat_to_world(*towns.OPEN_SEA))
    seed = np.zeros_like(land)
    seed[oy, ox] = True
    water = _components(t == world_map.SEA, seed)
    stand = _components(land, _shift_or(water))
    stand_n = towns.per_cell(stand)
    near_sea = _grow(e["water"] >= OPEN, REACH)
    arrival = np.zeros((H, W), dtype=bool)
    for cells in e["arrival"].values():
        for x, y in cells:
            arrival[y, x] = True
    _memo[year] = (stand_n, near_sea, arrival)
    return _memo[year]


def fitting():
    """The cells that fit a spot in every era."""
    ok = np.ones((H, W), dtype=bool)
    for year in world_map.YEARS:
        stand_n, near_sea, arrival = era_fit(year)
        ok &= (stand_n >= LAND_MIN) & near_sea & \
            ~_grow(arrival, TOWN_GAP - 1)
    ok[:MARGIN, :] = ok[-MARGIN:, :] = False
    ok[:, :MARGIN] = ok[:, -MARGIN:] = False
    return ok


# --- Spots --------------------------------------------------------------

def _window(lon, lat):
    """An anchor's cells within SNAP: (slices, xs, ys, distances)."""
    px, py = _geo.lonlat_to_world(lon, lat)
    fx, fy = px / SIDE - 0.5, py / SIDE - 0.5
    cx, cy = int(round(fx)), int(round(fy))
    x0, y0 = max(cx - SNAP, 0), max(cy - SNAP, 0)
    x1, y1 = min(cx + SNAP + 1, W), min(cy + SNAP + 1, H)
    ys, xs = np.mgrid[y0:y1, x0:x1]
    return (slice(y0, y1), slice(x0, x1)), xs, ys, np.hypot(xs - fx,
                                                            ys - fy)


def place():
    """([(x, y, place)] in ANCHORS order, [(place, missing)]): the
    anchors with the fewest fitting cells per spot choose first."""
    ok = fitting()
    small = np.ones((H, W), dtype=bool)
    for year in world_map.YEARS:
        small &= era_fit(year)[0] < HALF
    wins = [_window(lon, lat) for _, lon, lat, _ in ANCHORS]
    order = sorted(range(len(ANCHORS)), key=lambda i: (
        (ok[wins[i][0]] & (wins[i][3] <= SNAP)).sum() / ANCHORS[i][3], i))
    taken = np.zeros((H, W), dtype=bool)
    got = {}
    short = []
    for i in order:
        name, _, _, n = ANCHORS[i]
        sl, xs, ys, d = wins[i]
        cost = d + np.where(small[sl], 1.0, 0.0)
        got[i] = []
        for _ in range(n):
            free = ok[sl] & ~taken[sl] & (d <= SNAP)
            if not free.any():
                short.append((name, n - len(got[i])))
                break
            k = int(np.argmin(np.where(free, cost, np.inf)))
            x, y = int(xs.flat[k]), int(ys.flat[k])
            got[i].append((x, y, name))
            taken[max(y - SPACING + 1, 0):y + SPACING,
                  max(x - SPACING + 1, 0):x + SPACING] = True
    return [s for i in range(len(ANCHORS)) for s in got[i]], short


def spots():
    """[(x, y, place)], in ANCHORS order."""
    if "spots" not in _memo:
        if sum(a[3] for a in ANCHORS) != SPOTS:
            raise SystemExit("treasure: the anchors give %d spots, not %d"
                             % (sum(a[3] for a in ANCHORS), SPOTS))
        out, short = place()
        if short:
            raise SystemExit("treasure: no cell within %d cells for %s"
                             % (SNAP, ", ".join("%s (%d)" % s
                                                for s in short)))
        _memo["spots"] = out
    return _memo["spots"]


def check(sp):
    """Raise SystemExit unless every spot fits every era."""
    problems = []
    if len(sp) != SPOTS:
        problems.append("%d spots" % len(sp))
    cells = [(x, y) for x, y, _ in sp]
    if len(set(cells)) != len(cells):
        problems.append("spots repeat")
    for i, (x, y) in enumerate(cells):
        for (u, v) in cells[i + 1:]:
            if max(abs(u - x), abs(v - y)) < SPACING:
                problems.append("%d,%d and %d,%d closer than %d cells"
                                % (x, y, u, v, SPACING))
        if not (MARGIN <= x < W - MARGIN and MARGIN <= y < H - MARGIN):
            problems.append("%d,%d by the map's edge" % (x, y))
    for year in world_map.YEARS:
        stand_n, near_sea, arrival = era_fit(year)
        ay, ax = np.nonzero(arrival)
        for x, y, place in sp:
            if stand_n[y, x] < LAND_MIN:
                problems.append("%d %s %d,%d: not land a party reaches"
                                % (year, place, x, y))
            if not near_sea[y, x]:
                problems.append("%d %s %d,%d: no open sea within %d cells"
                                % (year, place, x, y, REACH))
            gap = int(np.min(np.maximum(abs(ax - x), abs(ay - y))))
            if gap < TOWN_GAP:
                problems.append("%d %s %d,%d: %d cells from a town's "
                                "arrival cells" % (year, place, x, y, gap))
    if problems:
        raise SystemExit("treasure: " + "; ".join(problems[:20]))


def nearest_town(year, x, y):
    """(name, |dx| + |dy|) of the era's town the engine names for a
    spot: the nearest arrival cell."""
    e = towns._era(year)
    best = None
    for rec in e["records"]:
        for u, v in rec["arrival"]:
            d = abs(u - x) + abs(v - y)
            if best is None or d < best[1]:
                best = (rec["name"], d)
    return best


def stats(sp):
    moved = []
    for x, y, place in sp:
        a = next(a for a in ANCHORS if a[0] == place)
        px, py = _geo.lonlat_to_world(a[1], a[2])
        moved.append(math.hypot(x + 0.5 - px / SIDE, y + 0.5 - py / SIDE))
    far = [nearest_town(yr, x, y)[1] for yr in world_map.YEARS
           for x, y, _ in sp]
    return ("snapped %.1f cells from their anchors on average, %.1f at "
            "most; the named town's arrival cells %d to %d cells away "
            "(|dx| + |dy|, over the eras), median %d"
            % (sum(moved) / len(moved), max(moved), min(far), max(far),
               sorted(far)[len(far) // 2]))


def build():
    sp = spots()
    check(sp)
    parts = __doc__.split("\n\n")[1:]
    text = " ".join(" ".join(p.split()) for p in parts)
    notes = "%s %d anchors; %s." % (text, len(ANCHORS), stats(sp))
    return {"spots": [[x, y] for x, y, _ in sp]}, notes


TABLES = [("data/treasure", "treasure.json", build)]


if __name__ == "__main__":
    sp = spots()
    check(sp)
    print(stats(sp))
    for x, y, place in sp:
        print("%-24s %3d,%3d  near %s" % (place, x, y,
                                          nearest_town(1680, x, y)))
