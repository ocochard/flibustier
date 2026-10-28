/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * flibustier-seamap: print an era's sailing world, one character per map
 * cell, for the career bot's route planning: '.' open sea over the whole
 * cell, ',' sea at its centre only, '#' land, '1' shallows.
 *
 * With --pixels, the pixels instead: first the 256 map tiles, one line
 * of 16 x 16 characters each ('.' sea, '1' shallows, '#' land), then one
 * line per map row of two hex digits per cell, the tile it draws, then
 * one line per ship class of the speeds by heading to the wind (16
 * sectors).
 *
 * With --arrivals, the cells where a ship reaches a town instead, one
 * "town x y" line each.
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
	int era = -1, x, y, i, j, t, c, u, pixels = 0, arrivals = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--data") == 0 && i + 1 < argc)
			data = argv[++i];
		else if (strcmp(argv[i], "--pixels") == 0)
			pixels = 1;
		else if (strcmp(argv[i], "--arrivals") == 0)
			arrivals = 1;
		else
			for (j = 0; j < 6; j++)
				if (atoi(argv[i]) == years[j])
					era = j;
	}
	if (era < 0 || data == NULL) {
		fprintf(stderr, "usage: flibustier-seamap [--data PATH] [--pixels | --arrivals] YEAR\n");
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
	if (arrivals) {
		for (i = 0; i < (int)w.narrival; i++)
			printf("%d %d %d\n", w.arrival[i].town, w.arrival[i].x,
			    w.arrival[i].y);
		blob_free(&b);
		packs_free(packs);
		return (0);
	}
	if (pixels) {
		for (t = 0; t < 256; t++) {
			for (i = 0; i < WORLD_TILE * WORLD_TILE; i++) {
				u = w.map_terrain[t * WORLD_TILE * WORLD_TILE + i];
				putchar(u == TERRAIN_SEA ? '.' :
				    u == TERRAIN_LAND ? '#' : '1');
			}
			putchar('\n');
		}
		for (y = 0; y < WORLD_CELLS_H; y++) {
			for (x = 0; x < WORLD_CELLS_W; x++)
				printf("%02x", w.map[y * WORLD_CELLS_W + x]);
			putchar('\n');
		}
		for (t = 0; t < SHIP_CLASSES; t++) {
			for (i = 0; i < WIND_SECTORS; i++)
				printf("%d%c", w.speed[t][i],
				    i == WIND_SECTORS - 1 ? '\n' : ' ');
		}
		blob_free(&b);
		packs_free(packs);
		return (0);
	}
	for (y = 0; y < WORLD_CELLS_H; y++) {
		for (x = 0; x < WORLD_CELLS_W; x++) {
			t = TERRAIN_SEA;
			for (j = 0; j < WORLD_TILE; j++)
				for (i = 0; i < WORLD_TILE; i++) {
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
