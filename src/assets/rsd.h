/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_RSD_H
#define FLIBUSTIER_RSD_H

#include <stddef.h>
#include <stdint.h>

/*
 * Apply an RSD patch, the per-era changes to the 1560 overview map, onto
 * "npixels" indexed pixels in place.  Returns 0 on success, -1 if the
 * stream is malformed or writes past the image.
 */
int	rsd_apply(const uint8_t *data, size_t len, uint8_t *pixels,
	    size_t npixels);

#endif
