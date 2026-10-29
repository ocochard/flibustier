#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Write the open pack's coverage: every asset ID of the game, and
whether the pack replaces it.

    coverage.py [--data PATH] [--pack DIR] [--out FILE]

The IDs come from "flibustier --list" over the original data, and for
music and sound, which the game data plays through its sound drivers,
from the IDs the engine's source asks for; the pack's from its pack.ini
and pack.d/*.ini.  For the text the pack gives, the entries it lacks
are counted against the original's tables ("flibustier --export-text",
into a temporary directory).  Prints a Markdown table, or writes it to
FILE.
"""

import argparse
import glob
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

# What each kind of ID is, and the kinds not meant to be drawn.
KINDS = {
    "pic": "full-screen pictures",
    "spr": "sprite banks",
    "cur": "sprite banks (raw)",
    "grp": "sword fight bodies",
    "img": "standalone pictures",
    "tiles": "tile sets",
    "tiles15": "battle screen tiles",
    "ui": "interface sprites",
    "music": "music",
    "sound": "sound effects",
    "font": "fonts",
    "patch": "map era patches",
    "towns": "towns per era",
    "world": "sailing worlds",
    "text": "text",
    "driver": "sound drivers",
    "program": "game program",
}
NOT_ART = {"patch", "towns", "world", "driver", "program"}
# The 16-colour copies the installer makes, which the engine never uses.
UNUSED = {"ui/misc16", "ui/miscf16", "ui/miscg16", "tiles/16map16",
          "tiles/16map32", "tiles/tmap16", "img/cloud16"}


def engine_audio():
    """The music and sound IDs the engine plays (they replace the sound
    drivers' tunes and effects, so the game data has none to list),
    from its source: variants (-2, -3...) count with their tune."""
    found = set()
    for path in glob.glob(os.path.join(ROOT, "src", "**", "*.c"),
                          recursive=True):
        for m in re.finditer(r'"((?:music|sound)/[a-z0-9-]+)"',
                             open(path, errors="replace").read()):
            found.add(re.sub(r"-\d+$", "", m.group(1)))
    found.discard("sound/name")		# a prefix, not an ID
    return sorted(found)


def pack_ids(pack):
    ids = {}
    for ini in [os.path.join(pack, "pack.ini")] + sorted(
            glob.glob(os.path.join(pack, "pack.d", "*.ini"))):
        section = None
        for line in open(ini):
            line = line.strip()
            if line.startswith("["):
                section = line
            elif section == "[assets]" and "=" in line and \
                    not line.startswith("#"):
                k, v = (x.strip() for x in line.split("=", 1))
                ids[k] = v
    return ids


def text_gaps(exe, data, pack, have):
    """Per text ID the pack gives: the entries the original has (not
    empty) and the pack lacks, as "string N" and "group N" lines."""
    gaps = {}
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([exe, "--data", data, "--export-text", tmp],
                       capture_output=True, check=True)
        for i, f in sorted(have.items()):
            parts = i.split("/")
            if parts[0] != "text" or len(parts) != 3:
                continue
            ref = os.path.join(tmp, parts[1], parts[2] + ".json")
            if not os.path.exists(ref) or not f.endswith(".json"):
                continue
            key = "strings" if parts[2] == "cstr" else "groups"
            orig = json.load(open(ref))[key]
            mine = json.load(open(os.path.join(pack, f))).get(key, {})
            missing = []
            for n, v in orig.items():
                m = mine.get(n)
                if key == "strings" and v and not m:
                    missing.append("string " + n)
                elif key == "groups" and len(m or []) < len(v):
                    missing.append("group %s (%d of %d words)" % (
                        n, len(m or []), len(v)))
            gaps[i] = missing
    return gaps


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", default=os.environ.get("FLIBUSTIER_DATA"))
    ap.add_argument("--pack", default=os.path.expanduser(
        "~/flibustier-openpack"))
    ap.add_argument("--out")
    args = ap.parse_args()
    exe = os.path.join(ROOT, "build", "flibustier")
    game = subprocess.run([exe, "--data", args.data, "--list"],
                          capture_output=True, text=True, check=True)
    audio = engine_audio()
    ids = sorted(set(game.stdout.split()) | set(audio))
    have = pack_ids(args.pack)
    # A tune counts as done when the pack has it or any of its variants.
    for i in audio:
        if i not in have:
            v = sorted(k for k in have if re.fullmatch(re.escape(i) +
                                                       r"-\d+", k))
            if v:
                have[i] = have[v[0]]
    rows, counts = [], {}
    for i in ids:
        kind = i.split("/")[0]
        if kind in NOT_ART:
            state = "not art"
        elif i in UNUSED:
            state = "not used"
        elif i in have:
            state = "done"
        else:
            state = "to do"
        counts.setdefault(kind, {}).setdefault(state, 0)
        counts[kind][state] += 1
        rows.append((i, kind, state, have.get(i, "")))
    # Music and sounds of the pack only (no original ID to replace one
    # to one): counted by the pack.
    extra = sorted(k for k in have if k not in ids and
                   re.sub(r"-\d+$", "", k) not in ids)
    out = ["# Open pack coverage", "",
           "Generated by `tools/artgen/coverage.py` from the game's asset "
           "list and the pack's manifests.", "",
           "## Summary", "",
           "| Kind | What | Done | To do | Not used | Not art |",
           "|---|---|---:|---:|---:|---:|"]
    for kind in sorted(counts):
        c = counts[kind]
        out.append("| `%s` | %s | %d | %d | %d | %d |" % (
            kind, KINDS.get(kind, ""), c.get("done", 0), c.get("to do", 0),
            c.get("not used", 0), c.get("not art", 0)))
    tot = {s: sum(c.get(s, 0) for c in counts.values())
           for s in ("done", "to do", "not used", "not art")}
    out.append("| **all** | | **%d** | **%d** | **%d** | **%d** |" % (
        tot["done"], tot["to do"], tot["not used"], tot["not art"]))
    variants = sorted(k for k in have if k not in ids and k not in extra)
    if variants:
        out += ["", "Music and sound have no IDs in the game data (the "
                "original plays them through its sound drivers): their "
                "rows are the IDs the engine plays. The pack also holds "
                "%d variants of them, played at random." % len(variants)]
    if extra:
        out += ["", "The pack also holds %d assets the engine does not "
                "ask for: %s." % (len(extra), ", ".join(
                    "`%s`" % k for k in extra))]
    gaps = text_gaps(exe, args.data, args.pack, have)
    if gaps:
        out += ["", "## Text entries", "",
                "Entries the original's tables have and the pack's lack "
                "(shown empty in the game).", ""]
        for i, missing in sorted(gaps.items()):
            out.append("- `%s`: %s" % (i, "%d missing: %s" % (
                len(missing), ", ".join(missing)) if missing else
                "complete"))
    out += ["", "## Every asset", "", "| ID | State | Pack file |",
            "|---|---|---|"]
    for i, kind, state, f in rows:
        out.append("| `%s` | %s | %s |" % (i, state,
                                          "`%s`" % f if f else ""))
    text = "\n".join(out) + "\n"
    if args.out:
        with open(args.out, "w") as fp:
            fp.write(text)
    else:
        sys.stdout.write(text)


main()
