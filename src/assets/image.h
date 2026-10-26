/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_IMAGE_H
#define STMALO_IMAGE_H

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
};

void	image_free(struct image *img);

/* Fill "palette" with a fixed 256-colour palette for true-colour art. */
void	image_fixed_palette(struct image *img);

/*
 * Derive the indexed pixels of a true-colour image: each pixel is the
 * palette colour nearest to the average of the art pixels it covers.
 * Returns 0, or -1 without true colours or memory.
 */
int	image_quantize(struct image *img);

/* Forget the true colours: the image is drawn from its indices. */
void	image_drop_rgba(struct image *img);

#endif
