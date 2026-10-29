/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdlib.h>
#include <string.h>

#include "fb.h"

int
fb_init(struct fb *fb, int height)
{
	int w, h, i, o;

	fb_free(fb);
	h = height - height % 3;
	if (h < FB_MIN_H || h > FB_MAX_H)
		return (-1);
	w = h / 3 * 4;
	fb->idx = calloc((size_t)w * h, 1);
	fb->rgb = calloc((size_t)w * h, 3);
	fb->tc = calloc((size_t)w * h, 1);
	fb->x0 = malloc((FB_W + 1) * sizeof(*fb->x0));
	fb->y0 = malloc((FB_H + 1) * sizeof(*fb->y0));
	fb->lx = malloc((size_t)w * sizeof(*fb->lx));
	fb->ly = malloc((size_t)h * sizeof(*fb->ly));
	if (fb->idx == NULL || fb->rgb == NULL || fb->tc == NULL ||
	    fb->x0 == NULL || fb->y0 == NULL || fb->lx == NULL ||
	    fb->ly == NULL) {
		fb_free(fb);
		return (-1);
	}
	fb->out_w = w;
	fb->out_h = h;
	for (i = 0; i <= FB_W; i++)
		fb->x0[i] = i * w / FB_W;
	for (i = 0; i <= FB_H; i++)
		fb->y0[i] = i * h / FB_H;
	for (i = 0; i < FB_W; i++)
		for (o = fb->x0[i]; o < fb->x0[i + 1]; o++)
			fb->lx[o] = i;
	for (i = 0; i < FB_H; i++)
		for (o = fb->y0[i]; o < fb->y0[i + 1]; o++)
			fb->ly[o] = i;
	return (0);
}

void
fb_free(struct fb *fb)
{
	free(fb->idx);
	free(fb->rgb);
	free(fb->tc);
	free(fb->x0);
	free(fb->y0);
	free(fb->lx);
	free(fb->ly);
	fb->idx = fb->rgb = fb->tc = NULL;
	fb->x0 = fb->y0 = fb->lx = fb->ly = NULL;
	fb->out_w = fb->out_h = 0;
}

/* Fill the canvas under 640x480 columns x0..x1-1 of row y with "c". */
static void
span(struct fb *fb, int x0, int x1, int y, uint8_t c)
{
	int oy;
	size_t o, n = (size_t)(fb->x0[x1] - fb->x0[x0]);

	for (oy = fb->y0[y]; oy < fb->y0[y + 1]; oy++) {
		o = (size_t)oy * fb->out_w + fb->x0[x0];
		memset(fb->idx + o, c, n);
		memset(fb->tc + o, 0, n);
	}
}

/* fb_put() on a canvas larger than the screen. */
void
fb_put_scaled(struct fb *fb, size_t i, uint8_t c)
{
	int x = (int)(i % FB_W);

	span(fb, x, x + 1, (int)(i / FB_W), c);
}

void
fb_clear(struct fb *fb, uint8_t index)
{
	size_t n = (size_t)fb->out_w * fb->out_h;

	memset(fb->px, index, sizeof(fb->px));
	memset(fb->idx, index, n);
	memset(fb->tc, 0, n);
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
	for (yy = y; yy < y + h; yy++) {
		memset(fb->px + (size_t)yy * FB_W + x, index, (size_t)w);
		span(fb, x, x + w, yy, index);
	}
}

/* Copy "n" palette indices to canvas offset "o". */
static void
span_copy(struct fb *fb, size_t o, const uint8_t *src, size_t n)
{
	memcpy(fb->idx + o, src, n);
	memset(fb->tc + o, 0, n);
}

/* Canvas coordinate of 640x480 coordinate "v", also off screen. */
static int
canvas_at(int v, int out, int in)
{
	long p = (long)v * out;

	return ((int)(p >= 0 ? p / in : -((-p + in - 1) / in)));
}

