/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdio.h>
#include <string.h>

#include "pcx.h"
#include "rsd.h"
#include "tileset.h"
#include "towns.h"
#include "world.h"

#define ERA_BLOCK_LEN	0x200

int
world_terrain(const struct world *w, int x, int y)
{
	int tile;

	if (x < 0 || y < 0 || x >= WORLD_PX_W || y >= WORLD_PX_H)
		return (TERRAIN_LAND);
	tile = w->map[(y / WORLD_TILE) * WORLD_CELLS_W + x / WORLD_TILE];
	return (w->colour_class[w->tiles[tile * WORLD_TILE * WORLD_TILE +
	    (y % WORLD_TILE) * WORLD_TILE + x % WORLD_TILE]]);
}

static int
load_map(struct world *w, struct packs *packs, int era, char *err,
    size_t errlen)
{
	struct image base;
	struct tileset ts;
	struct blob b;
	char id[32];
	int rc;

	if (packs_load(packs, "pic/map1560", &b) != 0) {
		snprintf(err, errlen, "cannot load pic/map1560");
		return (-1);
	}
	rc = pcx_decode(b.data, b.len, &base);
	blob_free(&b);
	if (rc != 0 || base.w != WORLD_CELLS_W || base.h != WORLD_CELLS_H) {
		image_free(&base);
		snprintf(err, errlen, "bad pic/map1560");
		return (-1);
	}
	memcpy(w->map, base.pixels, sizeof(w->map));
	image_free(&base);
	/* Every era patches the 1560 map, as the game does. */
	if (era > 0) {
		snprintf(id, sizeof(id), "patch/map%d", town_era_years[era]);
		if (packs_load(packs, id, &b) != 0) {
			snprintf(err, errlen, "cannot load %s", id);
			return (-1);
		}
		rc = rsd_apply(b.data, b.len, w->map, sizeof(w->map));
		blob_free(&b);
		if (rc != 0) {
			snprintf(err, errlen, "bad %s", id);
			return (-1);
		}
	}
	if (packs_load(packs, "tiles/map", &b) != 0) {
		snprintf(err, errlen, "cannot load tiles/map");
		return (-1);
	}
	rc = tileset_decode(b.data, b.len, &ts);
	blob_free(&b);
	if (rc != 0 || ts.tile_w != WORLD_TILE || ts.count != 256) {
		tileset_free(&ts);
		snprintf(err, errlen, "bad tiles/map");
		return (-1);
	}
	memcpy(w->tiles, ts.pixels, sizeof(w->tiles));
	memcpy(w->palette, ts.palette, sizeof(w->palette));
	tileset_free(&ts);
	return (0);
}

int
world_load(struct world *w, struct packs *packs,
    const struct exe_layout *layout, const uint8_t *exe, size_t len,
    int era, char *err, size_t errlen)
{
	const uint8_t *p;
	int block;

	memset(w, 0, sizeof(*w));
	if (era < 0 || era >= EXE_ERAS) {
		snprintf(err, errlen, "bad era");
		return (-1);
	}
	w->era = era;
	if (load_map(w, packs, era, err, errlen) != 0)
		return (-1);
	/* The game numbers eras from 1 and uses block max(era, 1), so 1560
	 * and 1600 share a block (VA 0x1361c). */
	block = era > 0 ? era : 1;
	if ((p = exe_at(layout, exe, len, layout->colour_class_va, 256)) ==
	    NULL)
		goto bad;
	memcpy(w->colour_class, p, 256);
	if ((p = exe_at(layout, exe, len, layout->sine_va, 128)) == NULL)
		goto bad;
	memcpy(w->sine, p, 128);
	if ((p = exe_at(layout, exe, len, layout->era_block_va +
	    (uint32_t)block * ERA_BLOCK_LEN, sizeof(w->speed))) == NULL)
		goto bad;
	memcpy(w->speed, p, sizeof(w->speed));
	return (0);
bad:
	snprintf(err, errlen, "tables missing from the executable");
	return (-1);
}
