/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_IMAGE_H
#define FLIBUSTIER_IMAGE_H

#include <stdint.h>

/*
 * 8-bit indexed image with an optional 8-bit-per-channel RGB palette.
 * Replacement art also keeps its true colours, at its own resolution:
 * rgba_w x rgba_h pixels cover the w x h indexed ones.
 */
struct image {
	int		 w;
	int		 h;
	uint8_t		*pixels;	/* w * h indices, row-major */
	uint8_t		 palette[256 * 3];
	int		 has_palette;
	uint8_t		*rgba;		/* rgba_w * rgba_h * 4, or NULL */
	int		 rgba_w;
	int		 rgba_h;
	int		 rgba_stride;	/* pixels between rows; 0: rgba_w */
};

void	image_free(struct image *img);

/*
 * A fixed 256-colour palette for true-colour art without original data:
 * a 6x6x6 colour cube from entry 0 (black), then 40 greys.
 */
void	palette_fixed(uint8_t *pal);
void	image_fixed_palette(struct image *img);
/* Whether "img" has the fixed palette, not an original one. */
int	image_has_fixed_palette(const struct image *img);

/*
 * Reduce aw x ah true colours to w x h palette indices "px": each is the
 * colour of "pal" nearest to the average of the art pixels it covers.
 * With "mask", a pixel is opaque (1) when that average alpha is at least
 * half, and opaque pixels never get index 0, which raw sprite frames use
 * for transparency.  Returns 0, or -1 without memory.
 */
int	rgba_reduce(const uint8_t *rgba, int aw, int ah, int w, int h,
	    const uint8_t *pal, uint8_t *px, uint8_t *mask);

/*
 * Derive the indexed pixels of a true-colour image: each pixel is the
 * palette colour nearest to the average of the art pixels it covers.
 * Returns 0, or -1 without true colours or memory.
 */
int	image_quantize(struct image *img);

/* Forget the true colours: the image is drawn from its indices. */
void	image_drop_rgba(struct image *img);

#endif
