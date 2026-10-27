#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Build a 17th-century sailing ship in Blender and render it at the
game's sixteen headings.

The game draws a ship as a three-quarter view from the south at one of
sixteen headings, clockwise from north (frame = heading >> 4), the same
sixteen for sailing (spr/shipN) and for sea battles (cur/shipNa holds
the first eight, cur/shipNb the last).  The ship is modelled here, from
a few measures per size, so every heading shows the very same ship.

    blender -b -P ship.py -- --size 2 --out DIR [--frame 156x116]

writes DIR/0.png to DIR/15.png, transparent around the ship.
"""

import argparse
import math
import os
import sys

import bpy
import bmesh
from mathutils import Vector

# Per size (the game's three: small, medium, large ships): hull length,
# beam, height of the sheer, the castles, the masts (position along the
# hull from the stern, height) and their yards.
SIZES = {
    0: {"length": 14.0, "beam": 4.2, "sheer": 1.3, "stern": 1.4,
        "bow": 0.6, "masts": [(0.42, 11.0), (0.68, 12.5)],
        "yards": 2},
    1: {"length": 20.0, "beam": 5.6, "sheer": 1.7, "stern": 2.4,
        "bow": 1.0, "masts": [(0.22, 12.0), (0.50, 17.5), (0.74, 15.5)],
        # A merchantman's rig: two square sails a mast, a lateen mizzen.
        "yards": 2, "lateen": True},
    2: {"length": 26.0, "beam": 7.2, "sheer": 2.1, "stern": 2.8,
        "bow": 1.6, "masts": [(0.20, 15.0), (0.50, 22.0), (0.75, 19.0)],
        "yards": 3},
}


BRACE = 30		# degrees the yards are braced round


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


def hull(s, mats):
    """The hull from cross sections, with the deck on top and the
    transom across the stern."""
    L, B = s["length"], s["beam"]
    n = 24
    bm = bmesh.new()
    rings = []
    for i in range(n + 1):
        t = -1 + 2 * i / n
        y = t * L / 2
        w = half_width(t, B)
        # The sheer rises to the castles at each end.
        top = s["sheer"] + (s["stern"] if t < -0.55 else 0) * \
            min(1, (-0.55 - t) / 0.15 if t < -0.55 else 0) + \
            (s["bow"] * min(1, (t - 0.7) / 0.15) if t > 0.7 else 0)
        # Cut flat at the waterline: the sprite shows no more of it.
        pts = [(-w, top), (-w * 1.02, 0.3), (-w * 0.96, 0.0),
               (w * 0.96, 0.0), (w * 1.02, 0.3), (w, top)]
        rings.append([bm.verts.new((x, y, z)) for x, z in pts])
    for a, b in zip(rings, rings[1:]):
        for k in range(len(a) - 1):
            bm.faces.new((a[k], a[k + 1], b[k + 1], b[k]))
    bm.faces.new(list(reversed(rings[0])))		# the transom
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    edges = [(r[0].co.copy(), r[-1].co.copy()) for r in rings]
    ob = mesh_object("hull", bm, mats["hull"])
    # The deck: a band over the tops of the sections, a little lower.
    bm = bmesh.new()
    left = [bm.verts.new((l.x * 0.97, l.y, l.z - 0.25)) for l, _ in edges]
    right = [bm.verts.new((r.x * 0.97, r.y, r.z - 0.25)) for _, r in edges]
    for k in range(n):
        bm.faces.new((left[k], right[k], right[k + 1], left[k + 1]))
    mesh_object("deck", bm, mats["deck"])
    # A painted wale along each side, and the stern's windows.
    for side in (-1, 1):
        bm = bmesh.new()
        prev = None
        for i in range(2, n - 1):
            t = -1 + 2 * i / n
            w = half_width(t, B) * 1.03
            v1 = bm.verts.new((side * w, t * L / 2, s["sheer"] * 0.55))
            v2 = bm.verts.new((side * w, t * L / 2, s["sheer"] * 0.75))
            if prev:
                bm.faces.new((prev[0], v1, v2, prev[1]))
            prev = (v1, v2)
        mesh_object("wale", bm, mats["wale"])
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
    mod.thickness = 0.04
    return ob


def lateen(m, y, base, height, s, mats):
    """A lateen sail on the aftmost mast: a triangle under a long yard
    slanting down forward, along the hull."""
    top = (0, y - s["beam"] * 0.9, base + height * 0.95)
    low = (0, y + s["beam"] * 0.9, base + height * 0.35)
    cylinder("lateen%d" % m, top, low, 0.08, mats["spar"])
    bm = bmesh.new()
    vs = [bm.verts.new(p) for p in (top, low,
                                    (0.6, y - s["beam"] * 0.5,
                                     base + height * 0.30))]
    bm.faces.new(vs)
    ob = mesh_object("lateensail%d" % m, bm, mats["sail"])
    ob.modifiers.new("solid", "SOLIDIFY").thickness = 0.04


def rig(s, mats):
    L = s["length"]
    for m, (pos, height) in enumerate(s["masts"]):
        y = -L / 2 + pos * L
        base = s["sheer"] - 0.3
        cylinder("mast%d" % m, (0, y, base), (0, y, base + height),
                 0.16 + 0.03 * len(s["masts"]), mats["spar"])
        # Yards and the sails between them, narrower up the mast.
        if m == 0 and s.get("lateen"):
            lateen(m, y, base, height, s, mats)
            continue
        levels = s["yards"]
        for k in range(levels):
            z1 = base + height * (0.42 + 0.55 * (k + 1) / levels) - 0.3
            z0 = base + height * (0.42 + 0.55 * k / levels) - 0.3 \
                if k else base + height * 0.30
            width = s["beam"] * (1.9 - 0.45 * k)
            # Braced round, as sailing with the wind on the quarter, so
            # the sails show at every heading.
            brace = bpy.data.objects.new("brace%d_%d" % (m, k), None)
            brace.location = (0, y, 0)
            brace.rotation_euler = (0, 0, math.radians(BRACE))
            bpy.context.collection.objects.link(brace)
            for ob in (cylinder("yard%d_%d" % (m, k), (-width / 2, 0.25, z1),
                                (width / 2, 0.25, z1), 0.09, mats["spar"]),
                       sail("sail%d_%d" % (m, k), 0.35, z0 + 0.15, z1,
                            width * 0.92, width * 0.86, 0.9, mats["sail"])):
                ob.parent = brace
        # A pennant at the masthead.
        bm = bmesh.new()
        top = base + height
        vs = [bm.verts.new(p) for p in ((0, y, top), (0, y - 2.2, top - 0.3),
                                         (0, y, top - 0.6))]
        bm.faces.new(vs)
        mesh_object("pennant%d" % m, bm, mats["flag"])
    # The bowsprit, raking forward from the bow.
    cylinder("bowsprit", (0, L / 2 - 1.0, s["sheer"] + s["bow"]),
             (0, L / 2 + L * 0.13, s["sheer"] + s["bow"] + L * 0.07),
             0.15, mats["spar"])


def scene(frame_w, frame_h, extent):
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
    # game's sprites are drawn.
    cam = bpy.data.cameras.new("cam")
    cam.type = "ORTHO"
    cam.ortho_scale = extent
    cob = bpy.data.objects.new("cam", cam)
    # Aimed at the ship's middle, which the game centres the frame on.
    elev = math.radians(38)
    dist = 100
    mid = SIZES[2]["masts"][1][1] * 0.32
    cob.location = (0, -dist * math.cos(elev), dist * math.sin(elev) + mid)
    cob.rotation_euler = (math.pi / 2 - elev, 0, 0)
    bpy.context.collection.objects.link(cob)
    sc.camera = cob


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=2)
    ap.add_argument("--out", required=True)
    ap.add_argument("--frame", default="156x116",
                    help="pixels of one frame (the game's 78x58 x2)")
    ap.add_argument("--headings", type=int, default=16)
    args = ap.parse_args(argv)
    fw, fh = (int(v) for v in args.frame.split("x"))
    s = SIZES[args.size]

    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = {
        "hull": material("hull", (0.10, 0.05, 0.025), 0.8),
        "deck": material("deck", (0.36, 0.25, 0.15), 0.9),
        "wale": material("wale", (0.75, 0.55, 0.12), 0.5, 0.3),
        "spar": material("spar", (0.25, 0.16, 0.09), 0.8),
        "sail": material("sail", (0.86, 0.80, 0.66), 0.95),
        "flag": material("flag", (0.65, 0.08, 0.06), 0.8),
    }
    hull(s, mats)
    rig(s, mats)
    # Everything turns together about the ship's middle.
    pivot = bpy.data.objects.new("ship", None)
    bpy.context.collection.objects.link(pivot)
    for ob in list(bpy.context.collection.objects):
        if ob is not pivot and ob.parent is None and ob.type in ("MESH",
                                                                "EMPTY"):
            ob.parent = pivot
    # The same scale for every size, so a galleon is larger than a sloop:
    # the frame fits the largest ship turned any way.
    big = SIZES[2]
    extent = max(big["length"] * 1.3, max(h for _, h in big["masts"]) * 1.15)
    scene(fw, fh, extent * max(1, fw / fh))
    os.makedirs(args.out, exist_ok=True)
    for k in range(args.headings):
        # Heading k: clockwise from north (the bow up the screen).
        pivot.rotation_euler = (0, 0, -2 * math.pi * k / args.headings)
        bpy.context.scene.render.filepath = os.path.join(args.out,
                                                         "%d.png" % k)
        bpy.ops.render.render(write_still=True)


main()
