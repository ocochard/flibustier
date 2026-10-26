/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_SHEET_H
#define STMALO_SHEET_H

#include "data/pack.h"
#include "sprite.h"

/*
 * Sprite banks from any pack: an original bank (SPR, CRS, CUR, BMP, GRP)
 * or a replacement sprite sheet, a text file naming a PNG and the
 * rectangle of each frame in it:
 *
 *	[sheet]
 *	image = tavern.png
 *
 *	[frames]
 *	# frame = x y w h, in art pixels
 *	0 = 0 0 300 200
 *	1 = 300 0 120 80
 *
 * Fencing groups (grp/) number their frames "bank.frame": "3.0".  Frame
 * numbers start at 0 with no gaps.  A frame covers its art size times
 * 480 / art_height of its pack in 640x480 pixels, rounded, so game code
 * places it as the original.
 *
 * Sheet frames keep their true colours and also get palette indices and
 * a mask, for code that works on those: the original frame's when it is
 * installed with the same size, else the art reduced to the fixed
 * palette, opaque where alpha is at least half.
 *
 * The *_from_blob() variants take the blob packs_load() returned for
 * "id".  All return 0, or -1 if the bank is missing or malformed.
 */
enum bank_kind {
	BANK_RUNS,	/* run-coded: SPR, CRS */
	BANK_RAW	/* raw: CUR, battle tile BMP */
};

int	bank_load(struct packs *packs, const char *id, enum bank_kind kind,
	    struct sprite_bank *out);
int	bank_from_blob(struct packs *packs, const char *id,
	    const struct blob *b, enum bank_kind kind, struct sprite_bank *out);
int	group_load(struct packs *packs, const char *id,
	    struct sprite_group *out);
int	group_from_blob(struct packs *packs, const char *id,
	    const struct blob *b, struct sprite_group *out);

#endif
