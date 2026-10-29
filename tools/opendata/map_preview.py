#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Preview the open sailing map (tables/world_map.py) drawn with the
open tile sheet's art (frame 0): the whole world at 4 pixels a cell, a
few crops at the art's full size, and the cells whose tile terrain
differs from the geography.

    TILE_ART=PACK/art map_preview.py OUTDIR
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "tables"))
import _geo  # noqa: E402
import tile_terrain  # noqa: E402
import world_map  # noqa: E402

CROPS = [
    ("jamaica", (-78.6, 18.7, -76.0, 17.6)),
    ("windward", (-62.0, 15.0, -59.3, 11.8)),
    ("keys", (-82.2, 25.8, -80.0, 24.4)),
    ("bahamas", (-79.5, 27.0, -76.0, 24.0)),
]
SMALL = 4


def tiles_art():
    a = tile_terrain.load(os.path.join(tile_terrain.art_dir(), "map.png"))
    side = a.shape[1] // 64
    f0 = a[:4 * side]
    return f0.reshape(4, side, 64, side, 3).transpose(0, 2, 1, 3, 4) \
        .reshape(256, side, side, 3), side


def compose(grid, art):
    h, w = grid.shape
    side = art.shape[1]
    return art[grid].transpose(0, 2, 1, 3, 4).reshape(h * side, w * side, 3)


def save(path, rgb):
    with open(path, "wb") as f:
        f.write(tile_terrain.png(rgb))
    print(path)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    grid = world_map.base_grid()
    art, side = tiles_art()
    k = side // SMALL
    small = art[:, :k * SMALL, :k * SMALL].reshape(
        256, SMALL, k, SMALL, k, 3).mean(axis=(2, 4)).astype(np.uint8)
    save(os.path.join(out, "world.png"), compose(grid, small))
    for name, (lw, ln, le, ls) in CROPS:
        x0, y0 = _geo.lonlat_to_world(lw, ln)
        x1, y1 = _geo.lonlat_to_world(le, ls)
        c0, r0 = int(x0) // _geo.CELL, int(y0) // _geo.CELL
        c1, r1 = int(x1) // _geo.CELL + 1, int(y1) // _geo.CELL + 1
        save(os.path.join(out, name + ".png"),
             compose(grid[r0:r1, c0:c1], art))
    # Mismatch: per cell, red for land/sea disagreement pixels, yellow
    # for those involving shallows, over a grey of the raster.
    got = world_map.terrain_of(grid)
    want = world_map.solved()["raster"]
    H, W = grid.shape
    s = _geo.CELL
    ls = ((got == world_map.LAND) != (want == world_map.LAND)) & \
        (got != world_map.SHALLOWS) & (want != world_map.SHALLOWS)
    other = (got != want) & ~ls
    ls = ls.reshape(H, s, W, s).sum(axis=(1, 3))
    other = other.reshape(H, s, W, s).sum(axis=(1, 3))
    land = (want == world_map.LAND).reshape(H, s, W, s).mean(axis=(1, 3))
    img = np.zeros((H, W, 3), np.uint8)
    img[...] = (40 + 100 * land)[..., None].astype(np.uint8)
    img[other > 8] = (220, 200, 0)
    img[ls > 0] = (255, 0, 0)
    save(os.path.join(out, "mismatch.png"), img)
    print(world_map.fidelity(grid)[1])


if __name__ == "__main__":
    main()
