/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_FONT_H
#define FLIBUSTIER_FONT_H

#include <stddef.h>
#include <stdint.h>

#define FONT_SUB	16	/* pen steps per pixel */

struct glyph {
	int	ink;		/* width of the drawn part */
	int	advance;	/* pen advance; may be smaller than ink */
	int	advance_sub;	/* the same in 1/FONT_SUB pixels */
};

/*
 * A glyph of a replacement font, rasterised for the canvas: coverage
 * 0..255, placed (x, y) canvas pixels from the pen and the line top.
 */
struct hd_glyph {
	int		 x, y, w, h;
	uint8_t		*cov;		/* w * h, or NULL: nothing drawn */
};

/*
 * 1-bit font.  Glyph i covers character code (first + i) for text fonts,
 * or is the i-th drop capital.  Bitmaps are one byte per pixel (0 or 1)
 * in a cell_w x height box per glyph.
 *
 * In the text fonts most glyphs start at column 8 of their 32-pixel cell
 * and script swashes overhang to the left of it, so the pen origin is
 * probably at origin_x.  This is inferred from the bitmaps, not from the
 * game code.
 */
struct font {
	int		 height;
	int		 cell_w;
	int		 origin_x;
	unsigned	 first;
	size_t		 count;
	struct glyph	*glyphs;
	uint8_t		*bitmap;	/* count * cell_w * height */
	int		 space_sub;	/* space advance; 0: height / 3 */

	/* Replacement fonts: the glyphs for a canvas hd_height pixels
	 * high, or NULL. */
	int		 hd_height;
	struct hd_glyph	*hd;		/* count */
};

/*
 * PIRATE1..4.CHR: text fonts for characters 0x21..0xAF.  CAPS.CHR:
 * decorated drop capitals.  Both return 0 on success, -1 on malformed
 * input.
 */
int	font_decode(const uint8_t *data, size_t len, struct font *out);
int	caps_decode(const uint8_t *data, size_t len, struct font *out);
void	font_free(struct font *font);

/* Pixel (x, y) of glyph i, or 0 outside the cell. */
int	font_pixel(const struct font *font, size_t i, int x, int y);

/*
 * Drop capitals: the character (Mac Roman) of capital i, or -1, and the
 * capital of character c, or -1.  A-Z, then accented capitals.
 */
#define CAPS_COUNT	33
int	caps_char(size_t i);
int	caps_index(int c);

#endif
