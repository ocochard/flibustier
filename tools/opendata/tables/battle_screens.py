#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""The battle screens: screens/sea and screens/land (108 screens each of
15 x 15 cells, one of the open battle sheet's 256 tiles per cell) and
data/battle-sets, the sea screens a fleet meets along a coast or in
open sea.

Written for the open pack; every screen is drawn here from the towns'
geography (tables/towns.py) and the open battle sheet's own tiles
(art/battle.png, its terrain masks art/battle-terrain.png at 32 x 32
pixels a tile, and its ground in art/battle.tiles), read from the
directory in TILE_ART, or else from the art/ of the pack opendata.py
writes.  Nothing comes from the original game.

How the engine reads them (src/sim/battle.c, land.c).  A fort battle
is fought on the sea screen of the town's id, with the town's fort and
ship cells (data/towns/<year>); the colour under a ship's centre (its
start cell plus 40, 30 pixels) is sea, shallows (they scrape a hull) or
land (it beaches the ship, or lands the men when the fort is within
reach).  Other sea battles take a set by the land seen from the fleet
(north, east, south, west, or none: open) and a random piece 0-3 of it;
scrolling along the coast steps to the next piece, (piece + scroll) & 3,
and scrolling off it to piece 4.  A land battle is fought on the land
screen of the town's id: the town's men stand at its land cell and on
the clear cells around it, the attackers come ashore at its party cell
and the cells diagonally beside it.  A tile's ground (clear, cover,
swamp, water) sets the troops' speed, cover halves the losses and hides
a unit, water doubles them; nothing blocks the way.

The towns.  Each town's cells follow from the direction of its water
(towns.battle_cells(), the same in every era, which the checks below
confirm): on the sea screen the shore runs across that direction just
seaward of the fort, so the fort tile (123) stands on the land with the
waterline less than a cell beyond it, the town's house (124) at the land
cell behind it and more houses around by the town's size (its most
citizens in any era), and the ship starts on open water with the way to
the fort and to the screen's edge clear.  On the land screen the shore
lies past the party and the men beside it, so they land on clear ground
with the town ahead of them across land, the fort tile again at the
fort cell.  Away from those cells the shore wanders by a seeded noise:
headlands, bays, a river mouth, offshore cays.  Each town also gets a
character from its geography: the Main's and Central America's ports
jungle (woods, a river), the great Spanish ports cleared land and a
bigger town, the large islands' towns mixed woods and fields, the small
islands open ground with cays offshore, the dry coast (Coro, Araya,
Curacao, Bonaire, Margarita) rock and scrub, Florida and the Carolinas
marsh and pine woods with barrier cays, Maracaibo's narrows and
Gibraltar's lake shore marsh and pools (Maracaibo with the far shore of
the strait in the corners), the inland towns no sea at all on their
land screen (Granada its lake), Caracas among rocks, Puerto Principe on
open plain.  The seed is the town's id, so no two towns look alike.

The sets.  Screens 65-89: north 65-69, east 70-74, south 75-79, west
80-84 (pieces 0-3 a coast, piece 4 open water with a few cays), open
85-89 (open sea, no land: the set of a fleet out of sight of land).
Each coast is one strip four screens long, cyclic, drawn and tiled as a
whole and cut into its pieces, so a piece's edge runs on into the next
piece's and the fourth's into the first's; the land keeps to the side
the set is named for, within the outer 5 cells, clear of where the
ships start (the field's middle).  Screens 90-107 are open sea.  The
land screens 65-107 belong to no town; they are open country with a
village, valid but unused.

Tiling.  Each screen (or strip) is designed as a terrain raster first,
one class per pixel (sea, shallows, land; a 6 pixel shallows halo along
every shore, as the art paints it), and a ground plan per cell (plain,
woods, marsh, rock, house, fort).  Cells all land take the plan's tile,
cells all water a sea tile (a few with white caps far from land); the
coast cells take the coast tile (128-254, and near a town the shore
houses 68-79) whose mask best matches the raster, pixel by pixel, and
then, among those within a tolerance, the one whose edges best match
its four neighbours' (iterated conditional modes, as tables/world_map.py
does for the sailing map), so a coastline runs on across tile borders.
A design that fails the checks is drawn again with the next seed.

