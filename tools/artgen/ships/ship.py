#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Build a 16th-17th century sailing ship in Blender and render it.

The ships are built in metres from the measures of ships.md, which
gives their sources (and marks what is estimated).  Two views:

- Sailing: a three-quarter view from the south at the game's sixteen
  headings, clockwise from north (frame = heading >> 4), the same
  sixteen for sailing (spr/shipN) and for sea battles (cur/shipNa holds
  the first eight, cur/shipNb the last), every heading the very same
  ship.
- The sea battle's damage picture: the side view, bow to the left, from
  whole to sinking.

    blender -b -P ship.py -- --size 2 --out DIR [--frame 156x116]
                         [--headings 16] [--blend MODEL.blend]
    blender -b -P ship.py -- --class galleon --damage 5 --out DIR
                         [--frame 1304x776]

writes DIR/0.png to DIR/15.png, transparent around the ship, or with
--damage DIR/d0.png (whole) to d4.png (sinking); with --blend, the model
itself, to open and change in Blender.
"""

import argparse
import math
import os
import sys

import bpy
import bmesh

# The photographic materials (../materials.py, copied beside this file
# on the render host).
sys.path[:0] = [os.path.dirname(os.path.abspath(__file__)),
                os.path.dirname(os.path.dirname(os.path.abspath(__file__)))]
import materials			# noqa: E402
from mathutils import Vector


def ship(L, B, free, stern, stern_len, bow, bow_len, main, rig,
         **extra):
    """A class's measures (ships.md): length on deck, beam, freeboard,
    the sterncastle's and forecastle's height above the deck and length
    (fraction of L), the mainmast's height above the deck, and per mast
    from the stern (position, height as a fraction of the main, square
    sails stacked, or "lateen", "lateen+1" with a square topsail, or
    "gaff")."""
    masts = [(pos, main * h, sails) for pos, h, sails in rig]
    return dict(length=L, beam=B, sheer=free, stern=stern,
                stern_len=stern_len, bow=bow, bow_len=bow_len, masts=masts,
                **extra)


# The game's nine ship classes (ships.md for each figure).
CLASSES = {
    "pinnace": ship(22.0, 6.2, 1.8, 3.0, 0.30, 1.0, 0.12, 21.3,
                    [(0.22, 0.75, "lateen"), (0.50, 1.0, 2),
                     (0.83, 0.85, 2)], spritsail=1, sprit_rake=35,
                    paint=("dark oak", "dark oak", "black")),
    "sloop": ship(18.0, 5.5, 1.2, 0.3, 0.15, 0.0, 0.0, 20.4,
                  [(0.62, 1.0, "gaff")], rake=10, bowsprit=0.55,
                  sprit_rake=10, jibs=2,
                  paint=("cedar", "cedar", "black")),
    "barque": ship(21.0, 5.2, 1.5, 3.5, 0.30, 2.0, 0.15, 19.5,
                   [(0.22, 0.70, "lateen"), (0.52, 1.0, 2),
                    (0.85, 0.85, 2)], spritsail=1, sprit_rake=35,
                   paint=("tar", "tar", "red")),
    "fluyt": ship(35.0, 8.0, 1.8, 3.0, 0.25, 0.5, 0.08, 29.7,
                  [(0.22, 0.72, "lateen"), (0.53, 1.0, 3),
                   (0.86, 0.88, 3)], spritsail=1, sprit_rake=30,
                  tumblehome=0.65, paint=("varnish", "green", "ochre")),
    "merchantman": ship(27.0, 7.7, 2.2, 4.0, 0.32, 2.0, 0.18, 27.0,
                        [(0.22, 0.70, "lateen"), (0.52, 1.0, 2),
                         (0.86, 0.85, 2)], spritsail=1, sprit_rake=38,
                        paint=("black", "buff", "black")),
    "frigate": ship(34.0, 8.2, 2.2, 2.5, 0.30, 1.0, 0.12, 31.8,
                    [(0.22, 0.72, "lateen+1"), (0.52, 1.0, 3),
                     (0.86, 0.88, 3)], spritsail=2, sprit_rake=30,
                    beakhead=0.06,	# "a short beakhead" (ships.md)
                    paint=("black", "black", "ochre")),
    "war galleon": ship(48.0, 11.7, 4.5, 7.0, 0.33, 3.0, 0.18, 43.0,
                        [(0.22, 0.75, "lateen+1"), (0.52, 1.0, 3),
                         (0.86, 0.90, 3)], spritsail=2, sprit_rake=35,
                        tumblehome=0.80, beakhead=0.10, gundecks=2,
                        paint=("dark oak", "deep red", "gold"), gilt=True),
    "galleon": ship(40.0, 10.0, 3.0, 5.0, 0.35, 3.0, 0.18, 33.9,
                    [(0.22, 0.70, "lateen"), (0.52, 1.0, 2),
                     (0.86, 0.88, 2)], spritsail=2, sprit_rake=35,
                    tumblehome=0.85, beakhead=0.10,
                    paint=("dark oak", "ochre", "dark red"), gilt=True),
    "fast galleon": ship(31.0, 7.0, 2.5, 3.5, 0.30, 1.5, 0.15, 27.0,
                         [(0.22, 0.70, "lateen"), (0.52, 1.0, 3),
                          (0.86, 0.85, 2)], spritsail=1, sprit_rake=32,
                         beakhead=0.06, paint=("red", "black", "gold")),
}

# The game's three sailing sizes, each drawn as one class (ships.md).
SIZES = {0: CLASSES["pinnace"], 1: CLASSES["merchantman"],
         2: CLASSES["galleon"]}

BRACE = 30		# degrees the yards are braced round
SPRITSAILS = True	# set the sprit sails (see rig())
SPRIT_SIZE = 0.55	# the spritsail's width, of the beam

# The paints of ships.md, for each class's lower hull, upper works and
# wales (their "paint"), and its gun ports and gilding.
PAINTS = {
    "dark oak": (0.08, 0.05, 0.03), "cedar": (0.32, 0.16, 0.08),
    "tar": (0.05, 0.035, 0.025), "varnish": (0.10, 0.055, 0.025),
    "black": (0.018, 0.016, 0.015), "green": (0.07, 0.17, 0.09),
    "ochre": (0.52, 0.35, 0.11), "red": (0.42, 0.05, 0.03),
    "buff": (0.50, 0.44, 0.30),
    "deep red": (0.30, 0.025, 0.02), "dark red": (0.22, 0.03, 0.02),
    "gold": (0.80, 0.58, 0.16),
}


def material(name, colour, rough=0.7, metal=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*colour, 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    return m


def mesh_object(name, bm, mat):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    ob.data.materials.append(mat)
    bpy.context.collection.objects.link(ob)
    return ob


def half_width(t, beam):
    """The hull's half beam at t (-1 stern .. 1 bow): full aft, fine
    forward."""
    if t >= 0:
        return beam / 2 * max(0.0, 1 - t ** 2.2)
    return beam / 2 * max(0.35, 1 - (-t) ** 5)


def sheer_at(t, s):
    """The top of the side at t: the freeboard, rising to the castles."""
    top = s["sheer"]
    aft = -1 + 2 * s["stern_len"]		# where the sterncastle begins
    fore = 1 - 2 * s["bow_len"]
    # The castles rise in a slope over most of their length, not as a
    # box (which, seen from ahead, an image model takes for a tower).
    if t < aft:
        top += s["stern"] * min(1.0, (aft - t) / (0.7 * (aft + 1)))
    if s["bow_len"] and t > fore:
        top += s["bow"] * min(1.0, (t - fore) / 0.08)
    return top


def hull(s, mats):
    """The hull from cross sections cut flat at the waterline (the
    sprites show no more of it), its sides drawing in towards the top by
    the tumblehome, with the deck and the transom."""
    L, B = s["length"], s["beam"]
    tumble = s.get("tumblehome", 0.92)
    n = 32
    bm = bmesh.new()
    rings = []
    for i in range(n + 1):
        t = -1 + 2 * i / n
        y = t * L / 2
        w = half_width(t, B)
        top = sheer_at(t, s)
        mid = s["sheer"] * 0.45
        pts = [(-w * tumble, top), (-w * 1.0, mid), (-w * 0.96, 0.0),
               (w * 0.96, 0.0), (w * 1.0, mid), (w * tumble, top)]
        rings.append([bm.verts.new((x, y, z)) for x, z in pts])
    for a, b in zip(rings, rings[1:]):
        for k in range(len(a) - 1):
            f = bm.faces.new((a[k], a[k + 1], b[k + 1], b[k]))
            # The upper works, above the hull's middle, in their paint.
            f.material_index = 1 if k in (0, len(a) - 2) else 0
    bm.faces.new(list(reversed(rings[0]))).material_index = 1  # transom
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    edges = [(r[0].co.copy(), r[-1].co.copy()) for r in rings]
    ob = mesh_object("hull", bm, mats["hull"])
    ob.data.materials.append(mats["upper"])
    # The deck: a band over the tops of the sections, a little lower.
    bm = bmesh.new()
    left = [bm.verts.new((l.x * 0.97, l.y, l.z - 0.3)) for l, _ in edges]
    right = [bm.verts.new((r.x * 0.97, r.y, r.z - 0.3)) for _, r in edges]
    for k in range(n):
        bm.faces.new((left[k], right[k], right[k + 1], left[k + 1]))
    mesh_object("deck", bm, mats["deck"])
    # A painted wale along each side, and a band over each gun deck.
    for d in range(s.get("gundecks", 1) + 1):
        z0 = s["sheer"] * (0.30 + 0.32 * d) / (1 + 0.3 * d)
        for side in (-1, 1):
            bm = bmesh.new()
            prev = None
            for i in range(2, n - 1):
                t = -1 + 2 * i / n
                w = half_width(t, B) * 1.02
                v1 = bm.verts.new((side * w, t * L / 2, z0))
                v2 = bm.verts.new((side * w, t * L / 2, z0 + 0.45))
                if prev:
                    bm.faces.new((prev[0], v1, v2, prev[1]))
                prev = (v1, v2)
            mesh_object("wale", bm, mats["wale"])
    # A row of gun ports along each gun deck, between the wales.
    ports = max(3, round(L / 3.2))
    for d in range(s.get("gundecks", 1)):
        z0 = s["sheer"] * (0.30 + 0.32 * d) / (1 + 0.3 * d) + 0.45
        h = min(0.8, s["sheer"] * 0.18)
        for side in (-1, 1):
            for p in range(ports):
                t = -0.75 + 1.4 * (p + 0.5) / ports
                w = half_width(t, B) * 1.025
                y = t * L / 2
                bm = bmesh.new()
                vs = [bm.verts.new((side * w, y + dy, z0 + dz))
                      for dy, dz in ((-0.35, 0), (0.35, 0), (0.35, h),
                                     (-0.35, h))]
                bm.faces.new(vs)
                mesh_object("port", bm, mats["port"])
                # Its lid, open: hinged at the port's top, swung out and
                # up, red inside as the period's were.
                bm = bmesh.new()
                out = side * 0.55 * h
                vs = [bm.verts.new((side * w + dx, y + dy, z0 + h + dz))
                      for dy, dx, dz in ((-0.37, 0, 0), (0.37, 0, 0),
                                         (0.37, out, h * 0.55),
                                         (-0.37, out, h * 0.55))]
                bm.faces.new(vs)
                mesh_object("lid", bm, mats["lid"])
    if s.get("gilt"):
        # Gilded carving along the top of the sterncastle and transom.
        aft = -1 + 2 * s["stern_len"]
        for side in (-1, 1):
            bm = bmesh.new()
            prev = None
            for i in range(0, n + 1):
                t = -1 + 2 * i / n
                if t > aft:
                    break
                w = half_width(t, B) * tumble * 1.02
                top = sheer_at(t, s)
                v1 = bm.verts.new((side * w, t * L / 2, top - 0.9))
                v2 = bm.verts.new((side * w, t * L / 2, top))
                if prev:
                    bm.faces.new((prev[0], v1, v2, prev[1]))
                prev = (v1, v2)
            mesh_object("gilt", bm, mats["gilt"])
    if s.get("beakhead"):
        # A beakhead: a narrow platform jutting forward from the bow.
        tip = L / 2 + L * s["beakhead"]
        z = s["sheer"] * 0.8
        bm = bmesh.new()
        vs = [bm.verts.new(p) for p in ((-0.6, L / 2 - 1.0, z),
                                        (0.6, L / 2 - 1.0, z),
                                        (0.0, tip, z + 0.5),
                                        (0.0, L / 2 - 1.0, z - 1.2))]
        bm.faces.new((vs[0], vs[1], vs[2]))
        bm.faces.new((vs[0], vs[2], vs[3]))
        bm.faces.new((vs[1], vs[3], vs[2]))
        mesh_object("beakhead", bm, mats["wale"])
        # Its knee, the curved timber from the stem out under it; the
        # head rails sweeping from the bow's sides out to the figure;
        # the figurehead, a gilded lion, on the knee's end.
        stem = (0.0, L / 2 - 0.4, z - 2.4)
        knee = (0.0, tip - 0.3, z + 0.1)
        cylinder("knee", stem, knee, 0.22, mats["wale"])
        for side in (-1, 1):
            for k, dz in enumerate((0.9, 0.35)):
                a_ = (side * half_width(0.9, B) * 0.9, L / 2 - 2.2,
                      z + dz + 0.3)
                mid = (side * 0.45, (L / 2 + tip) / 2, z + dz * 0.6)
                cylinder("headrail", a_, mid, 0.07, mats["wale"])
                cylinder("headrail", mid, knee, 0.07, mats["wale"])
        bpy.ops.mesh.primitive_uv_sphere_add(radius=1, segments=16,
                                             ring_count=8,
                                             location=(0.0, tip - 0.1,
                                                       z + 0.45))
        fig = bpy.context.object
        fig.name = "figurehead"
        fig.scale = (0.35, 0.6, 0.45)
        fig.data.materials.append(mats["gilt"])
    return ob


def cylinder(name, a, b, r, mat):
    """A spar from a to b."""
    a, b = Vector(a), Vector(b)
    d = b - a
    bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=r,
                                        depth=d.length,
                                        location=(a + b) / 2)
    ob = bpy.context.object
    ob.name = name
    ob.rotation_mode = "QUATERNION"
    ob.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(d)
    ob.data.materials.append(mat)
    return ob


def sail(name, y, z0, z1, w0, w1, belly, mat):
    """A square sail hanging from a yard at z1 down to z0, its middle
    blown forward by "belly"."""
    bm = bmesh.new()
    nx, nz = 8, 6
    grid = []
    for j in range(nz + 1):
        v = j / nz
        z = z0 + (z1 - z0) * v
        w = w0 + (w1 - w0) * v
        row = []
        for i in range(nx + 1):
            u = -1 + 2 * i / nx
            bulge = belly * (1 - u * u) * math.sin(math.pi * v)
            row.append(bm.verts.new((u * w / 2, y + bulge, z)))
        grid.append(row)
    for j in range(nz):
        for i in range(nx):
            bm.faces.new((grid[j][i], grid[j][i + 1], grid[j + 1][i + 1],
                          grid[j + 1][i]))
    ob = mesh_object(name, bm, mat)
    mod = ob.modifiers.new("solid", "SOLIDIFY")
    mod.thickness = 0.05
    return ob


def flat(name, pts, mat, dx=0.25):
    """A flat sail through the points, set off the mast by dx."""
    bm = bmesh.new()
    vs = [bm.verts.new((p[0] + dx, p[1], p[2])) for p in pts]
    bm.faces.new(vs)
    ob = mesh_object(name, bm, mat)
    ob.modifiers.new("solid", "SOLIDIFY").thickness = 0.05
    return ob


def square_sails(m, y, base, height, count, start, s, mats, info):
    """"count" square sails up the mast from "start" of its height, each
    narrower than the one below, braced round so they show at every
    heading."""
    yard0 = s["beam"] * 1.9
    for k in range(count):
        lo = start + (0.97 - start) * k / count
        hi = start + (0.97 - start) * (k + 1) / count
        z0, z1 = base + height * lo, base + height * hi
        width = yard0 * (1 - 0.22 * (k + (1 if start > 0.5 else 0)))
        brace = bpy.data.objects.new("brace%d_%d" % (m, k), None)
        brace.location = (0, y, 0)
        brace.rotation_euler = (0, 0, math.radians(BRACE))
        bpy.context.collection.objects.link(brace)
        r = 0.005 * s["length"]
        parts = (cylinder("yard%d_%d" % (m, k), (-width / 2, 0.3, z1),
                          (width / 2, 0.3, z1), r, mats["spar"]),
                 sail("sail%d_%d" % (m, k), 0.4, z0 + 0.2, z1,
                      width * 0.9, width * 0.82, width * 0.08,
                      mats["sail"]))
        for ob in parts:
            ob.parent = brace
        info["levels"].append([brace, *parts])


def rig(s, mats):
    """The masts and their sails; returns, per mast from the stern, the
    objects that make it and its sails from the bottom up, for the
    damage."""
    L = s["length"]
    masts = []
    for m, (pos, height, kind) in enumerate(s["masts"]):
        y = -L / 2 + pos * L
        base = s["sheer"] - 0.3
        rake = math.radians(s.get("rake", 0))
        top = (0, y - height * math.sin(rake),
               base + height * math.cos(rake))
        before = set(bpy.data.objects)
        mast = cylinder("mast%d" % m, (0, y, base), top,
                        0.009 * L + 0.1, mats["spar"])
        info = {"y": y, "base": base, "height": height, "mast": mast,
                "levels": [], "fore_aft": kind in ("lateen", "lateen+1",
                                                   "gaff")}
        masts.append(info)
        if kind == "gaff":
            # A sloop's rig: the mainsail between a boom and a gaff aft of
            # the mast, the jibs to the bowsprit, a square topsail.
            lean = math.sin(rake)
            boom = (0, y - L * 0.48, base + 1.0)
            throat = (0, y - height * 0.6 * lean, base + height * 0.6)
            peak = (0, y - L * 0.40, base + height * 0.70)
            cylinder("boom%d" % m, (0, y, base + 1.0), boom, 0.12,
                     mats["spar"])
            cylinder("gaff%d" % m, throat, peak, 0.1, mats["spar"])
            flat("mainsail%d" % m, ((0, y - 0.2, base + 1.1),
                                    (0, throat[1] - 0.2, throat[2]),
                                    peak, boom), mats["sail"])
            tip = L / 2 + L * s.get("bowsprit", 0.35)
            for j in range(s.get("jibs", 1)):
                flat("jib%d" % j,
                     ((0, y + 0.3 - height * (0.85 - 0.12 * j) * lean,
                       base + height * (0.85 - 0.12 * j)),
                      (0, y + 0.3, base + 1.4),
                      (0, L / 2 + (tip - L / 2) * (1 - 0.4 * j),
                       s["sheer"] + 0.8)), mats["sail"], 0.25 + 0.3 * j)
            info["levels"].append(list(set(bpy.data.objects) - before -
                                       {mast}))
            square_sails(m, y - height * 0.85 * lean, base, height, 1,
                         0.78, s, mats, info)
        elif isinstance(kind, str) and kind.startswith("lateen"):
            # A lateen sail: a triangle under a long yard slanting down
            # forward, along the hull; "+1" a square topsail above it.
            lt = (0, y - s["beam"] * 1.1, base + height * 0.78)
            lb = (0, y + s["beam"] * 1.0, base + height * 0.18)
            cylinder("lateen%d" % m, lt, lb, 0.008 * L, mats["spar"])
            flat("lateensail%d" % m, (lt, lb, (0, y - s["beam"] * 0.6,
                                               base + height * 0.15)),
                 mats["sail"], 0.5)
            info["levels"].append(list(set(bpy.data.objects) - before -
                                       {mast}))
            if kind == "lateen+1":
                square_sails(m, y, base, height, 1, 0.80, s, mats, info)
        else:
            square_sails(m, y, base, height, kind, 0.28, s, mats, info)
        # Its standing rigging, tarred rope: shrouds from the lower top
        # down to the hull's sides each side, then from the masthead to
        # the top's crosstrees, and a stay forward from the top.
        rope = mats["rope"]
        tw = half_width(2 * y / L, s["beam"]) * 1.03
        lower = base + height * 0.55
        made = []
        for side in (-1, 1):
            for k in range(6):
                dy = -1.1 + 0.44 * k
                made.append(cylinder("shroud%d" % m, (0, y, lower),
                                     (side * tw, y + dy, s["sheer"] + 0.2),
                                     0.035, rope))
            for k in range(2):
                made.append(cylinder("topshroud%d" % m,
                                     (0, top[1], base + height * 0.92),
                                     (side * 1.1, y - 0.4 + 0.8 * k, lower),
                                     0.025, rope))
        # Backstays from the masthead down aft to the hull's sides.
        for side in (-1, 1):
            made.append(cylinder("backstay%d" % m,
                                 (0, top[1], base + height * 0.92),
                                 (side * tw, y - 2.6, s["sheer"] + 0.2),
                                 0.025, rope))
        if pos < 0.95:
            ahead = -L / 2 + min(1.0, pos + 0.32) * L
            made.append(cylinder("stay%d" % m, (0, y, lower),
                                 (0, ahead, s["sheer"] + 0.5), 0.045, rope))
        if info["levels"]:
            info["levels"][0].extend(made)
        else:
            info["levels"].append(made)
        # A pennant at the masthead.
        bm = bmesh.new()
        vs = [bm.verts.new(p) for p in (top, (0, top[1] - 0.07 * L,
                                               top[2] - 0.3),
                                         (0, top[1], top[2] - 0.7))]
        bm.faces.new(vs)
        info["pennant"] = mesh_object("pennant%d" % m, bm, mats["flag"])
    # The bowsprit, raked up from the bow, and its sprit sails: a
    # spritsail under it, a spritsail topsail at its end.
    rake = math.radians(s.get("sprit_rake", 35))
    length = L * s.get("bowsprit", 0.35)
    root = (0, L / 2 - 1.0, s["sheer"] + s["bow"])
    end = (0, root[1] + length * math.cos(rake),
           root[2] + length * math.sin(rake))
    cylinder("bowsprit", root, end, 0.006 * L + 0.1, mats["spar"])
    # As the period's: the spritsail under the bowsprit, the sprit
    # topsail standing on its end (jibs came in only about 1700).
    for k in range(s.get("spritsail", 0) if SPRITSAILS else 0):
        # Small and well out on the bowsprit, so the bow stays in view.
        at = 0.85 if k == 0 else 1.0
        y = root[1] + (end[1] - root[1]) * at
        z = root[2] + (end[2] - root[2]) * at
        w = s["beam"] * SPRIT_SIZE * (1.0 if k == 0 else 0.7)
        if k == 0:			# hanging below the bowsprit
            z0, z1 = z - w * 0.55, z
        else:				# standing on its end
            z0, z1 = z, z + w * 0.6
        # Braced round as the masts' yards are: unbraced, the side's
        # view sees it edge on, a strip hanging from the bowsprit.
        brace = bpy.data.objects.new("spritbrace%d" % k, None)
        brace.location = (0, y, 0)
        brace.rotation_euler = (0, 0, math.radians(BRACE))
        bpy.context.collection.objects.link(brace)
        sp = sail("spritsail%d" % k, 0, z0, z1, w, w * 0.9, w * 0.05,
                  mats["sail"])
        sp.parent = brace
    return masts


def remove(obs):
    for ob in obs:
        if ob is not None and ob.name in bpy.data.objects:
            bpy.data.objects.remove(ob, do_unlink=True)


def break_mast(m, s, pivot, mats, fall):
    """Mast "m" shot away: a stump, its sails gone, and with "fall" the
    mast lying fallen over the side, aft."""
    for part in m["levels"]:
        remove(part)
    m["levels"] = []
    remove([m.pop("pennant", None), m["mast"]])
    top = m["base"] + m["height"] * 0.12
    m["mast"] = cylinder("stump", (0, m["y"], m["base"]), (0, m["y"], top),
                         0.01 * s["length"] + 0.12, mats["spar"])
    m["mast"].parent = pivot
    m["broken"] = True
    if fall:
        ob = cylinder("fallen", (0, m["y"], top),
                      (s["beam"] * 0.4, m["y"] - s["length"] * 0.45,
                       -0.5), 0.009 * s["length"] + 0.1, mats["spar"])
        ob.parent = pivot


def damage(level, s, masts, pivot, mats):
    """Take the ship one step further from whole (level 0) to sinking
    (4); each step keeps the earlier ones' damage, and each from the
    second on takes masts away: the mizzen, then the mainmast, then the
    rest.  Torn sails, holes, smoke and fire are left to the redraw:
    this is the shape.  Masts run from the stern (masts[0]) forward."""
    main = masts[len(masts) // 2] if masts else None
    if level == 1 and main and len(main["levels"]) > 1:
        # One sail shot away, high on the mainmast.
        remove(main["levels"].pop())
    elif level == 2:
        # The topmasts shot away, and the mizzen gone by the board.
        for m in masts:
            remove([m.pop("pennant", None)])
            if len(m["levels"]) <= 1:
                continue
            remove(m["levels"].pop())
            if m["fore_aft"]:
                continue		# its mast carries the lower sail too
            remove([m["mast"]])
            m["mast"] = cylinder("stub", (0, m["y"], m["base"]),
                                 (0, m["y"], m["base"] + m["height"] *
                                  0.72), 0.01 * s["length"] + 0.1,
                                 mats["spar"])
            m["mast"].parent = pivot
        if len(masts) >= 3:
            break_mast(masts[0], s, pivot, mats, False)
    elif level == 3 and main:
        # The mainmast broken and fallen over the side.
        break_mast(main, s, pivot, mats, True)
    elif level == 4:
        # Sinking, every mast gone but stumps: listing hard, down by the
        # bow and low in the water (the water hides what goes under).
        for m in masts:
            if not m.get("broken"):
                break_mast(m, s, pivot, mats, False)
        pivot.rotation_euler = (math.radians(-8), math.radians(-22),
                                pivot.rotation_euler[2])
        pivot.location.z = -s["sheer"] * 1.1


def water():
    """The sea's surface as a holdout: clear in the picture, hiding all
    of the ship that is under it."""
    bpy.ops.mesh.primitive_plane_add(size=800, location=(0, 0, 0.02))
    ob = bpy.context.object
    ob.name = "water"
    m = bpy.data.materials.new("holdout")
    m.use_nodes = True
    nodes = m.node_tree.nodes
    nodes.clear()
    hold = nodes.new("ShaderNodeHoldout")
    out = nodes.new("ShaderNodeOutputMaterial")
    m.node_tree.links.new(hold.outputs[0], out.inputs[0])
    ob.data.materials.append(m)


def ocean(t):
    """The open sea at time t (seconds): Blender's ocean, a wide field of
    moving waves, dark blue water lit and reflecting the sky."""
    ob = bpy.data.objects.get("ocean")
    if ob is None:
        bpy.ops.mesh.primitive_plane_add(size=2, location=(0, 0, 0))
        ob = bpy.context.object
        ob.name = "ocean"
        mod = ob.modifiers.new("ocean", "OCEAN")
        mod.geometry_mode = "GENERATE"
        mod.spatial_size = 200
        mod.resolution = 12
        mod.repeat_x = mod.repeat_y = 12
        mod.wave_scale = 0.75
        mod.wind_velocity = 11.0
        # Whitecaps where the waves break.
        mod.use_foam = True
        mod.foam_layer_name = "foam"
        mod.foam_coverage = 0.25
        mod.choppiness = 1.1
        mod.wave_alignment = 0.4
        mod.wave_direction = math.radians(30)
        ob.location = (-1200, -1200, 0)
        m = bpy.data.materials.new("sea")
        m.use_nodes = True
        nt = m.node_tree
        b_ = nt.nodes["Principled BSDF"]
        b_.inputs["Roughness"].default_value = 0.22
        b_.inputs["IOR"].default_value = 1.33
        if "Specular IOR Level" in b_.inputs:
            b_.inputs["Specular IOR Level"].default_value = 0.35
        # Deep teal water, white where its foam is.
        foam = nt.nodes.new("ShaderNodeAttribute")
        foam.attribute_name = "foam"
        ramp = nt.nodes.new("ShaderNodeValToRGB")
        ramp.color_ramp.elements[0].position = 0.35
        ramp.color_ramp.elements[0].color = (0.004, 0.035, 0.045, 1)
        ramp.color_ramp.elements[1].position = 0.75
        ramp.color_ramp.elements[1].color = (0.75, 0.78, 0.78, 1)
        nt.links.new(foam.outputs["Fac"], ramp.inputs["Fac"])
        nt.links.new(ramp.outputs["Color"], b_.inputs["Base Color"])
        ob.data.materials.append(m)
        for f in ob.data.polygons:
            f.use_smooth = True
    ob.modifiers["ocean"].time = t
    return ob


def side_camera(frame_w, frame_h, extent):
    """The sea battle's view: from the ship's port side, nearly level,
    the bow to the left, the waterline near the bottom of the frame."""
    sc = bpy.context.scene
    cam = sc.camera.data
    cam.ortho_scale = extent
    elev = math.radians(6)
    dist = 200
    high = extent * frame_h / frame_w
    mid = high * (0.5 - 0.06)		# the waterline near the bottom
    sc.camera.location = (-dist * math.cos(elev), 0,
                          dist * math.sin(elev) + mid)
    sc.camera.rotation_euler = (math.pi / 2 - elev, 0, -math.pi / 2)
    sun = bpy.data.objects["sun"]
    sun.rotation_euler = (math.radians(55), math.radians(-20),
                          math.radians(-110))


def scene(frame_w, frame_h, extent, mid):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    # As the fencers: denoised where Blender has a denoiser, else
    # enough light paths, adaptively.
    denoisers = [i.identifier for i in sc.cycles.bl_rna.properties[
        "denoiser"].enum_items]
    if denoisers:
        sc.cycles.samples = 96
        sc.cycles.use_denoising = True
        sc.cycles.denoiser = denoisers[0]
    else:
        sc.cycles.use_denoising = False
        sc.cycles.samples = 256
        sc.cycles.use_adaptive_sampling = True
        sc.cycles.adaptive_threshold = 0.02
        sc.cycles.sample_clamp_indirect = 3.0
    sc.cycles.device = "CPU"
    sc.render.film_transparent = True
    sc.render.resolution_x = frame_w
    sc.render.resolution_y = frame_h
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.view_settings.view_transform = "Standard"
    materials.sky(sc, 0.6, (0.55, 0.65, 0.8))
    # A warm low sun, late in the day, and a warm sky's light.
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = 3.2
    sun.angle = 0.2
    sun.color = (1.0, 0.86, 0.68)
    sob = bpy.data.objects.new("sun", sun)
    sob.rotation_euler = (math.radians(50), math.radians(-25),
                          math.radians(-30))
    bpy.context.collection.objects.link(sob)
    # A three-quarter view from the south, without perspective, as the
    # game's sprites are drawn, aimed at the ship's middle, which the
    # game centres the frame on.
    cam = bpy.data.cameras.new("cam")
    cam.type = "ORTHO"
    cam.ortho_scale = extent
    cam.clip_end = 1000
    cob = bpy.data.objects.new("cam", cam)
    elev = math.radians(38)
    dist = 200
    cob.location = (0, -dist * math.cos(elev), dist * math.sin(elev) + mid)
    cob.rotation_euler = (math.pi / 2 - elev, 0, 0)
    bpy.context.collection.objects.link(cob)
    sc.camera = cob


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=2)
    ap.add_argument("--class", dest="cls", choices=sorted(CLASSES),
                    help="a ship class, instead of a size")
    ap.add_argument("--out", required=True)
    ap.add_argument("--frame", default="156x116",
                    help="pixels of one frame (the game's 78x58 x2)")
    ap.add_argument("--headings", type=int, default=16)
    ap.add_argument("--damage", type=int, default=0,
                    help="render the side view from whole to sinking in "
                    "that many steps (DIR/d0.png...), not the headings")
    ap.add_argument("--blend", help="also save the built model there")
    ap.add_argument("--views", type=int, default=0,
                    help="with --portrait: N views at sea, DIR/viewN.png")
    ap.add_argument("--portrait", action="store_true",
                    help="render the encounter's portrait, DIR/portrait.png:"
                    " the ship filling the view, low over the water")
    args = ap.parse_args(argv)
    fw, fh = (int(v) for v in args.frame.split("x"))
    s = CLASSES[args.cls] if args.cls else SIZES[args.size]

    bpy.ops.wm.read_factory_settings(use_empty=True)
    lower, upper, wale = s.get("paint", ("dark oak", "dark oak", "gold"))
    # Scanned materials (materials.LIBRARY), each tinted to its paint:
    # planked hulls and decks, wooden spars, sailcloth, gilt; a plain
    # colour where its file is missing.
    pbr = materials.pbr
    mats = {
        "hull": pbr("hull", "planks", PAINTS[lower], 0.8),
        "upper": pbr("upper", "planks", PAINTS[upper], 0.75),
        "wale": pbr("wale", "gilt" if wale == "gold" else "planks",
                    PAINTS[wale], 0.5, 0.4 if wale == "gold" else 0.0),
        "port": material("port", (0.01, 0.008, 0.006), 0.9),
        "gilt": pbr("gilt", "gilt", PAINTS["gold"], 0.35, 0.8),
        "deck": pbr("deck", "planks", (0.36, 0.25, 0.15), 0.9),
        "spar": pbr("spar", "planks", (0.25, 0.16, 0.09), 0.8),
        "sail": pbr("sail", "canvas", (0.92, 0.84, 0.66), 0.95),
        "flag": pbr("flag", "wool", (0.65, 0.08, 0.06), 0.8),
        "lid": pbr("lid", "planks", (0.45, 0.06, 0.04), 0.8),
        "rope": pbr("rope", "rope", (0.12, 0.08, 0.05), 0.9),
    }
    hull(s, mats)
    masts = rig(s, mats)
    # Everything turns together about the ship's middle.
    pivot = bpy.data.objects.new("ship", None)
    bpy.context.collection.objects.link(pivot)
    for ob in list(bpy.context.collection.objects):
        if ob is not pivot and ob.parent is None and ob.type in ("MESH",
                                                                "EMPTY"):
            ob.parent = pivot
    # Sailing: one scale for every size, so a galleon is larger than a
    # pinnace; the frame fits the largest turned any way.
    big = SIZES[2]
    bigmast = max(h for _, h, _ in big["masts"])
    extent = max(big["length"] * 1.45, bigmast * 1.15)
    scene(fw, fh, extent * max(1, fw / fh), bigmast * 0.32)
    if args.blend:
        # The model as built, heading north, with its camera and light.
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.blend),
                                    compress=True)
    os.makedirs(args.out, exist_ok=True)
    if args.portrait:
        # As a lookout sees her through his glass: nearly level, her bow
        # to the left and a little towards him, filling the view.
        tall = max(h for _, h, _ in s["masts"]) + s["sheer"]
        side_camera(fw, fh, max(s["length"] * 1.6, tall * 1.5))
        cam = bpy.context.scene.camera
        cam.location = (-200 * math.cos(math.radians(4)), 0,
                        200 * math.sin(math.radians(4)) + tall * 0.55)
        cam.rotation_euler = (math.radians(86), 0, -math.pi / 2)
        if not args.views:
            water()
            pivot.rotation_euler = (0, 0, math.radians(48))
            bpy.context.scene.render.filepath = os.path.join(
                args.out, "portrait.png")
            bpy.ops.render.render(write_still=True)
            return
        # The glass's views: the real sea and sky, the waves moving on
        # between views and the ship riding them, rising and falling,
        # pitching and rolling in time with them.
        sc = bpy.context.scene
        sc.render.film_transparent = False
        # A warm low sun from behind the lookout, late in the day.
        sun = bpy.data.objects["sun"]
        sun.data.energy = 3.6
        sun.data.color = (1.0, 0.8, 0.58)
        sun.rotation_euler = (math.radians(72), 0, math.radians(-120))
        # Through a glass: a long lens from a lookout's height a few
        # hundred metres off, so the sea runs to a true horizon behind
        # her and the sky fills the top.
        span = max(s["length"] * 1.6, tall * 1.5)
        cam.data.type = "PERSP"
        cam.data.lens = 200
        cam.data.sensor_width = 36
        dist = span * 200 / 36
        cam.location = (-dist, 0, 12.0)
        cam.rotation_euler = (math.radians(90.0 + math.degrees(
            math.atan((tall * 0.42 - 12.0) / dist))), 0, -math.pi / 2)
        cam.data.clip_end = 20000
        # The far sea out to the horizon, flat, the same water.
        bpy.ops.mesh.primitive_plane_add(size=40000, location=(0, 0, -0.3))
        far = bpy.context.object
        far.name = "far-sea"
        # A clear sky with a low sun, physically scattered: deep blue
        # aloft, warm towards the horizon.
        world = bpy.data.worlds.new("sea-sky")
        world.use_nodes = True
        wt = world.node_tree
        sky = wt.nodes.new("ShaderNodeTexSky")
        kinds = [i.identifier for i in sky.bl_rna.properties[
            "sky_type"].enum_items]
        for k in ("MULTIPLE_SCATTERING", "SINGLE_SCATTERING", "NISHITA"):
            if k in kinds:
                sky.sky_type = k
                break
        if hasattr(sky, "sun_elevation"):
            sky.sun_elevation = math.radians(14)
            sky.sun_rotation = math.radians(210)
        if hasattr(sky, "air_density"):
            sky.air_density = 1.2
        wt.links.new(sky.outputs["Color"],
                     wt.nodes["Background"].inputs["Color"])
        wt.nodes["Background"].inputs["Strength"].default_value = 0.08
        sc.world = world
        # A film's response, so the sunlit canvas keeps its folds.
        for vt in ("AgX", "Filmic"):
            try:
                sc.view_settings.view_transform = vt
                break
            except TypeError:
                pass
        sc.world.cycles_visibility.camera = True \
            if hasattr(sc.world, "cycles_visibility") else None
        for n in range(args.views):
            t = n * 1.1
            sea = ocean(t)
            if not far.data.materials:
                far.data.materials.append(sea.data.materials[0])
            ph = 2 * math.pi * n / args.views
            # Riding at her waterline: a little up and down with the
            # swell, never pushed under it.
            pivot.location.z = 0.2 * math.sin(ph) + 0.15
            pivot.rotation_euler = (math.radians(2.0 * math.sin(ph + 1.2)),
                                    math.radians(1.5 * math.cos(ph)),
                                    math.radians(48))
            sc.render.filepath = os.path.join(args.out, "view%d.png" % n)
            bpy.ops.render.render(write_still=True)
        return
    if args.damage:
        # Each class wider with the square root of its length, against
        # the largest (true scale would make a sloop a speck).
        top = CLASSES["war galleon"]
        tall = max(h for _, h, _ in top["masts"]) + top["sheer"]
        full = max(top["length"] * 1.25, tall * 1.02 * fw / fh)
        side_camera(fw, fh, full * math.sqrt(s["length"] / top["length"]))
        water()
        pivot.rotation_euler = (0, 0, 0)
        for d in range(args.damage):
            damage(d, s, masts, pivot, mats)
            bpy.context.scene.render.filepath = os.path.join(
                args.out, "d%d.png" % d)
            bpy.ops.render.render(write_still=True)
        return
    for k in range(args.headings):
        # Heading k: clockwise from north (the bow up the screen).
        pivot.rotation_euler = (0, 0, -2 * math.pi * k / args.headings)
        bpy.context.scene.render.filepath = os.path.join(args.out,
                                                         "%d.png" % k)
        bpy.ops.render.render(write_still=True)


main()
