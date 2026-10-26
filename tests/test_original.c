/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Tests against the user's own Pirates! Gold data.  Set STMALO_DATA to
 * the GOG DATA.DAT, its install directory, or an extracted copy.  The
 * test is skipped (exit code 77) when it is not set.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data/exe.h"
#include "data/pack.h"
#include "assets/font.h"
#include "assets/img.h"
#include "assets/pcx.h"
#include "assets/rsd.h"
#include "assets/scenario.h"
#include "assets/sprite.h"
#include "assets/text.h"
#include "assets/tiles.h"
#include "assets/tileset.h"
#include "assets/towns.h"
#include "assets/world.h"
#include "view/view.h"

#define SKIP	77

static int failures;

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,	\
		    __LINE__, #cond);					\
		failures++;						\
	}								\
} while (0)

static int
load_pcx(struct packs *packs, const char *id, struct image *img)
{
	struct blob b;
	int rc;

	if (packs_load(packs, id, &b) != 0)
		return (-1);
	rc = strcmp(b.ext, "pcx") == 0 ? pcx_decode(b.data, b.len, img) : -1;
	blob_free(&b);
	return (rc);
}

static void
test_pictures(struct packs *packs)
{
	static const struct {
		const char	*id;
		int		 w, h;
	} pics[] = {
		{ "pic/town",		640, 480 },
		{ "pic/tavern",		640, 480 },
		{ "pic/f-engl",		800, 400 },
		{ "pic/s_frig",		604, 403 },
		{ "pic/baseflag",	200, 200 },
	};
	struct image img;
	size_t i;

	for (i = 0; i < sizeof(pics) / sizeof(pics[0]); i++) {
		CHECK(load_pcx(packs, pics[i].id, &img) == 0);
		CHECK(img.w == pics[i].w && img.h == pics[i].h);
		image_free(&img);
	}
}

static void
test_era_maps(struct packs *packs)
{
	static const char *eras[] = {
		"patch/map1600", "patch/map1620", "patch/map1640",
		"patch/map1660", "patch/map1680",
	};
	struct image base;
	struct blob b;
	uint8_t *px;
	size_t i, n, changed, k;

	CHECK(load_pcx(packs, "pic/map1560", &base) == 0);
	if (base.pixels == NULL)
		return;
	n = (size_t)base.w * base.h;
	CHECK(n == 640 * 480);
	if ((px = malloc(n)) == NULL)
		return;
	for (i = 0; i < sizeof(eras) / sizeof(eras[0]); i++) {
		memcpy(px, base.pixels, n);
		CHECK(packs_load(packs, eras[i], &b) == 0);
		CHECK(rsd_apply(b.data, b.len, px, n) == 0);
		blob_free(&b);
		for (changed = 0, k = 0; k < n; k++)
			changed += px[k] != base.pixels[k];
		/* Each era changes a few hundred pixels of the 1560 map. */
		CHECK(changed > 100 && changed < 2000);
	}
	free(px);
	image_free(&base);
}

static void
test_other_ids(struct packs *packs)
{
	static const char *ids[] = {
		"program/main", "text/eng/cstr", "text/fre/cstn",
		"spr/tavern", "cur/ship0a", "grp/mybody", "img/cloud",
		"img/wpnsel", "tiles/map", "tiles15/sbtl", "font/pirate1",
		"ui/misc", "driver/asound",
	};
	struct blob b;
	size_t i;

	for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
		if (packs_load(packs, ids[i], &b) != 0) {
			fprintf(stderr, "missing %s: %s\n", ids[i],
			    packs_error(packs));
			failures++;
			continue;
		}
		CHECK(b.len > 0);
		blob_free(&b);
	}
	CHECK(packs_load(packs, "text/spa/cstr", &b) == -1);
}

static int
starts(const char *s, const char *prefix)
{
	return (strncmp(s, prefix, strlen(prefix)) == 0);
}

