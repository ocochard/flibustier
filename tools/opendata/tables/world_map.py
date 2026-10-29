#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""The open world's sailing map: map/1560 ... map/1680 as Tiled maps,
640 x 480 cells, each cell one of the open tile sheet's 256 tiles.

Built from the open geography (tables/_geo.py, terrain_raster() at pixel
resolution) and the open tile sheet's own terrain masks
(art/map-terrain.png, 16 x 16 terrain pixels per tile, the same grain
as the raster's 16 x 16 pixels per cell).  The masks are read from the
directory in TILE_ART, or else from the art/ of the pack opendata.py
writes.

Tiles used.  Only bare geography: open sea, plain land, and the coast
tiles.  Left out: the tiles with a building (68-79, 123-127, 255: towns
belong to the town step, which overlays them per era) and the spare sea
tiles with white surf (56-59: their mask is open sea, so they would
show breakers where a ship sails freely).  Tiles 40-55 are open
shallows (solid, edges and corners toward the sea) for the shelf away
from the shore.

Matching.  Each cell's 16 x 16 block of the raster is compared pixel by
pixel with each tile's mask; a pixel that is land on one side and sea
on the other costs LAND_SEA, land against shallows LAND_SHALLOWS,
shallows against sea SEA_SHALLOWS.  A cell of open sea only takes a
plain sea tile, a cell with no water a land tile; a cell with
shallows but no land is matched like a coast, among the tiles with no
land (no stray land at sea).

Seams.  A second pass (iterated conditional modes, cells in two
interleaved halves, until nothing changes) lets each coastal cell take,
among the tiles within TOLERANCE of its best cost, the one that also
best matches its four neighbours' facing edges: each pair of touching
edge pixels costs as above, times SEAM.  So a coastline drawn on one
tile goes on across the border on the next.  Every island of the raster
keeps land: when no chosen tile over an island's bounding box has land,
the cell holding a pixel of it is held to tiles with land and the
seams run again.

Variety.  Among equally good tiles (the 40 open sea and 51 plain land
variants above all) a hash of the cell and the tile breaks the tie:
varied, and the same on every run.

The six eras share this base map (base_grid()); each era's map puts
that era's towns on it, the building tiles tables/towns.py chooses.
"""
import json
import os

import numpy as np

import _geo
import tile_terrain

W, H = _geo.WORLD_W // _geo.CELL, _geo.WORLD_H // _geo.CELL
SIDE = _geo.CELL
YEARS = (1560, 1600, 1620, 1640, 1660, 1680)

# Tiles kept out of the bare geography.
BUILDINGS = tuple(range(68, 80)) + tuple(range(123, 128)) + (255,)
SURF = tuple(range(56, 60))
EXCLUDED = BUILDINGS + SURF

LAND_SEA = 4
LAND_SHALLOWS = 2
SEA_SHALLOWS = 1
SEAM = 40
TOLERANCE = 8.0     # of the best cost, and at least TOLERANCE_MIN
TOLERANCE_MIN = 256

# Pixel classes, 0 sea, 1 shallows, 2 land, and the cost between them.
SEA, SHALLOWS, LAND = 0, 1, 2
COST = np.array([[0, SEA_SHALLOWS, LAND_SEA],
                 [SEA_SHALLOWS, 0, LAND_SHALLOWS],
                 [LAND_SEA, LAND_SHALLOWS, 0]], dtype=np.float32)
NAMES = ("sea", "shallows", "land")

TSJ = "map.tsj"
ART_TILE = 36       # art/map.png at art_height 1080: 36 px, 64 a row
ART_W, ART_H = 64 * ART_TILE, 32 * ART_TILE


def raster_classes(t):
    """_geo terrain (1 shallows, 2 land, 3 sea) to classes."""
    lut = np.array([0, SHALLOWS, LAND, SEA], dtype=np.uint8)
    return lut[t]


def masks():
    """Each tile's terrain classes, (256, 16, 16)."""
    a = tile_terrain.load(os.path.join(tile_terrain.art_dir(),
                                       "map-terrain.png"))
    if a.shape[:2] != (4 * SIDE, 64 * SIDE):
        raise SystemExit("world_map: map-terrain.png is not 1024 x 64")
    cls = np.full(a.shape[:2], 255, dtype=np.uint8)
    for k, col in ((SEA, (0, 0, 255)), (SHALLOWS, (0, 255, 255)),
                   (LAND, (0, 255, 0))):
        cls[(a == col).all(axis=2)] = k
    if (cls == 255).any():
        raise SystemExit("world_map: map-terrain.png has other colours")
    return cls.reshape(4, SIDE, 64, SIDE).transpose(0, 2, 1, 3) \
        .reshape(256, SIDE, SIDE)


def hash_jitter(cells, tiles):
    """A repeatable value in [0, 0.5) per (cell index, tile)."""
    c = np.asarray(cells, dtype=np.uint64)[:, None]
    t = np.asarray(tiles, dtype=np.uint64)[None, :]
    h = (c * np.uint64(0x9E3779B1) + t * np.uint64(0x85EBCA77)) \
        & np.uint64(0xFFFFFFFF)
    h ^= h >> np.uint64(15)
    h = (h * np.uint64(0x2C1B3C6D)) & np.uint64(0xFFFFFFFF)
    h ^= h >> np.uint64(12)
    return (h % np.uint64(1 << 16)).astype(np.float32) / (1 << 17)


def pick(cells, tiles):
    """A repeatable choice among "tiles" for each cell index."""
    j = hash_jitter(cells, tiles)
    return np.asarray(tiles)[j.argmin(axis=1)]


_memo = {}


def _solve():
    m = masks()
    raster = raster_classes(_geo.terrain_raster(1))
    blocks = raster.reshape(H, SIDE, W, SIDE).transpose(0, 2, 1, 3) \
        .reshape(H * W, SIDE * SIDE)
    usable = np.array([t for t in range(256) if t not in EXCLUDED])
    tm = m[usable].reshape(len(usable), -1)
    t_land = (tm == LAND).any(axis=1)
    plain_sea = usable[(tm == SEA).all(axis=1)]
    plain_land = usable[(tm == LAND).all(axis=1)]

    c_land = (blocks == LAND).any(axis=1)
    c_water = (blocks != LAND).any(axis=1)
    c_shallows = (blocks == SHALLOWS).any(axis=1)
    grid = np.empty(H * W, dtype=np.int64)
    sea_only = ~c_land & ~c_shallows
    land_only = ~c_water
    grid[sea_only] = pick(np.nonzero(sea_only)[0], plain_sea)
    grid[land_only] = pick(np.nonzero(land_only)[0], plain_land)

    # Coastal cells: cost of every usable tile, one-hot product.
    coast = np.nonzero(~sea_only & ~land_only)[0]
    onehot = np.stack([blocks[coast] == k for k in range(3)], axis=2) \
        .reshape(len(coast), -1).astype(np.float32)
    # Per tile pixel and raster class: the cost of that pair.
    tcost = COST[tm].reshape(len(usable), -1)          # (n, 256*3)
    data = onehot @ tcost.T                             # (cells, n)
    # Shallows away from land: no tile with land (no stray land at sea).
    data[np.ix_(~c_land[coast], t_land)] = np.inf
    best = data.min(axis=1, keepdims=True)
    tol = np.maximum(best * TOLERANCE, TOLERANCE_MIN)
    allowed = data <= best + tol

    # Seams: edge pixel costs between every two usable tiles.
    east = m[usable][:, :, -1]
    west = m[usable][:, :, 0]
    south = m[usable][:, -1, :]
    north = m[usable][:, 0, :]
    lr = COST[east[:, None, :], west[None, :, :]].sum(axis=2)  # a left of b
    ud = COST[south[:, None, :], north[None, :, :]].sum(axis=2)  # a above b
    index = np.full(256, -1, dtype=np.int64)
    index[usable] = np.arange(len(usable))
    jitter = hash_jitter(coast, usable)
    cy, cx = coast // W, coast % W
    g2 = grid.reshape(H, W)
    parity = (cx + cy) % 2

    def sweep():
        changed = 0
        for p in (0, 1):
            sel = parity == p
            ys, xs = cy[sel], cx[sel]
            seam = np.zeros((sel.sum(), len(usable)), dtype=np.float32)
            for dy, dx, table, mine in ((0, -1, lr, False),
                                        (0, 1, lr, True),
                                        (-1, 0, ud, False),
                                        (1, 0, ud, True)):
                ny, nx = ys + dy, xs + dx
                ok = (ny >= 0) & (ny < H) & (nx >= 0) & (nx < W)
                nb = index[g2[np.clip(ny, 0, H - 1), np.clip(nx, 0, W - 1)]]
                rows = table[:, nb].T if mine else table[nb, :]
                seam += np.where(ok[:, None], rows, 0)
            e = np.where(allowed[sel], data[sel] + SEAM * seam, np.inf) + \
                jitter[sel]
            new = usable[e.argmin(axis=1)]
            changed += int((new != g2[ys, xs]).sum())
            g2[ys, xs] = new
        return changed

    def lost_islands():
        """The coastal cells of islands no chosen tile shows."""
        shown = (m[g2] == LAND).any(axis=(2, 3))
        out = []
        for x, y, x0, y0, x1, y1 in reps:
            cx0, cy0 = max(x0 // SIDE, 0), max(y0 // SIDE, 0)
            cx1, cy1 = min(x1 // SIDE + 1, W), min(y1 // SIDE + 1, H)
            c = (y // SIDE) * W + x // SIDE
            if not shown[cy0:cy1, cx0:cx1].any() and c in pos:
                out.append(pos[c])
        return out

    # Best tiles first, then seams; an island the seams wash away gets
    # its cell held to land tiles, and the seams run again.
    pos = {c: i for i, c in enumerate(coast)}
    _, reps = _geo._full()
    g2[cy, cx] = usable[(data + jitter).argmin(axis=1)]
    forced, sweeps = 0, 0
    for _ in range(8):
        for _ in range(50):
            sweeps += 1
            if sweep() == 0:
                break
        lost = lost_islands()
        if not lost:
            break
        for i in lost:
            allowed[i] &= t_land
            if not allowed[i].any():
                allowed[i] = t_land
            forced += 1
    return {"grid": g2.astype(np.uint8), "masks": m, "raster": raster,
            "forced": forced, "sweeps": sweeps, "coast_n": len(coast),
            "lost": len(lost_islands())}


def solved():
    if "s" not in _memo:
        _memo["s"] = _solve()
    return _memo["s"]


def base_grid():
    """The base map, (480, 640) tile numbers; a copy to overlay."""
    return solved()["grid"].copy()


def terrain_of(grid):
    """The pixel terrain classes the tiles of "grid" give."""
    m = solved()["masks"]
    return m[grid].transpose(0, 2, 1, 3).reshape(H * SIDE, W * SIDE)


def fidelity(grid=None):
    """(confusion, text): pixels by (raster class, tile class)."""
    s = solved()
    if grid is None:
        grid = s["grid"]
    got = terrain_of(grid)
    conf = np.bincount((s["raster"].astype(np.int64) * 3 + got).ravel(),
                       minlength=9).reshape(3, 3)
    total = conf.sum()
    wrong = total - np.trace(conf)
    parts = ["%s as %s %.3f%%" % (NAMES[a], NAMES[b],
                                 100.0 * conf[a, b] / total)
             for a in range(3) for b in range(3) if a != b]
    text = ("pixels differing from the raster %.3f%%: %s" %
            (100.0 * wrong / total, ", ".join(parts)))
    return conf, text


def tmj(grid):
    return {
        "type": "map", "version": "1.10", "orientation": "orthogonal",
        "renderorder": "right-down", "infinite": False,
        "width": W, "height": H,
        "tilewidth": ART_TILE, "tileheight": ART_TILE,
        "nextlayerid": 2, "nextobjectid": 1,
        "tilesets": [{"firstgid": 1, "source": TSJ}],
        "layers": [{
            "type": "tilelayer", "id": 1, "name": "map", "x": 0, "y": 0,
            "width": W, "height": H, "opacity": 1, "visible": True,
            "data": [int(v) + 1 for v in grid.ravel()]}],
    }


def notes():
    s = solved()
    parts = __doc__.split("\n\n")[1:]
    text = " ".join(" ".join(p.split()) for p in parts)
    return "%s %d coastal cells, %d islands restored, %d seam sweeps; %s." \
        % (text, s["coast_n"], s["forced"], s["sweeps"], fidelity()[1])


def era_grid(year):
    """The era's map: the base map with the era's towns drawn on it
    (tables/towns.py chooses their building tiles)."""
    import towns
    grid = base_grid()
    for x, y, tile in towns.era_tiles(year):
        grid[y, x] = tile
    return grid


def builder(year):
    return lambda: (tmj(era_grid(year)), notes())


def tileset():
    ts = {"type": "tileset", "version": "1.10", "name": "map",
          "tilewidth": ART_TILE, "tileheight": ART_TILE, "tilecount": 256,
          "columns": 64, "margin": 0, "spacing": 0,
          "image": "../art/map.png", "imagewidth": ART_W,
          "imageheight": ART_H}
    return (json.dumps(ts, indent=1) + "\n",
            "Tiled tileset for the maps: frame 0 of art/map.png, for "
            "opening them in Tiled (the engine reads tiles/map).")


TABLES = [("map/%d" % y, "map-%d.tmj" % y, builder(y)) for y in YEARS]
FILES = [("data/" + TSJ, tileset)]
