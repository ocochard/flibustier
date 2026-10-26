/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiled.h"

#define FLIP_FLAGS	0xF0000000u	/* flips and hexagonal rotation */
#define TILE_LIMIT	256

static int
fail(char *err, size_t errlen, const char *msg)
{
	snprintf(err, errlen, "Tiled map: %s", msg);
	return (-1);
}

static int
is(const struct json *j, const char *key, const char *want)
{
	const char *v = json_get_str(j, key);

	return (v == NULL || strcmp(v, want) == 0);
}

/* Read the tile layer "l" into "out" (w * h cells). */
static int
read_layer(const struct json *l, int w, int h, int64_t firstgid,
    uint8_t *out, char *err, size_t errlen)
{
	const struct json *data = json_get(l, "data");
	const struct json *c;
	int64_t gid;
	size_t i;

	if (json_get_int(l, "width", -1) != w ||
	    json_get_int(l, "height", -1) != h)
		return (fail(err, errlen, "a layer is not the map's size"));
	if (!is(l, "encoding", "csv") || !is(l, "compression", ""))
		return (fail(err, errlen, "layer data must be stored as CSV"));
	if (data == NULL || data->type != JSON_ARRAY ||
	    data->count != (size_t)w * h)
		return (fail(err, errlen, "a layer lacks its cells"));
	for (i = 0; i < data->count; i++) {
		c = data->items[i];
		if (c->type != JSON_INT || c->num < 0 || c->num > UINT32_MAX)
			return (fail(err, errlen, "a cell is not a tile"));
		gid = c->num;
		if (gid & FLIP_FLAGS)
			return (fail(err, errlen, "flipped or rotated tile"));
		if (gid == 0)
			return (fail(err, errlen, "empty cell"));
		if (gid < firstgid || gid - firstgid >= TILE_LIMIT)
			return (fail(err, errlen, "tile number out of range"));
		out[i] = (uint8_t)(gid - firstgid);
	}
	return (0);
}

int
tiled_read(const uint8_t *data, size_t len, int w, int h, size_t nlayers,
    uint8_t *out, char *err, size_t errlen)
{
	const struct json *layers, *sets, *l, *inf;
	const char *type;
	struct json *root;
	char *text;
	int64_t firstgid;
	size_t i, n = 0;
	int rc = -1;

	if ((text = malloc(len + 1)) == NULL)
		return (fail(err, errlen, "out of memory"));
	memcpy(text, data, len);
	text[len] = '\0';
	root = json_parse_reals(text, err, errlen);
	free(text);
	if (root == NULL)
		return (-1);
	layers = json_get(root, "layers");
	sets = json_get(root, "tilesets");
	if (root->type != JSON_OBJECT || layers == NULL ||
	    layers->type != JSON_ARRAY) {
		fail(err, errlen, "no layers");
		goto out;
	}
	inf = json_get(root, "infinite");
	if (!is(root, "orientation", "orthogonal") ||
	    (inf != NULL && (inf->type != JSON_BOOL || inf->num != 0))) {
		fail(err, errlen, "only fixed-size orthogonal maps");
		goto out;
	}
	if (json_get_int(root, "width", -1) != w ||
	    json_get_int(root, "height", -1) != h) {
		fail(err, errlen, "the map has the wrong size");
		goto out;
	}
	if (sets == NULL || sets->type != JSON_ARRAY || sets->count != 1 ||
	    (firstgid = json_get_int(sets->items[0], "firstgid", 0)) < 1) {
		fail(err, errlen, "the map needs exactly one tileset");
		goto out;
	}
	for (i = 0; i < layers->count; i++) {
		l = layers->items[i];
		if ((type = json_get_str(l, "type")) == NULL)
			type = "";
		if (strcmp(type, "group") == 0) {
			fail(err, errlen, "group layers are not supported");
			goto out;
		}
		if (strcmp(type, "tilelayer") != 0)
			continue;	/* objects, images: not ours */
		if (n == nlayers) {
			fail(err, errlen, "too many tile layers");
			goto out;
		}
		if (read_layer(l, w, h, firstgid, out + n * (size_t)w * h, err,
		    errlen) != 0)
			goto out;
		n++;
	}
	if (n != nlayers) {
		fail(err, errlen, "too few tile layers");
		goto out;
	}
	rc = 0;
out:
	json_free(root);
	return (rc);
}

void
tiled_write(struct jbuf *b, int w, int h, int tile_w, int tile_h,
    size_t nlayers, const uint8_t *grid, const char *name,
    const char *tileset)
{
	char label[96];
	size_t i, k, n = (size_t)w * h;

	jb_raw(b, "{\"type\":\"map\",\"version\":\"1.10\",\"orientation\":"
	    "\"orthogonal\",\"renderorder\":\"right-down\",\"infinite\":false,"
	    "\"width\":");
	jb_int(b, w);
	jb_raw(b, ",\"height\":");
	jb_int(b, h);
	jb_raw(b, ",\"tilewidth\":");
	jb_int(b, tile_w);
	jb_raw(b, ",\"tileheight\":");
	jb_int(b, tile_h);
	jb_raw(b, ",\"nextlayerid\":");
	jb_int(b, (int64_t)nlayers + 1);
	jb_raw(b, ",\"nextobjectid\":1,\"tilesets\":[{\"firstgid\":1,"
	    "\"source\":");
	jb_str(b, tileset);
	jb_raw(b, "}],\"layers\":[");
	for (k = 0; k < nlayers; k++) {
		if (nlayers == 1)
			snprintf(label, sizeof(label), "%s", name);
		else
			snprintf(label, sizeof(label), "%s %zu", name, k);
		jb_raw(b, k > 0 ? ",\n{" : "\n{");
		jb_raw(b, "\"type\":\"tilelayer\",\"id\":");
		jb_int(b, (int64_t)k + 1);
		jb_raw(b, ",\"name\":");
		jb_str(b, label);
		jb_raw(b, ",\"x\":0,\"y\":0,\"width\":");
		jb_int(b, w);
		jb_raw(b, ",\"height\":");
		jb_int(b, h);
		jb_raw(b, ",\"opacity\":1,\"visible\":true,\"data\":[");
		for (i = 0; i < n; i++) {
			if (i > 0)
				jb_raw(b, i % (size_t)w == 0 ? ",\n" : ",");
			jb_int(b, grid[k * n + i] + 1);
		}
		jb_raw(b, "]}");
	}
	jb_raw(b, "]}\n");
}

void
tiled_write_tileset(struct jbuf *b, const char *name, int tile, int count,
    const char *image, int w, int h)
{
	jb_raw(b, "{\"type\":\"tileset\",\"version\":\"1.10\",\"name\":");
	jb_str(b, name);
	jb_raw(b, ",\"tilewidth\":");
	jb_int(b, tile);
	jb_raw(b, ",\"tileheight\":");
	jb_int(b, tile);
	jb_raw(b, ",\"tilecount\":");
	jb_int(b, count);
	jb_raw(b, ",\"columns\":");
	jb_int(b, w / tile);
	jb_raw(b, ",\"margin\":0,\"spacing\":0,\"image\":");
	jb_str(b, image);
	jb_raw(b, ",\"imagewidth\":");
	jb_int(b, w);
	jb_raw(b, ",\"imageheight\":");
	jb_int(b, h);
	jb_raw(b, "}\n");
}
