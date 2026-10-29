#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Photographic materials and sky for the pack's Blender models.

The models (fencers/fencer.py, fencers/walker.py, ships/ship.py) are
rendered with real scanned materials, so their renders are the pack's
pictures as they are, with no image model over them: colour, roughness
and normal maps from ambientCG, and a sky from Poly Haven, both public
domain (CC0).  LIBRARY names what each part wears; the files are fetched
on the render host into $FLIBUSTIER_CC0 (~/flibustier-cc0) by fetch(),
and the pack's provenance records the asset names.

Blender only (bpy); a model calls pbr() and sky(), which fall back to a
plain colour when the files are missing.
"""

import glob
import os

LIB = os.environ.get("FLIBUSTIER_CC0", os.path.expanduser("~/flibustier-cc0"))

# What each kind of part is made of: an ambientCG material, and how many
# times its picture repeats in a metre.
LIBRARY = {
    "linen": ("Fabric061", 6.0),
    "wool": ("Fabric066", 6.0),
    "leather": ("Leather026", 5.0),
    "planks": ("Planks023A", 1.2),
    "canvas": ("Fabric086", 2.0),
    "rope": ("Rope001", 8.0),
    "iron": ("Metal041B", 2.0),
    "gilt": ("Metal048C", 3.0),
}
SKY = "kloofendal_48d_partly_cloudy_puresky"	# a Poly Haven HDRI

_means = {}


def _file(asset, kind):
    found = sorted(glob.glob(os.path.join(LIB, asset, "*_%s.jpg" % kind)))
    return found[0] if found else None


def _mean(image):
    """The picture's mean brightness, to tint it without darkening
    (sampled: the host's Blender may have no numpy)."""
    if image.name not in _means:
        px = image.pixels[:]
        step = 4 * 97
        vals = [(px[i] + px[i + 1] + px[i + 2]) / 3
                for i in range(0, len(px) - 3, step)]
        _means[image.name] = max(0.05, sum(vals) / max(1, len(vals)))
    return _means[image.name]


def pbr(name, kind, colour=None, rough=0.7, metal=0.0, sss=0.0):
    """A material of LIBRARY's "kind" (or a plain one if None), tinted to
    "colour": its picture's light and dark kept, its hue the colour's."""
    import bpy
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    if colour is not None:
        bsdf.inputs["Base Color"].default_value = (*colour, 1.0)
    if sss and "Subsurface Weight" in bsdf.inputs:
        bsdf.inputs["Subsurface Weight"].default_value = sss
    if kind is None or kind not in LIBRARY:
        return m
    asset, repeat = LIBRARY[kind]
    col = _file(asset, "Color")
    if col is None:
        return m
    coord = nt.nodes.new("ShaderNodeTexCoord")
    mapping = nt.nodes.new("ShaderNodeMapping")
    mapping.inputs["Scale"].default_value = (repeat, repeat, repeat)
    nt.links.new(coord.outputs["Object"], mapping.inputs["Vector"])

    def tex(path, data):
        t = nt.nodes.new("ShaderNodeTexImage")
        t.image = bpy.data.images.load(path, check_existing=True)
        if data:
            t.image.colorspace_settings.name = "Non-Color"
        t.projection = "BOX"
        t.projection_blend = 0.3
        nt.links.new(mapping.outputs["Vector"], t.inputs["Vector"])
        return t

    c = tex(col, False)
    if colour is None:
        nt.links.new(c.outputs["Color"], bsdf.inputs["Base Color"])
    else:
        bw = nt.nodes.new("ShaderNodeRGBToBW")
        nt.links.new(c.outputs["Color"], bw.inputs["Color"])
        norm = nt.nodes.new("ShaderNodeMath")
        norm.operation = "DIVIDE"
        norm.inputs[1].default_value = _mean(c.image)
        nt.links.new(bw.outputs["Val"], norm.inputs[0])
        mix = nt.nodes.new("ShaderNodeMix")
        mix.data_type = "RGBA"
        mix.blend_type = "MULTIPLY"
        mix.inputs["Factor"].default_value = 1.0
        mix.inputs["A"].default_value = (*colour, 1.0)
        nt.links.new(norm.outputs["Value"], mix.inputs["B"])
        nt.links.new(mix.outputs["Result"], bsdf.inputs["Base Color"])
    r = _file(asset, "Roughness")
    if r:
        nt.links.new(tex(r, True).outputs["Color"], bsdf.inputs["Roughness"])
    n = _file(asset, "NormalGL")
    if n:
        nm = nt.nodes.new("ShaderNodeNormalMap")
        nt.links.new(tex(n, True).outputs["Color"], nm.inputs["Color"])
        nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    if metal and _file(asset, "Metalness"):
        nt.links.new(tex(_file(asset, "Metalness"), True).outputs["Color"],
                     bsdf.inputs["Metallic"])
    return m


def sky(scene, strength=1.0, fallback=(0.6, 0.65, 0.75)):
    """The world lit by the sky's photograph (only its light: the film
    stays transparent), or a plain colour without it."""
    import bpy
    world = bpy.data.worlds.new("sky")
    world.use_nodes = True
    nt = world.node_tree
    bg = nt.nodes["Background"]
    bg.inputs["Strength"].default_value = strength
    path = os.path.join(LIB, "%s_1k.hdr" % SKY)
    if os.path.exists(path):
        env = nt.nodes.new("ShaderNodeTexEnvironment")
        env.image = bpy.data.images.load(path, check_existing=True)
        nt.links.new(env.outputs["Color"], bg.inputs["Color"])
    else:
        bg.inputs["Color"].default_value = (*fallback, 1)
    scene.world = world
    return world


def fetch_command():
    """A shell command that fetches the library on the render host, once."""
    cmds = ['mkdir -p "%s" && cd "%s"' % (LIB.replace(
        os.path.expanduser("~"), "$HOME"), LIB.replace(
        os.path.expanduser("~"), "$HOME"))]
    for asset, _ in sorted(set(LIBRARY.values())):
        cmds.append('[ -d %s ] || { curl -sfL -o %s.zip '
                    '"https://ambientcg.com/get?file=%s_1K-JPG.zip" && '
                    'mkdir %s && (cd %s && unzip -q ../%s.zip) && '
                    'rm %s.zip; }' % ((asset,) * 7))
    # The human body: Blender's Human Base Meshes bundle (CC0).
    cmds.append('[ -d human ] || { mkdir human && (cd human && curl -sfL '
                '-o hbm.zip "https://download.blender.org/demo/asset-bundles/'
                'human-base-meshes/human-base-meshes-bundle-v1.4.1.zip" && '
                'unzip -q hbm.zip && rm hbm.zip); }')
    cmds.append('[ -f %s_1k.hdr ] || curl -sfL -o %s_1k.hdr '
                '"https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/'
                '%s_1k.hdr"' % ((SKY,) * 3))
    return " && ".join(cmds)
