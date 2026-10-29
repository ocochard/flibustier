#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Preview the open world's terrain (tables/_geo.py): the whole world at
cell resolution with a dozen real ports of the period, and a few crops
at pixel resolution.

    geo_preview.py OUTDIR
"""
import os
import collections
import heapq
import math
import struct
import sys
import zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "tables"))
import _geo  # noqa: E402

COLOURS = np.array([[0, 0, 0], [80, 200, 220], [50, 140, 50],
                    [10, 30, 100]], dtype=np.uint8)

# Real positions (lon, lat) of ports of 1560-1680.
PORTS = [
    ("Havana", -82.358, 23.136),
    ("Cartagena", -75.479, 10.391),
    ("Portobelo", -79.655, 9.553),
    ("Veracruz", -96.134, 19.173),
    ("Santo Domingo", -69.884, 18.473),
    ("Port Royal", -76.841, 17.937),
    ("Tortuga", -72.790, 20.035),
    ("Curacao", -68.934, 12.108),
    ("Barbados", -59.616, 13.098),
    ("St. Augustine", -81.313, 29.894),
    ("Maracaibo", -71.640, 10.654),
    ("Campeche", -90.524, 19.845),
]

# Sea routes and their targets in cells (a ship sails CELLS_A_DAY).
CELLS_A_DAY = 6
ROUTES = [
    ("Port Royal", "Tortuga", 18, 30),
    ("St. Augustine", "Havana", 24, 42),
    ("Port Royal", "Cartagena", 24, 42),
    ("Tortuga", "Curacao", 24, 42),
    ("Havana", "Cartagena", 72, 120),
    ("Portobelo", "Havana", 72, 120),
    ("Barbados", "Veracruz", 126, 210),
]

# Crops: name, (lon_w, lat_n, lon_e, lat_s).
CROPS = [
    ("jamaica", (-78.6, 18.7, -76.0, 17.6)),
    ("windward", (-62.0, 15.0, -59.3, 11.8)),
    ("keys", (-82.2, 25.8, -80.0, 24.4)),
    ("bahamas", (-79.5, 27.0, -76.0, 24.0)),
]


def png(path, rgb):
    h, w, _ = rgb.shape
    raw = b"".join(b"\0" + rgb[y].tobytes() for y in range(h))

    def chunk(t, d):
        c = t + d
        return struct.pack(">I", len(d)) + c + \
            struct.pack(">I", zlib.crc32(c) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0,
                                           0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def mark(rgb, x, y, r=3):
    h, w, _ = rgb.shape
    for d in range(-r, r + 1):
        for px, py in ((x + d, y), (x, y + d)):
            if 0 <= px < w and 0 <= py < h:
                rgb[py, px] = (255, 40, 40)


def nearest(t, x, y, want, reach=64):
    """Distance in pixels from (x, y) to the nearest pixel of class
    want, or None within reach."""
    y0, x0 = max(y - reach, 0), max(x - reach, 0)
    win = t[y0:y + reach + 1, x0:x + reach + 1]
    ys, xs = np.nonzero(win == want)
    if not len(xs):
        return None
    return float(np.sqrt(((xs + x0 - x) ** 2 + (ys + y0 - y) ** 2).min()))


OPEN_STEP = 4         # pixels per block of the open sea check
APPROACH_CELLS = 4    # open sea must be this close to each port


def open_sea(full):
    """Blocks of OPEN_STEP pixels that are all sea and joined (4-way)
    to the sea at the world's edge: where a ship bigger than a sloop
    sails without touching shallows."""
    s = OPEN_STEP
    h, w = full.shape[0] // s, full.shape[1] // s
    sea = (full.reshape(h, s, w, s) == _geo.SEA).all(axis=(1, 3))
    flat = sea.ravel()
    seen = np.zeros(h * w, dtype=bool)
    q = collections.deque()
    for y in range(h):
        for x in (0, w - 1):
            q.append(y * w + x)
    for x in range(w):
        for y in (0, h - 1):
            q.append(y * w + x)
    while q:
        i = q.popleft()
        if seen[i] or not flat[i]:
            continue
        seen[i] = True
        y, x = divmod(i, w)
        if x > 0:
            q.append(i - 1)
        if x < w - 1:
            q.append(i + 1)
        if y > 0:
            q.append(i - w)
        if y < h - 1:
            q.append(i + w)
    return seen.reshape(h, w)


def port_cells(cells):
    """Each port's nearest water cell."""
    water = cells != _geo.LAND
    ys, xs = np.nonzero(water)
    out = {}
    for name, lon, lat in PORTS:
        x, y = _geo.lonlat_to_world(lon, lat)
        cx, cy = x / _geo.CELL - 0.5, y / _geo.CELL - 0.5
        i = int(np.argmin((xs - cx) ** 2 + (ys - cy) ** 2))
        out[name] = (int(ys[i]), int(xs[i]))
    return out


def sea_distances(cells, start, diagonal=1.0):
    """Path length (cells) over water cells from start, 8-connected;
    a diagonal step costs "diagonal" (1: steps, sqrt(2): octile)."""
    h, w = cells.shape
    water = (cells != _geo.LAND).ravel()
    dist = np.full(h * w, np.inf)
    s = start[0] * w + start[1]
    dist[s] = 0
    heap = [(0.0, s)]
    steps = [(dy, dx, diagonal if dy and dx else 1.0)
             for dy in (-1, 0, 1) for dx in (-1, 0, 1) if dy or dx]
    while heap:
        d, i = heapq.heappop(heap)
        if d > dist[i]:
            continue
        y, x = divmod(i, w)
        for dy, dx, c in steps:
            ny, nx = y + dy, x + dx
            if 0 <= ny < h and 0 <= nx < w:
                j = ny * w + nx
                if water[j] and d + c < dist[j]:
                    dist[j] = d + c
                    heapq.heappush(heap, (d + c, j))
    return dist.reshape(h, w)


