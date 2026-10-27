#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbe
#
# Play each original sound effect, then the pack's replacement, one
# after the other.
#
#   compare.sh ORIGINALS PACK [NAME ...]
#
# ORIGINALS is a directory written by "flibustier --export-sounds DIR";
# PACK is the pack (~/flibustier-openpack); NAMEs are the sounds to hear
# (broadside, swing...), all of them when none is given.  Plays with the
# first of mpv, sox's play or ffplay found.

set -eu
if [ $# -lt 2 ]; then
	echo "usage: compare.sh ORIGINALS PACK [NAME ...]" >&2
	exit 2
fi
orig=$1
pack=$2
shift 2
if [ $# -eq 0 ]; then
	set -- $(cd "$orig" && ls *.wav | sed 's/\.wav$//')
fi
if command -v mpv >/dev/null; then
	play() { mpv --really-quiet --no-video "$1"; }
elif command -v play >/dev/null; then
	play() { command play -q "$1"; }
elif command -v ffplay >/dev/null; then
	play() { ffplay -nodisp -autoexit -loglevel quiet "$1"; }
else
	echo "compare.sh: needs mpv, sox's play or ffplay" >&2
	exit 1
fi
for name in "$@"; do
	new=$(ls "$pack/sound/$name".* 2>/dev/null | head -1 || true)
	printf '%-12s original' "$name"
	play "$orig/$name.wav"
	if [ -n "$new" ]; then
		printf ', new\n'
		sleep 0.4
		play "$new"
	else
		printf ' (no replacement: the game keeps it)\n'
	fi
	sleep 0.8
done