Checks (the build stops on a failure): every town in every era has the
same cells; on its sea screen the fort cell holds the fort tile, a ship
can reach land within the fort's reach from its start, through water,
and also the screen's edge, the start is open sea, and the land cell
holds a house; on its land screen the land cell holds a house, the
party's three cells are on the field, the party cell clear and the
others dry, joined to the town over dry ground, and the town's men all
find clear cells on the field; each coast piece has land along its
named edge, open sea where the ships start and none on the far half,
and the four pieces join; the open set has no land nor shallows.
"""
import json
import math
import os
import random
from collections import deque

import numpy as np

import tile_terrain
import towns
import world_map

N = 15                  # cells a side
T = 32                  # pixels a cell, in the engine and the masks
FIELD = N * T
SCREENS = 108
TOWN_IDS = 65           # 0-64: tables/towns.py's ids
SET_BASE = 65
SETS = ("north", "east", "south", "west", "open")
ART_TILE = 72           # art/battle.png at art_height 1080
TSJ = "battle.tsj"

SEA, SHALLOWS, LAND = 0, 1, 2
COST = np.array([[0, 1, 4], [1, 0, 2], [4, 2, 0]], dtype=np.float32)
HALO = 6                # shallows pixels along a shore
SEAM = 6.0
TOLERANCE = 0.6         # of the best cost, and at least TOLERANCE_MIN
TOLERANCE_MIN = 160.0
HOUSE_BONUS = 60.0      # a shore house near a town, over a bare coast
SURF_SHARE = 0.12       # white caps among the open sea tiles
TRIES = 12

# The sheet's tiles, by the art's design (checked against the masks and
# the ground on loading).
SEA_TILES = tuple(range(0, 40))
SURF_TILES = tuple(range(40, 60))
PLAIN = tuple(range(60, 68)) + tuple(range(80, 96)) + \
    tuple(range(116, 123)) + (127,)
SHORE_HOUSES = tuple(range(68, 80))
WOODS = tuple(range(96, 112))
MARSH = tuple(range(112, 116))
FORT, TOWN, ROCK, POOLS = 123, 124, 125, 126
HOUSES = SHORE_HOUSES + (TOWN,)
GROUNDS = ("clear", "cover", "swamp", "water")
CLEAR, COVER, SWAMP, WATER = range(4)

# Ground plan codes for the cells all land.
P_PLAIN, P_WOODS, P_MARSH, P_POOLS, P_ROCK, P_TOWN, P_FORT = range(7)

# Characters: woods, marsh, rock (shares of the free land), the shore's
# swing (pixels), cays offshore, a river, a far shore, no sea ashore.
CHARACTERS = {
    "port": dict(woods=0.10, marsh=0.02, rock=0.02, swing=50, cays=1,
                 river=0.5, houses=1),
    "jungle": dict(woods=0.42, marsh=0.06, rock=0.00, swing=70, cays=1,
                   river=1.0, houses=0),
    "island": dict(woods=0.24, marsh=0.03, rock=0.04, swing=80, cays=2,
                   river=0.5, houses=0),
    "islet": dict(woods=0.10, marsh=0.00, rock=0.03, swing=90, cays=4,
                  river=0.0, houses=0),
    "dry": dict(woods=0.04, marsh=0.00, rock=0.12, swing=70, cays=2,
                river=0.0, houses=0),
    "lowland": dict(woods=0.22, marsh=0.20, rock=0.00, swing=60, cays=3,
                    river=1.0, houses=0),
    "lake": dict(woods=0.12, marsh=0.22, rock=0.00, swing=45, cays=0,
                 river=0.0, houses=0, pools=True),
    "narrows": dict(woods=0.10, marsh=0.18, rock=0.00, swing=40, cays=0,
                    river=0.0, houses=0, pools=True, narrows=True),
    "plain": dict(woods=0.06, marsh=0.02, rock=0.02, swing=0, cays=0,
                  river=0.0, houses=0, inland=True),
    "mountain": dict(woods=0.18, marsh=0.00, rock=0.22, swing=0, cays=0,
                     river=0.0, houses=0, inland=True),
    "scrub": dict(woods=0.12, marsh=0.00, rock=0.08, swing=0, cays=0,
                  river=0.0, houses=0, inland=True),
    "forest": dict(woods=0.40, marsh=0.04, rock=0.02, swing=0, cays=0,
                   river=1.0, houses=0, inland=True),
    "inland_lake": dict(woods=0.20, marsh=0.15, rock=0.02, swing=50,
                        cays=1, river=0.0, houses=0, pools=True),
}
TOWN_CHARACTER = {
    "havana": "port", "cartagena": "port", "sanjuan": "port",
    "veracruz": "port", "domingo": "port", "isthmus": "port",
    "campeche": "port", "agustin": "lowland",
    "santiago": "island", "trinidad": "island", "baracoa": "island",
    "plata": "island", "cristi": "dry", "yaguana": "island",
    "tortuga": "island", "goave": "island", "cap": "island",
    "german": "island", "jamaica": "island", "pinos": "lowland",
    "marta": "jungle", "hacha": "dry", "tolu": "jungle",
    "chagres": "jungle", "pheasant": "jungle", "espana": "jungle",
    "trujillo": "jungle", "honduras": "jungle", "belize": "lowland",
    "trist": "lowland", "blewfields": "jungle", "tampico": "lowland",
    "coro": "dry", "cumana": "dry", "araya": "dry", "margarita": "dry",
    "curacao": "dry", "bonaire": "dry",
    "maracaibo": "narrows", "gibraltar": "lake",
    "caroline": "lowland", "charles": "lowland", "chfort": "lowland",
    "bermuda": "islet", "eleuthera": "islet", "nassau": "islet",
    "providence": "islet", "kitts": "island", "nevis": "islet",
    "antigua": "islet", "montserrat": "islet", "eustatius": "islet",
    "maarten": "islet", "tortola": "islet", "guadeloupe": "island",
    "martinique": "island", "lucia": "island", "barbados": "islet",
    "grenada": "island", "tobago": "island",
    "principe": "plain", "caracas": "mountain", "merida": "scrub",
    "panama": "forest", "granada": "inland_lake",
}
# The coast sets' look: their character and seed.
SET_CHARACTER = {"north": ("jungle", 1), "east": ("islet", 2),
                 "south": ("dry", 3), "west": ("lowland", 4)}


# --- The sheet ----------------------------------------------------------

def _sheet():
    """Per tile: terrain classes (256, 32, 32) and ground (256)."""
    a = tile_terrain.load(os.path.join(tile_terrain.art_dir(),
                                       "battle-terrain.png"))
    if a.shape[:2] != (4 * T, 64 * T):
        raise SystemExit("battle_screens: battle-terrain.png is not "
                         "2048 x 128")
    cls = np.full(a.shape[:2], 255, dtype=np.uint8)
    for k, col in ((SEA, (0, 0, 255)), (SHALLOWS, (0, 255, 255)),
                   (LAND, (0, 255, 0))):
        cls[(a == col).all(axis=2)] = k
    if (cls == 255).any():
        raise SystemExit("battle_screens: battle-terrain.png has other "
                         "colours")
    masks = cls.reshape(4, T, 64, T).transpose(0, 2, 1, 3) \
        .reshape(256, T, T)
    ground = np.zeros(256, dtype=np.uint8)
    path = os.path.join(tile_terrain.art_dir(), "battle.tiles")
    section = None
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line.startswith("["):
                section = line
            elif section == "[ground]" and "=" in line:
                k, v = (x.strip() for x in line.split("=", 1))
                ground[int(k)] = GROUNDS.index(v)
    return masks, ground


def _check_sheet(masks, ground):
    def want(tiles, what, ok):
        bad = [t for t in tiles if not ok(t)]
        if bad:
            raise SystemExit("battle_screens: tiles %s are not %s"
                             % (bad, what))
    land = (masks == LAND).all(axis=(1, 2))
    edges_land = np.array([(m[0] == LAND).all() and (m[-1] == LAND).all()
                           and (m[:, 0] == LAND).all() and
                           (m[:, -1] == LAND).all() for m in masks])
    sea = (masks == SEA).all(axis=(1, 2))
    want(SEA_TILES + SURF_TILES, "open sea", lambda t: sea[t])
    want(PLAIN, "clear land", lambda t: land[t] and ground[t] == CLEAR)
    want(WOODS + (FORT, TOWN, ROCK), "covered land",
         lambda t: land[t] and ground[t] == COVER)
    want(MARSH, "marsh", lambda t: land[t] and ground[t] == SWAMP)
    want((POOLS,), "pools", lambda t: edges_land[t] and ground[t] == SWAMP)
    want(SHORE_HOUSES, "shore houses", lambda t: ground[t] == COVER)


def _mixed(masks):
    """The coast tiles: land and water both."""
    return np.array([t for t in range(128, 255)
                     if (masks[t] == LAND).any() and
                     (masks[t] != LAND).any()])


# --- Raster tools -------------------------------------------------------

def _wave(t, rng, period, terms=6, cyclic=False, rough=1.2):
    """Smooth noise over "t", in [-1, 1]; with "cyclic", of period
    "period" exactly."""
    out = np.zeros_like(t, dtype=np.float64)
    for k in range(1, terms + 1):
        f = k if cyclic else k * rng.uniform(0.7, 1.3)
        out += np.sin(2 * np.pi * f * t / period +
                      rng.uniform(0, 2 * np.pi)) / k ** rough
    m = np.abs(out).max()
    return out / m if m > 0 else out


def _field(h, w, rng, scale=3.0, terms=5, cyc=None):
    """Smooth noise on an h x w cell grid, as ranks in [0, 1); "cyc"
    ("x" or "y") makes it periodic along that axis."""
    y, x = np.mgrid[0:h, 0:w].astype(np.float64)
    out = np.zeros((h, w))
    for _ in range(terms):
        a = rng.uniform(0, 2 * np.pi)
        lam = scale * rng.uniform(0.7, 1.6)
        kx, ky = math.cos(a) * 2 * np.pi / lam, math.sin(a) * 2 * np.pi / lam
        if cyc == "x":
            kx = 2 * np.pi * max(1, round(kx * w / (2 * np.pi))) / w
        elif cyc == "y":
            ky = 2 * np.pi * max(1, round(ky * h / (2 * np.pi))) / h
        out += np.sin(kx * x + ky * y + rng.uniform(0, 2 * np.pi))
    r = np.empty(h * w)
    r[np.argsort(out.ravel(), kind="stable")] = np.arange(h * w) / (h * w)
    return r.reshape(h, w)


def _dilate(m, r, wrap=(False, False)):
    """Square dilation of a boolean mask by r pixels."""
    out = m
    for ax in (0, 1):
        if r <= 0:
            break
        pad = [(0, 0), (0, 0)]
        pad[ax] = (r + 1, r)
        p = np.pad(out, pad, mode="wrap" if wrap[ax] else "constant")
        c = np.cumsum(p, axis=ax, dtype=np.int32)
        n = out.shape[ax]
        hi = np.take(c, np.arange(2 * r + 1, 2 * r + 1 + n), axis=ax)
        lo = np.take(c, np.arange(0, n), axis=ax)
        out = hi - lo > 0
    return out.copy() if out is m else out


def _with_halo(land, wrap=(False, False)):
    r = np.where(land, LAND, SEA).astype(np.uint8)
    r[_dilate(land, HALO, wrap) & ~land] = SHALLOWS
    return r


def _blob(X, Y, cx, cy, r, rng, wavy=1.0):
    """A round island of about radius r, its outline wavy."""
    p2, p3 = rng.uniform(0, 6.3), rng.uniform(0, 6.3)
    out = np.zeros(X.shape, dtype=bool)
    h, w = X.shape
    y0, y1 = max(int(cy - 1.5 * r), 0), min(int(cy + 1.5 * r) + 2, h)
    x0, x1 = max(int(cx - 1.5 * r), 0), min(int(cx + 1.5 * r) + 2, w)
    if y0 >= y1 or x0 >= x1:
        return out
    x, y = X[y0:y1, x0:x1], Y[y0:y1, x0:x1]
    a = np.arctan2(y - cy, x - cx)
    k = 1 + wavy * (0.25 * np.sin(2 * a + p2) + 0.12 * np.sin(3 * a + p3))
    out[y0:y1, x0:x1] = np.hypot(x - cx, y - cy) < r * k
    return out


def _cells(raster):
    h, w = raster.shape[0] // T, raster.shape[1] // T
    return raster.reshape(h, T, w, T).transpose(0, 2, 1, 3) \
        .reshape(h, w, T * T)


def _cell_dist(water, wrap=(False, False)):
    """Chebyshev cells to the nearest True cell of "water"."""
    h, w = water.shape
    d = np.full((h, w), 99)
    q = deque()
    for y, x in zip(*np.nonzero(water)):
        d[y, x] = 0
        q.append((y, x))
    while q:
        y, x = q.popleft()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                ny, nx = y + dy, x + dx
                if wrap[0]:
                    ny %= h
                if wrap[1]:
                    nx %= w
                if 0 <= ny < h and 0 <= nx < w and d[ny, nx] > d[y, x] + 1:
                    d[ny, nx] = d[y, x] + 1
                    q.append((ny, nx))
    return d


# --- The tiler ----------------------------------------------------------

class Tiler:
    def __init__(self):
        self.masks, self.ground = _sheet()
        _check_sheet(self.masks, self.ground)
        self.mixed = _mixed(self.masks)
        self.cand = np.concatenate([self.mixed, np.array(SHORE_HOUSES)])
        self.house = np.isin(self.cand, SHORE_HOUSES)
        m = self.masks
        east, west = m[:, :, -1], m[:, :, 0]
        south, north = m[:, -1, :], m[:, 0, :]
        # lr[a, b]: a left of b; ud[a, b]: a above b.
        self.lr = COST[east[:, None, :], west[None, :, :]].sum(axis=2)
        self.ud = COST[south[:, None, :], north[None, :, :]].sum(axis=2)
        cm = m[self.cand].reshape(len(self.cand), -1)
        self.tcost = COST[cm].reshape(len(self.cand), -1)  # (n, 1024 * 3)

    def solve(self, raster, plan, salt, near_town=None,
              wrap=(False, False)):
        """Tiles for "raster" (h*32, w*32 classes); "plan" the ground
        plan of each cell (used where a cell is all land); "near_town"
        a boolean cell mask where shore houses are welcome."""
        blocks = _cells(raster)
        h, w = blocks.shape[:2]
        flat = blocks.reshape(h * w, -1)
        has_land = (flat == LAND).any(axis=1).reshape(h, w)
        has_water = (flat != LAND).any(axis=1).reshape(h, w)
        grid = np.zeros((h, w), dtype=np.int64)
        idx = np.arange(h * w).reshape(h, w) + salt * 1000
        # All land: the plan's tiles.
        groups = {P_PLAIN: PLAIN, P_WOODS: WOODS, P_MARSH: MARSH,
                  P_POOLS: (POOLS,), P_ROCK: (ROCK,), P_TOWN: (TOWN,),
                  P_FORT: (FORT,)}
        for code, tiles in groups.items():
            sel = ~has_water & (plan == code)
            if sel.any():
                grid[sel] = world_map.pick(idx[sel], tiles)
        # All water: sea, with white caps away from the land.
        dist = _cell_dist(has_land, wrap)
        sel = ~has_land
        surf = world_map.hash_jitter(idx[sel] + 77, [0])[:, 0] < \
            SURF_SHARE / 2
        far = dist[sel] >= 3
        tiles = np.where(surf & far,
                         world_map.pick(idx[sel], SURF_TILES),
                         world_map.pick(idx[sel], SEA_TILES))
        grid[sel] = tiles
        # The coast.
        coast = has_land & has_water
        cy, cx = np.nonzero(coast)
        if len(cy) == 0:
            return grid
        onehot = np.stack([blocks[cy, cx] == k for k in range(3)], axis=2) \
            .reshape(len(cy), -1).astype(np.float32)
        data = onehot @ self.tcost.T
        near = np.zeros(len(cy), dtype=bool) if near_town is None else \
            near_town[cy, cx]
        data[np.ix_(~near, self.house)] = np.inf
        data[np.ix_(near, self.house)] -= HOUSE_BONUS
        best = data.min(axis=1, keepdims=True)
        allowed = data <= best + np.maximum(np.abs(best) * TOLERANCE,
                                            TOLERANCE_MIN)
        jitter = world_map.hash_jitter(idx[cy, cx], self.cand)
        grid[cy, cx] = self.cand[(data + jitter).argmin(axis=1)]
        parity = (cx + cy) % 2

        def neighbour(ys, xs, dy, dx):
            ny, nx = ys + dy, xs + dx
            ok = np.ones(len(ys), dtype=bool)
            if wrap[0]:
                ny = ny % h
            else:
                ok &= (ny >= 0) & (ny < h)
            if wrap[1]:
                nx = nx % w
            else:
                ok &= (nx >= 0) & (nx < w)
            return grid[np.clip(ny, 0, h - 1), np.clip(nx, 0, w - 1)], ok

        for _ in range(60):
            changed = 0
            for p in (0, 1):
                sel = parity == p
                ys, xs = cy[sel], cx[sel]
                seam = np.zeros((sel.sum(), len(self.cand)),
                                dtype=np.float32)
                for dy, dx, table, mine in ((0, -1, self.lr, False),
                                            (0, 1, self.lr, True),
                                            (-1, 0, self.ud, False),
                                            (1, 0, self.ud, True)):
                    nb, ok = neighbour(ys, xs, dy, dx)
                    rows = table[np.ix_(self.cand, nb)].T if mine else \
                        table[np.ix_(nb, self.cand)]
                    seam += np.where(ok[:, None], rows, 0)
                e = np.where(allowed[sel], data[sel] + SEAM * seam,
                             np.inf) + jitter[sel]
                new = self.cand[e.argmin(axis=1)]
                changed += int((new != grid[ys, xs]).sum())
                grid[ys, xs] = new
            if changed == 0:
                break
        return grid

    def terrain(self, grid):
        h, w = grid.shape
        return self.masks[grid].transpose(0, 2, 1, 3).reshape(h * T, w * T)

    def grounds(self, grid):
        return self.ground[grid]


# --- The towns ----------------------------------------------------------

def town_table():
    """Per town id: key, cells, direction of its water, most citizens,
    inland; every era's cells must agree."""
    out = {}
    for year in towns.YEARS:
        e = towns._era(year)
        for r, rec in zip(e["rows"], e["records"]):
            cells = {k: tuple(rec[k]) for k in ("land", "party", "fort",
                                                "ship")}
            t = out.setdefault(rec["id"], {"key": r["key"],
                                           "cells": cells,
                                           "citizens": 0, "years": [],
                                           "inland": bool(rec["flags"] &
                                                          towns.INLAND)})
            if t["cells"] != cells or t["key"] != r["key"]:
                raise SystemExit("battle_screens: town %d's battle cells "
                                 "differ between eras" % rec["id"])
            t["citizens"] = max(t["citizens"], rec["citizens"])
            t["years"].append(year)
    for tid, t in out.items():
        c = t["cells"]
        d = (int(np.sign(c["party"][0] - c["land"][0])),
             int(np.sign(c["party"][1] - c["land"][1])))
        if d == (0, 0) or tid >= TOWN_IDS:
            raise SystemExit("battle_screens: town %d: no water side" % tid)
        t["d"] = d
    return out


