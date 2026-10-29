#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""The open world's geography: a terrain raster of the Caribbean built
from Natural Earth (public domain), for the map, town and treasure
modules to share.

Extent and projection.  The world is 640 x 480 cells of 16 x 16 pixels,
10240 x 7680 pixels, x east and y south.  It is a spherical Mercator
(conformal, so coasts keep their real shapes and compass bearings are
straight lines, as on a period sea chart) from 98.5 W to 58.0 W, and
from 7.0 N up to the latitude that makes the frame exactly 4:3 (about
35.0 N).  That holds the whole theatre of 1560-1680: the Gulf of Mexico
with Veracruz and Tampico on the west edge, Florida and the Carolinas
coast up to Cape Hatteras, the Bahamas, the Greater and Lesser Antilles
with open sea east of Barbados, and the Spanish Main from Yucatan and
the Isthmus (both coasts of Panama) to Trinidad.  One degree of
longitude is about 253 pixels, a pixel about 440 m at the equator.

Terrain values are the engine's (src/assets/world.h): 1 shallows, 2
land, 3 sea.  Land is Natural Earth's 1:10m land plus its minor
islands, filled at pixel centres with an even-odd scanline rule; an
island under MIN_ISLAND_PX pixels is stamped as a 2 x 2 block so the
Keys, the Bahama cays and the Grenadines survive.  Shallows are the
coastal shoals and reefs a big ship wrecks on (src/sim/sim.c, reef()),
not the whole shelf: water shallower than 200 m (outside the 1:10m 200 m
bathymetry polygons) within SHELF_PX of land, plus every water pixel
within FRINGE_PX of land, so each coast has a fringe.  Distances are
octagonal (alternating square and diamond steps), close to round.
Where the shelf band would close a harbour's only approach, a CHANNELS
line (the real navigable channel, in degrees) is cut through it:
shallows within CHANNEL_PX of the line become sea, fringe included (the
Maracaibo strait is narrower than two fringes).

    lonlat_to_world(lon, lat) -> (x, y)    world pixels, floats
    world_to_lonlat(x, y) -> (lon, lat)
    terrain_raster(scale=1)                numpy uint8 [H / scale, W / scale]

