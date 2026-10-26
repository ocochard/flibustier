/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_PNG_H
#define STMALO_PNG_H

#include <stddef.h>
#include <stdint.h>

#include "image.h"

/*
 * Decode a PNG of replacement art drawn for a 640x480 screen shown
 * "art_height" pixels high (1080 for 1080p).  Fills the true colours and
 * the size in 640x480 pixels, rounded; the indexed pixels and palette
 * are left empty.  Returns 0, or -1 on malformed input.
 */
int	png_decode(const uint8_t *data, size_t len, int art_height,
	    struct image *out);

#endif
