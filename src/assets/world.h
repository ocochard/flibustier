/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_WORLD_H
#define FLIBUSTIER_WORLD_H

#include <stdint.h>

#include "data/exe.h"
#include "data/pack.h"
#include "datafields.h"
#include "towns.h"

#define WORLD_CELLS_W	640
#define WORLD_CELLS_H	480
#define WORLD_TILE	16
#define TREASURE_SPOTS	256	/* where treasure is buried */
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
#define SHOT_STEPS		16	/* a shot's arc, start to fall */
#define SWORD_MOVES		16
#define SWORD_FRAMES		6	/* table stride per move */
#define SWORD_WEAPONS		3	/* rapier, longsword, cutlass */
#define SWORD_FLAGS		6
#define SWORD_SCRIPTS		8
#define SWORD_STEPS		8
#define SWORD_BANKS		3

/* Sword fight tables (see "Sword fights" in game-rules.md). */
struct sword_tables {
	int16_t		flag_y[SWORD_FLAGS];	/* flag height by bar / 8 */
	int16_t		frames[SWORD_MOVES];	/* frames per move */
	uint8_t		reach_level[BATTLE_LEVELS];
	uint8_t		reach_weapon[SWORD_WEAPONS];
	uint8_t		foe_reach[SWORD_WEAPONS];
	int16_t		delay[4];		/* timer units per tick */
	int16_t		player_edge, foe_edge;
	uint8_t		hit[BATTLE_LEVELS];	/* hit thresholds */
	uint8_t		foe_hit[BATTLE_LEVELS];
	uint8_t		far_move[12];		/* input remap at a distance */
	uint8_t		once[SWORD_MOVES];	/* input taken once */
	uint8_t		guard[SWORD_MOVES * SWORD_FRAMES];
	uint8_t		strike[SWORD_MOVES * SWORD_FRAMES];
	uint8_t		step_in[SWORD_MOVES * SWORD_FRAMES];
	uint8_t		step_back[SWORD_MOVES * SWORD_FRAMES];
	uint8_t		far_ai[8];		/* the opponent's far moves */
	uint8_t		caution[BATTLE_LEVELS];
	uint8_t		area[SWORD_MOVES];	/* body area a strike hits */
	uint8_t		scripts[SWORD_BANKS][SWORD_SCRIPTS][SWORD_STEPS];
	uint8_t		line[SWORD_MOVES];	/* the line of a move */
};

#define BATTLE_TILE	32
#define BATTLE_SCREENS	108	/* SBTL.BMP: 15 x 15 battle tiles each */
#define BATTLE_SCREEN	15
#define BATTLE_SETS	5	/* coast north, east, south, west; open */
#define BATTLE_PIECES	5	/* four along the coast; open sea */
#define LAND_SCREENS	61	/* LBTL.BMP: land battle screens */

/* Terrain classes of map pixel colours. */
enum terrain {
	TERRAIN_SHALLOWS = 1,
	TERRAIN_LAND = 2,
	TERRAIN_SEA = 3
};

/* Ground of a land battle tile. */
enum {
	GROUND_CLEAR,
	GROUND_COVER,
	GROUND_SWAMP,
	GROUND_WATER
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
	/*
	 * Terrain (TERRAIN_*) of every pixel of every map tile, and of the
	 * 32 x 32 tiles sea battles draw (the colour under a ship gives
	 * its terrain, VA 0x8200).  For the original data, the executable's
	 * class of each tile pixel's colour.
	 */
	uint8_t		map_terrain[256 * WORLD_TILE * WORLD_TILE];
	uint8_t		battle_terrain[256 * BATTLE_TILE * BATTLE_TILE];
	/* The battle screens, and which ones each set offers (VA
	 * 0x33fd0). */
	uint8_t		battle_screens[BATTLE_SCREENS][BATTLE_SCREEN *
			    BATTLE_SCREEN];
	uint8_t		battle_sets[BATTLE_SETS][BATTLE_PIECES];
	/* Land battles: their screens, and each tile's ground (GROUND_*;
	 * for the original data the executable's code at VA 0x3235c,
	 * grouped as VA 0x7197 does). */
	uint8_t		land_screens[LAND_SCREENS][BATTLE_SCREEN *
			    BATTLE_SCREEN];
	uint8_t		land_ground[256];
	int16_t		treasure[TREASURE_SPOTS][2];	/* x, y cells */
	/* The cells where the era's towns are reached, in the game's
	 * order (struct era_towns, arrival). */
	struct town_cell arrival[TOWN_CELLS];
	size_t		narrival;
	uint8_t		coast[256];	/* per map tile (VA 0x33b18) */
	uint8_t		sine[128];
	int8_t		speed[SHIP_CLASSES][WIND_SECTORS];
	/* Ships met at sea: per nation, 5 records of type, nation, crew,
	 * guns, role, and the loot's gold and goods factors (game-rules.md,
	 * "Speed table" for the era block). */
	uint8_t		encounter[ENCOUNTER_NATIONS][ENCOUNTER_SHIPS]
			    [ENCOUNTER_LEN];
	/* The treasure galleon met off the treasure fleet's town (the
	 * record after them, VA 0x47112). */
	uint8_t		treasure_ship[ENCOUNTER_LEN];
	/* Era block bytes 0xF8, 0xF9 (VA 0x4707e, 0x4707f): a diplomatic
	 * event comes when rand(range) < 28; a Spanish shipwright serves
	 * up to "rank x 25 + tolerance" in town wealth. */
	uint8_t		diplomacy_range;
	uint8_t		wright_tolerance;
	/* Sea battles: the enemy's decision per strength comparison, and
	 * hit boxes (half sizes per ship type; widening per difficulty). */
	uint8_t		battle_ai[BATTLE_AI_CASES];
	int16_t		half_w[BATTLE_TYPES];
	int16_t		half_h[BATTLE_TYPES];
	int16_t		hit_steps[BATTLE_LEVELS];
	/* A shot's arc, by step (VA 0x8405): the balls' height above
	 * their shadow (0 or less), and the spread frame 0-3. */
	int16_t		shot_rise[SHOT_STEPS];
	int16_t		shot_frame[SHOT_STEPS];
	/* The bow cross's offset by heading / 16 (VA 0xf903), x and y. */
	int16_t		bow_mark[16 * 2];
	struct sword_tables sword;
};

/*
 * Open world data, read when a pack gives it (JSON):
 *	data/treasure		{ "spots": [ [x, y], ... ] }, TREASURE_SPOTS
 *				cells of the world map
 *	data/battle-sets	{ "north": [5 screens], "east": [...],
 *				"south": [...], "west": [...], "open": [...] }:
 *				the sea battle screens offered by a coast that
 *				way, or in open sea
 * Names of the sets, in battle_sets order.
 */
extern const char *const battle_set_names[BATTLE_SETS];
/* data/sword: the fields of struct sword_tables. */
extern const struct data_field sword_fields[];
extern const size_t sword_nfields;

/* Returns 0, or -1 with a message in "err". */
int	world_load(struct world *w, struct packs *packs,
	    const struct exe_layout *layout, const uint8_t *exe, size_t len,
	    int era, char *err, size_t errlen);

/* Terrain class of the world pixel (x, y); land outside the world. */
int	world_terrain(const struct world *w, int x, int y);

/* The town reached on map cell (x, y), or -1. */
int	world_arrival(const struct world *w, int x, int y);

/* The town of the arrival cell nearest map cell (x, y) by |dx| + |dy|,
 * or -1 with none. */
int	world_nearest_town(const struct world *w, int x, int y);

#endif
