/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <string.h>

#include "img.h"
#include "pcx.h"
#include "pic.h"
#include "png.h"

/* An original picture: an IMG for img/ IDs, else a PCX. */
static int
orig_decode(const char *id, const struct blob *b, struct image *out)
{
	if (strncmp(id, "img/", 4) == 0)
		return (img_decode(b->data, b->len, out));
	return (pcx_decode(b->data, b->len, out));
}

int
pic_decode(struct packs *packs, const char *id, const struct blob *b,
    struct image *out)
{
	struct blob ob;
	struct image orig;

	if (strcmp(b->ext, "png") != 0)
		return (orig_decode(id, b, out));
	if (png_decode(b->data, b->len, b->art_height, out) != 0)
		return (-1);
	if (packs_load_ext(packs, id, strncmp(id, "img/", 4) == 0 ? "img" :
	    "pcx", &ob) == 0) {
		if (orig_decode(id, &ob, &orig) == 0) {
			/* An IMG has no palette: it is drawn in the screen's. */
			if (orig.has_palette) {
				memcpy(out->palette, orig.palette,
				    sizeof(out->palette));
				out->has_palette = 1;
			}
			if (orig.w == out->w && orig.h == out->h) {
				out->pixels = orig.pixels;
				orig.pixels = NULL;
			}
			image_free(&orig);
		}
		blob_free(&ob);
	}
	if (!out->has_palette)
		image_fixed_palette(out);
	if (out->pixels == NULL && image_quantize(out) != 0) {
		image_free(out);
		return (-1);
	}
	return (0);
}

int
pic_load(struct packs *packs, const char *id, struct image *out)
{
	struct blob b;
	int rc;

	memset(out, 0, sizeof(*out));
	if (packs_load(packs, id, &b) != 0)
		return (-1);
	rc = pic_decode(packs, id, &b, out);
	blob_free(&b);
	return (rc);
}