def _frame(d):
    """Pixel centres and (u, v): u toward the water, v along the
    shore, from the field's middle."""
    Y, X = np.mgrid[0:FIELD, 0:FIELD].astype(np.float64) + 0.5
    dx, dy = d
    n = math.hypot(dx, dy)
    U = ((X - FIELD / 2) * dx + (Y - FIELD / 2) * dy) / n
    V = (-(X - FIELD / 2) * dy + (Y - FIELD / 2) * dx) / n
    return X, Y, U, V


def _uv(d, x, y):
    dx, dy = d
    n = math.hypot(dx, dy)
    return (((x - FIELD / 2) * dx + (y - FIELD / 2) * dy) / n,
            (-(x - FIELD / 2) * dy + (y - FIELD / 2) * dx) / n)


def _cell_mask(cells, grow=0):
    m = np.zeros((FIELD, FIELD), dtype=bool)
    for x, y in cells:
        m[max(y * T - grow, 0):(y + 1) * T + grow,
          max(x * T - grow, 0):(x + 1) * T + grow] = True
    return m


def ship_centre(c):
    return c["ship"][0] * T + 16 + 40, c["ship"][1] * T + 16 + 30


def fort_centre(c):
    return c["fort"][0] * T + 16, c["fort"][1] * T + 16


