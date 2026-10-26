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
#define ENCOUNTER_OFF	0x100	/* in the era block */
#define BATTLE_AI_OFF	0x80

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
	if (packs_load(packs, "tiles/battle", &b) != 0) {
		snprintf(err, errlen, "cannot load tiles/battle");
		return (-1);
	}
	rc = tileset_decode(b.data, b.len, &ts);
	blob_free(&b);
	if (rc != 0 || ts.tile_w != BATTLE_TILE || ts.count != 256) {
		tileset_free(&ts);
		snprintf(err, errlen, "bad tiles/battle");
		return (-1);
	}
	memcpy(w->battle_tiles, ts.pixels, sizeof(w->battle_tiles));
	tileset_free(&ts);
	return (0);
}

static int
read_words(const struct exe_layout *layout, const uint8_t *exe, size_t len,
    uint32_t va, int16_t *out, size_t n)
{
	const uint8_t *p;
	size_t i;

	if ((p = exe_at(layout, exe, len, va, 2 * n)) == NULL)
		return (-1);
	for (i = 0; i < n; i++)
		out[i] = (int16_t)(p[2 * i] | p[2 * i + 1] << 8);
	return (0);
}

static int
read_bytes(const struct exe_layout *layout, const uint8_t *exe, size_t len,
    uint32_t va, uint8_t *out, size_t n)
{
	const uint8_t *p;

	if ((p = exe_at(layout, exe, len, va, n)) == NULL)
		return (-1);
	memcpy(out, p, n);
	return (0);
}

/* The sword fight tables: offsets in the block at sword_va. */
static int
sword_load(const struct exe_layout *layout, const uint8_t *exe, size_t len,
    struct sword_tables *t)
{
	const uint32_t b = layout->sword_va;
	int16_t edges[2];

	if (read_words(layout, exe, len, layout->sword_moves_va, t->flag_y,
	    SWORD_FLAGS) != 0 ||
	    read_words(layout, exe, len, layout->sword_moves_va + 0x0c,
	    t->frames, SWORD_MOVES) != 0 ||
	    read_bytes(layout, exe, len, b, t->reach_level,
	    sizeof(t->reach_level)) != 0 ||
	    read_bytes(layout, exe, len, b + 0x05, t->reach_weapon,
	    sizeof(t->reach_weapon)) != 0 ||
	    read_bytes(layout, exe, len, b + 0x09, t->foe_reach,
	    sizeof(t->foe_reach)) != 0 ||
	    read_words(layout, exe, len, b + 0x0c, t->delay, 4) != 0 ||
	    read_words(layout, exe, len, b + 0x24, edges, 2) != 0 ||
	    read_bytes(layout, exe, len, b + 0x2c, t->hit,
	    sizeof(t->hit)) != 0 ||
	    read_bytes(layout, exe, len, b + 0x31, t->foe_hit,
	    sizeof(t->foe_hit)) != 0 ||
	    read_bytes(layout, exe, len, b + 0x42, t->far_move,
	    sizeof(t->far_move)) != 0 ||
	    read_bytes(layout, exe, len, b + 0x52, t->once,
	    sizeof(t->once)) != 0 ||
	    read_bytes(layout, exe, len, b + 0x62, t->guard,
	    sizeof(t->guard)) != 0 ||
	    read_bytes(layout, exe, len, b + 0xc2, t->strike,
	    sizeof(t->strike)) != 0 ||
	    read_bytes(layout, exe, len, b + 0x122, t->step_in,
	    sizeof(t->step_in)) != 0 ||
	    read_bytes(layout, exe, len, b + 0x182, t->step_back,
	    sizeof(t->step_back)) != 0 ||
	    read_bytes(layout, exe, len, b + 0x2a2, t->far_ai,
	    sizeof(t->far_ai)) != 0 ||
	    read_bytes(layout, exe, len, b + 0x2ba, t->caution,
	    sizeof(t->caution)) != 0 ||
	    read_bytes(layout, exe, len, b + 0x2bf, t->area,
	    sizeof(t->area)) != 0 ||
	    read_bytes(layout, exe, len, b + 0x2cf, &t->scripts[0][0][0],
	    sizeof(t->scripts)) != 0 ||
	    read_bytes(layout, exe, len, b + 0x38f, t->line,
	    sizeof(t->line)) != 0)
		return (-1);
	t->player_edge = edges[0];
	t->foe_edge = edges[1];
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
	if ((p = exe_at(layout, exe, len, layout->era_block_va +
	    (uint32_t)block * ERA_BLOCK_LEN + ENCOUNTER_OFF,
	    sizeof(w->encounter))) == NULL)
		goto bad;
	memcpy(w->encounter, p, sizeof(w->encounter));
	if ((p = exe_at(layout, exe, len, layout->era_block_va +
	    (uint32_t)block * ERA_BLOCK_LEN + BATTLE_AI_OFF,
	    sizeof(w->battle_ai))) == NULL)
		goto bad;
	memcpy(w->battle_ai, p, sizeof(w->battle_ai));
	if (read_words(layout, exe, len, layout->half_w_va, w->half_w,
	    BATTLE_TYPES) != 0 || read_words(layout, exe, len,
	    layout->half_h_va, w->half_h, BATTLE_TYPES) != 0 ||
	    read_words(layout, exe, len, layout->hit_steps_va, w->hit_steps,
	    BATTLE_LEVELS) != 0 || sword_load(layout, exe, len, &w->sword) != 0)
		goto bad;
	return (0);
bad:
	snprintf(err, errlen, "tables missing from the executable");
	return (-1);
}
