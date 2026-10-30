/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdlib.h>
#include <string.h>

#include "image.h"

#define CUBE		6	/* levels per channel of the fixed palette */
#define CACHE_BITS	5	/* per channel, for nearest-colour lookups */
#define CACHE_NONE	0xFFFF

void
image_free(struct image *img)
{
	free(img->pixels);
	free(img->rgba);
	memset(img, 0, sizeof(*img));
}

void
image_drop_rgba(struct image *img)
{
	free(img->rgba);
	img->rgba = NULL;
	img->rgba_w = img->rgba_h = 0;
}

void
palette_fixed(uint8_t *pal)
{
	int i, v;

	for (i = 0; i < CUBE * CUBE * CUBE; i++) {
		pal[3 * i] = (uint8_t)(i / (CUBE * CUBE) * 51);
		pal[3 * i + 1] = (uint8_t)(i / CUBE % CUBE * 51);
		pal[3 * i + 2] = (uint8_t)(i % CUBE * 51);
	}
	for (; i < 256; i++) {
		v = (i - CUBE * CUBE * CUBE + 1) * 255 / 41;
		memset(&pal[3 * i], v, 3);
	}
}

void
image_fixed_palette(struct image *img)
{
	palette_fixed(img->palette);
	img->has_palette = 1;
}

int
image_has_fixed_palette(const struct image *img)
{
	uint8_t pal[256 * 3];

	palette_fixed(pal);
	return (img->has_palette && memcmp(img->palette, pal,
	    sizeof(pal)) == 0);
}

/* The entry of "pal" from "first" on nearest to (r, g, b). */
static uint8_t
nearest(const uint8_t *pal, int first, int r, int g, int b)
{
	int i, best = first, dr, dg, db;
	long d, bestd = -1;

	for (i = first; i < 256; i++) {
		dr = pal[3 * i] - r;
		dg = pal[3 * i + 1] - g;
		db = pal[3 * i + 2] - b;
		d = (long)dr * dr + (long)dg * dg + (long)db * db;
		if (bestd < 0 || d < bestd) {
			bestd = d;
			best = i;
		}
	}
	return ((uint8_t)best);
}

/* The art pixels [*a0, *a1) under indexed pixel "i" of "n", never empty. */
static void
span_of(int i, int n, int art, int *a0, int *a1)
{
	*a0 = (int)((long)i * art / n);
	*a1 = (int)((long)(i + 1) * art / n);
	if (*a1 <= *a0)
		*a1 = *a0 + 1;
	if (*a1 > art) {
		*a1 = art;
		*a0 = art - 1;
	}
}

static int
level(unsigned bucket)
{
	return ((int)(bucket * 255 / ((1u << CACHE_BITS) - 1)));
}

int
rgba_reduce(const uint8_t *rgba, int aw, int ah, int w, int h,
    const uint8_t *pal, uint8_t *px, uint8_t *mask)
{
	uint16_t *cache;
	const uint8_t *p;
	unsigned sum[4], key, n, shift = 8 - CACHE_BITS;
	int x, y, ax, ay, ax0, ax1, ay0, ay1, k, first = mask != NULL;
	size_t i;

	if ((cache = malloc(sizeof(*cache) << (3 * CACHE_BITS))) == NULL)
		return (-1);
	memset(cache, 0xFF, sizeof(*cache) << (3 * CACHE_BITS));
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			i = (size_t)y * w + x;
			span_of(x, w, aw, &ax0, &ax1);
			span_of(y, h, ah, &ay0, &ay1);
			sum[0] = sum[1] = sum[2] = sum[3] = 0;
			for (ay = ay0; ay < ay1; ay++)
				for (ax = ax0; ax < ax1; ax++) {
					p = rgba + 4 * ((size_t)ay * aw + ax);
					for (k = 0; k < 4; k++)
						sum[k] += p[k];
				}
			n = (unsigned)((ax1 - ax0) * (ay1 - ay0));
			for (k = 0; k < 4; k++)
				sum[k] /= n;
			if (mask != NULL && !(mask[i] = sum[3] >= 128)) {
				px[i] = 0;
				continue;
			}
			key = (sum[0] >> shift) << (2 * CACHE_BITS) |
			    (sum[1] >> shift) << CACHE_BITS | sum[2] >> shift;
			/* The bucket's own colour, spread over 0..255, so the
			 * result does not depend on which colour filled it
			 * first and black and white stay exact. */
			if (cache[key] == CACHE_NONE)
				cache[key] = nearest(pal, first,
				    level(sum[0] >> shift),
				    level(sum[1] >> shift),
				    level(sum[2] >> shift));
			px[i] = (uint8_t)cache[key];
		}
	free(cache);
	return (0);
}

int
image_quantize(struct image *img)
{
	uint8_t *px;

	if (img->rgba == NULL || img->w < 1 || img->h < 1)
		return (-1);
	if ((px = malloc((size_t)img->w * img->h)) == NULL)
		return (-1);
	if (rgba_reduce(img->rgba, img->rgba_w, img->rgba_h, img->w, img->h,
	    img->palette, px, NULL) != 0) {
		free(px);
		return (-1);
	}
	free(img->pixels);
	img->pixels = px;
	return (0);
}
