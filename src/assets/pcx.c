/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdlib.h>
#include <string.h>

#include "pcx.h"

#define HEADER_LEN	128
#define PALETTE_LEN	(1 + 256 * 3)	/* 0x0C marker, then RGB triplets */
#define MAX_DIM		4096

static unsigned
le16(const uint8_t *p)
{
	return ((unsigned)p[0] | (unsigned)p[1] << 8);
}

int
pcx_decode(const uint8_t *data, size_t len, struct image *out)
{
	unsigned w, h, bpl, x, y, run;
	size_t i, end;
	uint8_t b, *row;

	memset(out, 0, sizeof(*out));
	if (len < HEADER_LEN + PALETTE_LEN)
		return (-1);
	/* Manufacturer 10, RLE encoding, 8 bits per pixel, one plane. */
	if (data[0] != 10 || data[2] != 1 || data[3] != 8 || data[65] != 1)
		return (-1);
	w = le16(data + 8) - le16(data + 4) + 1;
	h = le16(data + 10) - le16(data + 6) + 1;
	bpl = le16(data + 66);
	if (w == 0 || h == 0 || w > MAX_DIM || h > MAX_DIM || bpl < w)
		return (-1);
	end = len - PALETTE_LEN;
	if (data[end] != 0x0C)
		return (-1);
	if ((row = malloc(bpl)) == NULL)
		return (-1);
	if ((out->pixels = malloc((size_t)w * h)) == NULL) {
		free(row);
		return (-1);
	}
	/* Runs may cross line boundaries, so keep the run across rows. */
	i = HEADER_LEN;
	run = 0;
	b = 0;
	for (y = 0; y < h; y++) {
		for (x = 0; x < bpl; x++) {
			if (run == 0) {
				if (i >= end)
					goto fail;
				b = data[i++];
				run = 1;
				if ((b & 0xC0) == 0xC0) {
					run = b & 0x3F;
					if (run == 0 || i >= end)
						goto fail;
					b = data[i++];
				}
			}
			row[x] = b;
			run--;
		}
		memcpy(out->pixels + (size_t)y * w, row, w);
	}
	free(row);
	out->w = (int)w;
	out->h = (int)h;
	memcpy(out->palette, data + end + 1, sizeof(out->palette));
	out->has_palette = 1;
	return (0);
fail:
	free(row);
	image_free(out);
	return (-1);
}
