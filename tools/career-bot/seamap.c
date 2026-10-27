/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * flibustier-seamap: print an era's sailing world, one character per map
 * cell, for the career bot's route planning: '.' open sea over the whole
 * cell, ',' sea at its centre only, '#' land, '1' shallows.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/world.h"
#include "data/exe.h"
#include "data/pack.h"

static const int years[] = { 1560, 1600, 1620, 1640, 1660, 1680 };

int
main(int argc, char **argv)
{
	static struct world w;
	struct packs *packs;
	struct blob b;
	const struct exe_layout *layout;
	const char *data = getenv("FLIBUSTIER_DATA");
	char err[256];
	int era = -1, x, y, i, j, t, c, u;

	if (argc == 4 && strcmp(argv[1], "--data") == 0) {
		data = argv[2];
		argv += 2;
		argc -= 2;
	}
	for (i = 0; argc == 2 && i < 6; i++)
		if (atoi(argv[1]) == years[i])
			era = i;
	if (era < 0 || data == NULL) {
		fprintf(stderr, "usage: flibustier-seamap [--data PATH] YEAR\n");
		return (2);
	}
	if ((packs = packs_new()) == NULL ||
	    packs_add_original(packs, data) != 0 ||
	    packs_load(packs, "program/main", &b) != 0) {
		fprintf(stderr, "%s\n", packs != NULL ? packs_error(packs) :
		    "out of memory");
		return (1);
	}
	if ((layout = exe_identify(b.data, b.len)) == NULL ||
	    world_load(&w, packs, layout, b.data, b.len, era, err,
	    sizeof(err)) != 0) {
		fprintf(stderr, "%s\n", layout == NULL ?
		    "unknown PIRATESG.EXE build" : err);
		return (1);
	}
	for (y = 0; y < WORLD_CELLS_H; y++) {
		for (x = 0; x < WORLD_CELLS_W; x++) {
			t = TERRAIN_SEA;
			for (j = 1; j < WORLD_TILE; j += 7)
				for (i = 1; i < WORLD_TILE; i += 7) {
					u = world_terrain(&w, x * WORLD_TILE + i,
					    y * WORLD_TILE + j);
					if (u != TERRAIN_SEA)
						t = u;
				}
			c = world_terrain(&w, x * WORLD_TILE + WORLD_TILE / 2,
			    y * WORLD_TILE + WORLD_TILE / 2);
			putchar(t == TERRAIN_SEA ? '.' : c == TERRAIN_SEA ? ',' :
			    c == TERRAIN_LAND ? '#' : '1');
		}
		putchar('\n');
	}
	blob_free(&b);
	packs_free(packs);
	return (0);
}
