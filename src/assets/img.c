/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdlib.h>
#include <string.h>

#include "img.h"

#define MAX_DIM	1024

static unsigned
le16(const uint8_t *p)
{
	return ((unsigned)p[0] | (unsigned)p[1] << 8);
}

/*
 * RLE control byte b: with bit 7 set, (b & 0x7F) literal bytes follow;
 * otherwise the next byte is repeated b times.  Runs are meant to stay
 * within a row, but WPNSEL.IMG starts with a zero-length run and has two
 * runs overflowing their row by one or two pixels.  Zero runs are skipped
 * and the overflow is dropped, so the next row starts aligned.  Carrying
 * the overflow into the next row instead shifts the picture by up to two
 * pixels; the two readings cannot be told apart from the data alone.
 */
static int
unpack_rows(const uint8_t *p, size_t len, uint8_t *px, unsigned w,
    unsigned h)
{
	size_t i;
	unsigned y, x, n, fit;
	uint8_t b;

	i = 0;
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x += fit) {
			if (i >= len)
				return (-1);
			b = p[i++];
			n = b & 0x7F;
			fit = x + n > w ? w - x : n;
			if (b & 0x80) {
				if (i + n > len)
					return (-1);
				memcpy(px + (size_t)y * w + x, p + i, fit);
				i += n;
			} else {
				if (i >= len)
					return (-1);
				memset(px + (size_t)y * w + x, p[i++], fit);
			}
		}
	}
	return (0);
}

int
img_decode(const uint8_t *data, size_t len, struct image *out)
{
	unsigned w, h, mode;

	memset(out, 0, sizeof(*out));
	if (len < 6)
		return (-1);
	w = le16(data);
	h = le16(data + 2);
	mode = le16(data + 4);
	if (w == 0 || h == 0 || w > MAX_DIM || h > MAX_DIM)
		return (-1);
	if ((out->pixels = malloc((size_t)w * h)) == NULL)
		return (-1);
	if (mode == 0) {
		if (6 + (size_t)w * h > len)
			goto fail;
		memcpy(out->pixels, data + 6, (size_t)w * h);
	} else if (unpack_rows(data + 6, len - 6, out->pixels, w, h) != 0)
		goto fail;
	out->w = (int)w;
	out->h = (int)h;
	return (0);
fail:
	image_free(out);
	return (-1);
}
