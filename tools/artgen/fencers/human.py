#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""A clothed, rigged human for the pack's figures.

The body is Blender's realistic male base mesh (the Human Base Meshes
bundle, CC0, fetched on the render host by materials.fetch_command()).
It is dressed by regions of its own surface (shirt, sash, breeches,
boots, the skin bare) in materials.py's photographic materials, the
clothes a little loose; a kerchief and hair are added on the head.  An
armature is fitted to it and bound by bone heat, then its arms and legs
are straightened to hang as the fencers' figure's do (fencer.py: limbs
hanging at 0), so the same pose angles drive it: each joint turns about
the figure's side axis, positive towards its front.

The figure faces +X, its right ("front", nearer a camera at -Y) at -Y,
as fencer.py's.  Blender only.
"""

import math
import os

import bpy
from mathutils import Matrix, Vector

import materials

BUNDLE = os.path.join(materials.LIB, "human", "human-base-meshes-bundle-v1.4.1",
                      "human_base_meshes_bundle.blend")
BODY = "GEO-body_male_realistic"

# The joints, in the mesh's A-pose (metres; the mesh centred, facing -Y,
# its right at -X), measured on it.
SHOULDER = (0.17, 0.0, 1.40)
ARM_TIP = (0.44, -0.095, 0.763)
HIP = (0.09, 0.0, 0.89)
KNEE = (0.12, -0.01, 0.48)
ANKLE = (0.15, 0.02, 0.08)
TOE = (0.17, -0.13, 0.02)
ROOT, CHEST, NECK, CROWN = 0.95, 1.40, 1.52, 1.70
UPPER, FORE, HAND = 0.30, 0.26, 0.12
SMOOTH_CHEST = 10	# passes softening the chest under the shirt

# Each joint of the pose (fencer.py's names) and its bone.
BONES = {"spine": "spine", "neck": "neck",
         "front-shoulder": "upper.R", "front-elbow": "fore.R",
         "front-wrist": "hand.R", "back-shoulder": "upper.L",
         "back-elbow": "fore.L", "back-wrist": "hand.L",
         "front-hip": "thigh.R", "front-knee": "shin.R",
         "front-ankle": "foot.R", "back-hip": "thigh.L",
         "back-knee": "shin.L", "back-ankle": "foot.L"}


def _side(p, s):
    """A left-side point for side s (+1 left, -1 right)."""
    return Vector((s * p[0], p[1], p[2]))


def _load():
    with bpy.data.libraries.load(BUNDLE, link=False) as (src, dst):
        dst.objects = [BODY]
    ob = dst.objects[0]
    bpy.context.collection.objects.link(ob)
    for m in list(ob.modifiers):
        ob.modifiers.remove(m)
    for child in list(bpy.data.objects):
        if child.parent == ob:
            bpy.data.objects.remove(child, do_unlink=True)
    # Centred on the origin, its feet on the ground.
    me = ob.data
    mw = ob.matrix_world.copy()
    me.transform(mw)
    ob.matrix_world = Matrix.Identity(4)
    xs = [v.co.x for v in me.vertices]
    ys = [v.co.y for v in me.vertices]
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    me.transform(Matrix.Translation((-cx, -cy, 0)))
    return ob


def _region(c, n, kerchief=True):
    """The part of the clothes a face at "c" (its normal "n") is in."""
    x, y, z = abs(c.x), c.y, c.z
    if kerchief and (z > 1.6 or (z > 1.53 and y > 0.0)):
        return "kerchief"			# the crown and the back
    if z > 1.53 and y > 0.02:
        return "hair"
    if z > 1.47:
        return "skin"				# the face and neck
    if x > 0.33 and z < 0.92:
        return "skin"				# the hands
    if x > 0.24 and z > 0.75:
        return "shirt"				# the arms, sleeved
    if z > 1.0:
        return "shirt"
    if z > 0.92:
        return "sash"
    if z > 0.42:
        return "breeches"
    return "boots"


def dress(ob, look, kerchief=(0.6, 0.05, 0.04)):
    """The clothes, by regions of the body, a little loose; the kerchief
    over the crown, the hair below it."""
    me = ob.data
    names = ["skin", "shirt", "sash", "breeches", "boots", "hair",
             "kerchief"]
    loose = {"shirt": 0.018, "sash": 0.022, "breeches": 0.012,
             "boots": 0.008, "skin": 0.0, "hair": 0.008, "kerchief": 0.014}
    colour = dict(look, kerchief=kerchief or look["hair"])
    fabric = {"kerchief": "wool"}
    from fencer import FABRIC
    for name in names:
        me.materials.append(materials.pbr(
            "human-" + name, fabric.get(name, FABRIC.get(name)),
            colour[name], rough=0.55 if name == "skin" else 0.8,
            sss=0.15 if name == "skin" else 0.0))
    push = [0.0] * len(me.vertices)
    for f in me.polygons:
        r = _region(f.center, f.normal, kerchief is not None)
        f.material_index = names.index(r)
        for v in f.vertices:
            push[v] = max(push[v], loose[r])
    # The shirt hangs loose over the torso, not along its muscles: the
    # chest and belly smoothed flat (the body's sculpted pectorals would
    # read as a woman's), then the cloth pushed off it.
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.verts.ensure_lookup_table()
    torso = [v for v in bm.verts if abs(v.co.x) < 0.2 and
             1.02 < v.co.z < 1.44 and push[v.index] > 0]
    for _ in range(SMOOTH_CHEST):
        bmesh.ops.smooth_vert(bm, verts=torso, factor=0.4,
                              use_axis_x=True, use_axis_y=True)
    bm.to_mesh(me)
    bm.free()
    me.update()
    for v in me.vertices:
        v.co += v.normal * push[v.index]
    for f in me.polygons:
        f.use_smooth = True
    sub = ob.modifiers.new("smooth", "SUBSURF")
    sub.levels = sub.render_levels = 1


def rig(ob, parts):
    """The armature, bound by bone heat, then its arms and legs turned
    to hang straight down and that made its rest."""
    arm = bpy.data.armatures.new("human-rig")
    rob = bpy.data.objects.new("human-rig", arm)
    bpy.context.collection.objects.link(rob)
    bpy.context.view_layer.objects.active = rob
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm.edit_bones

    def bone(name, head, tail, parent=None):
        b = eb.new(name)
        b.head, b.tail = Vector(head), Vector(tail)
        b.roll = 0
        if parent:
            b.parent = eb[parent]
            b.use_connect = False
        return b

    bone("spine", (0, 0, ROOT), (0, 0, CHEST))
    bone("neck", (0, 0, CHEST), (0, 0, NECK), "spine")
    bone("head", (0, 0, NECK), (0, 0, CROWN), "neck")
    bone("pelvis", (0, 0, ROOT), (0, 0, ROOT - 0.06))
    for s, side in ((1, "L"), (-1, "R")):
        sh = _side(SHOULDER, s)
        d = (_side(ARM_TIP, s) - sh).normalized()
        el, wr = sh + d * UPPER, sh + d * (UPPER + FORE)
        bone("upper." + side, sh, el, "spine")
        bone("fore." + side, el, wr, "upper." + side)
        bone("hand." + side, wr, wr + d * HAND, "fore." + side)
        bone("thigh." + side, _side(HIP, s), _side(KNEE, s), "pelvis")
        bone("shin." + side, _side(KNEE, s), _side(ANKLE, s),
             "thigh." + side)
        bone("foot." + side, _side(ANKLE, s), _side(TOE, s),
             "shin." + side)
    bpy.ops.object.mode_set(mode="OBJECT")
    # Bound by bone heat.
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    ob.select_set(True)
    rob.select_set(True)
    bpy.context.view_layer.objects.active = rob
    bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    # The limbs straightened: each chain turned to hang down, the foot
    # forward; then that pose made the mesh's and the rig's rest.
    bpy.ops.object.mode_set(mode="POSE")
    for name in ("upper.L", "upper.R", "fore.L", "fore.R", "hand.L",
                 "hand.R", "thigh.L", "thigh.R", "shin.L", "shin.R"):
        pb = rob.pose.bones[name]
        bpy.context.view_layer.update()
        cur = (pb.tail - pb.head).normalized()
        want = Vector((0, 0, -1))
        q = cur.rotation_difference(want)
        mat = pb.matrix.copy()
        loc = mat.to_translation()
        pb.matrix = (Matrix.Translation(loc) @ q.to_matrix().to_4x4() @
                     Matrix.Translation(-loc) @ mat)
        bpy.context.view_layer.update()
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.context.view_layer.objects.active = ob
    for m in ob.modifiers:
        if m.type == "ARMATURE":
            # Below the smoothing, applied: the mesh keeps the pose.
            bpy.ops.object.modifier_move_to_index(modifier=m.name, index=0)
            bpy.ops.object.modifier_apply(modifier=m.name)
    bpy.context.view_layer.objects.active = rob
    bpy.ops.object.mode_set(mode="POSE")
    bpy.ops.pose.armature_apply(selected=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    mod = ob.modifiers.new("rig", "ARMATURE")
    mod.object = rob
    ob.modifiers.move(len(ob.modifiers) - 1, 0)
    for pb in rob.pose.bones:
        pb.rotation_mode = "XYZ"
    return rob


def build(look, kerchief=(0.6, 0.05, 0.04)):
    """The dressed, rigged human: (rig, [its meshes]); "kerchief" its
    colour, or None for bare hair."""
    ob = _load()
    dress(ob, look, kerchief)
    parts = []
    rob = rig(ob, parts)
    # Facing +X, as fencer.py's figure.
    rob.rotation_euler = (0, 0, math.pi / 2)
    return rob, [ob] + parts


def pose(rob, p, heading=0.0):
    """Set fencer.py's pose "p" (degrees, positive towards the front,
    the hips at p["hips"] metres), the figure turned to "heading"."""
    rob.location = (p.get("x", 0.0), 0, p.get("hips", 0.9) - 0.9)
    rob.rotation_euler = (0, 0, math.pi / 2 + math.radians(heading))
    for key, name in BONES.items():
        pb = rob.pose.bones[name]
        a = math.radians(p.get(key, 0.0))
        # Each bone's own X is the figure's side axis (roll 0, the
        # limbs vertical); its front is -Y in the rig's space.
        up = pb.bone.tail_local.z > pb.bone.head_local.z
        pb.rotation_euler = (-a if up else a, 0, 0)


def ground(rob, height=None):
    """Lower or raise the posed figure so its lower foot stands on the
    ground: its ankle at the rest's height above it."""
    bpy.context.view_layer.update()
    rest = ANKLE[2] if height is None else height
    low = min((rob.matrix_world @ rob.pose.bones[b].head).z
              for b in ("foot.L", "foot.R"))
    rob.location.z += rest - low
