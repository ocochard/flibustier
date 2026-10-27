/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_PIC_H
#define FLIBUSTIER_PIC_H

#include "data/pack.h"
#include "image.h"

/*
 * Pictures (pic/ and img/ IDs): an original PCX or IMG, or replacement
 * art as a PNG drawn at its pack's art height.  Replacement art keeps its true colours and also gets
 * indexed pixels and a palette, so code working on indices (colour keys,
 * palette effects, sprites drawn in the picture's palette) still works.
 * Those come from the original when it is installed: its pixels if the
 * size matches, else the PNG reduced to its palette (an IMG, which has
 * no palette of its own, gives only its pixels).  Without it, the
 * PNG is reduced to a fixed palette.
 *
 * pic_decode() takes the blob packs_load() returned for "id"; pic_load()
 * loads it.  Both return 0, or -1 if the picture is missing or malformed.
 */
int	pic_decode(struct packs *packs, const char *id, const struct blob *b,
	    struct image *out);
int	pic_load(struct packs *packs, const char *id, struct image *out);

#endif
