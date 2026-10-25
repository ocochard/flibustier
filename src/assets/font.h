/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_FONT_H
#define STMALO_FONT_H

#include <stddef.h>
#include <stdint.h>

struct glyph {
	int	ink;		/* width of the drawn part */
	int	advance;	/* pen advance; may be smaller than ink */
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

#endif
