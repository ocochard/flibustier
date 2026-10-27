/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Treasure maps and digging: the "Treasure maps" section of
 * aidd_docs/memory/internal/game-rules.md (VA 0x1f08a, 0xa801).
 */

#include <stdlib.h>

#include "internal.h"

#define SEARCH_DAYS	3
#define LOST_GOLD	100000L
#define RELATIVES	4

/* The town nearest a cell by |dx| + |dy| (VA 0x1adfc, which walks a
 * list of town cells kept at VA 0x4ca58). */
static int
town_nearest(const struct sim *s, int x, int y)
{
	int best = 0, bestd = -1, d;
	size_t i;

	for (i = 0; i < s->ntowns; i++) {
		d = abs(s->towns[i].x - x) + abs(s->towns[i].y - y);
		if (bestd < 0 || d < bestd) {
			best = (int)i;
			bestd = d;
		}
	}
	return (best);
}

void
sim_map_piece(struct sim *s, int map)
{
	struct sim_map *m;
	int k, x, y, town, bit, tries = 0;

	if (map < 0 || map >= SIM_MAPS)
		return;
	m = &s->maps[map];
	if (map == MAP_LOST)
		m->value = (int16_t)s->relative;
	if (m->pieces == 0 && s->world != NULL && s->ntowns > 0) {
		/* Any spot but near the player's town or another map's
		 * town (an empty map's last town counts too).  The original
		 * draws for ever; the engine takes the 2048th draw. */
		for (;;) {
			k = rng_below(&s->rng, TREASURE_SPOTS);
			x = s->world->treasure[k][0];
			y = s->world->treasure[k][1];
			town = town_nearest(s, x, y);
			if (++tries >= 8 * TREASURE_SPOTS ||
			    (town != s->player_town &&
			    town != s->maps[MAP_PIRATE].town &&
			    town != s->maps[MAP_RELATIVE].town &&
			    town != s->maps[MAP_LOST].town))
				break;
		}
		m->x = (int16_t)x;
		m->y = (int16_t)y;
		m->town = (int16_t)town;
	}
	if ((m->pieces & MAP_WHOLE) != MAP_WHOLE) {
		do
			bit = 8 >> rng_below(&s->rng, 4);
		while (m->pieces & bit);
		m->pieces = (uint16_t)(m->pieces | bit);
	}
	if (s->player.difficulty == 0)
		m->pieces = MAP_WHOLE;
	m->pieces |= MAP_SHOWN;
}

int
sim_search(struct sim *s, struct sim_find found[SIM_MAPS])
{
	struct sim_map *m;
	int cx = s->fleet.x / WORLD_TILE, cy = s->fleet.y / WORLD_TILE;
	int i, j, k, n = 0;

	sim_advance(s, SEARCH_DAYS);
	for (i = 0; i < SIM_MAPS; i++) {
		m = &s->maps[i];
		if (m->pieces == 0 || abs(cx - m->x) >= 2 ||
		    abs(cy - m->y) >= 2)
			continue;
		found[n].map = i;
		found[n].gold = 0;
		found[n].value = 0;
		if (i == MAP_PIRATE) {
			found[n].gold = (rng_below(&s->rng, 9) + 5) * 1000L;
			sim_log_news(s, NEWS_TREASURE, 0, 0);
		} else if (i == MAP_RELATIVE) {
			/* The relative tells of a lost treasure: its map's
			 * first piece. */
			found[n].value = s->relative;
			sim_map_piece(s, MAP_LOST);
			sim_log_news(s, NEWS_RESCUED, s->relative, 0);
			if (s->relative < RELATIVES)
				s->relative++;
			/* The villains' trail goes cold: every slot freed,
			 * no towns behind them (VA 0xa801). */
			for (k = 0; k < SIM_VILLAINS; k++) {
				s->villains[k].active = 0;
				for (j = 0; j < 4; j++)
					s->villains[k].trail[j] = -1;
			}
		} else {
			found[n].gold = LOST_GOLD;
			found[n].value = m->value;
			sim_log_news(s, NEWS_LOST_TREASURE, m->value, 0);
		}
		s->fleet.gold += found[n].gold;
		m->pieces = 0;
		n++;
	}
	return (n);
}
