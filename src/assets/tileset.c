/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdlib.h>
#include <string.h>

#include "tileset.h"

#define PALETTE_OFF	0x09	/* matches the pictures' palettes: sbat.pcx */
#define TILES_OFF	0x318
#define RECORD_HDR	10	/* u16 size, w, h, w - 1, h - 1 */
#define RECORD_PAD	2
#define MAX_TILE	256

static unsigned
le16(const uint8_t *p)
{
	return ((unsigned)p[0] | (unsigned)p[1] << 8);
}

void
tileset_free(struct tileset *ts)
{
	free(ts->pixels);
	free(ts->rgba);
	free(ts->terrain);
	free(ts->ground);
	memset(ts, 0, sizeof(*ts));
}

int
tileset_decode(const uint8_t *data, size_t len, struct tileset *out)
{
	size_t i, n, off, tsize;
	unsigned w, h, v;
	int nonzero;

	memset(out, 0, sizeof(*out));
	if (len < TILES_OFF + RECORD_HDR || le16(data) != 3)
		return (-1);
	n = le16(data + 6);
	w = le16(data + TILES_OFF + 2);
	h = le16(data + TILES_OFF + 4);
	if (n == 0 || w == 0 || h == 0 || w > MAX_TILE || h > MAX_TILE)
		return (-1);
	tsize = (size_t)w * h;
	if ((out->pixels = malloc(n * tsize)) == NULL)
		return (-1);
	for (off = TILES_OFF, i = 0; i < n; i++) {
		if (off + RECORD_HDR + tsize + RECORD_PAD > len ||
		    le16(data + off) != ((tsize + 6) & 0xFFFF) ||
		    le16(data + off + 2) != w || le16(data + off + 4) != h ||
		    le16(data + off + 6) != w - 1 ||
		    le16(data + off + 8) != h - 1) {
			tileset_free(out);
			return (-1);
		}
		memcpy(out->pixels + i * tsize, data + off + RECORD_HDR, tsize);
		off += RECORD_HDR + tsize + RECORD_PAD;
	}
	/* Expand the 6-bit palette; an all-zero one means "none". */
	for (nonzero = 0, i = 0; i < sizeof(out->palette); i++) {
		v = data[PALETTE_OFF + i] & 0x3F;
		out->palette[i] = (uint8_t)(v << 2 | v >> 4);
		nonzero |= v != 0;
	}
	out->has_palette = nonzero;
	out->tile_w = (int)w;
	out->tile_h = (int)h;
	out->count = n;
	return (0);
}
