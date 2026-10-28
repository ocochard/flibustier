#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Wave a flag in Blender and render its frames.

The game shows a ship's colours as 8 frames of a flag waving on its
staff, 200x200 each, seen through the spyglass (pic/f-*, pic/fjolly*).
The flag here is a cloth from its staff, its design a picture, rippled
by waves running out from the staff; after 8 frames the waves are back
where they began, so the frames loop.  A Jolly Roger's fly is torn.

    blender -b -P flag.py -- --design DESIGN.png --out DIR [--frame 450]
                          [--torn] [--frames 8]

writes DIR/0.png to DIR/7.png, transparent around the flag and staff.
"""

import argparse
import math
import os
import sys

import bpy
import bmesh


def material(name, colour, rough=0.7):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*colour, 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    return m


def cloth(design, torn):
    """The flag's material: its design, its alpha (a torn fly), and a
    weave to light."""
    m = bpy.data.materials.new("cloth")
    m.use_nodes = True
    m.blend_method = "HASHED" if hasattr(m, "blend_method") else None
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.85
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(os.path.abspath(design))
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    if torn:
        # Ragged towards the fly: noise against the distance from the
        # staff, so the tears grow towards the free end.
        coord = nt.nodes.new("ShaderNodeTexCoord")
        noise = nt.nodes.new("ShaderNodeTexNoise")
        noise.inputs["Scale"].default_value = 9.0
        noise.inputs["Detail"].default_value = 8.0
        sep = nt.nodes.new("ShaderNodeSeparateXYZ")
        nt.links.new(coord.outputs["UV"], noise.inputs["Vector"])
        nt.links.new(coord.outputs["UV"], sep.inputs["Vector"])
        add = nt.nodes.new("ShaderNodeMath")
        add.operation = "ADD"
        nt.links.new(noise.outputs["Fac"], add.inputs[0])
        far = nt.nodes.new("ShaderNodeMath")
        far.operation = "MULTIPLY"
        far.inputs[1].default_value = 0.6
        nt.links.new(sep.outputs["X"], far.inputs[0])
        nt.links.new(far.outputs[0], add.inputs[1])
        cut = nt.nodes.new("ShaderNodeMath")
        cut.operation = "LESS_THAN"
        cut.inputs[1].default_value = 1.08
        nt.links.new(add.outputs[0], cut.inputs[0])
        nt.links.new(cut.outputs[0], bsdf.inputs["Alpha"])
    return m


def flag_mesh(width, height, mat):
    """A grid, its staff edge at x 0."""
    nx, ny = 48, 32
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new()
    verts = [[bm.verts.new((width * i / nx, 0, height * j / ny))
              for i in range(nx + 1)] for j in range(ny + 1)]
    for j in range(ny):
        for i in range(nx):
            f = bm.faces.new((verts[j][i], verts[j][i + 1],
                              verts[j + 1][i + 1], verts[j + 1][i]))
            for loop in f.loops:
                co = loop.vert.co
                loop[uv].uv = (co.x / width, co.z / height)
    me = bpy.data.meshes.new("flag")
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new("flag", me)
    ob.data.materials.append(mat)
    bpy.context.collection.objects.link(ob)
    return ob


def wave(ob, width, phase):
    """Ripple the flag: waves running from the staff to the fly, growing
    with the distance from the staff, and a droop at the fly."""
    me = ob.data
    for v in me.vertices:
        x, z = v.co.x, v.co.z
        u = x / width
        y = 0.22 * u * math.sin(2 * math.pi * (1.4 * u - phase)) + \
            0.07 * u * math.sin(2 * math.pi * (3.0 * u + 0.7 * z - 2 * phase))
        v.co.y = y * width
        # The fly sags and flutters a little in height too.
        v.co.z = z - 0.06 * width * u * u + \
            0.02 * width * u * math.sin(2 * math.pi * (2 * u - phase))


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--design", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--frame", type=int, default=450)
    ap.add_argument("--frames", type=int, default=8)
    ap.add_argument("--torn", action="store_true")
    args = ap.parse_args(argv)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 48
    sc.cycles.device = "CPU"
    sc.render.film_transparent = True
    sc.render.resolution_x = sc.render.resolution_y = args.frame
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.view_settings.view_transform = "Standard"
    world = bpy.data.worlds.new("sky")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = \
        (0.55, 0.65, 0.8, 1)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.8
    sc.world = world
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = 3.0
    sob = bpy.data.objects.new("sun", sun)
    sob.rotation_euler = (math.radians(40), math.radians(-30),
                          math.radians(20))
    bpy.context.collection.objects.link(sob)

    width, height = 1.5, 1.0
    rig = bpy.data.objects.new("rig", None)
    bpy.context.collection.objects.link(rig)
    flag = flag_mesh(width, height, cloth(args.design, args.torn))
    flag.parent = rig
    # The staff, rising from the lower left, the flag at its head.
    bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.025,
                                        depth=2.4,
                                        location=(-0.03, 0, -0.1))
    staff = bpy.context.object
    staff.data.materials.append(material("staff", (0.12, 0.07, 0.03)))
    staff.parent = rig
    # As the original's: the staff leaning to the right, the flag flying
    # up to the right of it.
    rig.rotation_euler = (0, math.radians(22), 0)

    cam = bpy.data.cameras.new("cam")
    cam.type = "ORTHO"
    cam.ortho_scale = 2.7
    cob = bpy.data.objects.new("cam", cam)
    cob.location = (0.95, -10, 0.15)
    cob.rotation_euler = (math.pi / 2, 0, 0)
    bpy.context.collection.objects.link(cob)
    sc.camera = cob

    base = [v.co.copy() for v in flag.data.vertices]
    os.makedirs(args.out, exist_ok=True)
    for f in range(args.frames):
        for v, co in zip(flag.data.vertices, base):
            v.co = co
        wave(flag, width, f / args.frames)
        flag.data.update()
        sc.render.filepath = os.path.join(args.out, "%d.png" % f)
        bpy.ops.render.render(write_still=True)


main()
