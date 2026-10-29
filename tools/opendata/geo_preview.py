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
