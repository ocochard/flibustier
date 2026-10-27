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
    if t < aft:
        top += s["stern"] * min(1.0, (aft - t) / 0.08)
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
                v2 = bm.verts.new((side * w, t * L / 2, z0 + 0.25))
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
    for k in range(s.get("spritsail", 0)):
        at = 0.65 if k == 0 else 1.0
        y = root[1] + (end[1] - root[1]) * at
        z = root[2] + (end[2] - root[2]) * at
        w = s["beam"] * (1.0 if k == 0 else 0.6)
        if k == 0:			# hanging below the bowsprit
            z0, z1 = z - w * 0.7, z
        else:				# standing on its end
            z0, z1 = z, z + w * 0.6
        sail("spritsail%d" % k, y, z0, z1, w, w * 0.9, w * 0.05,
             mats["sail"])
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
    sc.cycles.samples = 64
    sc.cycles.device = "CPU"
    sc.render.film_transparent = True
    sc.render.resolution_x = frame_w
    sc.render.resolution_y = frame_h
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.view_settings.view_transform = "Standard"
    world = bpy.data.worlds.new("sky")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = \
        (0.55, 0.65, 0.8, 1)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = \
        0.6
    sc.world = world
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = 2.6
    sun.angle = 0.2
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
    args = ap.parse_args(argv)
    fw, fh = (int(v) for v in args.frame.split("x"))
    s = CLASSES[args.cls] if args.cls else SIZES[args.size]

    bpy.ops.wm.read_factory_settings(use_empty=True)
    lower, upper, wale = s.get("paint", ("dark oak", "dark oak", "gold"))
    mats = {
        "hull": material("hull", PAINTS[lower], 0.8),
        "upper": material("upper", PAINTS[upper], 0.75),
        "wale": material("wale", PAINTS[wale], 0.5,
                         0.4 if wale == "gold" else 0.0),
        "port": material("port", (0.01, 0.008, 0.006), 0.9),
        "gilt": material("gilt", PAINTS["gold"], 0.35, 0.8),
        "deck": material("deck", (0.36, 0.25, 0.15), 0.9),
        "spar": material("spar", (0.25, 0.16, 0.09), 0.8),
        "sail": material("sail", (0.86, 0.80, 0.66), 0.95),
        "flag": material("flag", (0.65, 0.08, 0.06), 0.8),
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
