#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbé
#
# Analyse PIRATESG.EXE with Ghidra headless: carve the flat 32-bit X-32
# image, import it as raw i386 at base 0, then decompile every function.
# Output goes to <outdir>, which must stay outside the repository: the
# decompiled code is derived from the original program.
#
# usage: analyze.sh PIRATESG.EXE <outdir> [lo hi]
#   lo, hi: optional virtual address range for refs.txt (default: the
#           era town blocks of the GOG build)

set -eu

GHIDRA=${GHIDRA:-/usr/local/share/ghidra}
IMAGE_OFFSET=$((0x47a7))	# GOG build; see src/data/exe.c

[ $# -ge 2 ] || { echo "usage: $0 PIRATESG.EXE outdir [lo hi]" >&2; exit 1; }
exe=$1
out=$2
lo=${3:-0x38D00}
hi=${4:-0x3E900}
here=$(cd "$(dirname "$0")" && pwd)

mkdir -p "$out/proj"
tail -c +$((IMAGE_OFFSET + 1)) "$exe" > "$out/pirates_flat.bin"
"$GHIDRA/support/analyzeHeadless" "$out/proj" pirates \
    -import "$out/pirates_flat.bin" -overwrite \
    -loader BinaryLoader -loader-baseAddr 0x0 \
    -processor x86:LE:32:default -cspec gcc \
    -scriptPath "$here" -postScript DumpAll.java "$out" "$lo" "$hi" \
    > "$out/headless.log" 2>&1
grep "functions:" "$out/headless.log"