/* Sample the true colours of "img" drawn at (x, y) onto the canvas. */
static void
image_rgba(struct fb *fb, const struct image *img, int x, int y)
{
	int cx0 = canvas_at(x, fb->out_w, FB_W);
	int cx1 = canvas_at(x + img->w, fb->out_w, FB_W);
	int cy0 = canvas_at(y, fb->out_h, FB_H);
	int cy1 = canvas_at(y + img->h, fb->out_h, FB_H);
	int ox, oy, ax, ay, ox0, ox1;
	const uint8_t *row, *a;
	uint8_t *d;

	ox0 = cx0 < 0 ? 0 : cx0;
	ox1 = cx1 > fb->out_w ? fb->out_w : cx1;
	if (ox1 <= ox0 || cy1 <= cy0)
		return;
	for (oy = cy0 < 0 ? 0 : cy0; oy < cy1 && oy < fb->out_h; oy++) {
		ay = (int)((long)(oy - cy0) * img->rgba_h / (cy1 - cy0));
		row = img->rgba + (size_t)ay * (img->rgba_stride > 0 ?
		    img->rgba_stride : img->rgba_w) * 4;
		d = fb->rgb + ((size_t)oy * fb->out_w + ox0) * 3;
		for (ox = ox0; ox < ox1; ox++, d += 3) {
			ax = (int)((long)(ox - cx0) * img->rgba_w / (cx1 - cx0));
			a = row + (size_t)ax * 4;
			d[0] = a[0];
			d[1] = a[1];
			d[2] = a[2];
		}
		memset(fb->tc + (size_t)oy * fb->out_w + ox0, 1,
		    (size_t)(ox1 - ox0));
	}
}

void
fb_image(struct fb *fb, const struct image *img, int x, int y,
    int use_palette)
{
	int sy, x0, x1, ox, oy;
	const uint8_t *src;
	uint8_t *d;

	if (use_palette && img->has_palette)
		memcpy(fb->pal, img->palette, sizeof(fb->pal));
	x0 = x < 0 ? -x : 0;
	x1 = x + img->w > FB_W ? FB_W - x : img->w;
	if (x1 <= x0)
		return;
	for (sy = 0; sy < img->h; sy++) {
		if (y + sy < 0 || y + sy >= FB_H)
			continue;
		src = img->pixels + (size_t)sy * img->w;
		memcpy(fb->px + (size_t)(y + sy) * FB_W + x + x0, src + x0,
		    (size_t)(x1 - x0));
		if (img->rgba != NULL)
			continue;
		if (fb->out_h == FB_H) {
			span_copy(fb, (size_t)(y + sy) * FB_W + x + x0,
			    src + x0, (size_t)(x1 - x0));
			continue;
		}
		for (oy = fb->y0[y + sy]; oy < fb->y0[y + sy + 1]; oy++) {
			d = fb->idx + (size_t)oy * fb->out_w;
			for (ox = fb->x0[x + x0]; ox < fb->x0[x + x1]; ox++)
				d[ox] = src[fb->lx[ox] - x];
			memset(fb->tc + (size_t)oy * fb->out_w +
			    fb->x0[x + x0], 0,
			    (size_t)(fb->x0[x + x1] - fb->x0[x + x0]));
		}
	}
	if (img->rgba != NULL)
		image_rgba(fb, img, x, y);
}

