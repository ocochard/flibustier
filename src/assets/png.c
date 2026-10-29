/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
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
/* Room for 4K art: the sea battle's tile sheet, 64 tiles a row, runs
 * 9216 pixels wide there. */
#define STBI_MAX_DIMENSIONS	16384
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

static uint32_t
crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
	size_t i;
	int k;

	crc = ~crc;
	for (i = 0; i < n; i++) {
		crc ^= p[i];
		for (k = 0; k < 8; k++)
			crc = crc >> 1 ^ (0xEDB88320u & -(crc & 1));
	}
	return (~crc);
}

static void
be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static int
chunk(FILE *fp, const char *type, const uint8_t *data, size_t n)
{
	uint8_t b[4];
	uint32_t crc;

	be32(b, (uint32_t)n);
	crc = crc32_update(0, (const uint8_t *)type, 4);
	crc = crc32_update(crc, data, n);
	if (fwrite(b, 1, 4, fp) != 4 || fwrite(type, 1, 4, fp) != 4 ||
	    (n > 0 && fwrite(data, 1, n, fp) != n))
		return (-1);
	be32(b, crc);
	return (fwrite(b, 1, 4, fp) == 4 ? 0 : -1);
}

int
png_write_rgb(const char *path, const uint8_t *rgb, int w, int h)
{
	static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n',
	    0x1a, '\n' };
	const size_t row = (size_t)w * 3 + 1, raw = row * (size_t)h;
	const size_t blocks = raw / 65535 + 1;
	uint8_t ihdr[13], *z, *q;
	uint32_t a = 1, b = 0;
	size_t i, n, left;
	FILE *fp;
	int rc = -1;

	/* zlib: header, stored deflate blocks of the filtered rows (filter
	 * 0 each), Adler-32. */
	if ((z = malloc(2 + raw + 5 * blocks + 4)) == NULL)
		return (-1);
	z[0] = 0x78;
	z[1] = 0x01;
	q = z + 2;
	for (i = 0, left = raw; i < blocks; i++) {
		n = left < 65535 ? left : 65535;
		*q++ = i + 1 == blocks ? 1 : 0;
		*q++ = (uint8_t)n;
		*q++ = (uint8_t)(n >> 8);
		*q++ = (uint8_t)~n;
		*q++ = (uint8_t)(~n >> 8);
		q += n;
		left -= n;
	}
	/* Fill the blocks' data, row by row, skipping their headers. */
	for (i = 0, q = z + 2 + 5; i < raw; i++) {
		if (i > 0 && i % 65535 == 0)
			q += 5;
		*q = i % row == 0 ? 0 : rgb[i / row * (size_t)w * 3 +
		    i % row - 1];
		a = (a + *q) % 65521;
		b = (b + a) % 65521;
		q++;
	}
	be32(q, b << 16 | a);
	q += 4;
	be32(ihdr, (uint32_t)w);
	be32(ihdr + 4, (uint32_t)h);
	ihdr[8] = 8;		/* bits */
	ihdr[9] = 2;		/* RGB */
	ihdr[10] = ihdr[11] = ihdr[12] = 0;
	if ((fp = fopen(path, "wb")) != NULL) {
		rc = fwrite(sig, 1, 8, fp) == 8 &&
		    chunk(fp, "IHDR", ihdr, 13) == 0 &&
		    chunk(fp, "IDAT", z, (size_t)(q - z)) == 0 &&
		    chunk(fp, "IEND", NULL, 0) == 0 ? 0 : -1;
		if (fclose(fp) != 0)
			rc = -1;
	}
	free(z);
	return (rc);
}
