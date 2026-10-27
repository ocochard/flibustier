#!/usr/bin/env python3
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbe
#
"""Convert the Pieces of 8-bit "Pirates! Gold (HQ Remake)" album into the
open pack's music: OGG Vorbis files and the pack.d/music.ini fragment.

Needs ffmpeg with libvorbis.  The album is not in the repository; the
script maps its 37 tracks onto the game's tunes (see asset-packs.md).
"""
import argparse
import os
import subprocess
from concurrent.futures import ThreadPoolExecutor

ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
ap.add_argument("--album", default=os.path.expanduser(
    "~/Music/Pirates! Gold (HQ Remake)"), help="the album's directory")
ap.add_argument("--pack", default=os.path.expanduser("~/flibustier-openpack"),
                help="the pack to write into")
args = ap.parse_args()
SRC = args.album
PACK = args.pack
AUTHOR = "Pieces of 8-bit"
URL = "https://soundcloud.com/piecesof8bit"

# track number -> asset ID; the 2022 remakes first, 2015 takes and the
# extra tunes as variants (-2, -3...).
MAP = [
    (12, "music/title"), (1, "music/title-2"),
    (2, "music/treasure"), (24, "music/treasure-2"),
    (25, "music/lost-treasure"),
    (3, "music/harbour"), (4, "music/harbour-2"), (13, "music/harbour-3"),
    (14, "music/harbour-4"),
    (5, "music/barmaid"), (32, "music/barmaid-2"),
    (6, "music/trade"), (20, "music/trade-2"),
    (7, "music/shipwright"), (19, "music/shipwright-2"),
    (8, "music/news-titled"), (22, "music/news-titled-2"),
    (9, "music/news"), (21, "music/news-2"),
    (10, "music/prison"), (36, "music/prison-2"),
    (11, "music/daughter"), (34, "music/daughter-2"),
    (15, "music/tavern"), (16, "music/tavern-2"), (17, "music/tavern-3"),
    (18, "music/tavern-4"),
    (23, "music/plunder"),
    (26, "music/bank"),
    (27, "music/promotion"),
    (28, "music/rescue"), (29, "music/rescue-2"), (30, "music/rescue-3"),
    (31, "music/niece"),
    (33, "music/family"),
    (35, "music/wedding"),
    (37, "music/verdict"),
]


def source(n):
    for f in sorted(os.listdir(SRC)):
        if f.startswith("%02d - " % n):
            return f
    raise SystemExit("track %d missing" % n)


def title(f):
    return f.split(" - ", 2)[2].rsplit(".", 1)[0]


def convert(job):
    n, aid = job
    f = source(n)
    out = os.path.join(PACK, aid + ".ogg")
    if not os.path.exists(out):
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i",
                        os.path.join(SRC, f), "-vn", "-c:a", "libvorbis",
                        "-q:a", "6", "-metadata", "artist=" + AUTHOR,
                        "-metadata", "comment=" + URL, out], check=True)
    return n, aid, f


os.makedirs(os.path.join(PACK, "music"), exist_ok=True)
os.makedirs(os.path.join(PACK, "pack.d"), exist_ok=True)
with ThreadPoolExecutor(8) as ex:
    done = sorted(ex.map(convert, MAP), key=lambda r: r[1])

lines = ["# The music: \"Pirates! Gold (HQ Remake)\" by %s, %s" % (AUTHOR, URL),
         "# A fan remake of the game's music, kept with its author's credit;",
         "# converted from the album's files to OGG Vorbis (quality 6) by",
         "# tools/music_pack.py.  Each line: the tune, then the track.",
         "", "[assets]"]
for n, aid, f in done:
    lines.append("# %02d %s" % (n, title(f)))
    lines.append("%s = %s.ogg" % (aid, aid))
with open(os.path.join(PACK, "pack.d", "music.ini"), "w") as fp:
    fp.write("\n".join(lines) + "\n")
with open(os.path.join(PACK, "music", "CREDITS.txt"), "w") as fp:
    fp.write("Pirates! Gold (HQ Remake)\nby %s\n%s\n\n" % (AUTHOR, URL))
    fp.write("A fan remake of the music of Pirates! Gold (MicroProse, 1993).\n"
             "All rights belong to the author; used here with credit.\n\n")
    for n, aid, f in sorted(done):
        fp.write("%02d  %-40s %s.ogg\n" % (n, title(f), aid))
print(len(done), "tracks")