void
fb_image_part(struct fb *fb, const struct image *img, int sx, int sy, int w,
    int h, int dx, int dy, int key)
{
	int x, y, px, py, ox, oy, ox0, ox1, oy0, oy1, lx, ly, ax, ay;
	const uint8_t *a;
	uint8_t c, *d;

	for (y = 0; y < h; y++) {
		py = dy + y;
		if (py < 0 || py >= FB_H || sy + y < 0 || sy + y >= img->h)
			continue;
		for (x = 0; x < w; x++) {
			px = dx + x;
			if (px < 0 || px >= FB_W || sx + x < 0 || sx + x >= img->w)
				continue;
			c = img->pixels[(size_t)(sy + y) * img->w + sx + x];
			if (key < 0 || c != key)
				fb_put(fb, (size_t)py * FB_W + px, c);
		}
	}
	if (img->rgba == NULL)
		return;
	/* The true colours of the same part, where the key lets it through,
	 * at the canvas's resolution. */
	ox0 = canvas_at(dx, fb->out_w, FB_W);
	ox1 = canvas_at(dx + w, fb->out_w, FB_W);
	oy0 = canvas_at(dy, fb->out_h, FB_H);
	oy1 = canvas_at(dy + h, fb->out_h, FB_H);
	for (oy = oy0 < 0 ? 0 : oy0; oy < oy1 && oy < fb->out_h; oy++) {
		ly = sy + (int)((long)(oy - oy0) * h / (oy1 - oy0));
		if (ly < 0 || ly >= img->h || dy + ly - sy < 0 ||
		    dy + ly - sy >= FB_H)
			continue;
		for (ox = ox0 < 0 ? 0 : ox0; ox < ox1 && ox < fb->out_w; ox++) {
			lx = sx + (int)((long)(ox - ox0) * w / (ox1 - ox0));
			if (lx < 0 || lx >= img->w || dx + lx - sx < 0 ||
			    dx + lx - sx >= FB_W)
				continue;
			if (key >= 0 &&
			    img->pixels[(size_t)ly * img->w + lx] == key)
				continue;
			/* The art pixel under this canvas pixel: its place in
			 * the part, in the picture's pixels, times the art's
			 * scale. */
			ay = (int)(((long)sy * (oy1 - oy0) + (long)(oy - oy0) *
			    h) * img->rgba_h / ((long)(oy1 - oy0) * img->h));
			ax = (int)(((long)sx * (ox1 - ox0) + (long)(ox - ox0) *
			    w) * img->rgba_w / ((long)(ox1 - ox0) * img->w));
			if (ax >= img->rgba_w || ay >= img->rgba_h)
				continue;
			a = img->rgba + ((size_t)ay * (img->rgba_stride > 0 ?
			    img->rgba_stride : img->rgba_w) + (size_t)ax) * 4;
			d = fb->rgb + ((size_t)oy * fb->out_w + ox) * 3;
			d[0] = a[0];
			d[1] = a[1];
			d[2] = a[2];
			fb->tc[(size_t)oy * fb->out_w + ox] = 1;
		}
	}
}

/*
 * Blend the true colours of frame "f" at (x, y), in 1/"sub" pixels, over
 * the canvas, inside the canvas rectangle [cx0, cx1) x [cy0, cy1).
 */
static void
frame_rgba(struct fb *fb, const struct frame *f, int x, int y, int sub,
    int cx0, int cy0, int cx1, int cy1)
{
	int bx0 = canvas_at(x, fb->out_w, FB_W * sub);
	int bx1 = canvas_at(x + f->w * sub, fb->out_w, FB_W * sub);
	int by0 = canvas_at(y, fb->out_h, FB_H * sub);
	int by1 = canvas_at(y + f->h * sub, fb->out_h, FB_H * sub);
	int ox, oy, ax, ay, k;
	const uint8_t *a, *under;
	uint8_t *d;
	size_t o;

	if (bx1 <= bx0 || by1 <= by0)
		return;
	if (bx0 > cx0)
		cx0 = bx0;
	if (bx1 < cx1)
		cx1 = bx1;
	if (by0 > cy0)
		cy0 = by0;
	if (by1 < cy1)
		cy1 = by1;
	for (oy = cy0; oy < cy1; oy++) {
		ay = (int)((long)(oy - by0) * f->rgba_h / (by1 - by0));
		for (ox = cx0; ox < cx1; ox++) {
			ax = (int)((long)(ox - bx0) * f->rgba_w / (bx1 - bx0));
			a = f->rgba + ((size_t)ay * f->rgba_w + ax) * 4;
			if (a[3] == 0)
				continue;
			o = (size_t)oy * fb->out_w + ox;
			d = fb->rgb + o * 3;
			if (a[3] == 255)
				memcpy(d, a, 3);
			else {
				/* Over what is there now, palette included. */
				under = fb->tc[o] ? d : fb->pal + 3 * fb->idx[o];
				for (k = 0; k < 3; k++)
					d[k] = (uint8_t)((a[k] * a[3] + under[k] *
					    (255 - a[3]) + 127) / 255);
			}
			fb->tc[o] = 1;
		}
	}
}