/* Decode one asset with the decoder its ID prefix calls for. */
static int
decode_any(const char *id, const struct blob *b)
{
	struct image img;
	struct sprite_bank bank;
	struct sprite_group grp;
	struct tileset ts;
	struct font font;
	struct strtab tab;
	struct strgroups groups;
	int rc = 0;

	if (starts(id, "pic/")) {
		if ((rc = pcx_decode(b->data, b->len, &img)) == 0)
			image_free(&img);
	} else if (starts(id, "spr/") || starts(id, "ui/")) {
		if ((rc = bank_decode_runs(b->data, b->len, &bank)) == 0)
			bank_free(&bank);
	} else if (starts(id, "cur/") || starts(id, "tiles15/")) {
		if ((rc = bank_decode_raw(b->data, b->len, &bank)) == 0)
			bank_free(&bank);
	} else if (starts(id, "grp/")) {
		if ((rc = group_decode(b->data, b->len, &grp)) == 0)
			group_free(&grp);
	} else if (starts(id, "img/")) {
		if ((rc = img_decode(b->data, b->len, &img)) == 0)
			image_free(&img);
	} else if (starts(id, "tiles/")) {
		if ((rc = tileset_decode(b->data, b->len, &ts)) == 0)
			tileset_free(&ts);
	} else if (strcmp(id, "font/caps") == 0) {
		if ((rc = caps_decode(b->data, b->len, &font)) == 0)
			font_free(&font);
	} else if (starts(id, "font/")) {
		if ((rc = font_decode(b->data, b->len, &font)) == 0)
			font_free(&font);
	} else if (starts(id, "text/") && strstr(id, "/cstr") != NULL) {
		if ((rc = cstr_decode(b->data, b->len, &tab)) == 0)
			strtab_free(&tab);
	} else if (starts(id, "text/")) {
		if ((rc = cstn_decode(b->data, b->len, &groups)) == 0)
			strgroups_free(&groups);
	}
	/* Patches are checked by test_era_maps; others are opaque. */
	return (rc);
}

static void
test_all_assets(struct packs *packs)
{
	char **ids;
	size_t n, i, decoded;
	struct blob b;

	CHECK(packs_list(packs, &ids, &n) == 0);
	for (decoded = 0, i = 0; i < n; i++) {
		if (packs_load(packs, ids[i], &b) != 0) {
			fprintf(stderr, "cannot load listed %s\n", ids[i]);
			failures++;
			continue;
		}
		if (decode_any(ids[i], &b) != 0) {
			fprintf(stderr, "cannot decode %s\n", ids[i]);
			failures++;
		} else
			decoded++;
		blob_free(&b);
	}
	printf("%zu assets listed, %zu loaded and decoded\n", n, decoded);
	/* 104 pictures alone; the CD holds about 250 asset files. */
	CHECK(n > 200);
	packs_list_free(ids, n);
}

static void
test_details(struct packs *packs)
{
	static const int heights[] = { 25, 15, 14, 30 };
	char id[32];
	struct blob b;
	struct sprite_bank bank;
	struct sprite_group grp;
	struct tileset ts;
	struct font font;
	struct strtab tab;
	struct strgroups groups;
	int i;

	CHECK(packs_load(packs, "spr/buysell", &b) == 0);
	CHECK(bank_decode_runs(b.data, b.len, &bank) == 0);
	CHECK(bank.count == 93);
	bank_free(&bank);
	blob_free(&b);

	CHECK(packs_load(packs, "cur/ship0a", &b) == 0);
	CHECK(bank_decode_raw(b.data, b.len, &bank) == 0);
	CHECK(bank.count == 8);	/* one frame per heading */
	bank_free(&bank);
	blob_free(&b);

	CHECK(packs_load(packs, "grp/mybody", &b) == 0);
	CHECK(group_decode(b.data, b.len, &grp) == 0);
	CHECK(grp.count == 16);
	group_free(&grp);
	blob_free(&b);

	CHECK(packs_load(packs, "tiles/map", &b) == 0);
	CHECK(tileset_decode(b.data, b.len, &ts) == 0);
	CHECK(ts.count == 256 && ts.tile_w == 16 && ts.has_palette);
	tileset_free(&ts);
	blob_free(&b);
	CHECK(packs_load(packs, "tiles/battle", &b) == 0);
	CHECK(tileset_decode(b.data, b.len, &ts) == 0);
	CHECK(ts.tile_w == 32 && ts.tile_h == 32);
	tileset_free(&ts);
	blob_free(&b);

	for (i = 0; i < 4; i++) {
		snprintf(id, sizeof(id), "font/pirate%d", i + 1);
		CHECK(packs_load(packs, id, &b) == 0);
		CHECK(font_decode(b.data, b.len, &font) == 0);
		CHECK(font.height == heights[i] && font.count == 143);
		font_free(&font);
		blob_free(&b);
	}
	CHECK(packs_load(packs, "font/caps", &b) == 0);
	CHECK(caps_decode(b.data, b.len, &font) == 0);
	CHECK(font.count == 33 && font.height == 34);
	font_free(&font);
	blob_free(&b);

	CHECK(packs_load(packs, "text/fre/cstr", &b) == 0);
	CHECK(cstr_decode(b.data, b.len, &tab) == 0);
	CHECK(tab.count == 322);
	strtab_free(&tab);
	blob_free(&b);

	CHECK(packs_load(packs, "text/eng/cstn", &b) == 0);
	CHECK(cstn_decode(b.data, b.len, &groups) == 0);
	CHECK(groups.count == 108);
	/* Group 8: ship types smallest first, then "Fort", " (damaged)". */
	CHECK(groups.groups[8].count == 11 &&
	    strcmp(groups.groups[8].str[0], "Pinnace") == 0 &&
	    strcmp(groups.groups[8].str[9], "Fort") == 0);
	/* Stored counts are wrong in two groups of the original. */
	CHECK(groups.declared[74] == 5 && groups.groups[74].count == 6);
	strgroups_free(&groups);
	blob_free(&b);
}

