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
import socket
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
    # DiffSynth's Canny patch: lighter than the InstantX Union ControlNet,
    # which with the model exhausts framework2's shared-memory GPU.
    "controlnet": "qwen_image_canny_diffsynth_controlnet.safetensors",
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


def generation_size(w, h, area=1.0):
    """Qwen-Image works best near 1.6 megapixels ("area" times that):
    keep the aspect, use multiples of 16."""
    scale = (area * 1472 * 1104 / (w * h)) ** 0.5
    return (max(16, round(w * scale / 16) * 16),
            max(16, round(h * scale / 16) * 16))


def characters(text, spec):
    """Put each "{name}" of [characters] into a prompt, so a person looks
    the same in every picture they appear in."""
    for name, desc in spec.get("characters", {}).items():
        text = text.replace("{%s}" % name, " ".join(desc.split()))
    if "{" in text:
        sys.exit("unknown character in: " + text.strip()[:60])
    return text


def workflow(asset, profile, seed, out_w, out_h, prefix, spec):
    """The ComfyUI API graph for one asset.  A framed asset is rendered
    at its frame's size and placed on a plain field of the full size."""
    p = PROFILES[profile]
    k = spec["pack"]["art_height"] / SCREEN_H
    frame = asset.get("frame")
    prompt = characters(asset["prompt"], spec)
    if frame:
        fx, fy, fw, fh = (round(v * k) for v in frame)
        if asset.get("gilded", True):
            prompt = prompt.strip() + " " + spec["pack"]["framed"].strip()
        rw, rh = fw, fh
    else:
        rw, rh = out_w, out_h
    if spec["pack"].get("style"):
        prompt = prompt.strip() + " " + spec["pack"]["style"].strip()
    # A ControlNet encodes its sketch in one piece at the render's size,
    # which the shared-memory GPU may refuse: such assets render smaller.
    gw, gh = generation_size(rw, rh, asset.get("gen_area", 1.0))
    grade = spec.get("grade") if asset.get("grade", True) else None
    trim = asset.get("trim", spec["pack"].get("frame_trim", 0.0)
                     if frame else 0.0)
    g = {
        "unet": {"class_type": "UNETLoader", "inputs": {
            "unet_name": MODELS["unet"], "weight_dtype": "fp8_e4m3fn"}},
        "clip": {"class_type": "CLIPLoader", "inputs": {
            "clip_name": MODELS["clip"], "type": "qwen_image"}},
        "vae": {"class_type": "VAELoader", "inputs": {
            "vae_name": MODELS["vae"]}},
        "pos": {"class_type": "CLIPTextEncode", "inputs": {
            "text": prompt, "clip": ["clip", 0]}},
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
            "image": ["trim", 0] if trim else ["decode", 0],
            "upscale_method": "lanczos",
            "width": rw, "height": rh, "crop": "center"}},
        "save": {"class_type": "SaveImage", "inputs": {
            "images": ["resize", 0], "filename_prefix": prefix}},
    }
    if trim:
        # The model may leave some wall or table around the subject: cut
        # into the render so the subject reaches the edges.
        cx, cy = round(gw * trim), round(gh * trim)
        g["trim"] = {"class_type": "ImageCrop", "inputs": {
            "image": ["decode", 0], "width": gw - 2 * cx,
            "height": gh - 2 * cy, "x": cx, "y": cy}}
    picture = ["resize", 0]
    # Pieces the screen needs at exact places (the board its title is
    # written on, page-turn arrows...), rendered on their own and placed
    # there: the scene's model will not put them there.  "plaque" is a
    # piece with the pack's plaque prompt.
    pieces = list(asset.get("pieces", []))
    if asset.get("plaque"):
        pieces.insert(0, {"rect": asset["plaque"],
                          "prompt": spec["pack"]["plaque"]})
    if pieces:
        g["pshift"] = {"class_type": "ModelSamplingAuraFlow", "inputs": {
            "model": ["lora", 0] if p["lora"] else ["unet", 0],
            "shift": SHIFT}}
    for n, piece in enumerate(pieces):
        px, py, pw, ph = (round(v * k) for v in piece["rect"])
        pgw, pgh = generation_size(pw, ph, 0.25)
        ptext = piece["prompt"].strip()
        if spec["pack"].get("style"):
            ptext += " " + spec["pack"]["style"].strip()
        g["ppos%d" % n] = {"class_type": "CLIPTextEncode", "inputs": {
            "text": ptext, "clip": ["clip", 0]}}
        g["platent%d" % n] = {"class_type": "EmptySD3LatentImage",
                              "inputs": {"width": pgw, "height": pgh,
                                         "batch_size": 1}}
        g["psample%d" % n] = {"class_type": "KSampler", "inputs": {
            "model": ["pshift", 0], "seed": seed + n, "steps": p["steps"],
            "cfg": p["cfg"], "sampler_name": "euler", "scheduler": "simple",
            "positive": ["ppos%d" % n, 0], "negative": ["neg", 0],
            "latent_image": ["platent%d" % n, 0], "denoise": 1.0}}
        g["pdecode%d" % n] = {"class_type": "VAEDecodeTiled", "inputs": {
            "samples": ["psample%d" % n, 0], "vae": ["vae", 0],
            "tile_size": 512, "overlap": 64, "temporal_size": 64,
            "temporal_overlap": 8}}
        g["presize%d" % n] = {"class_type": "ImageScale", "inputs": {
            "image": ["pdecode%d" % n, 0], "upscale_method": "lanczos",
            "width": pw, "height": ph, "crop": "center"}}
        g["pplace%d" % n] = {"class_type": "ImageCompositeMasked",
                             "inputs": {
                                 "destination": picture,
                                 "source": ["presize%d" % n, 0],
                                 "x": px, "y": py, "resize_source": False}}
        picture = ["pplace%d" % n, 0]
        g["save"]["inputs"]["images"] = picture
    if grade and grade.get("uploaded"):
        # One colour grade for the series: match the colours of the
        # reference picture, then a touch of contrast.
        g["ref"] = {"class_type": "LoadImage", "inputs": {
            "image": grade["uploaded"]}}
        g["transfer"] = {"class_type": "ColorTransfer", "inputs": {
            "image_target": picture, "image_ref": ["ref", 0],
            "method": grade.get("method", "reinhard_lab"),
            "source_stats": "per_frame",
            "strength": grade.get("strength", 0.5)}}
        g["contrast"] = {"class_type": "AdjustContrast", "inputs": {
            "images": ["transfer", 0],
            "factor": grade.get("contrast", 1.0)}}
        picture = ["contrast", 0]
        g["save"]["inputs"]["images"] = picture
    if frame:
        g["field"] = {"class_type": "EmptyImage", "inputs": {
            "width": out_w, "height": out_h, "batch_size": 1,
            "color": int(asset["field"].lstrip("#"), 16)}}
        g["place"] = {"class_type": "ImageCompositeMasked", "inputs": {
            "destination": ["field", 0], "source": picture,
            "x": fx, "y": fy, "resize_source": False}}
        g["save"]["inputs"]["images"] = ["place", 0]
    model = ["unet", 0]
    if asset.get("uploaded_control"):
        # A line sketch of the composition, drawn for this pack: the
        # model follows it early, then renders freely.
        g["cpatch"] = {"class_type": "ModelPatchLoader", "inputs": {
            "name": MODELS["controlnet"]}}
        g["sketch"] = {"class_type": "LoadImage", "inputs": {
            "image": asset["uploaded_control"]}}
        g["control"] = {"class_type": "QwenImageDiffsynthControlnet",
                        "inputs": {
                            "model": model, "model_patch": ["cpatch", 0],
                            "vae": ["vae", 0], "image": ["sketch", 0],
                            "strength": asset.get("control_strength", 0.7),
                            "start_percent": 0.0,
                            "end_percent": asset.get("control_end", 0.6)}}
        model = ["control", 0]
    if p["lora"]:
        g["lora"] = {"class_type": "LoraLoaderModelOnly", "inputs": {
            "model": model, "lora_name": MODELS["lightning"],
            "strength_model": 1.0}}
        model = ["lora", 0]
    g["shift"] = {"class_type": "ModelSamplingAuraFlow", "inputs": {
        "model": model, "shift": SHIFT}}
    positive, negative = ["pos", 0], ["neg", 0]
    if asset.get("uploaded_inpaint"):
        # Repaint a masked region the model keeps getting wrong, without
        # the sketch, and keep every other pixel of the render.
        g["ishift"] = {"class_type": "ModelSamplingAuraFlow", "inputs": {
            "model": ["lora", 0] if p["lora"] else ["unet", 0],
            "shift": SHIFT}}
        g["imask"] = {"class_type": "LoadImageMask", "inputs": {
            "image": asset["uploaded_inpaint"], "channel": "red"}}
        base = ["decode", 0]
        fill = asset.get("inpaint_fill")
        if fill:
            # Cover the region with a patch of plain background first, so
            # a light repaint blends it in instead of redrawing what the
            # surroundings suggest.  fill = [from_x, from_y, x, y, w, h].
            g["patch"] = {"class_type": "ImageCrop", "inputs": {
                "image": base, "width": fill[4], "height": fill[5],
                "x": fill[0], "y": fill[1]}}
            g["prefill"] = {"class_type": "ImageCompositeMasked",
                            "inputs": {
                                "destination": base, "source": ["patch", 0],
                                "x": fill[2], "y": fill[3],
                                "resize_source": False}}
            base = ["prefill", 0]
        g["ienc"] = {"class_type": "VAEEncodeTiled", "inputs": {
            "pixels": base, "vae": ["vae", 0], "tile_size": 512,
            "overlap": 64, "temporal_size": 64, "temporal_overlap": 8}}
        g["inoise"] = {"class_type": "SetLatentNoiseMask", "inputs": {
            "samples": ["ienc", 0], "mask": ["imask", 0]}}
        iprompt = asset["inpaint_prompt"].strip()
        if spec["pack"].get("style"):
            iprompt += " " + spec["pack"]["style"].strip()
        g["ipos"] = {"class_type": "CLIPTextEncode", "inputs": {
            "text": iprompt, "clip": ["clip", 0]}}
        g["isample"] = {"class_type": "KSampler", "inputs": {
            "model": ["ishift", 0], "seed": seed, "steps": p["steps"],
            "cfg": p["cfg"], "sampler_name": "euler", "scheduler": "simple",
            "positive": ["ipos", 0], "negative": ["neg", 0],
            "latent_image": ["inoise", 0],
            "denoise": asset.get("inpaint_denoise", 1.0)}}
        g["idecode"] = {"class_type": "VAEDecodeTiled", "inputs": {
            "samples": ["isample", 0], "vae": ["vae", 0], "tile_size": 512,
            "overlap": 64, "temporal_size": 64, "temporal_overlap": 8}}
        g["repaint"] = {"class_type": "ImageCompositeMasked", "inputs": {
            "destination": base, "source": ["idecode", 0],
            "x": 0, "y": 0, "resize_source": False,
            "mask": ["imask", 0]}}
        rendered = ["repaint", 0]
    else:
        rendered = ["decode", 0]
    if trim:
        g["trim"]["inputs"]["image"] = rendered
    else:
        g["resize"]["inputs"]["image"] = rendered
    g["sample"] = {"class_type": "KSampler", "inputs": {
        "model": ["shift", 0], "seed": seed, "steps": p["steps"],
        "cfg": p["cfg"], "sampler_name": "euler", "scheduler": "simple",
        "positive": positive, "negative": negative,
        "latent_image": ["latent", 0], "denoise": 1.0}}
    return g