/*
 * The palette indices of frame "f" at (x, y), in 1/"sub" pixels, onto
 * the canvas, inside the canvas rectangle [cx0, cx1) x [cy0, cy1): an
 * original sprite between two 640x480 pixels.
 */
static void
frame_idx(struct fb *fb, const struct frame *f, int x, int y, int sub,
    int cx0, int cy0, int cx1, int cy1)
{
	int bx0 = canvas_at(x, fb->out_w, FB_W * sub);
	int bx1 = canvas_at(x + f->w * sub, fb->out_w, FB_W * sub);
	int by0 = canvas_at(y, fb->out_h, FB_H * sub);
	int by1 = canvas_at(y + f->h * sub, fb->out_h, FB_H * sub);
	int ox, oy, ax, ay;
	size_t i, o;

	if (bx1 <= bx0 || by1 <= by0)
		return;
	if (bx0 > cx0)
		cx0 = bx0;
	if (bx1 < cx1)
		cx1 = bx1;
	if (by0 > cy0)
		cy0 = by0;
	if (by1 < cy1)
		cy1 = by1;
	for (oy = cy0; oy < cy1; oy++) {
		ay = (int)((long)(oy - by0) * f->h / (by1 - by0));
		for (ox = cx0; ox < cx1; ox++) {
			ax = (int)((long)(ox - bx0) * f->w / (bx1 - bx0));
			i = (size_t)ay * f->w + ax;
			if (f->mask != NULL && !f->mask[i])
				continue;
			o = (size_t)oy * fb->out_w + ox;
			fb->idx[o] = f->pixels[i];
			fb->tc[o] = 0;
		}
	}
}

/*
 * fb_frame_clip() at (xs, ys) in 1/"sub" pixels: the 640x480 indices at
 * the nearest pixel; on the canvas, the true colours, or an original
 * frame's indices off a whole pixel, at the canvas pixel.
 */
static void
frame_clip(struct fb *fb, const struct frame *f, int xs, int ys, int sub,
    int x0, int y0, int x1, int y1)
{
	int fx, fy, px, py, x, y, between;
	size_t i;

	xs += f->ox * sub;
	ys += f->oy * sub;
	x = canvas_at(xs + sub / 2, 1, sub);
	y = canvas_at(ys + sub / 2, 1, sub);
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > FB_W - 1)
		x1 = FB_W - 1;
	if (y1 > FB_H - 1)
		y1 = FB_H - 1;
	if (x1 < x0 || y1 < y0)
		return;
	between = f->rgba == NULL && fb->out_h != FB_H &&
	    (xs % sub != 0 || ys % sub != 0);
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
			if (f->rgba != NULL || between)
				fb->px[(size_t)py * FB_W + px] = f->pixels[i];
			else
				fb_put(fb, (size_t)py * FB_W + px,
				    f->pixels[i]);
		}
	}
	if (f->rgba != NULL)
		frame_rgba(fb, f, xs, ys, sub, fb->x0[x0], fb->y0[y0],
		    fb->x0[x1 + 1], fb->y0[y1 + 1]);
	else if (between)
		frame_idx(fb, f, xs, ys, sub, fb->x0[x0], fb->y0[y0],
		    fb->x0[x1 + 1], fb->y0[y1 + 1]);
}

void
fb_frame_clip(struct fb *fb, const struct frame *f, int x, int y, int x0,
    int y0, int x1, int y1)
{
	frame_clip(fb, f, x, y, 1, x0, y0, x1, y1);
}

void
fb_frame_sub(struct fb *fb, const struct frame *f, int x, int y, int sub)
{
	frame_clip(fb, f, x, y, sub, 0, 0, FB_W - 1, FB_H - 1);
}

