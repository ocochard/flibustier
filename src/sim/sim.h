/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_SIM_H
#define STMALO_SIM_H

#include <stddef.h>
#include <stdint.h>

#include "assets/scenario.h"
#include "assets/towns.h"
#include "rng.h"

/*
 * Simulation core: the whole game state and the rules that change it.
 * No I/O, no clock, no raylib: time only moves through sim_advance(), and
 * randomness only comes from the state's own generator, so a seed plus a
 * command log always replays to the same state.  The rules follow
 * aidd_docs/memory/internal/game-rules.md.
 */

#define SIM_NEWS_MAX	256	/* most recent news kept in the state */
#define SIM_NO_TOWN	(-1)

/* News codes: the entries of the game's news list (CSTN group 20). */
enum news_code {
	NEWS_AMNESTY = 10,	/* a: nation */
	NEWS_INDIANS = 11,	/* a: town */
	NEWS_GOLD_MINE = 12,
	NEWS_MALARIA = 13,
	NEWS_GOVERNOR = 14,
	NEWS_PLUNDER = 15,
	NEWS_WAR = 16,		/* a, b: nations */
	NEWS_PEACE = 17,
	NEWS_ALLIANCE = 18,
	NEWS_CAPTURE = 19	/* a: nation, b: town */
};

struct sim_town {
	char		name[17];
	uint16_t	flags;
	uint16_t	x, y;
	uint16_t	nation;
	uint16_t	forts;
	uint16_t	soldiers;
	uint16_t	citizens;
	uint16_t	gold;
	uint16_t	wealth;
	uint16_t	standing;	/* field 9, reset by a new governor */
	uint16_t	governor;	/* field 11 */
	uint16_t	id;
};

struct sim_news {
	int32_t		day;
	int		code;		/* enum news_code */
	int		a, b;
};

struct sim {
	struct rng	rng;
	int		era_year;	/* era of the towns, 1560..1680 */
	int		year;		/* calendar year */
	int		day_of_year;	/* 0..359: twelve 30-day months */
	int		month;		/* month passed to world updates */
	int32_t		day;		/* days since the start, for news */
	int		diplomacy_range; /* unknown original value */
	int		player_town;	/* town the player is in, or -1 */
	int8_t		relations[4][4];
	int8_t		standing[4];	/* player standing per nation */
	size_t		ntowns;
	struct sim_town	towns[TOWN_MAX];
	size_t		nnews;		/* total logged, may exceed the ring */
	struct sim_news	news[SIM_NEWS_MAX];
};

/* Placeholder until the original's value is traced. */
#define SIM_DIPLOMACY_RANGE	100

void	sim_new_game(struct sim *s, const struct era_towns *era,
	    const struct scenario *scn, uint64_t seed);

/*
 * Let "days" pass: one world update per 30-day month boundary, and the
 * year turns every 360 days.
 */
void	sim_advance(struct sim *s, int32_t days);

/* One world update; exposed for tests. */
void	sim_world_update(struct sim *s);

/* News number i (0 = oldest still kept), or NULL. */
const struct sim_news *sim_news_at(const struct sim *s, size_t i);
size_t	sim_news_kept(const struct sim *s);

#endif
