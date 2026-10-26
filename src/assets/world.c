/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pcx.h"
#include "rsd.h"
#include "tiled.h"
#include "tiles.h"
#include "tileset.h"
#include "towns.h"
#include "world.h"
#include "sim/json.h"

const char *const battle_set_names[BATTLE_SETS] = {
	"north", "east", "south", "west", "open",
};

#define ERA_BLOCK_LEN	0x200
#define ENCOUNTER_OFF	0x100	/* in the era block */
#define BATTLE_AI_OFF	0x80
#define ERA_RANGES_OFF	0xF8	/* diplomacy range, shipwright tolerance */

int
world_terrain(const struct world *w, int x, int y)
{
	int tile;

	if (x < 0 || y < 0 || x >= WORLD_PX_W || y >= WORLD_PX_H)
		return (TERRAIN_LAND);
	tile = w->map[(y / WORLD_TILE) * WORLD_CELLS_W + x / WORLD_TILE];
	return (w->map_terrain[tile * WORLD_TILE * WORLD_TILE +
	    (y % WORLD_TILE) * WORLD_TILE + x % WORLD_TILE]);
}

/*
 * SBTL.BMP: u16 offsets, one per screen, then at each a u16 15, 15, 2
 * header and 225 battle tile numbers, row by row (VA 0x2a9e8).
 */
static int
load_screen_file(struct packs *packs, const char *id, uint8_t *out,
    size_t count, char *err, size_t errlen)
{
	struct blob b;
	size_t i, off, n = BATTLE_SCREEN * BATTLE_SCREEN;

	if (packs_load(packs, id, &b) != 0) {
		snprintf(err, errlen, "cannot load %s", id);
		return (-1);
	}
	for (i = 0; i < count; i++) {
		if (2 * i + 1 >= b.len)
			break;
		off = (size_t)(b.data[2 * i] | b.data[2 * i + 1] << 8);
		if (off + 6 + n > b.len)
			break;
		memcpy(out + i * n, b.data + off + 6, n);
	}
	blob_free(&b);
	if (i < count) {
		snprintf(err, errlen, "bad %s", id);
		return (-1);
	}
	return (0);
}

/*
 * Tile grids from a Tiled map "id", when a pack provides one: 0 when
 * read, 1 when there is none, -1 with a message in "err".
 */
static int
load_tiled(struct packs *packs, const char *id, int gw, int gh,
    size_t layers, uint8_t *out, char *err, size_t errlen)
{
	struct blob b;
	char why[192];
	int rc;

	if (packs_load(packs, id, &b) != 0)
		return (1);
	if (strcmp(b.ext, "tmj") != 0 && strcmp(b.ext, "json") != 0) {
		blob_free(&b);
		snprintf(err, errlen, "%s: not a Tiled map (.tmj)", id);
		return (-1);
	}
	rc = tiled_read(b.data, b.len, gw, gh, layers, out, why, sizeof(why));
	blob_free(&b);
	if (rc != 0)
		snprintf(err, errlen, "%s: %s", id, why);
	return (rc);
}

static int
load_screens(struct world *w, struct packs *packs, char *err, size_t errlen)
{
	int rc;

	/* Open packs: Tiled maps, one tile layer per screen. */
	if ((rc = load_tiled(packs, "screens/sea", BATTLE_SCREEN,
	    BATTLE_SCREEN, BATTLE_SCREENS, &w->battle_screens[0][0], err,
	    errlen)) < 0)
		return (-1);
	if (rc == 1 && load_screen_file(packs, "tiles15/sbtl",
	    &w->battle_screens[0][0], BATTLE_SCREENS, err, errlen) != 0)
		return (-1);
	if ((rc = load_tiled(packs, "screens/land", BATTLE_SCREEN,
	    BATTLE_SCREEN, LAND_SCREENS, &w->land_screens[0][0], err,
	    errlen)) != 1)
		return (rc);
	return (load_screen_file(packs, "tiles15/lbtl", &w->land_screens[0][0],
	    LAND_SCREENS, err, errlen));
}

/* Terrain of "n" tile pixels: the class of each one's colour. */
static void
classify(const uint8_t *classes, const uint8_t *px, size_t n, uint8_t *out)
{
	size_t i;

	for (i = 0; i < n; i++)
		out[i] = classes[px[i]];
}

/* The ground of the executable's land tile code (VA 0x7197). */
static uint8_t
ground(uint8_t c)
{
	return (c > 7 ? GROUND_WATER : c > 4 ? GROUND_CLEAR : c < 4 ?
	    GROUND_COVER : GROUND_SWAMP);
}