static const struct town *
find_town(const struct era_towns *era, const char *name)
{
	size_t i;

	for (i = 0; i < era->count; i++)
		if (strcmp(era->towns[i].name, name) == 0)
			return (&era->towns[i]);
	return (NULL);
}

static void
test_towns(struct packs *packs)
{
	static const size_t counts[EXE_ERAS] = { 34, 34, 40, 43, 43, 46 };
	/* Towns per nation (Spain, England, France, Holland) per era. */
	static const int nations[EXE_ERAS][4] = {
		{ 27, 2, 5, 0 }, { 30, 2, 2, 0 }, { 30, 4, 3, 3 },
		{ 28, 7, 5, 3 }, { 26, 8, 6, 3 }, { 25, 11, 7, 3 },
	};
	int per[4];
	const struct exe_layout *layout;
	const struct town *t;
	struct era_towns era;
	struct blob b;
	size_t i;
	int e;

	CHECK(packs_load(packs, "program/main", &b) == 0);
	layout = exe_identify(b.data, b.len);
	CHECK(layout != NULL);
	if (layout == NULL) {
		blob_free(&b);
		return;
	}
	for (e = 0; e < EXE_ERAS; e++) {
		CHECK(towns_load(layout, b.data, b.len, e, &era) == 0);
		CHECK(era.count == counts[e]);
		CHECK(strcmp(era.channel[0].name, "Florida Channel") == 0 &&
		    strcmp(era.channel[1].name, "Florida Channel") == 0);
		memset(per, 0, sizeof(per));
		for (i = 0; i < era.count; i++) {
			per[era.towns[i].nation]++;
			CHECK(era.towns[i].x < 640 && era.towns[i].y < 480);
			CHECK(era.towns[i].wealth <= 255);
		}
		CHECK(memcmp(per, nations[e], sizeof(per)) == 0);
	}
	CHECK(towns_load(layout, b.data, b.len, 4, &era) == 0);
	t = find_town(&era, "Havana");
	CHECK(t != NULL && t->x == 245 && t->y == 179 &&
	    t->nation == NATION_SPAIN && t->forts == 4);
	t = find_town(&era, "Jalapa");
	CHECK(t != NULL && (t->flags & TOWN_INLAND));
	t = find_town(&era, "Port Royale");
	CHECK(t != NULL && t->nation == NATION_ENGLAND &&
	    !(t->flags & TOWN_INLAND));
	blob_free(&b);
}

/* Every asset and virtual view opens and renders its first frame. */
static void
test_views(struct packs *packs)
{
	char **ids, err[256];
	size_t n, i;
	struct view *v;
	const struct rgb *img;
	uint8_t *a;
	size_t len;

	CHECK(packs_list(packs, &ids, &n) == 0);
	for (i = 0; i < n + view_virtual_count(); i++) {
		const char *id = i < n ? ids[i] : view_virtual_id(i - n);

		if ((v = view_open(packs, id, err, sizeof(err))) == NULL) {
			fprintf(stderr, "view %s: %s\n", id, err);
			failures++;
			continue;
		}
		if (view_render(v, view_start_frame(v), 0, &img) != 0 ||
		    img->w <= 0 || img->h <= 0) {
			fprintf(stderr, "view %s: cannot render\n", id);
			failures++;
		}
		view_close(v);
	}
	packs_list_free(ids, n);

	/* The sailing sea animates: two ticks differ around Havana. */
	v = view_open(packs, "world/1660", err, sizeof(err));
	CHECK(v != NULL);
	if (v == NULL)
		return;
	CHECK(view_render(v, view_start_frame(v), 0, &img) == 0);
	len = (size_t)img->w * img->h * 3;
	if ((a = malloc(len)) != NULL) {
		memcpy(a, img->px, len);
		CHECK(view_render(v, view_start_frame(v), 3, &img) == 0);
		CHECK(memcmp(a, img->px, len) != 0);
		/* After 32 passes every range is back where it started
		 * (208-215 turn every 4); not yet after 16. */
		CHECK(view_render(v, view_start_frame(v), 16, &img) == 0);
		CHECK(memcmp(a, img->px, len) != 0);
		CHECK(view_render(v, view_start_frame(v), 32, &img) == 0);
		CHECK(memcmp(a, img->px, len) == 0);
		free(a);
	}
	view_close(v);
}

