#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Generate the open pack's art with ComfyUI.

Each asset of assets.toml is rendered from its text prompt alone (never
from original art, so the result can carry an open licence) with
Qwen-Image-2512, resized to its size at the pack's art height, and
written to the pack with a provenance record: model files, prompt, seed
and workflow, enough to render it again.

    artgen.py [--server URL] [--pack DIR] [--draft] [--seed N] [ID ...]

Without IDs, every asset is rendered.  --draft uses the 8-step Lightning
LoRA, for trying prompts; finals use the full sampler.
"""

import argparse
import datetime
import hashlib
import json
import os
import subprocess
import sys
import time
import tomllib
import urllib.parse
import urllib.request
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
SCREEN_W, SCREEN_H = 640, 480

MODELS = {
    "unet": "qwen_image_2512_fp8_e4m3fn.safetensors",
    "clip": "qwen_2.5_vl_7b_fp8_scaled.safetensors",
    "vae": "qwen_image_vae.safetensors",
    "lightning": "Qwen-Image-2512-Lightning-8steps-V1.0-bf16.safetensors",
}

# Sampling: the full model for finals, Lightning for drafts.
PROFILES = {
    "final": {"steps": 40, "cfg": 2.5, "lora": False},
    "draft": {"steps": 8, "cfg": 1.0, "lora": True},
}
SHIFT = 3.1


def generation_size(w, h):
    """Qwen-Image works best near 1.6 megapixels: keep the aspect, use
    multiples of 16."""
    scale = (1472 * 1104 / (w * h)) ** 0.5
    return (max(16, round(w * scale / 16) * 16),
            max(16, round(h * scale / 16) * 16))


def workflow(asset, profile, seed, out_w, out_h, prefix):
    """The ComfyUI API graph for one asset."""
    p = PROFILES[profile]
    gw, gh = generation_size(out_w, out_h)
    g = {
        "unet": {"class_type": "UNETLoader", "inputs": {
            "unet_name": MODELS["unet"], "weight_dtype": "fp8_e4m3fn"}},
        "clip": {"class_type": "CLIPLoader", "inputs": {
            "clip_name": MODELS["clip"], "type": "qwen_image"}},
        "vae": {"class_type": "VAELoader", "inputs": {
            "vae_name": MODELS["vae"]}},
        "pos": {"class_type": "CLIPTextEncode", "inputs": {
            "text": asset["prompt"], "clip": ["clip", 0]}},
        "neg": {"class_type": "CLIPTextEncode", "inputs": {
            "text": asset.get("negative", DEFAULT_NEGATIVE),
            "clip": ["clip", 0]}},
        "latent": {"class_type": "EmptySD3LatentImage", "inputs": {
            "width": gw, "height": gh, "batch_size": 1}},
        # Tiled: one full-size decode needs a single large allocation,
        # which the shared-memory GPU of framework2 refuses.
        "decode": {"class_type": "VAEDecodeTiled", "inputs": {
            "samples": ["sample", 0], "vae": ["vae", 0],
            "tile_size": 512, "overlap": 64, "temporal_size": 64,
            "temporal_overlap": 8}},
        "resize": {"class_type": "ImageScale", "inputs": {
            "image": ["decode", 0], "upscale_method": "lanczos",
            "width": out_w, "height": out_h, "crop": "center"}},
        "save": {"class_type": "SaveImage", "inputs": {
            "images": ["resize", 0], "filename_prefix": prefix}},
    }
    model = ["unet", 0]
    if p["lora"]:
        g["lora"] = {"class_type": "LoraLoaderModelOnly", "inputs": {
            "model": model, "lora_name": MODELS["lightning"],
            "strength_model": 1.0}}
        model = ["lora", 0]
    g["shift"] = {"class_type": "ModelSamplingAuraFlow", "inputs": {
        "model": model, "shift": SHIFT}}
    g["sample"] = {"class_type": "KSampler", "inputs": {
        "model": ["shift", 0], "seed": seed, "steps": p["steps"],
        "cfg": p["cfg"], "sampler_name": "euler", "scheduler": "simple",
        "positive": ["pos", 0], "negative": ["neg", 0],
        "latent_image": ["latent", 0], "denoise": 1.0}}
    return g


DEFAULT_NEGATIVE = ("text, letters, words, watermark, signature, logo, "
                    "frame, border, cartoon, illustration, painting, "
                    "anime, 3d render, lowres, blurry, deformed")


def call(server, path, data=None):
    req = urllib.request.Request(server + path, data=data, headers={
        "Content-Type": "application/json"} if data else {})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def render(server, graph):
    """Queue "graph", wait for it, return the saved image's bytes."""
    client = str(uuid.uuid4())
    reply = json.loads(call(server, "/prompt", json.dumps(
        {"prompt": graph, "client_id": client}).encode()))
    if reply.get("node_errors"):
        raise RuntimeError(json.dumps(reply["node_errors"]))
    pid = reply["prompt_id"]
    while True:
        hist = json.loads(call(server, "/history/" + pid))
        if pid in hist:
            entry = hist[pid]
            status = entry.get("status", {})
            if status.get("status_str") == "error":
                for kind, msg in status.get("messages", []):
                    if kind == "execution_error":
                        raise RuntimeError("%s: %s" % (
                            msg.get("node_type"),
                            msg.get("exception_message", "").split("\n")[0]))
                raise RuntimeError("generation failed")
            for out in entry.get("outputs", {}).values():
                for img in out.get("images", []):
                    q = urllib.parse.urlencode({
                        "filename": img["filename"],
                        "subfolder": img.get("subfolder", ""),
                        "type": img.get("type", "output")})
                    return call(server, "/view?" + q)
            raise RuntimeError("no image in the output")
        time.sleep(2)