static int
load_map(struct world *w, struct packs *packs, int era,
    const uint8_t *classes, char *err, size_t errlen)
{
	struct image base;
	struct tileset ts;
	struct blob b;
	char id[32];
	int rc;

	/* Open packs: the era's map as a Tiled map. */
	snprintf(id, sizeof(id), "map/%d", town_era_years[era]);
	if ((rc = load_tiled(packs, id, WORLD_CELLS_W, WORLD_CELLS_H, 1,
	    w->map, err, errlen)) < 0)
		return (-1);
	if (rc == 0)
		goto tiles;
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
tiles:
	rc = tileset_load(packs, "tiles/map", &ts);
	if (rc != 0 || ts.tile_w != WORLD_TILE || ts.count != 256) {
		tileset_free(&ts);
		snprintf(err, errlen, "bad tiles/map");
		return (-1);
	}
	memcpy(w->tiles, ts.pixels, sizeof(w->tiles));
	memcpy(w->palette, ts.palette, sizeof(w->palette));
	/* A tile sheet may bring its own terrain. */
	if (ts.terrain != NULL)
		memcpy(w->map_terrain, ts.terrain, sizeof(w->map_terrain));
	else
		classify(classes, w->tiles, sizeof(w->tiles), w->map_terrain);
	tileset_free(&ts);
	rc = tileset_load(packs, "tiles/battle", &ts);
	if (rc != 0 || ts.tile_w != BATTLE_TILE || ts.count != 256) {
		tileset_free(&ts);
		snprintf(err, errlen, "bad tiles/battle");
		return (-1);
	}
	if (ts.terrain != NULL)
		memcpy(w->battle_terrain, ts.terrain,
		    sizeof(w->battle_terrain));
	else
		classify(classes, ts.pixels, sizeof(w->battle_terrain),
		    w->battle_terrain);
	if (ts.ground != NULL)
		memcpy(w->land_ground, ts.ground, sizeof(w->land_ground));
	tileset_free(&ts);
	return (load_screens(w, packs, err, errlen));
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

/* Parse JSON "id" from the packs: 1 if none, -1 if malformed, 0. */
static int
load_json(struct packs *packs, const char *id, struct json **out, char *err,
    size_t errlen)
{
	struct blob b;
	char *text;

	*out = NULL;
	if (packs_load(packs, id, &b) != 0)
		return (1);
	if ((text = malloc(b.len + 1)) == NULL) {
		blob_free(&b);
		snprintf(err, errlen, "out of memory");
		return (-1);
	}
	memcpy(text, b.data, b.len);
	text[b.len] = '\0';
	blob_free(&b);
	*out = json_parse(text, err, errlen);
	free(text);
	return (*out != NULL ? 0 : -1);
}

/* An array of "n" integers from lo to hi into "out". */
static int
int_list(const struct json *a, size_t n, int lo, int hi, int *out)
{
	size_t i;

	if (a == NULL || a->type != JSON_ARRAY || a->count != n)
		return (-1);
	for (i = 0; i < n; i++) {
		if (a->items[i]->type != JSON_INT || a->items[i]->num < lo ||
		    a->items[i]->num > hi)
			return (-1);
		out[i] = (int)a->items[i]->num;
	}
	return (0);
}

/* data/treasure and data/battle-sets, when a pack gives them. */
static int
load_places(struct world *w, struct packs *packs, char *err, size_t errlen)
{
	const struct json *spots;
	struct json *j;
	int rc, xy[2], set[BATTLE_PIECES], k, i;

	if ((rc = load_json(packs, "data/treasure", &j, err, errlen)) < 0)
		return (-1);
	if (rc == 0) {
		spots = json_get(j, "spots");
		rc = spots != NULL && spots->type == JSON_ARRAY &&
		    spots->count == TREASURE_SPOTS ? 0 : -1;
		for (i = 0; rc == 0 && i < TREASURE_SPOTS; i++) {
			rc = int_list(spots->items[i], 2, 0, WORLD_CELLS_W - 1,
			    xy);
			if (rc == 0 && xy[1] >= WORLD_CELLS_H)
				rc = -1;
			w->treasure[i][0] = (int16_t)xy[0];
			w->treasure[i][1] = (int16_t)xy[1];
		}
		json_free(j);
		if (rc != 0) {
			snprintf(err, errlen, "data/treasure: %d spots of "
			    "[x, y] world cells", TREASURE_SPOTS);
			return (-1);
		}
	}
	if ((rc = load_json(packs, "data/battle-sets", &j, err, errlen)) < 0)
		return (-1);
	if (rc == 1)
		return (0);
	for (k = 0; rc == 0 && k < BATTLE_SETS; k++) {
		rc = int_list(json_get(j, battle_set_names[k]), BATTLE_PIECES,
		    0, BATTLE_SCREENS - 1, set);
		for (i = 0; rc == 0 && i < BATTLE_PIECES; i++)
			w->battle_sets[k][i] = (uint8_t)set[i];
	}
	json_free(j);
	if (rc != 0) {
		snprintf(err, errlen, "data/battle-sets: north, east, south, "
		    "west and open, %d screens each", BATTLE_PIECES);
		return (-1);
	}
	return (0);
}

int
world_load(struct world *w, struct packs *packs,
    const struct exe_layout *layout, const uint8_t *exe, size_t len,
    int era, char *err, size_t errlen)
{
	const uint8_t *p;
	int block, i;

	memset(w, 0, sizeof(*w));
	if (era < 0 || era >= EXE_ERAS) {
		snprintf(err, errlen, "bad era");
		return (-1);
	}
	w->era = era;
	/* The original ground first: a tile sheet may replace it. */
	if ((p = exe_at(layout, exe, len, layout->land_class_va, 256)) ==
	    NULL) {
		snprintf(err, errlen, "no land battle terrain");
		return (-1);
	}
	for (i = 0; i < 256; i++)
		w->land_ground[i] = ground(p[i]);
	if ((p = exe_at(layout, exe, len, layout->colour_class_va, 256)) ==
	    NULL)
		goto bad;
	if (load_map(w, packs, era, p, err, errlen) != 0)
		return (-1);
	/* The game numbers eras from 1 and uses block max(era, 1), so 1560
	 * and 1600 share a block (VA 0x1361c). */
	block = era > 0 ? era : 1;
	if ((p = exe_at(layout, exe, len, layout->battle_sets_va,
	    2 * BATTLE_SETS * BATTLE_PIECES)) == NULL) {
		snprintf(err, errlen, "no battle screen sets");
		return (-1);
	}
	for (i = 0; i < BATTLE_SETS * BATTLE_PIECES; i++)
		w->battle_sets[i / BATTLE_PIECES][i % BATTLE_PIECES] =
		    (uint8_t)(p[2 * i] < BATTLE_SCREENS ? p[2 * i] : 0);
	if (read_words(layout, exe, len, layout->treasure_va,
	    &w->treasure[0][0], 2 * TREASURE_SPOTS) != 0) {
		snprintf(err, errlen, "no treasure spots");
		return (-1);
	}
	if ((p = exe_at(layout, exe, len, layout->coast_va, 256)) == NULL) {
		snprintf(err, errlen, "no coast table");
		return (-1);
	}
	memcpy(w->coast, p, 256);
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
	    (uint32_t)block * ERA_BLOCK_LEN + ENCOUNTER_OFF +
	    sizeof(w->encounter), sizeof(w->treasure_ship))) == NULL)
		goto bad;
	memcpy(w->treasure_ship, p, sizeof(w->treasure_ship));
	if ((p = exe_at(layout, exe, len, layout->era_block_va +
	    (uint32_t)block * ERA_BLOCK_LEN + BATTLE_AI_OFF,
	    sizeof(w->battle_ai))) == NULL)
		goto bad;
	memcpy(w->battle_ai, p, sizeof(w->battle_ai));
	if ((p = exe_at(layout, exe, len, layout->era_block_va +
	    (uint32_t)block * ERA_BLOCK_LEN + ERA_RANGES_OFF, 2)) == NULL)
		goto bad;
	w->diplomacy_range = p[0];
	w->wright_tolerance = p[1];
	if (read_words(layout, exe, len, layout->half_w_va, w->half_w,
	    BATTLE_TYPES) != 0 || read_words(layout, exe, len,
	    layout->half_h_va, w->half_h, BATTLE_TYPES) != 0 ||
	    read_words(layout, exe, len, layout->hit_steps_va, w->hit_steps,
	    BATTLE_LEVELS) != 0 || sword_load(layout, exe, len, &w->sword) != 0)
		goto bad;
	return (load_places(w, packs, err, errlen));
bad:
	snprintf(err, errlen, "tables missing from the executable");
	return (-1);
}