static void
test_scenarios(struct packs *packs)
{
	const struct exe_layout *layout;
	struct scenario scn;
	struct blob b;
	int i, a, c, named = 0;

	CHECK(packs_load(packs, "program/main", &b) == 0);
	if ((layout = exe_identify(b.data, b.len)) == NULL) {
		blob_free(&b);
		return;
	}
	for (i = 0; i < SCENARIO_COUNT; i++) {
		CHECK(scenario_load(layout, b.data, b.len, i, &scn) == 0);
		named += scn.name[0] != '\0';
		for (a = 0; a < 4; a++)
			for (c = 0; c < 4; c++)
				CHECK(scn.relations[a][c] == scn.relations[c][a]);
	}
	CHECK(named == SCENARIO_COUNT - 1);	/* no Dutch career in 1560 */
	CHECK(scenario_load(layout, b.data, b.len,
	    scenario_career(0, SCN_HOLLAND), &scn) == 0 && scn.name[0] == '\0');
	CHECK(scenario_load(layout, b.data, b.len,
	    scenario_career(0, SCN_ENGLAND), &scn) == 0);
	CHECK(strcmp(scn.name, "English Seahawk") == 0);
	CHECK(scn.relations[NATION_SPAIN][NATION_ENGLAND] == -1 &&
	    scn.relations[NATION_SPAIN][NATION_FRANCE] == -1);
	CHECK(scenario_load(layout, b.data, b.len, SCENARIO_CAREERS, &scn) == 0);
	CHECK(strcmp(scn.name, "John Hawkins") == 0);
	CHECK(scenario_load(layout, b.data, b.len, SCENARIO_COUNT, &scn) == -1);
	blob_free(&b);
}

/*
 * Terrain per tile pixel: for every era, each pixel of each map and
 * battle tile has the executable's class of its colour; in 1660, the
 * world's terrain is that of the tile under each world pixel.
 */
static void
test_terrain(struct packs *packs)
{
	static struct world w;
	static const char *const ids[2] = { "tiles/map", "tiles/battle" };
	const struct exe_layout *layout;
	const uint8_t *classes, *terrain;
	struct tileset ts;
	struct blob b;
	char err[256];
	size_t i, bad;
	int era, k, x, y, tile, counts[4] = { 0 };

	CHECK(packs_load(packs, "program/main", &b) == 0);
	if ((layout = exe_identify(b.data, b.len)) == NULL ||
	    (classes = exe_at(layout, b.data, b.len, layout->colour_class_va,
	    256)) == NULL) {
		CHECK(!"colour classes");
		blob_free(&b);
		return;
	}
	for (era = 0; era < EXE_ERAS; era++) {
		CHECK(world_load(&w, packs, layout, b.data, b.len, era, err,
		    sizeof(err)) == 0);
		for (k = 0; k < 2; k++) {
			terrain = k == 0 ? w.map_terrain : w.battle_terrain;
			CHECK(tileset_load(packs, ids[k], &ts) == 0);
			bad = 0;
			for (i = 0; i < ts.count * (size_t)ts.tile_w * ts.tile_h;
			    i++)
				bad += terrain[i] != classes[ts.pixels[i]];
			CHECK(bad == 0);
			tileset_free(&ts);
		}
		/* Every world pixel, in 1660; the classes seen there. */
		if (era != 4)
			continue;
		bad = 0;
		for (y = 0; y < WORLD_PX_H; y++)
			for (x = 0; x < WORLD_PX_W; x++) {
				tile = w.map[(y / WORLD_TILE) * WORLD_CELLS_W +
				    x / WORLD_TILE];
				k = world_terrain(&w, x, y);
				bad += k != classes[w.tiles[tile * WORLD_TILE *
				    WORLD_TILE + (y % WORLD_TILE) * WORLD_TILE +
				    x % WORLD_TILE]];
				if (k >= 0 && k < 4)
					counts[k]++;
			}
		CHECK(bad == 0);
	}
	CHECK(counts[TERRAIN_SEA] > counts[TERRAIN_LAND] &&
	    counts[TERRAIN_LAND] > 0 && counts[TERRAIN_SHALLOWS] > 0);
	blob_free(&b);
}

int
main(void)
{
	const char *path;
	struct packs *packs;

	if ((path = getenv("STMALO_DATA")) == NULL || *path == '\0') {
		printf("STMALO_DATA not set, skipping\n");
		return (SKIP);
	}
	if ((packs = packs_new()) == NULL)
		return (1);
	if (packs_add_original(packs, path) != 0) {
		fprintf(stderr, "%s\n", packs_error(packs));
		packs_free(packs);
		return (1);
	}
	test_pictures(packs);
	test_era_maps(packs);
	test_other_ids(packs);
	test_all_assets(packs);
	test_details(packs);
	test_towns(packs);
	test_views(packs);
	test_scenarios(packs);
	test_terrain(packs);
	packs_free(packs);
	if (failures > 0) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return (1);
	}
	printf("all original data tests passed\n");
	return (0);
}
