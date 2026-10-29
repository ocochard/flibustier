/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data/fsys.h"

#include "tiled.h"
#include "towns.h"
#include "world.h"
#include "worldfiles.h"

static int
write_file(const char *dir, const char *name, const struct jbuf *b,
    char *err, size_t errlen)
{
	char path[1024];
	FILE *fp;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if (b->err || (fp = fopen(path, "wb")) == NULL) {
		snprintf(err, errlen, "cannot write %s", path);
		return (-1);
	}
	fwrite(b->s, 1, b->len, fp);
	if (fclose(fp) != 0) {
		snprintf(err, errlen, "cannot write %s", path);
		return (-1);
	}
	return (0);
}

/* Write "b" as file "name", then empty it. */
static int
flush(const char *dir, const char *name, struct jbuf *b, char *err,
    size_t errlen)
{
	int rc = write_file(dir, name, b, err, errlen);

	jb_free(b);
	return (rc);
}

/*
 * The terrain of every pixel of a tileset's 256 tiles of "side" pixels,
 * for tools drawing replacement tiles that keep it: a PGM of 16 x 16
 * tiles, grey 85 for shallows, 170 for land, 255 for sea (TERRAIN_*
 * times 85).
 */
static int
write_terrain(const char *dir, const char *name, const uint8_t *terrain,
    int side, char *err, size_t errlen)
{
	char path[1024];
	FILE *fp;
	int t, x, y;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if ((fp = fopen(path, "wb")) == NULL) {
		snprintf(err, errlen, "cannot write %s", path);
		return (-1);
	}
	fprintf(fp, "P5\n%d %d\n255\n", 16 * side, 16 * side);
	for (y = 0; y < 16 * side; y++)
		for (x = 0; x < 16 * side; x++) {
			t = y / side * 16 + x / side;
			fputc(85 * terrain[(t * side + y % side) * side + x %
			    side], fp);
		}
	if (fclose(fp) != 0) {
		snprintf(err, errlen, "cannot write %s", path);
		return (-1);
	}
	return (0);
}

