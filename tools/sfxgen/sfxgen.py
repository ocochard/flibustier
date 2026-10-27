#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbe
#
"""Generate the open pack's sound effects with ComfyUI.

Each sound of sounds.toml is rendered from its text prompt alone (never
from the original's sounds, so the result carries no original data) with
Stable Audio Open 1.0, its silence trimmed, faded out, converted to OGG
Vorbis with ffmpeg, and written to the pack with a provenance record
(model files, prompt, seed, workflow) and the pack.d/sound.ini fragment.

    sfxgen.py [--server URL] [--pack DIR] [--seed N] [--force] [ID ...]

Without IDs, every sound missing from the pack is rendered; --force
renders the named (or all) sounds again.
"""

import argparse
import datetime
import json
import os
import subprocess
import sys
import tempfile
import time
import tomllib
import urllib.parse
import urllib.request
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))


def call(server, path, data=None):
    req = urllib.request.Request(server + path, data=data, headers={
        "Content-Type": "application/json"} if data else {})
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.read()


def workflow(spec, sound, seed, prefix):
    r = spec["render"]
    return {
        "ckpt": {"class_type": "CheckpointLoaderSimple",
                 "inputs": {"ckpt_name": r["checkpoint"]}},
        "t5": {"class_type": "CLIPLoader",
               "inputs": {"clip_name": r["text_encoder"],
                          "type": "stable_audio"}},
        "pos": {"class_type": "CLIPTextEncode",
                "inputs": {"text": sound["prompt"], "clip": ["t5", 0]}},
        "neg": {"class_type": "CLIPTextEncode",
                "inputs": {"text": sound.get("negative", r["negative"]),
                           "clip": ["t5", 0]}},
        "cond": {"class_type": "ConditioningStableAudio",
                 "inputs": {"positive": ["pos", 0], "negative": ["neg", 0],
                            "seconds_start": 0.0,
                            "seconds_total": float(sound["seconds"])}},
        "latent": {"class_type": "EmptyLatentAudio",
                   "inputs": {"seconds": float(sound["seconds"]),
                              "batch_size": 1}},
        "sample": {"class_type": "KSampler",
                   "inputs": {"model": ["ckpt", 0], "seed": seed,
                              "steps": r["steps"], "cfg": r["cfg"],
                              "sampler_name": r["sampler"],
                              "scheduler": r["scheduler"], "denoise": 1.0,
                              "positive": ["cond", 0],
                              "negative": ["cond", 1],
                              "latent_image": ["latent", 0]}},
        "decode": {"class_type": "VAEDecodeAudio",
                   "inputs": {"samples": ["sample", 0], "vae": ["ckpt", 2]}},
        "save": {"class_type": "SaveAudio",
                 "inputs": {"audio": ["decode", 0],
                            "filename_prefix": prefix}},
    }


def queue(server, graph):
    """Queue "graph"; returns its prompt ID.  Queued together, the sounds
    run back to back, the model loaded once, between other clients'
    jobs (the art generator shares the server)."""
    reply = json.loads(call(server, "/prompt", json.dumps(
        {"prompt": graph, "client_id": str(uuid.uuid4())}).encode()))
    if reply.get("node_errors"):
        raise RuntimeError(json.dumps(reply["node_errors"]))
    return reply["prompt_id"]


def fetch(server, pid):
    """Wait for prompt "pid"; return the saved audio file's bytes."""
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
                for a in out.get("audio", [])[:1]:
                    q = urllib.parse.urlencode({
                        "filename": a["filename"],
                        "subfolder": a.get("subfolder", ""),
                        "type": a.get("type", "output")})
                    return call(server, "/view?" + q)
            raise RuntimeError("no audio in the output")
        time.sleep(1)


def finish(raw, out):
    """Trim the silence at both ends, fade the last 50 ms, OGG Vorbis."""
    trim = ("silenceremove=start_periods=1:start_threshold=-45dB,"
            "areverse,silenceremove=start_periods=1:start_threshold=-45dB,"
            "afade=t=in:d=0.05,areverse,loudnorm=I=-16:TP=-1.5")
    with tempfile.NamedTemporaryFile(suffix=".flac") as f:
        f.write(raw)
        f.flush()
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", f.name,
                        "-af", trim, "-ar", "48000", "-c:a", "libvorbis",
                        "-q:a", "6", out], check=True)


def git_rev():
    try:
        return subprocess.run(["git", "-C", HERE, "rev-parse", "--short",
                               "HEAD"], capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def write_fragment(pack, spec):
    lines = ["# Generated by tools/sfxgen/sfxgen.py; see provenance/.",
             "# Sound effects rendered with Stable Audio Open 1.0 from text",
             "# prompts alone (no original sound as input).", "",
             "[assets]"]
    for s in spec["sound"]:
        f = s["id"] + ".ogg"
        if os.path.exists(os.path.join(pack, f)):
            lines.append("%s = %s" % (s["id"], f))
    os.makedirs(os.path.join(pack, "pack.d"), exist_ok=True)
    with open(os.path.join(pack, "pack.d", "sound.ini"), "w") as fp:
        fp.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--server", default=os.environ.get(
        "FLIBUSTIER_COMFYUI", "http://framework2:8188"))
    ap.add_argument("--pack", default=os.path.expanduser(
        "~/flibustier-openpack"))
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--force", action="store_true")
    ap.add_argument("ids", nargs="*")
    a = ap.parse_args()
    with open(os.path.join(HERE, "sounds.toml"), "rb") as f:
        spec = tomllib.load(f)
    todo = [s for s in spec["sound"] if not a.ids or s["id"] in a.ids]
    if a.ids and len(todo) != len(set(a.ids)):
        sys.exit("unknown id among: " + " ".join(a.ids))
    os.makedirs(os.path.join(a.pack, "sound"), exist_ok=True)
    os.makedirs(os.path.join(a.pack, "provenance"), exist_ok=True)
    jobs = []
    for s in todo:
        out = os.path.join(a.pack, s["id"] + ".ogg")
        if os.path.exists(out) and not a.force:
            continue
        name = s["id"].replace("/", "-")
        graph = workflow(spec, s, a.seed, "flibustier/" + name)
        jobs.append((s, out, name, graph, queue(a.server, graph)))
    t0 = time.time()
    for s, out, name, graph, pid in jobs:
        finish(fetch(a.server, pid), out)
        rec = {"id": s["id"], "file": s["id"] + ".ogg",
               "model": spec["render"]["checkpoint"],
               "text_encoder": spec["render"]["text_encoder"],
               "prompt": s["prompt"], "seed": a.seed,
               "seconds": s["seconds"], "workflow": graph,
               "tool": "tools/sfxgen/sfxgen.py@" + git_rev(),
               "date": datetime.date.today().isoformat()}
        with open(os.path.join(a.pack, "provenance", name + ".json"),
                  "w") as fp:
            json.dump(rec, fp, indent=1)
        print("%s: %.0f s" % (s["id"], time.time() - t0), flush=True)
    write_fragment(a.pack, spec)


if __name__ == "__main__":
    main()
