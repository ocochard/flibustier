#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
"""Write the open pack's game data: data/*.json, pack.d/data.ini and a
provenance record per table.

Every table is written anew (aidd_docs/plans/open-game-data.md, G4): a
module in tables/ builds it from its own design, and never reads the
original data.  A module defines

    TABLES = [(id, file, build), ...]

where "build()" returns (data, notes): the JSON value of the ID (its
format in aidd_docs/memory/internal/asset-packs.md, "Open game data")
and a short text saying how it was designed, kept in its provenance.
A module may instead, or also, define

    FILES = [(path, build), ...]

for files outside data/, at "path" in the pack (the tile sheets' terrain
masks beside art/map.png, for tools/artgen to name): "build()" returns
(content, notes), content bytes or text.

    opendata.py --pack DIR [--only MODULE]...
"""
import argparse
import datetime
import importlib
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "tables"))


def git_rev():
    try:
        return subprocess.run(["git", "-C", HERE, "rev-parse", "--short",
                               "HEAD"], capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def modules():
    return sorted(f[:-3] for f in os.listdir(os.path.join(HERE, "tables"))
                  if f.endswith(".py") and not f.startswith("_"))


def encode(v, indent=0):
    """JSON with arrays of plain values on one line, for review."""
    pad = " " * (indent + 2)
    if isinstance(v, dict):
        if not v:
            return "{}"
        items = ",\n".join(pad + json.dumps(k) + ": " + encode(x, indent + 2)
                           for k, x in v.items())
        return "{\n" + items + "\n" + " " * indent + "}"
    if isinstance(v, list) and any(isinstance(x, (dict, list)) for x in v):
        items = ",\n".join(pad + encode(x, indent + 2) for x in v)
        return "[\n" + items + "\n" + " " * indent + "]"
    return json.dumps(v, ensure_ascii=False)


def dump(value):
    return encode(value) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--pack", required=True, help="the pack's directory")
    ap.add_argument("--only", action="append", default=[],
                    help="build only this module (repeatable)")
    args = ap.parse_args()
    names = args.only or modules()
    for d in ("data", "pack.d", os.path.join("provenance", "data")):
        os.makedirs(os.path.join(args.pack, d), exist_ok=True)
    rev = git_rev()
    date = datetime.date.today().isoformat()
    lines = {}
    for name in names:
        mod = importlib.import_module(name)
        for tid, file, build in getattr(mod, "TABLES", []):
            data, notes = build()
            path = os.path.join("data", file)
            with open(os.path.join(args.pack, path), "w",
                      encoding="utf-8") as f:
                f.write(dump(data))
            prov = {"id": tid, "file": path, "generator": rev,
                    "module": "tools/opendata/tables/%s.py" % name,
                    "date": date, "design": notes}
            with open(os.path.join(args.pack, "provenance", "data",
                                   file), "w", encoding="utf-8") as f:
                f.write(json.dumps(prov, indent=2, ensure_ascii=False)
                        + "\n")
            lines[tid] = path
            print("%s = %s" % (tid, path))
        for path, build in getattr(mod, "FILES", []):
            content, notes = build()
            full = os.path.join(args.pack, path)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, "wb") as f:
                f.write(content.encode("utf-8") if isinstance(content, str)
                        else content)
            prov = {"file": path, "generator": rev,
                    "module": "tools/opendata/tables/%s.py" % name,
                    "date": date, "design": notes}
            with open(os.path.join(args.pack, "provenance", "data",
                                   os.path.basename(path) + ".json"), "w",
                      encoding="utf-8") as f:
                f.write(json.dumps(prov, indent=2, ensure_ascii=False)
                        + "\n")
            print(path)
    # The fragment lists every table present, from this run or before.
    ini = os.path.join(args.pack, "pack.d", "data.ini")
    if os.path.exists(ini):
        with open(ini, encoding="utf-8") as f:
            for line in f:
                if "=" in line and not line.lstrip().startswith("#"):
                    k, v = (x.strip() for x in line.split("=", 1))
                    lines.setdefault(k, v)
    with open(ini, "w", encoding="utf-8") as f:
        f.write("# Game data written for this pack by tools/opendata in "
                "the flibustier\n# repository; see provenance/data/.\n\n"
                "[assets]\n")
        for k in sorted(lines):
            f.write("%s = %s\n" % (k, lines[k]))


if __name__ == "__main__":
    main()
