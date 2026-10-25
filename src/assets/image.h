/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_IMAGE_H
#define STMALO_IMAGE_H

#include <stdint.h>

/* 8-bit indexed image with an optional 8-bit-per-channel RGB palette. */
struct image {
	int		 w;
	int		 h;
	uint8_t		*pixels;	/* w * h indices, row-major */
	uint8_t		 palette[256 * 3];
	int		 has_palette;
};

void	image_free(struct image *img);

#endif