void
fb_frame_sub_clip(struct fb *fb, const struct frame *f, int x, int y,
    int sub, int x0, int y0, int x1, int y1)
{
	frame_clip(fb, f, x, y, sub, x0, y0, x1, y1);
}

void
fb_frame_rgba_clip(struct fb *fb, const struct frame *f, int x, int y,
    int x0, int y0, int x1, int y1)
{
	if (f->rgba == NULL)
		return;
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > FB_W - 1)
		x1 = FB_W - 1;
	if (y1 > FB_H - 1)
		y1 = FB_H - 1;
	if (x1 < x0 || y1 < y0)
		return;
	frame_rgba(fb, f, x + f->ox, y + f->oy, 1, fb->x0[x0], fb->y0[y0],
	    fb->x0[x1 + 1], fb->y0[y1 + 1]);
}

void
fb_frame(struct fb *fb, const struct frame *f, int x, int y)
{
	fb_frame_clip(fb, f, x, y, 0, 0, FB_W - 1, FB_H - 1);
}

void
fb_glyph(struct fb *fb, const struct font *f, size_t g, int x_sub, int y,
    uint8_t ink)
{
	const struct hd_glyph *h;
	const uint8_t *col = fb->pal + 3 * ink, *under;
	int x, gx, gy, cx, cy, ox, oy, k, hd;
	uint8_t a, *d;
	size_t o;

	if (g >= f->count)
		return;
	hd = f->hd != NULL && f->hd_height == fb->out_h;
	/* The nearest pixel of the pen, as text layout rounds. */
	x = canvas_at(x_sub + FONT_SUB / 2, 1, FONT_SUB) - f->origin_x;
	for (gy = 0; gy < f->height; gy++)
		for (gx = 0; gx < f->cell_w; gx++) {
			if (!font_pixel(f, g, gx, gy) || x + gx < 0 ||
			    x + gx >= FB_W || y + gy < 0 || y + gy >= FB_H)
				continue;
			o = (size_t)(y + gy) * FB_W + x + gx;
			if (hd)
				fb->px[o] = ink;
			else
				fb_put(fb, o, ink);
		}
	if (!hd || (h = &f->hd[g])->cov == NULL)
		return;
	cx = canvas_at(x_sub, fb->out_w, FB_W * FONT_SUB) + h->x;
	cy = canvas_at(y, fb->out_h, FB_H) + h->y;
	for (gy = 0; gy < h->h; gy++) {
		oy = cy + gy;
		if (oy < 0 || oy >= fb->out_h)
			continue;
		for (gx = 0; gx < h->w; gx++) {
			ox = cx + gx;
			if (ox < 0 || ox >= fb->out_w ||
			    (a = h->cov[(size_t)gy * h->w + gx]) == 0)
				continue;
			o = (size_t)oy * fb->out_w + ox;
			d = fb->rgb + o * 3;
			under = fb->tc[o] ? d : fb->pal + 3 * fb->idx[o];
			for (k = 0; k < 3; k++)
				d[k] = (uint8_t)((col[k] * a + under[k] *
				    (255 - a) + 127) / 255);
			fb->tc[o] = 1;
		}
	}
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
	uint8_t lut[256 * 3], dim[256];
	const uint8_t *t;
	size_t i, n = (size_t)fb->out_w * fb->out_h;

	if (brightness < 0)
		brightness = 0;
	if (brightness > 256)
		brightness = 256;
	for (i = 0; i < sizeof(lut); i++)
		lut[i] = (uint8_t)(fb->pal[i] * brightness / 256);
	for (i = 0; i < sizeof(dim); i++)
		dim[i] = (uint8_t)(i * (unsigned)brightness / 256);
	for (i = 0; i < n; i++, rgb += 3) {
		if (fb->tc[i]) {
			t = fb->rgb + 3 * i;
			rgb[0] = dim[t[0]];
			rgb[1] = dim[t[1]];
			rgb[2] = dim[t[2]];
		} else
			memcpy(rgb, lut + 3 * fb->idx[i], 3);
	}
}
