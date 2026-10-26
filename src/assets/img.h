/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_IMG_H
#define FLIBUSTIER_IMG_H

#include <stddef.h>
#include <stdint.h>

#include "image.h"

/*
 * Decode an IMG picture (CLOUD.IMG, WPNSEL.IMG): u16 w, u16 h, u16 mode,
 * then raw pixels when mode is 0, or row-based RLE otherwise.  IMG files
 * carry no palette.  Returns 0 on success, -1 on malformed input.
 */
int	img_decode(const uint8_t *data, size_t len, struct image *out);

#endif