def git_rev():
    try:
        return subprocess.run(["git", "-C", HERE, "rev-parse", "--short",
                               "HEAD"], capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def write_manifest(pack, spec):
    """pack.ini: every asset that has art in the pack."""
    lines = ["# Generated by tools/artgen/artgen.py; see provenance/.",
             "[pack]", "name = " + spec["pack"]["name"],
             "license = " + spec["pack"]["license"],
             "art_height = %d" % spec["pack"]["art_height"], "",
             "[assets]"]
    for a in spec["asset"]:
        if os.path.exists(os.path.join(pack, a["file"])):
            lines.append("%s = %s" % (a["id"], a["file"]))
    with open(os.path.join(pack, "pack.ini"), "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--server", default=os.environ.get(
        "STMALO_COMFYUI", "http://framework2:8188"))
    ap.add_argument("--pack", default=os.path.expanduser(
        "~/stmalo-openpack"))
    ap.add_argument("--spec", default=os.path.join(HERE, "assets.toml"))
    ap.add_argument("--draft", action="store_true")
    ap.add_argument("--seed", type=int, help="override every seed")
    ap.add_argument("ids", nargs="*")
    args = ap.parse_args()

    with open(args.spec, "rb") as f:
        spec = tomllib.load(f)
    height = spec["pack"]["art_height"]
    assets = [a for a in spec["asset"] if not args.ids or
              a["id"] in args.ids]
    missing = set(args.ids) - {a["id"] for a in assets}
    if missing:
        sys.exit("unknown asset: " + ", ".join(sorted(missing)))
    profile = "draft" if args.draft else "final"
    system = json.loads(call(args.server, "/system_stats"))["system"]
    # Other workflows' models may fill the GPU: let ComfyUI unload them.
    call(args.server, "/free", json.dumps({"unload_models": True,
                                           "free_memory": True}).encode())
    for a in assets:
        lw, lh = a.get("size", [SCREEN_W, SCREEN_H])
        ow = round(lw * height / SCREEN_H)
        oh = round(lh * height / SCREEN_H)
        seed = args.seed if args.seed is not None else a["seed"]
        name = os.path.splitext(a["file"])[0]
        graph = workflow(a, profile, seed, ow, oh, "stmalo/" +
                         name.replace("/", "-"))
        t0 = time.time()
        png = render(args.server, graph)
        out = os.path.join(args.pack, a["file"])
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "wb") as f:
            f.write(png)
        prov = {
            "id": a["id"], "file": a["file"], "size": [ow, oh],
            "profile": profile, "seed": seed, "prompt": a["prompt"],
            "negative": a.get("negative", DEFAULT_NEGATIVE),
            "models": MODELS if PROFILES[profile]["lora"] else
            {k: v for k, v in MODELS.items() if k != "lightning"},
            "workflow_sha256": hashlib.sha256(json.dumps(
                graph, sort_keys=True).encode()).hexdigest(),
            "comfyui": system.get("comfyui_version"),
            "generator": git_rev(),
            "date": datetime.datetime.now(datetime.timezone.utc)
            .isoformat(timespec="seconds"),
        }
        pdir = os.path.join(args.pack, "provenance")
        os.makedirs(pdir, exist_ok=True)
        with open(os.path.join(pdir, name.replace("/", "-") + ".json"),
                  "w") as f:
            json.dump(prov, f, indent=2)
            f.write("\n")
        print("%s: %s %dx%d seed %d, %.0f s" % (a["id"], profile, ow, oh,
                                                seed, time.time() - t0))
    write_manifest(args.pack, spec)


if __name__ == "__main__":
    main()
