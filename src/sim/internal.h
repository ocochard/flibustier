/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_SIM_INTERNAL_H
#define STMALO_SIM_INTERNAL_H

#include "sim.h"

/* Helpers shared by the simulation's rule modules; not public API. */

/* floor(sine(heading) x v / 256), from the game's half-circle table. */
int	sim_sine_mul(const struct world *w, int heading, int v);

/* a / b, or 0 when a < b or b == 0, as the game's div16 helper. */
int	sim_div16(int a, int b);

/*
 * Compass bearing (0 north, 64 east, 256 per turn) of the vector
 * (dx, dy), y growing southward, with the game's integer approximation
 * (VA 0x1ae99).
 */
int	sim_bearing(int dx, int dy);

/* Move the reputation, a signed byte. */
static inline void
sim_reputation(struct sim *s, int d)
{
	int r = s->reputation + d;

	s->reputation = r < -128 ? -128 : r > 127 ? 127 : r;
}

/* A ship's room in the hold, in tens of tons (VA 0xdb7b, 0x1f2a1). */
static inline int
sim_ship_room(int type)
{
	return ((type & 0xF) * 2 + 2);
}

#define SCENE_ALLEY	0	/* sword fight pictures (VA 0x3361c) */

/*
 * A fresh start (VA 0x1ef87): the gold (the fortune less a tenth when
 * none) by 10, the flagship alone and repaired, its guns, 15 food, no
 * cargo, reputation + 16 men, months 0.
 */
void	sim_fresh_crew(struct sim *s, long gold);

/* Out of a town onto its water (VA 0x19b9e). */
void	sim_harbour_mouth(struct sim *s, int town);

/* A new named captain's id of "nation" (VA 0xd1fc), and the captured
 * ship's captain held or made a hostage (VA 0xdb7b). */
int	sim_name_captain(struct sim *s, int nation);
void	sim_capture_captain(struct sim *s);

/* The villains' month (VA 0xcba7). */
void	sim_villains_move(struct sim *s);

/* Add an entry to the log (news codes as CSTN 20). */
void	sim_log_news(struct sim *s, int code, int a, int b);

#endif
