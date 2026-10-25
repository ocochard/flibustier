/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_WORLD_H
#define STMALO_WORLD_H

#include <stdint.h>

#include "data/exe.h"
#include "data/pack.h"

#define WORLD_CELLS_W	640
#define WORLD_CELLS_H	480
#define WORLD_TILE	16
#define WORLD_PX_W	(WORLD_CELLS_W * WORLD_TILE)
#define WORLD_PX_H	(WORLD_CELLS_H * WORLD_TILE)
#define SHIP_CLASSES	8
#define WIND_SECTORS	16
#define ENCOUNTER_NATIONS	4
#define ENCOUNTER_SHIPS		5
#define ENCOUNTER_LEN		7
#define BATTLE_AI_CASES		27
#define BATTLE_TYPES		10	/* 9 ship types and the fort */
#define BATTLE_LEVELS		5

/* Terrain classes of map pixel colours. */
enum terrain {
	TERRAIN_SHALLOWS = 1,
	TERRAIN_LAND = 2,
	TERRAIN_SEA = 3
};

/*
 * Everything sailing needs about an era's world: the map (one tile number
 * per cell), the tiles' pixels, and tables from the executable.  Built
 * once from the data and never saved: a state only names its era.
 */
struct world {
	int		era;			/* 0..5 */
	uint8_t		map[WORLD_CELLS_W * WORLD_CELLS_H];
	uint8_t		tiles[256 * WORLD_TILE * WORLD_TILE];
	uint8_t		palette[256 * 3];	/* the map tiles' palette */
	uint8_t		colour_class[256];
	uint8_t		sine[128];
	int8_t		speed[SHIP_CLASSES][WIND_SECTORS];
	/* Ships met at sea: per nation, 5 records of type, nation, crew,
	 * guns, role and two bytes not decoded (game-rules.md). */
	uint8_t		encounter[ENCOUNTER_NATIONS][ENCOUNTER_SHIPS]
			    [ENCOUNTER_LEN];
	/* Sea battles: the enemy's decision per strength comparison, and
	 * hit boxes (half sizes per ship type; widening per difficulty). */
	uint8_t		battle_ai[BATTLE_AI_CASES];
	int16_t		half_w[BATTLE_TYPES];
	int16_t		half_h[BATTLE_TYPES];
	int16_t		hit_steps[BATTLE_LEVELS];
};

/* Returns 0, or -1 with a message in "err". */
int	world_load(struct world *w, struct packs *packs,
	    const struct exe_layout *layout, const uint8_t *exe, size_t len,
	    int era, char *err, size_t errlen);

/* Terrain class of the world pixel (x, y); land outside the world. */
int	world_terrain(const struct world *w, int x, int y);

#endif
