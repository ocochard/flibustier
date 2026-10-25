/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdlib.h>
#include <string.h>

#include "font.h"

/*
 * Text fonts: 144 entries of (file size / 144) bytes.  Entry 0 is a
 * header (01 FF nn 00); entry k is character 0x20 + k.  Each glyph entry
 * is u16 ink, u16 advance, then rows of 4-byte big-endian bitmaps with
 * the most significant bit leftmost.
 */
#define TEXT_ENTRIES	144
#define TEXT_ROWBYTES	4
#define TEXT_ORIGIN_X	8

/* Drop capitals: 208-byte entries, u16 w, u16 h, 34 rows of 6 bytes. */
#define CAPS_ENTRY	208
#define CAPS_ROWBYTES	6
#define CAPS_ROWS	34

static unsigned
le16(const uint8_t *p)
{
	return ((unsigned)p[0] | (unsigned)p[1] << 8);
}

void
font_free(struct font *font)
{
	free(font->glyphs);
	free(font->bitmap);
	memset(font, 0, sizeof(*font));
}

int
font_pixel(const struct font *font, size_t i, int x, int y)
{
	if (i >= font->count || x < 0 || y < 0 || x >= font->cell_w ||
	    y >= font->height)
		return (0);
	return (font->bitmap[(i * font->height + (size_t)y) * font->cell_w +
	    (size_t)x]);
}

/* Unpack "count" entries of "esize" bytes starting at "p". */
static int
unpack(struct font *out, const uint8_t *p, size_t count, size_t esize,
    int rowbytes, int rows)
{
	size_t i;
	int r, x;
	const uint8_t *e, *row;
	uint8_t *dst;

	out->count = count;
	out->height = rows;
	out->cell_w = rowbytes * 8;
	out->glyphs = calloc(count, sizeof(*out->glyphs));
	out->bitmap = calloc(count * (size_t)rows, (size_t)out->cell_w);
	if (out->glyphs == NULL || out->bitmap == NULL) {
		font_free(out);
		return (-1);
	}
	for (i = 0; i < count; i++) {
		e = p + i * esize;
		out->glyphs[i].ink = (int)le16(e);
		out->glyphs[i].advance = (int)le16(e + 2);
		for (r = 0; r < rows; r++) {
			row = e + 4 + (size_t)r * rowbytes;
			dst = out->bitmap + (i * rows + (size_t)r) *
			    out->cell_w;
			for (x = 0; x < out->cell_w; x++)
				dst[x] = row[x / 8] >> (7 - x % 8) & 1;
		}
	}
	return (0);
}

int
font_decode(const uint8_t *data, size_t len, struct font *out)
{
	size_t esize;
	int rows;

	memset(out, 0, sizeof(*out));
	if (len == 0 || len % TEXT_ENTRIES != 0)
		return (-1);
	esize = len / TEXT_ENTRIES;
	if (esize < 4 + TEXT_ROWBYTES || (esize - 4) % TEXT_ROWBYTES != 0 ||
	    data[0] != 0x01 || data[1] != 0xFF)
		return (-1);
	rows = (int)((esize - 4) / TEXT_ROWBYTES);
	if (unpack(out, data + esize, TEXT_ENTRIES - 1, esize, TEXT_ROWBYTES,
	    rows) != 0)
		return (-1);
	out->first = 0x21;
	out->origin_x = TEXT_ORIGIN_X;
	return (0);
}

int
caps_decode(const uint8_t *data, size_t len, struct font *out)
{
	memset(out, 0, sizeof(*out));
	if (len == 0 || len % CAPS_ENTRY != 0)
		return (-1);
	if (unpack(out, data, len / CAPS_ENTRY, CAPS_ENTRY, CAPS_ROWBYTES,
	    CAPS_ROWS) != 0)
		return (-1);
	return (0);
}
