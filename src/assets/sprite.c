/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdlib.h>
#include <string.h>

#include "sprite.h"

#define STRIDE		640	/* screen width the run positions assume */
#define END_OF_RUNS	0xFFFF
#define MAX_DIM		1024

static unsigned
le16(const uint8_t *p)
{
	return ((unsigned)p[0] | (unsigned)p[1] << 8);
}

static uint32_t
le32(const uint8_t *p)
{
	return ((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	    (uint32_t)p[3] << 24);
}

static void
frame_free(struct frame *f)
{
	free(f->pixels);
	free(f->mask);
	free(f->rgba);
}

void
bank_free(struct sprite_bank *bank)
{
	size_t i;

	for (i = 0; i < bank->count; i++)
		frame_free(&bank->frames[i]);
	free(bank->frames);
	memset(bank, 0, sizeof(*bank));
}

void
group_free(struct sprite_group *group)
{
	size_t i;

	for (i = 0; i < group->count; i++)
		bank_free(&group->banks[i]);
	free(group->banks);
	memset(group, 0, sizeof(*group));
}

/* Parse the frame header shared by both encodings. */
static int
frame_header(const uint8_t *p, size_t len, struct frame *f)
{
	if (len < 6)
		return (-1);
	f->w = (int)le16(p);
	f->h = (int)le16(p + 2);
	f->key = le16(p + 4);
	if (f->w == 0 || f->h == 0 || f->w > MAX_DIM || f->h > MAX_DIM)
		return (-1);
	if ((f->pixels = calloc((size_t)f->w, (size_t)f->h)) == NULL)
		return (-1);
	return (0);
}

static int
frame_runs(const uint8_t *p, size_t len, struct frame *f)
{
	size_t i, pos, x, y, skip, count;

	if (frame_header(p, len, f) != 0)
		return (-1);
	if ((f->mask = calloc((size_t)f->w, (size_t)f->h)) == NULL)
		return (-1);
	i = 6;
	pos = 0;
	for (;;) {
		if (i + 2 > len)
			return (-1);
		if ((skip = le16(p + i)) == END_OF_RUNS)
			return (0);	/* padding may follow */
		if (i + 4 > len)
			return (-1);
		count = le16(p + i + 2);
		i += 4;
		pos += skip;
		if (count == 0)
			continue;
		x = pos % STRIDE;
		y = pos / STRIDE;
		/* A run never wraps a row or leaves the frame box. */
		if (i + count > len || x + count > (size_t)f->w ||
		    y >= (size_t)f->h)
			return (-1);
		memcpy(f->pixels + y * (size_t)f->w + x, p + i, count);
		memset(f->mask + y * (size_t)f->w + x, 1, count);
		i += count;
		pos += count;
	}
}

static int
frame_raw(const uint8_t *p, size_t len, struct frame *f)
{
	if (frame_header(p, len, f) != 0)
		return (-1);
	if (6 + (size_t)f->w * f->h > len)
		return (-1);
	memcpy(f->pixels, p + 6, (size_t)f->w * f->h);
	return (0);
}

static int
bank_decode(const uint8_t *data, size_t len, struct sprite_bank *out,
    int (*decode)(const uint8_t *, size_t, struct frame *))
{
	size_t i, n, start, end;

	memset(out, 0, sizeof(*out));
	if (len < 2)
		return (-1);
	start = le16(data);
	n = start / 2;
	if (n == 0 || start % 2 != 0 || start > len)
		return (-1);
	if ((out->frames = calloc(n, sizeof(*out->frames))) == NULL)
		return (-1);
	out->count = n;
	for (i = 0; i < n; i++) {
		start = le16(data + 2 * i);
		end = i + 1 < n ? le16(data + 2 * (i + 1)) : len;
		if (start < 2 * n || end < start || end > len ||
		    decode(data + start, end - start, &out->frames[i]) != 0) {
			bank_free(out);
			return (-1);
		}
	}
	return (0);
}

int
bank_decode_runs(const uint8_t *data, size_t len, struct sprite_bank *out)
{
	return (bank_decode(data, len, out, frame_runs));
}

int
bank_decode_raw(const uint8_t *data, size_t len, struct sprite_bank *out)
{
	return (bank_decode(data, len, out, frame_raw));
}

int
group_decode(const uint8_t *data, size_t len, struct sprite_group *out)
{
	size_t i, n, start, end;

	memset(out, 0, sizeof(*out));
	if (len < 4)
		return (-1);
	start = le32(data);
	n = start / 4;
	if (n == 0 || start % 4 != 0 || start > len)
		return (-1);
	if ((out->banks = calloc(n, sizeof(*out->banks))) == NULL)
		return (-1);
	out->count = n;
	for (i = 0; i < n; i++) {
		start = le32(data + 4 * i);
		end = i + 1 < n ? le32(data + 4 * (i + 1)) : len;
		if (start < 4 * n || end < start || end > len ||
		    bank_decode_runs(data + start, end - start,
		    &out->banks[i]) != 0) {
			group_free(out);
			return (-1);
		}
	}
	return (0);
}