def _smooth(a, lo, hi):
    t = np.clip((a - lo) / (hi - lo), 0, 1)
    return t * t * (3 - 2 * t)


def _houses(t, rng):
    """The town's house cells: the land cell and more around it."""
    c = t["cells"]
    lx, ly = c["land"]
    n = 1 + min(5, t["citizens"] // 14)
    if CHARACTERS[t["char"]]["houses"]:
        n += 1
    near = [(lx + dx, ly + dy) for dy in (-1, 0, 1) for dx in (-1, 0, 1)
            if (dx or dy) and 0 <= lx + dx < N and 0 <= ly + dy < N and
            (lx + dx, ly + dy) != tuple(c["fort"])]
    rng.shuffle(near)
    # Side by side first, then on the diagonals.
    near.sort(key=lambda p: abs(p[0] - lx) + abs(p[1] - ly))
    return [(lx, ly)] + near[:n - 1]


def _plan(land_cells, rng, ch, water_cells, fixed, wrap=None):
    """A ground plan for the cells (h x w): woods, marsh, rock by the
    character, "fixed" {(x, y): code} first."""
    h, w = land_cells.shape
    plan = np.full((h, w), P_PLAIN)
    cyc = None if wrap is None else ("y" if wrap[0] else "x")
    fw = _field(h, w, rng, 3.0, cyc=cyc)
    fm = _field(h, w, rng, 2.5, cyc=cyc)
    fr = _field(h, w, rng, 1.6, cyc=cyc)
    dist = _cell_dist(water_cells, wrap or (False, False))
    free = land_cells.copy()
    for (x, y) in fixed:
        free[y, x] = False
    nfree = free.sum()

    def take(score, share, code):
        k = int(round(share * nfree))
        if k <= 0:
            return
        s = np.where(free, score, -np.inf).ravel()
        order = np.argsort(-s, kind="stable")[:k]
        order = order[np.isfinite(s[order])]
        plan.ravel()[order] = code
        free.ravel()[order] = False
    take(fr, ch["rock"], P_ROCK)
    if ch.get("pools"):
        take(fm - 0.12 * dist, ch["marsh"] * 0.5, P_POOLS)
        take(fm - 0.12 * dist, ch["marsh"] * 0.5, P_MARSH)
    else:
        take(fm - 0.12 * dist, ch["marsh"], P_MARSH)
    take(fw, ch["woods"], P_WOODS)
    for (x, y), code in fixed.items():
        plan[y, x] = code
    return plan


def _cays(X, Y, rng, count, keep_out, sea):
    """Cays at sea, out of "keep_out", clear of other land: each within
    one cell, halo and all, as the sheet's islet tiles draw them."""
    land = np.zeros(X.shape, dtype=bool)
    forbid = _dilate(~sea, 24) | keep_out
    h, w = X.shape[0] // T, X.shape[1] // T
    tries = 0
    while count > 0 and tries < 200:
        tries += 1
        cx = (rng.randrange(1, w - 1) + 0.5) * T
        cy = (rng.randrange(1, h - 1) + 0.5) * T
        b = _blob(X, Y, cx, cy, rng.uniform(5, 8), rng, 0.5)
        if (b & forbid).any():
            continue
        land |= b
        forbid |= _dilate(b, 40)
        count -= 1
    return land


def _river(U, V, rng, v0, length, width, shore):
    """A river from the shore at v0 inland (toward -u)."""
    us = np.linspace(shore - length, shore + 20, 80)
    vs = v0 + 26 * _wave(us, rng, 180, terms=3)
    m = np.zeros(U.shape, dtype=bool)
    for u, v in zip(us, vs):
        ww = width * (0.6 + 0.4 * (u - us[0]) / (us[-1] - us[0]))
        m |= (np.abs(U - u) < 5) & (np.abs(V - v) < ww / 2)
    return m


def town_sea(t, rng, tiler, salt):
    ch = CHARACTERS[t["char"]]
    c, d = t["cells"], t["d"]
    X, Y, U, V = _frame(d)
    fx, fy = fort_centre(c)
    uF, vF = _uv(d, fx, fy)
    sx, sy = ship_centre(c)
    uS, vS = _uv(d, sx, sy)
    ext = 16 * (abs(d[0]) + abs(d[1])) / math.hypot(*d)
    shore0 = uF + ext + rng.uniform(4, 9)
    swing = max(ch["swing"], 40)
    w = _smooth(np.abs(V - vF), 56, 150)
    shore = shore0 + swing * w * _wave(V, rng, 700) + \
        0.5 * swing * w
    land = U < shore
    # The ship's water: its start and the way in to the fort.
    t_ = np.clip(((X - sx) * (fx - sx) + (Y - sy) * (fy - sy)) /
                 max((fx - sx) ** 2 + (fy - sy) ** 2, 1), 0, 1)
    lane = np.hypot(X - (sx + t_ * (fx - sx)), Y - (sy + t_ * (fy - sy)))
    keep = (lane < 34) | (np.hypot(X - sx, Y - sy) < 60)
    if ch.get("narrows"):
        # The far shore of a strait, in the two corners across.
        far = np.abs(V) > 210 + 30 * _wave(U, rng, 500)
        land |= far & (U > shore0 + 80) & ~keep
    if rng.random() < ch["river"]:
        side = 1 if rng.random() < 0.5 else -1
        v0 = vF + side * rng.uniform(120, 190)
        land &= ~_river(U, V, rng, v0, rng.uniform(80, 160),
                        rng.uniform(22, 34), shore0)
    land |= _cell_mask([c["fort"], c["land"]], 10)
    for hx, hy in t["houses"]:
        land |= _cell_mask([(hx, hy)], 6)
    land &= ~(keep & (U > shore0 + 6))
    land |= _cays(X, Y, rng, ch["cays"], _dilate(keep, 20), ~land)
    raster = _with_halo(land)
    cells_land = ~(_cells(raster) != LAND).any(axis=2)
    cells_water = (_cells(raster) != LAND).all(axis=2)
    fixed = {tuple(c["fort"]): P_FORT}
    for hc in t["houses"]:
        fixed[hc] = P_TOWN
    plan = _plan(cells_land, rng, ch, cells_water, fixed)
    lx, ly = c["land"]
    near = np.zeros((N, N), dtype=bool)
    near[max(ly - 2, 0):ly + 3, max(lx - 2, 0):lx + 3] = True
    return tiler.solve(raster, plan, salt, near)


def town_land(t, rng, tiler, salt):
    ch = CHARACTERS[t["char"]]
    c, d = t["cells"], t["d"]
    X, Y, U, V = _frame(d)
    px_, py_ = c["party"]
    party = [(px_, py_), (px_ - 1, py_ - 1), (px_ + 1, py_ + 1)]
    lx, ly = c["land"]
    # The way from the party to the town, cell by cell.
    way = []
    for k in range(0, 21):
        f = k / 20
        way.append((int(round(px_ + f * (lx - px_))),
                    int(round(py_ + f * (ly - py_)))))
    keep = _cell_mask(party + [c["land"], c["fort"]] + way, 8)
    for hc in t["houses"]:
        keep |= _cell_mask([hc], 6)
    land = np.ones((FIELD, FIELD), dtype=bool)
    if not ch.get("inland"):
        um = max(_uv(d, (x + a) * T, (y + b) * T)[0]
                 for x, y in party for a in (0, 1) for b in (0, 1))
        shore0 = um + rng.uniform(4, 16)
        swing = max(ch["swing"], 40)
        vP = _uv(d, px_ * T + 16, py_ * T + 16)[1]
        w = _smooth(np.abs(V - vP), 70, 170)
        shore = shore0 - swing * w * (0.6 + 0.6 * _wave(V, rng, 600))
        land = U < shore
        if ch.get("narrows"):
            land &= ~((np.abs(V) > 230 + 30 * _wave(U, rng, 500)) &
                      (U > shore0 - 120))
        if rng.random() < ch["river"]:
            side = 1 if rng.random() < 0.5 else -1
            v0 = vP + side * rng.uniform(130, 200)
            land &= ~_river(U, V, rng, v0, rng.uniform(90, 170),
                            rng.uniform(22, 32), shore0 - swing * 0.6)
    elif ch["river"]:
        # An inland river across a corner, far from the way.
        a = rng.uniform(0, 2 * np.pi)
        ox, oy = math.cos(a), math.sin(a)
        dist = (X - FIELD / 2) * ox + (Y - FIELD / 2) * oy
        along = -(X - FIELD / 2) * oy + (Y - FIELD / 2) * ox
        bend = 20 * _wave(along, rng, 300, terms=3)
        land &= ~(np.abs(dist - 170 - bend) < 13)
    if ch.get("pools") or ch["marsh"] >= 0.15:
        for _ in range(2):
            cx, cy = rng.uniform(40, 440), rng.uniform(40, 440)
            b = _blob(X, Y, cx, cy, rng.uniform(14, 26), rng)
            if not (b & keep).any():
                land &= ~b
    land |= keep
    raster = _with_halo(land)
    cells_land = ~(_cells(raster) != LAND).any(axis=2)
    cells_water = (_cells(raster) != LAND).all(axis=2)
    fixed = {tuple(c["fort"]): P_FORT}
    for hc in t["houses"]:
        fixed[hc] = P_TOWN
    for p in party:
        fixed[p] = P_PLAIN
    # Clear ground about the town for its men.
    fixed.update(_men_cells(c["land"], fixed))
    plan = _plan(cells_land, rng, ch, cells_water, fixed)
    near = np.zeros((N, N), dtype=bool)
    near[max(ly - 2, 0):ly + 3, max(lx - 2, 0):lx + 3] = True
    return tiler.solve(raster, plan, salt + 500, near)


def _offsets():
    """src/sim/land.c's search for the town's men around its land
    cell, in its order."""
    out = []
    for k in range(48):
        d = ((k & 7) + 1) >> 1
        x = -d if k & 1 else d
        d = (((k >> 3) & 7) + 1) >> 1
        y = -d if (k >> 3) & 1 else d
        out.append((x, y))
    return out


def _men_cells(land, fixed):
    """Clear the first cells of the search not taken by the town."""
    out = {}
    for ox, oy in _offsets():
        x, y = land[0] + ox, land[1] + oy
        if not (0 <= x < N and 0 <= y < N) or (x, y) in fixed:
            continue
        out[(x, y)] = P_PLAIN
        if len(out) == 4:
            break
    return out


# --- The coast sets -----------------------------------------------------

def coast_strip(name, tiler):
    """The set's four coast pieces, drawn as one cyclic strip with the
    land to the north and turned to face the set's side."""
    char, seed = SET_CHARACTER[name]
    ch = CHARACTERS[char]
    rng = random.Random(9000 + seed)
    P = 4 * FIELD
    Y, X = np.mgrid[0:FIELD, 0:P].astype(np.float64) + 0.5
    x = X[0]
    shore = 78 + 46 * _wave(x, rng, P, terms=9, cyclic=True, rough=0.9)
    shore = np.clip(shore, 18, 150)
    land = Y < shore[None, :]
    # A river mouth or two, and lagoons behind the shore.
    for _ in range(1 + int(ch["river"])):
        x0 = rng.uniform(0, P)
        wx = rng.uniform(20, 30)
        bend = 14 * _wave(Y[:, 0], rng, 200, terms=2)
        dxw = (X - x0 - bend[:, None] + P / 2) % P - P / 2
        land &= ~((np.abs(dxw) < wx / 2) & (Y > 4))
    keep = np.zeros(land.shape, dtype=bool)
    for k in range(4):
        keep[150:360, k * FIELD + 150:k * FIELD + 380] = True
    keep[200:, :] = True
    cays = _cays(X, Y, rng, ch["cays"] + 3, keep, ~_dilate(land, 10))
    land |= cays
    land[245:, :] = False
    rot = {"north": 0, "east": 3, "south": 2, "west": 1}[name]
    land = np.rot90(land, rot).copy()
    wrap = (True, False) if rot % 2 else (False, True)
    raster = _with_halo(land, wrap)
    cl = ~(_cells(raster) != LAND).any(axis=2)
    cw = (_cells(raster) != LAND).all(axis=2)
    plan = _plan(cl, rng, ch, cw, {}, wrap=wrap)
    grid = tiler.solve(raster, plan, 200 + seed, None, wrap)
    if rot % 2:
        return [grid[k * N:(k + 1) * N, :] for k in range(4)]
    return [grid[:, k * N:(k + 1) * N] for k in range(4)]


def open_water(tiler, salt, cays, rng):
    Y, X = np.mgrid[0:FIELD, 0:FIELD].astype(np.float64) + 0.5
    keep = np.zeros((FIELD, FIELD), dtype=bool)
    keep[120:380, 120:380] = True
    land = _cays(X, Y, rng, cays, keep, np.ones((FIELD, FIELD), bool))
    raster = _with_halo(land)
    plan = np.full((N, N), P_PLAIN)
    return tiler.solve(raster, plan, salt)


# --- Checks -------------------------------------------------------------

G = 4                   # pixels a step of the way-finding


def _coarse(m, how):
    h, w = m.shape
    b = m.reshape(h // G, G, w // G, G)
    return b.all(axis=(1, 3)) if how == "all" else b.any(axis=(1, 3))


def _reach(passable, start):
    """The G x G blocks reached from pixel "start" (x, y) through blocks
    all passable."""
    c = _coarse(passable, "all")
    h, w = c.shape
    seen = np.zeros_like(c)
    x, y = start[0] // G, start[1] // G
    if not c[y, x]:
        return seen
    seen[y, x] = True
    q = deque([(y, x)])
    while q:
        y, x = q.popleft()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ny, nx = y + dy, x + dx
            if 0 <= ny < h and 0 <= nx < w and c[ny, nx] and \
                    not seen[ny, nx]:
                seen[ny, nx] = True
                q.append((ny, nx))
    return seen


def _erode(m, r):
    return ~_dilate(~m, r)


def check_sea_town(tid, t, grid, tiler):
    c = t["cells"]
    err = []
    fx, fy = c["fort"]
    lx, ly = c["land"]
    if grid[fy, fx] != FORT:
        err.append("no fort tile at the fort cell")
    if grid[ly, lx] not in HOUSES:
        err.append("no house at the land cell")
    ter = tiler.terrain(grid)
    sx, sy = ship_centre(c)
    if not (ter[sy - 16:sy + 17, sx - 16:sx + 17] == SEA).all():
        err.append("the ship does not start on open sea")
    reach = _reach(_erode(ter != LAND, 4), (sx, sy))
    # Within the fort's reach: a ship's centre that meets land there
    # lands the men (|dx|, |dy| <= 32 + a quarter of the hull).
    cx, cy = fort_centre(c)
    box = np.zeros((FIELD, FIELD), dtype=bool)
    box[max(cy - 33, 0):cy + 34, max(cx - 33, 0):cx + 34] = True
    shore = _dilate(ter == LAND, 7)
    if not (reach & _coarse(box & shore, "any")).any():
        err.append("no way to the fort by water")
    edge = np.zeros((FIELD, FIELD), dtype=bool)
    edge[:40, :] = edge[460:, :] = True
    edge[:, :50] = edge[:, 470:] = True
    if not (reach & _coarse(edge, "any")).any():
        err.append("no way out to the edge")
    # The straight run in: water until the fort's reach.
    n = int(math.hypot(cx - sx, cy - sy))
    for k in range(n):
        x = int(sx + (cx - sx) * k / n)
        y = int(sy + (cy - sy) * k / n)
        if abs(x - cx) <= 33 and abs(y - cy) <= 33:
            break
        if ter[y, x] == LAND:
            err.append("land between the ship and the fort")
            break
    return err


def check_land_town(tid, t, grid, tiler):
    c = t["cells"]
    g = tiler.grounds(grid)
    err = []
    lx, ly = c["land"]
    if grid[ly, lx] not in HOUSES:
        err.append("no house at the land cell")
    px_, py_ = c["party"]
    party = [(px_, py_), (px_ - 1, py_ - 1), (px_ + 1, py_ + 1)]
    for x, y in party:
        if not (0 <= x < N and 0 <= y < N):
            err.append("party cell %d,%d off the field" % (x, y))
            return err
        if g[y, x] == WATER:
            err.append("party cell %d,%d in water" % (x, y))
    if g[py_, px_] != CLEAR:
        err.append("party cell not clear")
    seen = {(px_, py_)}
    q = deque([(px_, py_)])
    while q:
        x, y = q.popleft()
        for ox, oy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (x + ox, y + oy)
            if 0 <= n[0] < N and 0 <= n[1] < N and n not in seen and \
                    g[n[1], n[0]] != WATER:
                seen.add(n)
                q.append(n)
    if (lx, ly) not in seen:
        err.append("the town is cut off from the party by water")
    for x, y in party[1:]:
        if (x, y) not in seen:
            err.append("party cell %d,%d cut off" % (x, y))
    used = set()
    for _ in range(4):
        pos = None
        for k, (ox, oy) in enumerate(_offsets()):
            x, y = lx + ox, ly + oy
            inside = 0 <= x < N and 0 <= y < N
            if (not inside or g[y, x] == CLEAR) and k not in used:
                used.add(k)
                pos = (x, y)
                break
        if pos is None:
            pos = (lx - 4, ly - 3)
        if not (0 <= pos[0] < N and 0 <= pos[1] < N):
            err.append("a unit of the town is placed off the field")
            break
    return err


def check_coast(name, pieces, tiler):
    err = []
    rot = {"north": 0, "east": 3, "south": 2, "west": 1}[name]
    for k, g in enumerate(pieces):
        ter = np.rot90(tiler.terrain(g), -rot)   # land to the north
        if (ter[0] == LAND).mean() < 0.15:
            err.append("piece %d: little land along its edge" % k)
        if (ter[FIELD // 2:] != SEA).any():
            err.append("piece %d: land or shallows on the far half" % k)
        if (ter[180:360, 180:380] != SEA).any():
            err.append("piece %d: not open sea where the ships start" % k)
    # The joins, piece k to piece k + 1 (and 3 to 0).
    worst = 0
    for k in range(4):
        a, b = tiler.terrain(pieces[k]), tiler.terrain(pieces[(k + 1) % 4])
        if rot % 2:
            ea, eb = a[-1, :], b[0, :]
        else:
            ea, eb = a[:, -1], b[:, 0]
        bad = int(((ea == LAND) != (eb == LAND)).sum())
        worst = max(worst, bad)
    if worst > 40:
        err.append("pieces join badly (%d pixels)" % worst)
    return err, worst


# --- The build ----------------------------------------------------------

_memo = {}


def _solve():
    tiler = Tiler()
    table = town_table()
    sea = [None] * SCREENS
    land = [None] * SCREENS
    report = []
    retries = 0
    for tid in sorted(table):
        t = table[tid]
        t["char"] = TOWN_CHARACTER[t["key"]]
        for kind, make, check, out in (
                ("sea", town_sea, check_sea_town, sea),
                ("land", town_land, check_land_town, land)):
            for k in range(TRIES):
                rng = random.Random(tid * 7919 + k * 104729 +
                                    (0 if kind == "sea" else 31))
                t["houses"] = _houses(t, random.Random(tid))
                g = make(t, rng, tiler, tid + 100 * k)
                err = check(tid, t, g, tiler)
                if not err:
                    break
                retries += 1
            if err:
                raise SystemExit("battle_screens: town %d (%s), %s "
                                 "screen: %s" % (tid, t["key"], kind,
                                                 "; ".join(err)))
            out[tid] = g
    sets = {}
    worst = 0
    for i, name in enumerate(SETS[:4]):
        pieces = coast_strip(name, tiler)
        err, w = check_coast(name, pieces, tiler)
        worst = max(worst, w)
        if err:
            raise SystemExit("battle_screens: %s set: %s" %
                             (name, "; ".join(err)))
        ids = []
        for k, g in enumerate(pieces):
            sid = SET_BASE + 5 * i + k
            sea[sid] = g
            ids.append(sid)
        sid = SET_BASE + 5 * i + 4
        sea[sid] = open_water(tiler, 400 + i, 3,
                              random.Random(5000 + i))
        ids.append(sid)
        sets[name] = ids
    ids = []
    for k in range(5):
        sid = SET_BASE + 20 + k
        g = open_water(tiler, 450 + k, 0, random.Random(6000 + k))
        if (tiler.terrain(g) != SEA).any():
            raise SystemExit("battle_screens: open set screen %d is not "
                             "open sea" % sid)
        sea[sid] = g
        ids.append(sid)
    sets["open"] = ids
    for sid in range(SCREENS):
        if sea[sid] is None:
            sea[sid] = open_water(tiler, 600 + sid, 0,
                                  random.Random(7000 + sid))
        if land[sid] is None:
            land[sid] = country(tiler, sid)
    report.append("%d towns, each checked on both screens (%d designs "
                  "drawn again); coast pieces join within %d pixels of "
                  "480" % (len(table), retries, worst))
    return {"sea": sea, "land": land, "sets": sets, "table": table,
            "tiler": tiler, "report": report}


def country(tiler, sid):
    """An unused land screen: open country and a village."""
    rng = random.Random(8000 + sid)
    ch = CHARACTERS["plain"]
    raster = np.full((FIELD, FIELD), LAND, dtype=np.uint8)
    fixed = {(7, 7): P_TOWN, (8, 7): P_TOWN, (7, 6): P_FORT}
    plan = _plan(np.ones((N, N), bool), rng, ch,
                 np.zeros((N, N), bool), fixed)
    return tiler.solve(raster, plan, 900 + sid)


def solved():
    if "s" not in _memo:
        _memo["s"] = _solve()
    return _memo["s"]


def tmj(grids, name):
    layers = []
    for k, g in enumerate(grids):
        layers.append({
            "type": "tilelayer", "id": k + 1, "name": "%s %d" % (name, k),
            "x": 0, "y": 0, "width": N, "height": N, "opacity": 1,
            "visible": k == 0, "data": [int(v) + 1 for v in g.ravel()]})
    return {
        "type": "map", "version": "1.10", "orientation": "orthogonal",
        "renderorder": "right-down", "infinite": False,
        "width": N, "height": N, "tilewidth": ART_TILE,
        "tileheight": ART_TILE, "nextlayerid": len(grids) + 1,
        "nextobjectid": 1, "tilesets": [{"firstgid": 1, "source": TSJ}],
        "layers": layers}


def notes():
    parts = __doc__.split("\n\n")[1:]
    text = " ".join(" ".join(p.split()) for p in parts)
    return "%s %s." % (text, "; ".join(solved()["report"]))


def build_sea():
    return tmj(solved()["sea"], "sea"), notes()


def build_land():
    return tmj(solved()["land"], "land"), notes()


def build_sets():
    return dict(solved()["sets"]), notes()


def tileset():
    ts = {"type": "tileset", "version": "1.10", "name": "battle",
          "tilewidth": ART_TILE, "tileheight": ART_TILE, "tilecount": 256,
          "columns": 64, "margin": 0, "spacing": 0,
          "image": "../art/battle.png", "imagewidth": 64 * ART_TILE,
          "imageheight": 32 * ART_TILE}
    return (json.dumps(ts, indent=1) + "\n",
            "Tiled tileset for the battle screens: frame 0 of "
            "art/battle.png, for opening them in Tiled (the engine reads "
            "tiles/battle).")


TABLES = [("screens/sea", "sea-screens.tmj", build_sea),
          ("screens/land", "land-screens.tmj", build_land),
          ("data/battle-sets", "battle-sets.json", build_sets)]
FILES = [("data/" + TSJ, tileset)]
