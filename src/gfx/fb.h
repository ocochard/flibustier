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

#define FB_MIN_H	240	/* output heights the canvas accepts */
#define FB_MAX_H	4320

/*
 * The game screen: 640x480 palette indices plus the current palette, as
 * on the original's 256-colour SVGA mode, and the same screen at the
 * output resolution (a 4:3 canvas, 1440x1080 for 1080p).
 *
 * Game code draws in 640x480 coordinates.  Every drawing call updates
 * both: original assets are enlarged onto the canvas as palette indices,
 * so palette effects (fades, cycling) still apply to them; replacement
 * art with true colours is sampled at the canvas resolution.  Code that
 * reads the screen reads the 640x480 indices.  fb_to_rgb() produces the
 * final colours, so the same frame can go to a window or a PNG.
 */
struct fb {
	uint8_t	 px[FB_W * FB_H];
	uint8_t	 pal[256 * 3];	/* 8 bits per channel */

	int	 out_w, out_h;	/* canvas size */
	uint8_t	*idx;		/* out_w * out_h palette indices */
	uint8_t	*rgb;		/* out_w * out_h * 3 true colours */
	uint8_t	*tc;		/* out_w * out_h: 1 where rgb shows */
	int	*x0, *y0;	/* canvas start of each 640x480 column, row */
	int	*lx, *ly;	/* 640x480 column, row of each canvas one */
};

/*
 * Set up the canvas "height" pixels high, rounded down to a multiple of 3
 * for a whole 4:3 width.  Returns 0, or -1 for a height out of
 * FB_MIN_H..FB_MAX_H or without memory.
 */
int	fb_init(struct fb *fb, int height);
void	fb_free(struct fb *fb);

void	fb_put_scaled(struct fb *fb, size_t i, uint8_t c);

/* Set pixel "i" (y * FB_W + x) to palette index "c". */
static inline void
fb_put(struct fb *fb, size_t i, uint8_t c)
{
	fb->px[i] = c;
	if (fb->out_h == FB_H) {	/* the canvas is the screen */
		fb->idx[i] = c;
		fb->tc[i] = 0;
	} else
		fb_put_scaled(fb, i, c);
}

void	fb_clear(struct fb *fb, uint8_t index);
void	fb_rect(struct fb *fb, int x, int y, int w, int h, uint8_t index);

/*
 * Draw a picture at (x, y), clipped; optionally take its palette.  True
 * colours, when the picture has them, go to the canvas.
 */
void	fb_image(struct fb *fb, const struct image *img, int x, int y,
	    int use_palette);

/*
 * Draw the (sx, sy, w, h) part of a picture at (dx, dy), clipped; pixels
 * of colour "key" are skipped unless it is negative.  This draws the
 * palette indices only.
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
 * Convert the canvas to RGB (out_w * out_h * 3 bytes) with brightness
 * 0..256, where 256 is full colour: a fade is just a brightness ramp.
 */
void	fb_to_rgb(const struct fb *fb, int brightness, uint8_t *rgb);

#endif
