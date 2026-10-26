/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "png.h"
#include "tiles.h"

#define SCREEN_H	480
#define MAX_TILES	1024
#define MAX_FRAMES	64

struct desc {
	char	image[256];
	int	tile, count, frames, period;
};

static char *
trim(char *s)
{
	char *end;

	while (isspace((unsigned char)*s))
		s++;
	end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		*--end = '\0';
	return (s);
}

static int
whole(const char *s, int lo, int hi, int *out)
{
	char *end;
	long v = strtol(s, &end, 10);

	if (*s == '\0' || *end != '\0' || v < lo || v > hi)
		return (-1);
	*out = (int)v;
	return (0);
}

static int
parse(const uint8_t *data, size_t len, struct desc *d)
{
	char *text, *line, *next, *eq, *key, *val;
	int in_tiles = 0, rc = -1;

	memset(d, 0, sizeof(*d));
	d->count = 256;
	d->frames = 1;
	d->period = 4;
	if ((text = malloc(len + 1)) == NULL)
		return (-1);
	memcpy(text, data, len);
	text[len] = '\0';
	for (line = text; line != NULL; line = next) {
		if ((next = strchr(line, '\n')) != NULL)
			*next++ = '\0';
		line = trim(line);
		if (*line == '\0' || *line == '#' || *line == ';')
			continue;
		if (strcmp(line, "[tiles]") == 0) {
			in_tiles = 1;
			continue;
		}
		if ((eq = strchr(line, '=')) == NULL || !in_tiles)
			goto out;
		*eq = '\0';
		key = trim(line);
		val = trim(eq + 1);
		if (strcmp(key, "image") == 0)
			snprintf(d->image, sizeof(d->image), "%s", val);
		else if ((strcmp(key, "tile") == 0 &&
		    whole(val, 1, 1024, &d->tile) != 0) ||
		    (strcmp(key, "count") == 0 &&
		    whole(val, 1, MAX_TILES, &d->count) != 0) ||
		    (strcmp(key, "frames") == 0 &&
		    whole(val, 1, MAX_FRAMES, &d->frames) != 0) ||
		    (strcmp(key, "period") == 0 &&
		    whole(val, 1, 1000, &d->period) != 0))
			goto out;
		/* Unknown keys are ignored, for forward compatibility. */
	}
	if (d->image[0] != '\0' && d->tile > 0)
		rc = 0;
out:
	free(text);
	return (rc);
}

/* Indices of a sheet without its original: frame 0, fixed palette. */
static int
reduce(struct tileset *ts)
{
	size_t i, n = (size_t)ts->tile_w * ts->tile_h;
	uint8_t *art;
	int y, ax, ay;

	palette_fixed(ts->palette);
	ts->has_palette = 1;
	if ((art = malloc((size_t)ts->art_tile * ts->art_tile * 4)) == NULL)
		return (-1);
	for (i = 0; i < ts->count; i++) {
		ax = (int)(i % (size_t)ts->columns) * ts->art_tile;
		ay = (int)(i / (size_t)ts->columns) * ts->art_tile;
		for (y = 0; y < ts->art_tile; y++)
			memcpy(art + (size_t)y * ts->art_tile * 4, ts->rgba +
			    ((size_t)(ay + y) * ts->rgba_w + ax) * 4,
			    (size_t)ts->art_tile * 4);
		if (rgba_reduce(art, ts->art_tile, ts->art_tile, ts->tile_w,
		    ts->tile_h, ts->palette, ts->pixels + i * n, NULL) != 0) {
			free(art);
			return (-1);
		}
	}
	free(art);
	return (0);
}

int
tileset_from_blob(struct packs *packs, const char *id, const struct blob *b,
    struct tileset *out)
{
	struct desc d;
	struct blob ib, ob;
	struct image art;
	struct tileset orig;
	int side, rows, rc;

	memset(out, 0, sizeof(*out));
	if (strcmp(b->ext, "tiles") != 0)
		return (tileset_decode(b->data, b->len, out));
	if (parse(b->data, b->len, &d) != 0 ||
	    packs_load_beside(packs, b, d.image, &ib) != 0)
		return (-1);
	/* At the original height, the art keeps its pixel size. */
	rc = png_decode(ib.data, ib.len, SCREEN_H, &art);
	blob_free(&ib);
	if (rc != 0)
		return (-1);
	side = (int)(((long)d.tile * SCREEN_H + b->art_height / 2) /
	    b->art_height);
	out->columns = art.rgba_w / d.tile;
	rows = out->columns > 0 ?
	    (d.count + out->columns - 1) / out->columns : 0;
	if (side < 1 || out->columns < 1 ||
	    art.rgba_h < rows * d.frames * d.tile) {
		image_free(&art);
		return (-1);
	}
	out->tile_w = out->tile_h = side;
	out->count = (size_t)d.count;
	out->rgba = art.rgba;
	out->rgba_w = art.rgba_w;
	out->rgba_h = art.rgba_h;
	out->art_tile = d.tile;
	out->frames = d.frames;
	out->period = d.period;
	out->frame_rows = rows;
	if ((out->pixels = malloc(out->count * (size_t)side * side)) == NULL) {
		tileset_free(out);
		return (-1);
	}
	/* The original's indices and palette, when its tiles match. */
	rc = -1;
	if (packs_load_ext(packs, id, "imb", &ob) == 0) {
		if (tileset_decode(ob.data, ob.len, &orig) == 0) {
			if (orig.count == out->count && orig.tile_w == side &&
			    orig.tile_h == side) {
				memcpy(out->pixels, orig.pixels, out->count *
				    (size_t)side * side);
				memcpy(out->palette, orig.palette,
				    sizeof(out->palette));
				out->has_palette = orig.has_palette;
				rc = 0;
			}
			tileset_free(&orig);
		}
		blob_free(&ob);
	}
	if (rc != 0 && reduce(out) != 0) {
		tileset_free(out);
		return (-1);
	}
	return (0);
}

int
tileset_load(struct packs *packs, const char *id, struct tileset *out)
{
	struct blob b;
	int rc;

	memset(out, 0, sizeof(*out));
	if (packs_load(packs, id, &b) != 0)
		return (-1);
	rc = tileset_from_blob(packs, id, &b, out);
	blob_free(&b);
	return (rc);
}

void
tileset_tile(const struct tileset *ts, size_t i, unsigned pass,
    struct image *out)
{
	int frame, ax, ay;

	memset(out, 0, sizeof(*out));
	if (i >= ts->count)
		return;
	out->w = ts->tile_w;
	out->h = ts->tile_h;
	out->pixels = ts->pixels + i * (size_t)ts->tile_w * ts->tile_h;
	if (ts->rgba == NULL)
		return;
	frame = (int)(pass / (unsigned)ts->period % (unsigned)ts->frames);
	ax = (int)(i % (size_t)ts->columns) * ts->art_tile;
	ay = (frame * ts->frame_rows + (int)(i / (size_t)ts->columns)) *
	    ts->art_tile;
	out->rgba = ts->rgba + ((size_t)ay * ts->rgba_w + ax) * 4;
	out->rgba_w = out->rgba_h = ts->art_tile;
	out->rgba_stride = ts->rgba_w;
}