def routes(cells):
    """[(a, b, steps, octile, lo, hi)] for ROUTES: 8-connected steps
    (what the targets count) and octile length."""
    pc = port_cells(cells)
    memo = {}
    out = []
    for a, b, lo, hi in ROUTES:
        if a not in memo:
            memo[a] = (sea_distances(cells, pc[a]),
                       sea_distances(cells, pc[a], math.sqrt(2)))
        out.append((a, b, float(memo[a][0][pc[b]]),
                    float(memo[a][1][pc[b]]), lo, hi))
    return out


def empty_sea(cells, far=(20, 40)):
    """Shares of water cells farther than each of "far" cells from
    land (Euclidean: no land in the disc of that radius)."""
    land = cells == _geo.LAND
    water = ~land
    out = []
    for f in far:
        # A water cell is farther than f when no land is within f.
        hit = np.zeros(cells.shape, dtype=bool)
        for dy in range(-f, f + 1):
            dx = int(math.isqrt(f * f - dy * dy))
            row = np.zeros_like(land)
            if dy >= 0:
                row[dy:] = land[:land.shape[0] - dy]
            else:
                row[:dy] = land[-dy:]
            # Horizontal run of +-dx.
            c = np.cumsum(np.pad(row, ((0, 0), (dx + 1, dx)))
                          .astype(np.int32), axis=1)
            run = c[:, 2 * dx + 1:] - c[:, :-2 * dx - 1]
            hit |= run > 0
        out.append(float((water & ~hit).sum()) / water.sum())
    return out


def print_routes(cells):
    ok = True
    print("%-26s %5s %5s %7s  %s" % ("route", "steps", "days", "octile",
                                     "target"))
    for a, b, n, o, lo, hi in routes(cells):
        good = lo <= n <= hi
        ok &= good
        print("%-26s %5.0f %5.1f %7.0f  %d-%d %s"
              % (a + " - " + b, n, n / CELLS_A_DAY, o, lo, hi,
                 "pass" if good else "FAIL"))
    f20, f40 = empty_sea(cells)
    print("water farther than 20 cells from land %.1f%%, than 40 %.1f%%"
          % (100 * f20, 100 * f40))
    return ok


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    full = _geo.terrain_raster(1)
    cells = _geo.terrain_raster(_geo.CELL)
    total = full.size
    for v, n in ((_geo.LAND, "land"), (_geo.SHALLOWS, "shallows"),
                 (_geo.SEA, "sea")):
        print("%-9s pixels %5.1f%%  cells %5.1f%%"
              % (n, 100.0 * (full == v).sum() / total,
                 100.0 * (cells == v).sum() / cells.size))
    print("extent lon %.2f..%.2f lat %.3f..%.3f"
          % (_geo.LON_W, _geo.LON_E, _geo.LAT_S, _geo.LAT_N))
    print_routes(cells)
    world = COLOURS[cells]
    reach = open_sea(full)
    closed = []
    for name, lon, lat in PORTS:
        x, y = _geo.lonlat_to_world(lon, lat)
        px, py = int(x), int(y)
        here = {1: "shallows", 2: "land", 3: "sea"}[int(full[py, px])]
        dl = nearest(full, px, py, _geo.LAND)
        dw = min(d for d in (nearest(full, px, py, _geo.SHALLOWS),
                             nearest(full, px, py, _geo.SEA))
                 if d is not None)
        print("%-14s px (%5d, %4d) cell (%3d, %3d) %-8s to land %s px,"
              " to water %.0f px" % (name, px, py, px // 16, py // 16,
                                     here, "%.0f" % dl if dl is not None
                                     else "-", dw))
        d = nearest(reach.astype(np.uint8), px // OPEN_STEP,
                    py // OPEN_STEP, 1, reach=16 * APPROACH_CELLS)
        cells_off = d * OPEN_STEP / 16 if d is not None else None
        print("%-14s open sea %s cells off" % ("", "%.1f" % cells_off
                                              if d is not None
                                              else "> %d" % 16))
        if cells_off is None or cells_off > APPROACH_CELLS:
            closed.append(name)
        mark(world, px // 16, py // 16)
    png(os.path.join(out, "world.png"), world)
    for name, (lw, ln, le, ls) in CROPS:
        x0, y0 = map(int, _geo.lonlat_to_world(lw, ln))
        x1, y1 = map(int, _geo.lonlat_to_world(le, ls))
        crop = COLOURS[full[y0:y1, x0:x1]].copy()
        for _, lon, lat in PORTS:
            x, y = _geo.lonlat_to_world(lon, lat)
            if x0 <= x < x1 and y0 <= y < y1:
                mark(crop, int(x) - x0, int(y) - y0, 8)
        png(os.path.join(out, "crop-%s.png" % name), crop)
    if closed:
        print("CLOSED: no open sea within %d cells of %s"
              % (APPROACH_CELLS, ", ".join(closed)))
        sys.exit(1)


if __name__ == "__main__":
    main()
