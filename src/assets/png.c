/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <limits.h>
#include <string.h>

#include "png.h"

/*
 * stb_image (public domain or MIT, src/external), limited to PNG.  Its
 * functions are static, so they cannot clash with raylib's own copy.
 */
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS	8192
#include "external/stb_image.h"

#define SCREEN_H	480

int
png_decode(const uint8_t *data, size_t len, int art_height,
    struct image *out)
{
	stbi_uc *rgba;
	int w, h, n;

	memset(out, 0, sizeof(*out));
	if (len > INT_MAX || art_height < 1)
		return (-1);
	if ((rgba = stbi_load_from_memory(data, (int)len, &w, &h, &n, 4)) ==
	    NULL)
		return (-1);
	out->w = (int)(((long)w * SCREEN_H + art_height / 2) / art_height);
	out->h = (int)(((long)h * SCREEN_H + art_height / 2) / art_height);
	if (out->w < 1 || out->h < 1) {
		stbi_image_free(rgba);
		memset(out, 0, sizeof(*out));
		return (-1);
	}
	out->rgba = rgba;
	out->rgba_w = w;
	out->rgba_h = h;
	return (0);
}