int
world_export(struct packs *packs, const char *dir, char *err, size_t errlen)
{
	const struct exe_layout *layout;
	struct world *w;
	struct era_towns towns;
	struct jbuf b = { 0 };
	struct blob exe;
	char name[64];
	int era, rc = -1, i, k;

	if (fsys_mkdir(dir) != 0 && errno != EEXIST) {
		snprintf(err, errlen, "cannot create %s", dir);
		return (-1);
	}
	if (packs_load(packs, "program/main", &exe) != 0 ||
	    (layout = exe_identify(exe.data, exe.len)) == NULL) {
		blob_free(&exe);
		snprintf(err, errlen, "missing or unknown PIRATESG.EXE");
		return (-1);
	}
	if ((w = calloc(1, sizeof(*w))) == NULL) {
		blob_free(&exe);
		snprintf(err, errlen, "out of memory");
		return (-1);
	}
	for (era = 0; era < EXE_ERAS; era++) {
		if (world_load(w, packs, layout, exe.data, exe.len, era, err,
		    errlen) != 0)
			goto out;
		tiled_write(&b, WORLD_CELLS_W, WORLD_CELLS_H, WORLD_TILE,
		    WORLD_TILE, 1, w->map, "map", "map.tsj");
		snprintf(name, sizeof(name), "map-%d.tmj", town_era_years[era]);
		if (flush(dir, name, &b, err, errlen) != 0)
			goto out;
	}
	/* The battle screens are the same in every era. */
	tiled_write(&b, BATTLE_SCREEN, BATTLE_SCREEN, BATTLE_TILE, BATTLE_TILE,
	    BATTLE_SCREENS, &w->battle_screens[0][0], "screen", "battle.tsj");
	if (flush(dir, "sea-screens.tmj", &b, err, errlen) != 0)
		goto out;
	tiled_write(&b, BATTLE_SCREEN, BATTLE_SCREEN, BATTLE_TILE, BATTLE_TILE,
	    LAND_SCREENS, &w->land_screens[0][0], "screen", "battle.tsj");
	if (flush(dir, "land-screens.tmj", &b, err, errlen) != 0)
		goto out;
	tiled_write_tileset(&b, "map", WORLD_TILE, 256, "map.png",
	    16 * WORLD_TILE, 16 * WORLD_TILE);
	if (flush(dir, "map.tsj", &b, err, errlen) != 0)
		goto out;
	tiled_write_tileset(&b, "battle", BATTLE_TILE, 256, "battle.png",
	    16 * BATTLE_TILE, 16 * BATTLE_TILE);
	if (flush(dir, "battle.tsj", &b, err, errlen) != 0)
		goto out;
	if (write_terrain(dir, "map-terrain.pgm", w->map_terrain, WORLD_TILE,
	    err, errlen) != 0 || write_terrain(dir, "battle-terrain.pgm",
	    w->battle_terrain, BATTLE_TILE, err, errlen) != 0)
		goto out;
	/* Towns per era, treasure spots, battle sets. */
	for (era = 0; era < EXE_ERAS; era++) {
		if (towns_for_era(packs, layout, exe.data, exe.len, era, &towns,
		    err, errlen) != 0)
			goto out;
		towns_encode(&towns, &b);
		snprintf(name, sizeof(name), "towns-%d.json",
		    town_era_years[era]);
		if (flush(dir, name, &b, err, errlen) != 0)
			goto out;
	}
	jb_raw(&b, "{\"spots\": [");
	for (i = 0; i < TREASURE_SPOTS; i++) {
		jb_raw(&b, i == 0 ? "\n  [" : i % 8 == 0 ? ",\n  [" : ", [");
		jb_int(&b, w->treasure[i][0]);
		jb_raw(&b, ", ");
		jb_int(&b, w->treasure[i][1]);
		jb_raw(&b, "]");
	}
	jb_raw(&b, "\n]}\n");
	if (flush(dir, "treasure.json", &b, err, errlen) != 0)
		goto out;
	for (k = 0; k < BATTLE_SETS; k++) {
		jb_raw(&b, k == 0 ? "{\"" : ",\n \"");
		jb_raw(&b, battle_set_names[k]);
		jb_raw(&b, "\": [");
		for (i = 0; i < BATTLE_PIECES; i++) {
			if (i > 0)
				jb_raw(&b, ", ");
			jb_int(&b, w->battle_sets[k][i]);
		}
		jb_raw(&b, "]");
	}
	jb_raw(&b, "}\n");
	if (flush(dir, "battle-sets.json", &b, err, errlen) != 0)
		goto out;
	jb_raw(&b, "# Exported from the original Pirates! Gold data: for local\n"
	    "# checks only, do not distribute.\n[pack]\n"
	    "name = World exported from the original\n"
	    "license = proprietary\n\n[assets]\n");
	for (era = 0; era < EXE_ERAS; era++) {
		snprintf(name, sizeof(name), "map/%d = map-%d.tmj\n",
		    town_era_years[era], town_era_years[era]);
		jb_raw(&b, name);
	}
	jb_raw(&b, "screens/sea = sea-screens.tmj\n"
	    "screens/land = land-screens.tmj\n"
	    "data/treasure = treasure.json\n"
	    "data/battle-sets = battle-sets.json\n");
	for (era = 0; era < EXE_ERAS; era++) {
		snprintf(name, sizeof(name), "data/towns/%d = towns-%d.json\n",
		    town_era_years[era], town_era_years[era]);
		jb_raw(&b, name);
	}
	if (flush(dir, "pack.ini", &b, err, errlen) != 0)
		goto out;
	rc = 0;
out:
	jb_free(&b);
	free(w);
	blob_free(&exe);
	return (rc);
}