The inputs are downloaded once into ~/.cache/flibustier-opendata/ and
checked against the SHA-256 below; the full-resolution raster is cached
there too, keyed by those hashes and the parameters.
"""
import hashlib
import json
import math
import os
import urllib.request

import numpy as np

WORLD_W = 10240
WORLD_H = 7680
CELL = 16

LON_W = -98.5
LON_E = -58.0
LAT_S = 7.0

MIN_ISLAND_PX = 4
FRINGE_PX = 3
SHELF_PX = 24
CHANNEL_PX = 6
# Navigable channels through the shelf band, as (lon, lat) polylines.
CHANNELS = {
    # Lake Maracaibo to the Gulf of Venezuela: the strait past
    # Maracaibo, Tablazo Bay and the bar off San Carlos.
    "maracaibo": [(-71.59, 10.24), (-71.57, 10.47), (-71.58, 10.65),
                  (-71.57, 10.72), (-71.61, 10.82), (-71.60, 11.02),
                  (-71.65, 11.11), (-71.69, 11.29)],
}
RASTER_VERSION = 2

SHALLOWS, LAND, SEA = 1, 2, 3

CACHE = os.path.join(os.path.expanduser("~"), ".cache",
                     "flibustier-opendata")
_NE = ("https://raw.githubusercontent.com/nvkelso/natural-earth-vector/"
       "v5.1.2/geojson/")
SOURCES = {
    "land": (_NE + "ne_10m_land.geojson",
             "1ac90796408bc6ad6911d69448485d3c4dbf2190370080368a09976e1c9f7416"),
    "minor_islands": (_NE + "ne_10m_minor_islands.geojson",
                      "8c933ca7a4760256bdc46408355706e39764b0fa01c160c28888b90b4faec29f"),
    "deep": (_NE + "ne_10m_bathymetry_K_200.geojson",
             "5c6c182da8608153ea2dce22dfb32c987e5390e0bd4a122266143ba181a7b636"),
}

# Mercator: px per radian of longitude, and the northern edge.
_K = WORLD_W / math.radians(LON_E - LON_W)


def _merc(lat):
    return np.log(np.tan(np.pi / 4 + np.radians(lat) / 2))


_Y_S = float(_merc(LAT_S))
_Y_N = _Y_S + WORLD_H / _K
LAT_N = math.degrees(math.atan(math.sinh(_Y_N)))


def lonlat_to_world(lon, lat):
    """World pixel (x, y) of a longitude and latitude in degrees (east
    and north positive).  Works on scalars or numpy arrays."""
    lat = np.clip(lat, -85.0, 85.0)
    x = np.radians(np.asarray(lon, dtype=float) - LON_W) * _K
    y = (_Y_N - _merc(lat)) * _K
    if np.ndim(x) == 0:
        return float(x), float(y)
    return x, y


def world_to_lonlat(x, y):
    """Longitude and latitude in degrees of a world pixel position."""
    lon = LON_W + np.degrees(np.asarray(x, dtype=float) / _K)
    lat = np.degrees(np.arctan(np.sinh(_Y_N - np.asarray(y, dtype=float)
                                       / _K)))
    if np.ndim(lon) == 0:
        return float(lon), float(lat)
    return lon, lat


def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def source(name):
    """Path of a checked input file, downloading it if needed."""
    url, sha = SOURCES[name]
    path = os.path.join(CACHE, url.rsplit("/", 1)[1])
    if not os.path.exists(path):
        os.makedirs(CACHE, exist_ok=True)
        tmp = path + ".part"
        with urllib.request.urlopen(url) as r, open(tmp, "wb") as f:
            while True:
                chunk = r.read(1 << 20)
                if not chunk:
                    break
                f.write(chunk)
        os.replace(tmp, path)
    got = _sha256(path)
    if got != sha:
        raise RuntimeError("%s: SHA-256 %s, expected %s (from %s)"
                           % (path, got, sha, url))
    return path


def _polygons(name):
    """Each polygon of a layer that may touch the world, as a list of
    rings in world pixels (n x 2 float arrays)."""
    margin = 1.0
    with open(source(name), "rb") as f:
        features = json.load(f)["features"]
    for feat in features:
        g = feat["geometry"]
        if g is None:
            continue
        polys = g["coordinates"] if g["type"] == "MultiPolygon" \
            else [g["coordinates"]]
        for poly in polys:
            rings = [np.asarray(r, dtype=float) for r in poly]
            lo = rings[0].min(axis=0)
            hi = rings[0].max(axis=0)
            if (hi[0] < LON_W - margin or lo[0] > LON_E + margin or
                    hi[1] < LAT_S - margin or lo[1] > LAT_N + margin):
                continue
            out = []
            for r in rings:
                x, y = lonlat_to_world(r[:, 0], r[:, 1])
                out.append(np.stack([x, y], axis=1))
            yield out


def _spans(rings):
    """Pixel spans [start, end) per row covered by a polygon, even-odd
    at pixel centres: (rows, starts, ends) clipped to the world."""
    x0s, y0s, x1s, y1s = [], [], [], []
    for r in rings:
        if len(r) < 3:
            continue
        a = r
        b = np.roll(r, -1, axis=0)
        x0s.append(a[:, 0]); y0s.append(a[:, 1])
        x1s.append(b[:, 0]); y1s.append(b[:, 1])
    empty = np.zeros(0, dtype=np.int64)
    if not x0s:
        return empty, empty, empty
    x0 = np.concatenate(x0s); y0 = np.concatenate(y0s)
    x1 = np.concatenate(x1s); y1 = np.concatenate(y1s)
    ymin = np.minimum(y0, y1)
    ymax = np.maximum(y0, y1)
    # Rows whose centre r + 0.5 is in [ymin, ymax).
    r0 = np.clip(np.ceil(ymin - 0.5), 0, WORLD_H).astype(np.int64)
    r1 = np.clip(np.ceil(ymax - 0.5), 0, WORLD_H).astype(np.int64)
    n = r1 - r0
    keep = n > 0
    if not keep.any():
        return empty, empty, empty
    x0, y0, x1, y1, r0, n = x0[keep], y0[keep], x1[keep], y1[keep], \
        r0[keep], n[keep]
    edge = np.repeat(np.arange(len(n)), n)
    first = np.repeat(np.cumsum(n) - n, n)
    rows = r0[edge] + (np.arange(len(edge)) - first)
    yc = rows + 0.5
    xs = x0[edge] + (yc - y0[edge]) * (x1[edge] - x0[edge]) / \
        (y1[edge] - y0[edge])
    order = np.lexsort((xs, rows))
    rows = rows[order][0::2]
    xa = xs[order][0::2]
    xb = xs[order][1::2]
    starts = np.clip(np.ceil(xa - 0.5), 0, WORLD_W).astype(np.int64)
    ends = np.clip(np.ceil(xb - 0.5), 0, WORLD_W).astype(np.int64)
    ok = ends > starts
    return rows[ok], starts[ok], ends[ok]


def _fill(names, islands):
    """Boolean mask of the union of the layers' polygons.  With
    "islands", stamp tiny ones and return each polygon's
    (x, y, x0, y0, x1, y1): a pixel on it and its bounding box."""
    rows, starts, ends, reps = [], [], [], []
    for name in names:
        for rings in _polygons(name):
            r, s, e = _spans(rings)
            if islands:
                outer = rings[0]
                lo = np.floor(outer.min(axis=0)).astype(int)
                hi = np.ceil(outer.max(axis=0)).astype(int)
                if int((e - s).sum()) < MIN_ISLAND_PX:
                    cx, cy = outer[:-1].mean(axis=0) if len(outer) > 1 \
                        else outer[0]
                    px = int(math.floor(cx - 0.5))
                    py = int(math.floor(cy - 0.5))
                    if not (0 <= px < WORLD_W - 1 and
                            0 <= py < WORLD_H - 1):
                        continue
                    r = np.array([py, py + 1])
                    s = np.array([px, px])
                    e = np.array([px + 2, px + 2])
                    rep = (px, py)
                else:
                    i = int(np.argmax(e - s))
                    rep = ((int(s[i]) + int(e[i])) // 2, int(r[i]))
                reps.append(rep + (lo[0], lo[1], hi[0], hi[1]))
            rows.append(r); starts.append(s); ends.append(e)
    rows = np.concatenate(rows)
    starts = np.concatenate(starts)
    ends = np.concatenate(ends)
    w = WORLD_W + 1
    diff = np.bincount(rows * w + starts, minlength=WORLD_H * w) - \
        np.bincount(rows * w + ends, minlength=WORLD_H * w)
    cover = np.cumsum(diff.reshape(WORLD_H, w), axis=1)[:, :WORLD_W]
    return cover > 0, np.array(reps, dtype=np.int64).reshape(-1, 6)


def _dilate(mask, radius):
    """Octagonal dilation: "radius" one-pixel steps, alternately the
    8 neighbours and the 4 neighbours."""
    out = mask.copy()
    for i in range(radius):
        h = out.copy()
        h[:, 1:] |= out[:, :-1]
        h[:, :-1] |= out[:, 1:]
        if i % 2 == 0:
            src = h
        else:
            src = out
        v = h.copy()
        v[1:, :] |= src[:-1, :]
        v[:-1, :] |= src[1:, :]
        out = v
    return out


def _channels(t):
    """Cut CHANNELS through the shallows."""
    r = CHANNEL_PX
    for line in CHANNELS.values():
        x, y = lonlat_to_world(np.array([p[0] for p in line]),
                               np.array([p[1] for p in line]))
        for ax, ay, bx, by in zip(x[:-1], y[:-1], x[1:], y[1:]):
            x0 = max(int(min(ax, bx)) - r - 1, 0)
            y0 = max(int(min(ay, by)) - r - 1, 0)
            x1 = min(int(max(ax, bx)) + r + 2, WORLD_W)
            y1 = min(int(max(ay, by)) + r + 2, WORLD_H)
            py, px = np.mgrid[y0:y1, x0:x1] + 0.5
            dx, dy = bx - ax, by - ay
            u = np.clip(((px - ax) * dx + (py - ay) * dy) /
                        (dx * dx + dy * dy), 0, 1)
            d2 = (px - ax - u * dx) ** 2 + (py - ay - u * dy) ** 2
            win = t[y0:y1, x0:x1]
            cut = (d2 <= r * r) & (win == SHALLOWS)
            win[cut] = SEA


def _key():
    h = hashlib.sha256()
    h.update(json.dumps([sorted((k, v[1]) for k, v in SOURCES.items()),
                         WORLD_W, WORLD_H, LON_W, LON_E, LAT_S,
                         MIN_ISLAND_PX, FRINGE_PX, SHELF_PX,
                         CHANNEL_PX, sorted(CHANNELS.items()),
                         RASTER_VERSION]
                        ).encode())
    return h.hexdigest()[:16]


def _build():
    land, reps = _fill(["land", "minor_islands"], True)
    deep, _ = _fill(["deep"], False)
    t = np.full((WORLD_H, WORLD_W), SEA, dtype=np.uint8)
    t[~deep & _dilate(land, SHELF_PX)] = SHALLOWS
    t[_dilate(land, FRINGE_PX)] = SHALLOWS
    t[land] = LAND
    _channels(t)
    return t, reps


_memo = {}


def _full():
    if "t" in _memo:
        return _memo["t"], _memo["reps"]
    path = os.path.join(CACHE, "terrain-%s.npz" % _key())
    if os.path.exists(path):
        with np.load(path) as z:
            t, reps = z["terrain"], z["islands"]
    else:
        t, reps = _build()
        os.makedirs(CACHE, exist_ok=True)
        tmp = path + ".part.npz"
        np.savez_compressed(tmp, terrain=t, islands=reps)
        os.replace(tmp, path)
    _memo["t"], _memo["reps"] = t, reps
    return t, reps


def terrain_raster(scale=1):
    """The world's terrain (SHALLOWS, LAND, SEA) at 1 / scale of pixel
    resolution: scale 1 is pixels, CELL (16) is map cells.

    A block is land when at least half its pixels are, sea when it has
    no land and more than half sea, shallows otherwise.  Every island
    keeps at least one land block: when its own block is not land and
    no block over its bounding box is, the block holding a pixel of it
    becomes land."""
    t, reps = _full()
    if scale == 1:
        return t.copy()
    if WORLD_W % scale or WORLD_H % scale:
        raise ValueError("scale %d does not divide the world" % scale)
    h, w = WORLD_H // scale, WORLD_W // scale
    b = t.reshape(h, scale, w, scale)
    n = scale * scale
    nland = (b == LAND).sum(axis=(1, 3))
    nsea = (b == SEA).sum(axis=(1, 3))
    out = np.full((h, w), SHALLOWS, dtype=np.uint8)
    out[(nland == 0) & (2 * nsea > n)] = SEA
    out[2 * nland >= n] = LAND
    for x, y, x0, y0, x1, y1 in reps:
        cx, cy = x // scale, y // scale
        if out[cy, cx] == LAND:
            continue
        bx0, by0 = max(x0 // scale, 0), max(y0 // scale, 0)
        bx1, by1 = min(x1 // scale + 1, w), min(y1 // scale + 1, h)
        if not (out[by0:by1, bx0:bx1] == LAND).any():
            out[cy, cx] = LAND
    return out
