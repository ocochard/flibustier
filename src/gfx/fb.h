/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_FB_H
#define STMALO_FB_H

#include <stdint.h>

#include "assets/image.h"
#include "assets/sprite.h"

#define FB_W	640
#define FB_H	480

/*
 * The game screen: 640x480 palette indices plus the current palette, as
 * on the original's 256-colour SVGA mode.  Everything is drawn on the
 * CPU; fb_to_rgb() produces the final colours, so the same frame can go
 * to a window or a PNG, and palette effects (fades, cycling) are just
 * palette changes.
 */
struct fb {
	uint8_t	px[FB_W * FB_H];
	uint8_t	pal[256 * 3];	/* 8 bits per channel */
};

void	fb_clear(struct fb *fb, uint8_t index);
void	fb_rect(struct fb *fb, int x, int y, int w, int h, uint8_t index);

/* Draw a picture at (x, y), clipped; optionally take its palette. */
void	fb_image(struct fb *fb, const struct image *img, int x, int y,
	    int use_palette);

/*
 * Draw the (sx, sy, w, h) part of a picture at (dx, dy), clipped; pixels
 * of colour "key" are skipped unless it is negative.
 */
void	fb_image_part(struct fb *fb, const struct image *img, int sx, int sy,
	    int w, int h, int dx, int dy, int key);

/* Draw a sprite frame at (x, y); transparent pixels are skipped. */
void	fb_frame(struct fb *fb, const struct frame *f, int x, int y);

/* The same, clipped to the rectangle (x0, y0)-(x1, y1), inclusive. */
void	fb_frame_clip(struct fb *fb, const struct frame *f, int x, int y,
	    int x0, int y0, int x1, int y1);

/* Palette index whose colour is closest to (r, g, b). */
uint8_t	fb_nearest(const struct fb *fb, int r, int g, int b);

/*
 * Convert to RGB (FB_W * FB_H * 3 bytes) with brightness 0..256, where
 * 256 is full colour: a fade is just a brightness ramp.
 */
void	fb_to_rgb(const struct fb *fb, int brightness, uint8_t *rgb);

#endif
