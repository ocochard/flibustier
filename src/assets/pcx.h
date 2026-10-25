/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_PCX_H
#define STMALO_PCX_H

#include <stddef.h>
#include <stdint.h>

#include "image.h"

/*
 * Decode a ZSoft PCX: version 5, RLE, 8 bpp, one plane, with the 256-colour
 * palette at the end of the file.  This is the only variant the game uses.
 * Returns 0 on success, -1 on malformed or unsupported input.
 */
int	pcx_decode(const uint8_t *data, size_t len, struct image *out);

#endif
