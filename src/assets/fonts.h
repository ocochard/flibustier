/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_FONTS_H
#define FLIBUSTIER_FONTS_H

#include "data/pack.h"
#include "font.h"

/*
 * Fonts from any pack (font/ IDs): an original CHR font, or a replacement
 * font, a text file naming a TrueType or OpenType file and how it sits in
 * the original's line:
 *
 *	[font]
 *	file = PirataOne-Regular.ttf
 *	# line height in 640x480 pixels, as the original font's
 *	height = 25
 *	# optional: glyph size (default: height) and baseline below the
 *	# line top (default: the font's ascender), in 640x480 pixels
 *	size = 22.5
 *	baseline = 19
 *
 * "font/caps" holds the drop capitals, in caps_char() order; the others
 * hold characters 0x21-0xFF (Mac Roman, as the game's text).
 *
 * A replacement font gets its 640x480 metrics and 1-bit glyphs, so layout
 * and code reading the screen work as with the original, plus glyphs
 * rasterised for a canvas "canvas_h" pixels high (FB_H for none).  The
 * advances keep their fractions (FONT_SUB).
 *
 * font_from_blob() takes the blob packs_load() returned for "id".  Both
 * return 0, or -1 if the font is missing or malformed.
 */
int	font_load(struct packs *packs, const char *id, int canvas_h,
	    struct font *out);
int	font_from_blob(struct packs *packs, const char *id,
	    const struct blob *b, int canvas_h, struct font *out);

#endif
