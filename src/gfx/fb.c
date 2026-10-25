/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <string.h>

#include "fb.h"

void
fb_clear(struct fb *fb, uint8_t index)
{
	memset(fb->px, index, sizeof(fb->px));
}

void
fb_rect(struct fb *fb, int x, int y, int w, int h, uint8_t index)
{
	int yy;

	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > FB_W)
		w = FB_W - x;
	if (y + h > FB_H)
		h = FB_H - y;
	if (w <= 0 || h <= 0)
		return;
	for (yy = y; yy < y + h; yy++)
		memset(fb->px + (size_t)yy * FB_W + x, index, (size_t)w);
}

void
fb_image(struct fb *fb, const struct image *img, int x, int y,
    int use_palette)
{
	int sy, x0, x1;

	if (use_palette && img->has_palette)
		memcpy(fb->pal, img->palette, sizeof(fb->pal));
	x0 = x < 0 ? -x : 0;
	x1 = x + img->w > FB_W ? FB_W - x : img->w;
	if (x1 <= x0)
		return;
	for (sy = 0; sy < img->h; sy++) {
		if (y + sy < 0 || y + sy >= FB_H)
			continue;
		memcpy(fb->px + (size_t)(y + sy) * FB_W + x + x0,
		    img->pixels + (size_t)sy * img->w + x0, (size_t)(x1 - x0));
	}
}

void
fb_frame_clip(struct fb *fb, const struct frame *f, int x, int y, int x0,
    int y0, int x1, int y1)
{
	int fx, fy, px, py;
	size_t i;

	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > FB_W - 1)
		x1 = FB_W - 1;
	if (y1 > FB_H - 1)
		y1 = FB_H - 1;
	for (fy = 0; fy < f->h; fy++) {
		py = y + fy;
		if (py < y0 || py > y1)
			continue;
		for (fx = 0; fx < f->w; fx++) {
			px = x + fx;
			i = (size_t)fy * f->w + fx;
			if (px < x0 || px > x1 ||
			    (f->mask != NULL && !f->mask[i]))
				continue;
			fb->px[(size_t)py * FB_W + px] = f->pixels[i];
		}
	}
}

void
fb_frame(struct fb *fb, const struct frame *f, int x, int y)
{
	fb_frame_clip(fb, f, x, y, 0, 0, FB_W - 1, FB_H - 1);
}

uint8_t
fb_nearest(const struct fb *fb, int r, int g, int b)
{
	int i, best = 0, dr, dg, db;
	long d, bestd = -1;

	for (i = 0; i < 256; i++) {
		dr = fb->pal[3 * i] - r;
		dg = fb->pal[3 * i + 1] - g;
		db = fb->pal[3 * i + 2] - b;
		d = (long)dr * dr + (long)dg * dg + (long)db * db;
		if (bestd < 0 || d < bestd) {
			bestd = d;
			best = i;
		}
	}
	return ((uint8_t)best);
}

void
fb_to_rgb(const struct fb *fb, int brightness, uint8_t *rgb)
{
	uint8_t lut[256 * 3];
	size_t i;

	if (brightness < 0)
		brightness = 0;
	if (brightness > 256)
		brightness = 256;
	for (i = 0; i < sizeof(lut); i++)
		lut[i] = (uint8_t)(fb->pal[i] * brightness / 256);
	for (i = 0; i < (size_t)FB_W * FB_H; i++)
		memcpy(rgb + 3 * i, lut + 3 * fb->px[i], 3);
}
