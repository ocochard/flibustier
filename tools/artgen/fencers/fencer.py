#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Build the sword fight's two fencers in Blender and render their
sprite layers.

The game draws each fencer as layers, frame by frame, all 141x90 and on
the same corner (grp/my* the player, grp/he* the opponent): the body,
the weapon (rapier, cutlass or longsword), the wounds (head, chest,
legs) and, for the opponent, a head of its own per scene.  Its 16 moves
(guard, step back, slash, lunge, each in the middle, low and high;
advance, retreat, fall, and the last) have the frames of BANKS.

A fencer here is a jointed figure, posed frame by frame from the key
poses of MOVES, seen from the side without perspective.  Each layer is
rendered alone, the others hiding what they cover (holdouts), so a
weapon passing behind its fencer is cut as the game's is.

    blender -b -P fencer.py -- --side my --out DIR [--frame 317x203]
                            [--blend MODEL.blend] [--moves 0,1,...]

writes DIR/LAYER/MOVE.FRAME.png for every layer of that side.
"""

import argparse
import math
import os
import sys

import bpy
from mathutils import Vector

# The photographic materials (../materials.py, copied beside this file
# on the render host).
sys.path[:0] = [os.path.dirname(os.path.abspath(__file__)),
                os.path.dirname(os.path.dirname(os.path.abspath(__file__)))]
import materials			# noqa: E402

# What each part of the clothes is made of (materials.LIBRARY), None a
# plain material; the skin a little translucent.
FABRIC = {"shirt": "linen", "sash": "wool", "breeches": "wool",
          "boots": "leather", "hair": None, "skin": None}

BANKS = [6, 6, 6, 6, 6, 6, 6, 6, 6, 4, 4, 4, 6, 6, 10, 6]
# The fall's last frames (14.3 on), the arms raised in surrender, are
# 141x114, 24 lines taller than the others (the game draws them 24
# higher, so the feet stay where they were).
TALL_MOVE, TALL_FROM, TALL = 14, 3, 114 / 90
FRAME_M = 3.08		# the frame's width in metres (141 pixels)
# Where each fencer stands in his frame, as in the game's: the player
# right of the middle facing left, the opponent left of it facing right
# (their bodies' middles at about 110 and 30 of the frame's 141 pixels).
CAMERA_X = {"my": -0.86, "he": 0.88}
SIDE = "my"

# The two fencers' clothes: (shirt, sash, breeches, boots, hair).
LOOKS = {
    "my": {"shirt": (0.85, 0.83, 0.78), "sash": (0.55, 0.04, 0.03),
           "breeches": (0.16, 0.09, 0.05), "boots": (0.03, 0.025, 0.02),
           "hair": (0.22, 0.12, 0.05), "skin": (0.62, 0.40, 0.28)},
    "he": {"shirt": (0.06, 0.30, 0.10), "sash": (0.30, 0.18, 0.06),
           "breeches": (0.02, 0.02, 0.025), "boots": (0.02, 0.018, 0.015),
           "hair": (0.03, 0.025, 0.02), "skin": (0.55, 0.35, 0.24)},
}
# The opponent's heads, one per scene (hehead1 the deck's, hehead2 the
# alley's, hehead3 the fort's): what he wears on it.
HEADS = {"head1": "kerchief", "head2": "hat", "head3": "morion"}


def material(name, colour, rough=0.7, metal=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*colour, 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    return m


def joint(name, parent, at):
    """A joint: an empty at "at" (in its parent's space) that its limbs
    hang from; it turns about the view's axis (Y)."""
    ob = bpy.data.objects.new(name, None)
    bpy.context.collection.objects.link(ob)
    ob.parent = parent
    ob.location = at
    return ob


ENDS = {}		# a limb's rounded ends, by the limb's name


def capsule(name, parent, length, r0, r1, mat, up=False, depth=0.0):
    """A limb from its joint, down (or up) "length" metres, tapering
    from r0 to r1, its ends rounded (ENDS)."""
    sign = 1 if up else -1
    bpy.ops.mesh.primitive_cone_add(vertices=16, radius1=r0, radius2=r1,
                                    depth=length,
                                    location=(0, depth, sign * length / 2))
    ob = bpy.context.object
    ob.name = name
    if not up:
        ob.rotation_euler = (math.pi, 0, 0)
    ob.data.materials.append(mat)
    for end, r in ((0, r0), (sign * length, r1)):
        bpy.ops.mesh.primitive_uv_sphere_add(radius=r, segments=16,
                                             ring_count=8,
                                             location=(0, depth, end))
        s = bpy.context.object
        s.name = name + "-end"
        s.data.materials.append(mat)
        s.parent = parent
        ENDS.setdefault(ob.name, []).append(s)
    ob.parent = parent
    return ob


def blob(name, parent, at, size, mat):
    """A rounded box (a head, a hand, a foot)."""
    bpy.ops.mesh.primitive_uv_sphere_add(radius=1, segments=24,
                                         ring_count=12, location=at)
    ob = bpy.context.object
    ob.name = name
    ob.scale = size
    ob.data.materials.append(mat)
    ob.parent = parent
    return ob


class Fencer:
    """The figure: its joints, and its parts sorted into layers."""

    def __init__(self, side):
        look = LOOKS[side]
        m = {k: materials.pbr(side + "-" + k, FABRIC.get(k), v,
                              rough=0.55 if k == "skin" else 0.8,
                              sss=0.15 if k == "skin" else 0.0)
             for k, v in look.items()}
        self.side = side
        self.layers = {"body": [], "weapon": {}, "wounds": {}, "heads": {}}
        body = self.layers["body"]

        def keep(ob):
            body.append(ob)
            body.extend(ENDS.get(ob.name, []))
            return ob

        self.root = joint(side + "-root", None, (0, 0, 0.9))
        j = self.j = {}
        # The torso up from the hips, the head on it.
        j["spine"] = joint("spine", self.root, (0, 0, 0))
        keep(capsule("torso", j["spine"], 0.52, 0.17, 0.2, m["shirt"],
                     up=True))
        keep(capsule("sash", j["spine"], 0.1, 0.18, 0.18, m["sash"],
                     up=True))
        # The neck from inside the shoulders up into the head, so they
        # join at every pose.
        j["neck"] = joint("neck", j["spine"], (0, 0, 0.50))
        keep(capsule("neckp", j["neck"], 0.2, 0.075, 0.07, m["skin"],
                     up=True))
        head = [blob("head", j["neck"], (0.02, 0, 0.23), (0.1, 0.1, 0.12),
                     m["skin"])]
        head.append(blob("hair", j["neck"], (-0.02, 0, 0.27),
                         (0.105, 0.105, 0.09), m["hair"]))
        if side == "my":
            # Long hair to the shoulders, part of the body.
            head.append(blob("locks", j["neck"], (-0.07, 0, 0.17),
                             (0.06, 0.09, 0.1), m["hair"]))
            body.extend(head)
        else:
            # His head is a layer of its own per scene, over his body.
            for name, wear in HEADS.items():
                parts = [blob(name + "-face", j["neck"], (0.02, 0, 0.23),
                              (0.1, 0.1, 0.12), m["skin"]),
                         blob(name + "-beard", j["neck"], (0.05, 0, 0.16),
                              (0.07, 0.09, 0.05), m["hair"])]
                parts += self.headwear(name, wear, j["neck"])
                self.layers["heads"][name] = parts
            for ob in head:
                bpy.data.objects.remove(ob, do_unlink=True)
        # Arms: the sword arm in front of the body, the other behind.
        for arm, d in (("front", -0.22), ("back", 0.22)):
            j[arm + "-shoulder"] = joint(arm + "-shoulder", j["spine"],
                                         (0, d, 0.47))
            keep(capsule(arm + "-upper", j[arm + "-shoulder"], 0.3, 0.075,
                         0.065, m["shirt"]))
            j[arm + "-elbow"] = joint(arm + "-elbow",
                                      j[arm + "-shoulder"], (0, 0, -0.3))
            keep(capsule(arm + "-fore", j[arm + "-elbow"], 0.27, 0.06,
                         0.045, m["shirt"]))
            j[arm + "-wrist"] = joint(arm + "-wrist", j[arm + "-elbow"],
                                      (0, 0, -0.27))
            keep(blob(arm + "-hand", j[arm + "-wrist"], (0, 0, -0.05),
                      (0.04, 0.04, 0.055), m["skin"]))
        # Legs: the front leg nearer the view.
        for leg, d in (("front", -0.12), ("back", 0.12)):
            j[leg + "-hip"] = joint(leg + "-hip", self.root, (0, d, 0))
            keep(capsule(leg + "-thigh", j[leg + "-hip"], 0.45, 0.1, 0.075,
                         m["breeches"]))
            j[leg + "-knee"] = joint(leg + "-knee", j[leg + "-hip"],
                                     (0, 0, -0.45))
            keep(capsule(leg + "-shin", j[leg + "-knee"], 0.42, 0.07, 0.055,
                         m["boots"]))
            j[leg + "-ankle"] = joint(leg + "-ankle", j[leg + "-knee"],
                                      (0, 0, -0.42))
            keep(blob(leg + "-foot", j[leg + "-ankle"], (0.08, 0, -0.04),
                      (0.13, 0.05, 0.045), m["boots"]))
        self.weapons(j["front-wrist"])
        self.wounds(m)

    def headwear(self, name, wear, neck):
        cloth = material(name + "-wear", {"kerchief": (0.55, 0.05, 0.04),
                                          "hat": (0.05, 0.04, 0.03),
                                          "morion": (0.55, 0.55, 0.58)}[wear],
                         0.3 if wear == "morion" else 0.85,
                         0.9 if wear == "morion" else 0.0)
        if wear == "kerchief":
            return [blob(name + "-kerchief", neck, (-0.01, 0, 0.29),
                         (0.11, 0.11, 0.07), cloth)]
        if wear == "hat":
            return [blob(name + "-crown", neck, (0, 0, 0.35),
                         (0.08, 0.08, 0.07), cloth),
                    blob(name + "-brim", neck, (0, 0, 0.31),
                         (0.2, 0.2, 0.015), cloth)]
        return [blob(name + "-helmet", neck, (0, 0, 0.31),
                     (0.12, 0.12, 0.09), cloth),
                blob(name + "-comb", neck, (0, 0, 0.39),
                     (0.08, 0.01, 0.05), cloth),
                blob(name + "-rim", neck, (0, 0, 0.27),
                     (0.19, 0.12, 0.02), cloth)]

    def weapons(self, wrist):
        """The three blades in the sword hand, pointing on from the
        forearm."""
        steel = material("steel", (0.75, 0.75, 0.78), 0.2, 1.0)
        brass = material("brass", (0.7, 0.5, 0.18), 0.3, 1.0)
        grip = joint("grip", wrist, (0, 0, -0.06))
        self.j["grip"] = grip
        for name, length, width, guard in (("rapier", 1.0, 0.012, "cup"),
                                           ("cutlass", 0.72, 0.035, "shell"),
                                           ("sword", 0.95, 0.03, "cross")):
            blade = capsule(name + "-blade", grip, length, width,
                            width * 0.4, steel)
            parts = [blade] + ENDS.get(blade.name, [])
            if guard == "cup":
                parts.append(blob(name + "-cup", grip, (0, 0, -0.02),
                                  (0.06, 0.06, 0.03), steel))
            elif guard == "shell":
                parts.append(blob(name + "-shell", grip, (0, 0, -0.02),
                                  (0.05, 0.07, 0.025), brass))
            else:
                cross = capsule(name + "-cross", grip, 0.01, 0.015, 0.015,
                                brass)
                parts += [cross] + ENDS.get(cross.name, [])
                parts.append(blob(name + "-quillons", grip, (0, 0, -0.01),
                                  (0.012, 0.12, 0.012), brass))
            self.layers["weapon"][name] = parts

    def wounds(self, m):
        blood = material("blood", (0.35, 0.0, 0.0), 0.5)
        j = self.j
        self.layers["wounds"] = {
            "head": [blob("cut-head", j["neck"], (0.06, -0.11, 0.26),
                          (0.04, 0.01, 0.03), blood)],
            "chest": [blob("cut-chest", j["spine"], (0.12, -0.2, 0.35),
                           (0.07, 0.01, 0.05), blood)],
            "legs": [blob("cut-legs", j["front-hip"], (0.05, -0.1, -0.25),
                          (0.06, 0.01, 0.06), blood)],
        }

    def pose(self, p):
        """Set a pose: angles in degrees, positive towards the fencer's
        front, limbs hanging at 0; the hips at (x, height)."""
        self.root.location = (p.get("x", 0.0), 0, p.get("hips", 0.9))
        for name, ob in self.j.items():
            if name in ("grip",):
                continue
            ob.rotation_euler = (0, -math.radians(p.get(name, 0.0)), 0)
        self.j["grip"].rotation_euler = (
            0, -math.radians(p.get("blade", 0.0)), 0)


# The key poses.  Angles: hips and knees of each leg, shoulders and
# elbows of each arm, the spine and neck, and the blade in the hand.
GUARD = {"hips": 0.86, "spine": 6, "neck": -6,
         "front-hip": 28, "front-knee": -22, "front-ankle": -6,
         "back-hip": -26, "back-knee": -24, "back-ankle": 20,
         "front-shoulder": 72, "front-elbow": 18, "front-wrist": 0,
         "blade": 5, "back-shoulder": -40, "back-elbow": 95,
         "back-wrist": 20}


def at(base, **changes):
    p = dict(base)
    p.update(changes)
    return p


def line(base, level):
    """The guard's sword arm at the middle, low or high line."""
    return at(base, **{"front-shoulder": (72, 42, 112)[level],
                       "front-elbow": (18, 8, -6)[level],
                       "blade": (5, 10, -20)[level]})


def bob(base, n, k):
    """The guard's breathing: frame k of n."""
    s = math.sin(2 * math.pi * k / n)
    return at(base, hips=base["hips"] + 0.012 * s,
              **{"front-shoulder": base["front-shoulder"] + 3 * s,
                 "back-elbow": base["back-elbow"] + 5 * s})


def mix(a, b, t):
    keys = set(a) | set(b)
    return {k: a.get(k, 0.0) + (b.get(k, 0.0) - a.get(k, 0.0)) * t
            for k in keys}


def keyed(keys, n, k):
    """Frame k of n through key poses spread evenly."""
    if n == 1:
        return keys[0]
    t = k / (n - 1) * (len(keys) - 1)
    i = min(int(t), len(keys) - 2)
    return mix(keys[i], keys[i + 1], t - i)


def move_pose(move, k):
    """Move "move" (SWM_*), frame k."""
    n = BANKS[move]
    level = move % 3
    g = line(GUARD, level if move < 12 else 0)
    if move < 3:				# guard
        return bob(g, n, k)
    if move < 6:				# step back
        lift = at(g, **{"back-hip": -40, "back-knee": -50, "hips": 0.84})
        down = at(g, x=-0.12, **{"front-hip": 10, "front-knee": -10})
        return keyed([g, lift, down, at(down, x=-0.18), g], n, k)
    if move < 9:				# slash
        high = (150, 125, 165)[level]
        low = (55, 25, 85)[level]
        wind = at(g, spine=-4, **{"front-shoulder": high,
                                  "front-elbow": -40, "blade": -30})
        cut = at(g, spine=16, **{"front-shoulder": low, "front-elbow": 10,
                                 "blade": 20, "front-hip": 40,
                                 "front-knee": -40, "hips": 0.8})
        return keyed([g, wind, cut, cut, g], n, k)
    if move < 12:				# lunge
        out = at(g, x=0.22, hips=0.72, spine=14, **{
            "front-hip": 62, "front-knee": -62, "back-hip": -52,
            "back-knee": -4, "front-shoulder": (88, 62, 112)[level],
            "front-elbow": 0, "blade": (0, 10, -12)[level],
            "back-shoulder": -70, "back-elbow": 10})
        return keyed([g, out, out, g], n, k)
    if move == 12:				# advance
        step = at(g, x=0.1, **{"front-hip": 45, "front-knee": -40,
                               "hips": 0.84})
        return keyed([g, step, at(g, x=0.16), g], n, k)
    if move == 13:				# retreat
        step = at(g, x=-0.1, **{"back-hip": -45, "back-knee": -45,
                                "hips": 0.84})
        return keyed([g, step, at(g, x=-0.16), g], n, k)
    if move == 14:				# the fall: struck, then yielding
        hit = at(g, spine=-22, neck=-15, **{"front-shoulder": 20,
                                            "front-elbow": 10,
                                            "blade": 40})
        up = at(hit, spine=-6, neck=-10, hips=0.82, **{
            "front-shoulder": 165, "front-elbow": 25, "back-shoulder": 160,
            "back-elbow": 25, "blade": 80})
        return keyed([g, hit, hit, up, up, up, up, up, up, up], n, k)
    return bob(g, n, k)			# the last: back on guard


def scene(fw, fh):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 32
    sc.cycles.device = "CPU"
    sc.render.film_transparent = True
    sc.render.resolution_x = fw
    sc.render.resolution_y = fh
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.view_settings.view_transform = "Standard"
    materials.sky(sc, 0.8)
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = 3.0
    sob = bpy.data.objects.new("sun", sun)
    sob.rotation_euler = (math.radians(55), math.radians(-30),
                          math.radians(-25))
    bpy.context.collection.objects.link(sob)
    cam = bpy.data.cameras.new("cam")
    cam.type = "ORTHO"
    cam.ortho_scale = FRAME_M
    cob = bpy.data.objects.new("cam", cam)
    # From the front of the fight's line (-Y), level; the ground near
    # the frame's bottom, the fencer a little right of its middle.
    high = FRAME_M * fh / fw
    cob.location = (CAMERA_X[SIDE], -20, high / 2 - 0.04)
    cob["fw"], cob["fh"] = fw, fh
    cob.rotation_euler = (math.pi / 2, 0, 0)
    bpy.context.collection.objects.link(cob)
    sc.camera = cob


def frame_size(move, k):
    """A frame taller for the surrender, the camera raised with it so the
    ground stays at the frame's bottom."""
    sc = bpy.context.scene
    cob = sc.camera
    fw, fh = cob["fw"], cob["fh"]
    if move == TALL_MOVE and k >= TALL_FROM:
        th = round(fh * TALL)
    else:
        th = fh
    sc.render.resolution_y = th
    high = FRAME_M * fh / fw
    cob.location.z = high / 2 - 0.04 + FRAME_M * (th - fh) / fw / 2


def show(objects, holdouts):
    """Render only "objects", "holdouts" hiding what they cover."""
    shown = set(objects) | set(holdouts)
    for ob in bpy.data.objects:
        if ob.type != "MESH":
            continue
        ob.hide_render = ob not in shown
        ob.is_holdout = ob in holdouts


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--side", choices=("my", "he"), default="my")
    ap.add_argument("--out", required=True)
    ap.add_argument("--frame", default="317x203",
                    help="pixels of a frame (141x90 at 1080 lines)")
    ap.add_argument("--moves", help="only these moves, e.g. 0,9")
    ap.add_argument("--blend")
    args = ap.parse_args(argv)
    fw, fh = (int(v) for v in args.frame.split("x"))
    global SIDE
    SIDE = args.side

    bpy.ops.wm.read_factory_settings(use_empty=True)
    f = Fencer(args.side)
    if args.side == "my":
        f.root.scale = (-1, 1, 1)	# facing his opponent, to the left
    scene(fw, fh)
    f.pose(GUARD)
    if args.blend:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.blend),
                                    compress=True)
    body = f.layers["body"]
    heads = [ob for parts in f.layers["heads"].values() for ob in parts]
    passes = [("body", body, [])]
    # The opponent with each of his heads on, the picture redrawn: the
    # body is cut from the first (a body alone the redraw gives a
    # fencing mask), each head from its own, so head and body match.
    for name, parts in f.layers["heads"].items():
        passes.append(("bodyref" + name[4:], body + parts, []))
    for name, parts in f.layers["heads"].items():
        passes.append((name, parts, body))
    for name, parts in f.layers["weapon"].items():
        passes.append((name, parts, body + heads))
    for name, parts in f.layers["wounds"].items():
        passes.append((name, parts, []))
    moves = range(len(BANKS)) if not args.moves else \
        [int(v) for v in args.moves.split(",")]
    for move in moves:
        for k in range(BANKS[move]):
            f.pose(move_pose(move, k))
            frame_size(move, k)
            for name, objects, holdouts in passes:
                show(objects, holdouts)
                d = os.path.join(args.out, name)
                os.makedirs(d, exist_ok=True)
                bpy.context.scene.render.filepath = os.path.join(
                    d, "%d.%d.png" % (move, k))
                bpy.ops.render.render(write_still=True)


if __name__ == "__main__":	# fencers/walker.py imports the figure
    main()