DEFAULT_NEGATIVE = ("text, letters, words, watermark, signature, logo, "
                    "frame, border, cartoon, illustration, painting, "
                    "anime, 3d render, lowres, blurry, deformed")


def reachable(server):
    """The server's URL with the first address that answers: the host
    may resolve to addresses the server does not listen on (IPv6 for a
    ComfyUI on 0.0.0.0), each of which would stall every request."""
    u = urllib.parse.urlsplit(server)
    port = u.port or 80
    for family, kind, proto, _, addr in socket.getaddrinfo(
            u.hostname, port, type=socket.SOCK_STREAM):
        try:
            with socket.socket(family, kind, proto) as sock:
                sock.settimeout(2)
                sock.connect(addr)
        except OSError:
            continue
        host = "[%s]" % addr[0] if family == socket.AF_INET6 else addr[0]
        return "%s://%s:%d" % (u.scheme, host, port)
    sys.exit("cannot reach " + server)


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


def upload(server, path, name):
    """Put a local picture into ComfyUI's input folder as "name"."""
    boundary = uuid.uuid4().hex
    with open(path, "rb") as f:
        data = f.read()
    body = b"".join([
        b"--" + boundary.encode() + b"\r\n",
        b'Content-Disposition: form-data; name="image"; filename="' +
        name.encode() + b'"\r\n',
        b"Content-Type: image/png\r\n\r\n", data, b"\r\n",
        b"--" + boundary.encode() + b"\r\n",
        b'Content-Disposition: form-data; name="overwrite"\r\n\r\n',
        b"true\r\n",
        b"--" + boundary.encode() + b"--\r\n"])
    req = urllib.request.Request(server + "/upload/image", data=body,
                                 headers={"Content-Type":
                                          "multipart/form-data; boundary=" +
                                          boundary})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.loads(r.read())["name"]


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
    ap.add_argument("--keep-loaded", action="store_true",
                    help="do not unload ComfyUI's models first (for runs "
                    "back to back)")
    ap.add_argument("ids", nargs="*")
    args = ap.parse_args()
    args.server = reachable(args.server)

    with open(args.spec, "rb") as f:
        spec = tomllib.load(f)
    height = spec["pack"]["art_height"]
    assets = [a for a in spec["asset"] if not args.ids or
              a["id"] in args.ids]
    missing = set(args.ids) - {a["id"] for a in assets}
    if missing:
        sys.exit("unknown asset: " + ", ".join(sorted(missing)))
    profile = "draft" if args.draft else "final"
    failed = []
    system = json.loads(call(args.server, "/system_stats"))["system"]
    # The colour grade's reference: a picture already in the pack.
    grade = spec.get("grade")
    if grade:
        ref = os.path.join(args.pack, grade["reference"])
        if os.path.exists(ref):
            grade["uploaded"] = upload(args.server, ref,
                                       "stmalo-grade-reference.png")
            grade["reference_sha256"] = hashlib.sha256(
                open(ref, "rb").read()).hexdigest()
        else:
            print("no grade reference %s yet: rendering ungraded" % ref,
                  file=sys.stderr)
    # Other workflows' models may fill the GPU: let ComfyUI unload them.
    if not args.keep_loaded:
        call(args.server, "/free", json.dumps(
            {"unload_models": True, "free_memory": True}).encode())
    for a in assets:
        lw, lh = a.get("size", [SCREEN_W, SCREEN_H])
        ow = round(lw * height / SCREEN_H)
        oh = round(lh * height / SCREEN_H)
        seed = args.seed if args.seed is not None else a["seed"]
        if a.get("inpaint_mask"):
            a["uploaded_inpaint"] = upload(
                args.server, os.path.join(HERE, a["inpaint_mask"]),
                "stmalo-inpaint-" + os.path.basename(a["inpaint_mask"]))
        if a.get("control"):
            a["uploaded_control"] = upload(
                args.server, os.path.join(HERE, a["control"]),
                "stmalo-control-" + os.path.basename(a["control"]))
        name = os.path.splitext(a["file"])[0]
        graph = workflow(a, profile, seed, ow, oh, "stmalo/" +
                         name.replace("/", "-"), spec)
        t0 = time.time()
        try:
            png = render(args.server, graph)
        except RuntimeError as e:
            print("%s: failed: %s" % (a["id"], e), file=sys.stderr)
            failed.append(a["id"])
            continue
        out = os.path.join(args.pack, a["file"])
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "wb") as f:
            f.write(png)
        prov = {
            "id": a["id"], "file": a["file"], "size": [ow, oh],
            "profile": profile, "seed": seed,
            "prompt": graph["pos"]["inputs"]["text"],
            "frame": a.get("frame"), "field": a.get("field"),
            "control": {"sketch": a["control"],
                        "sha256": hashlib.sha256(open(os.path.join(
                            HERE, a["control"]), "rb").read()).hexdigest(),
                        "strength": a.get("control_strength", 0.7),
                        "end": a.get("control_end", 0.6)}
            if a.get("control") else None,
            "inpaint": {"mask": a["inpaint_mask"],
                        "sha256": hashlib.sha256(open(os.path.join(
                            HERE, a["inpaint_mask"]), "rb").read())
                        .hexdigest(),
                        "prompt": a["inpaint_prompt"].strip()}
            if a.get("inpaint_mask") else None,
            "grade": {k: v for k, v in grade.items() if k != "uploaded"}
            if grade and grade.get("uploaded") and a.get("grade", True)
            else None,
            "source": a.get("source", "text"),
            "negative": a.get("negative", DEFAULT_NEGATIVE),
            "models": {k: v for k, v in MODELS.items()
                       if (k != "lightning" or PROFILES[profile]["lora"])
                       and (k != "controlnet" or a.get("control"))},
            "workflow_sha256": hashlib.sha256(json.dumps(
                graph, sort_keys=True).encode()).hexdigest(),
            "comfyui": system.get("comfyui_version"),
            "generator": git_rev(),
            "date": datetime.datetime.now(datetime.timezone.utc)
            .isoformat(timespec="seconds"),
        }
        pdir = os.path.join(args.pack, "provenance")
        os.makedirs(pdir, exist_ok=True)
        with open(os.path.join(pdir, a["id"].replace("/", "-") + ".json"),
                  "w") as f:
            json.dump(prov, f, indent=2)
            f.write("\n")
        print("%s: %s %dx%d seed %d, %.0f s" % (a["id"], profile, ow, oh,
                                                seed, time.time() - t0))
    write_manifest(args.pack, spec)
    if failed:
        sys.exit("failed: " + ", ".join(failed))


if __name__ == "__main__":
    main()
