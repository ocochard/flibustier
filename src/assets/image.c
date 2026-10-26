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

/* A 6x6x6 colour cube, then a ramp of 40 greys. */
void
image_fixed_palette(struct image *img)
{
	int i, v;

	for (i = 0; i < CUBE * CUBE * CUBE; i++) {
		img->palette[3 * i] = (uint8_t)(i / (CUBE * CUBE) * 51);
		img->palette[3 * i + 1] = (uint8_t)(i / CUBE % CUBE * 51);
		img->palette[3 * i + 2] = (uint8_t)(i % CUBE * 51);
	}
	for (; i < 256; i++) {
		v = (i - CUBE * CUBE * CUBE + 1) * 255 / 41;
		memset(&img->palette[3 * i], v, 3);
	}
	img->has_palette = 1;
}

static uint8_t
nearest(const uint8_t *pal, int r, int g, int b)
{
	int i, best = 0, dr, dg, db;
	long d, bestd = -1;

	for (i = 0; i < 256; i++) {
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
image_quantize(struct image *img)
{
	uint16_t *cache;
	const uint8_t *p;
	unsigned sum[3], key, n, shift = 8 - CACHE_BITS;
	int x, y, ax, ay, ax0, ax1, ay0, ay1, k;
	uint8_t *px;

	if (img->rgba == NULL || img->w < 1 || img->h < 1)
		return (-1);
	if ((px = malloc((size_t)img->w * img->h)) == NULL)
		return (-1);
	if ((cache = malloc(sizeof(*cache) << (3 * CACHE_BITS))) == NULL) {
		free(px);
		return (-1);
	}
	memset(cache, 0xFF, sizeof(*cache) << (3 * CACHE_BITS));
	for (y = 0; y < img->h; y++)
		for (x = 0; x < img->w; x++) {
			span_of(x, img->w, img->rgba_w, &ax0, &ax1);
			span_of(y, img->h, img->rgba_h, &ay0, &ay1);
			sum[0] = sum[1] = sum[2] = 0;
			for (ay = ay0; ay < ay1; ay++)
				for (ax = ax0; ax < ax1; ax++) {
					p = img->rgba + 4 * ((size_t)ay *
					    img->rgba_w + ax);
					for (k = 0; k < 3; k++)
						sum[k] += p[k];
				}
			n = (unsigned)((ax1 - ax0) * (ay1 - ay0));
			for (k = 0; k < 3; k++)
				sum[k] /= n;
			key = (sum[0] >> shift) << (2 * CACHE_BITS) |
			    (sum[1] >> shift) << CACHE_BITS | sum[2] >> shift;
			/* The bucket's own colour, spread over 0..255, so the
			 * result does not depend on which colour filled it
			 * first and black and white stay exact. */
			if (cache[key] == CACHE_NONE)
				cache[key] = nearest(img->palette,
				    level(sum[0] >> shift),
				    level(sum[1] >> shift),
				    level(sum[2] >> shift));
			px[(size_t)y * img->w + x] = (uint8_t)cache[key];
		}
	free(cache);
	free(img->pixels);
	img->pixels = px;
	return (0);
}
