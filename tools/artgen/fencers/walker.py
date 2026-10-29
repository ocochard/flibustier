#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Render the landing party walking on the sailing map (spr/sailmisc).

The game shows the party ashore as one sailor walking, four frames in
each of eight directions, the frames' order the game's: west, north
east, north, south east, south, south west, east, north west.  The
sailor is the pack's human (human.py: a real body, dressed as the
player's fencer, a red kerchief), posed by the fencers' angles, walking,
seen from above at the sailing map's slant.

    blender -b -P walker.py -- --out DIR [--frame 128x208]

writes DIR/N.png for N 0-31, transparent around the sailor.
"""

import argparse
import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fencer					# noqa: E402

# The directions in the bank's order, as headings: 0 east, 90 north.
HEADINGS = [180, 45, 90, -45, -90, -135, 0, 135]

# A stride: four frames, the legs and arms swinging in turn.  At
# contact both legs are nearly straight, the heel ahead and the toe
# behind on the ground; passing, the swinging leg comes forward under
# the body a little bent, its foot just clear.
STRIDE = [
    {"front-hip": 17, "front-knee": -4, "back-hip": -15, "back-knee": -8,
     "back-ankle": 10, "front-shoulder": -14, "back-shoulder": 14,
     "front-elbow": 12, "back-elbow": 12},
    {"front-hip": -2, "front-knee": -3, "back-hip": 10, "back-knee": -20,
     "back-ankle": 6, "front-shoulder": -2, "back-shoulder": 2,
     "front-elbow": 10, "back-elbow": 10},
    {"front-hip": -15, "front-knee": -8, "front-ankle": 10, "back-hip": 17,
     "back-knee": -4, "front-shoulder": 14, "back-shoulder": -14,
     "front-elbow": 12, "back-elbow": 12},
    {"front-hip": 10, "front-knee": -20, "front-ankle": 6, "back-hip": -2,
     "back-knee": -3, "front-shoulder": 2, "back-shoulder": -2,
     "front-elbow": 10, "back-elbow": 10},
]


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--frame", default="128x208")
    ap.add_argument("--blend", help="save the model here")
    ap.add_argument("--slant", type=float, default=30,
                    help="the camera's angle down from level, degrees")
    args = ap.parse_args(argv)
    fw, fh = (int(v) for v in args.frame.split("x"))

    bpy.ops.wm.read_factory_settings(use_empty=True)
    import human
    rob, _ = human.build(fencer.LOOKS["my"])
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 96
    sc.cycles.device = "CPU"
    sc.render.film_transparent = True
    sc.render.resolution_x, sc.render.resolution_y = fw, fh
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.view_settings.view_transform = "Standard"
    fencer.materials.sky(sc, 0.9)
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = 2.4
    sob = bpy.data.objects.new("sun", sun)
    sob.rotation_euler = (math.radians(40), 0, math.radians(-30))
    bpy.context.collection.objects.link(sob)
    cam = bpy.data.cameras.new("cam")
    cam.type = "ORTHO"
    cam.ortho_scale = 2.05
    cob = bpy.data.objects.new("cam", cam)
    # Looking north, down at the slant, the sailor in the frame's middle.
    a = math.radians(args.slant)
    cob.location = (0, -20 * math.cos(a), 0.95 + 20 * math.sin(a))
    cob.rotation_euler = (math.pi / 2 - a, 0, 0)
    bpy.context.collection.objects.link(cob)
    sc.camera = cob

    if args.blend:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.blend),
                                    compress=True)
    os.makedirs(args.out, exist_ok=True)
    n = 0
    for heading in HEADINGS:
        for k in range(4):
            # The figure faces +X; turned to the heading (north up).
            human.pose(rob, fencer.at({"spine": 4, "neck": -4},
                                      **STRIDE[k]), heading)
            human.ground(rob)
            sc.render.filepath = os.path.join(args.out, "%d.png" % n)
            bpy.ops.render.render(write_still=True)
            n += 1


main()
