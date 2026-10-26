/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * The crew's mood: the "Crew's mood" section of
 * aidd_docs/memory/internal/game-rules.md (VA 0x1da9b, 0x1dd0e, 0x17efb).
 */

#include <math.h>
#include <string.h>

#include "hof.h"
#include "internal.h"

#define SCENE_DECK	3

int
sim_crew_mood(const struct sim *s)
{
	int crew = s->fleet.crew > 0 ? s->fleet.crew : 1;
	float share = (float)(s->fleet.gold / 10) / (float)crew;

	return ((int)(sqrt((double)share) * 2.0) - s->months + 4 -
	    s->player.difficulty);
}

void
sim_crew_leave(struct sim *s, int n)
{
	struct sim_fleet *f = &s->fleet;

	if (f->crew <= 0)
		return;
	if (n >= f->crew)
		n = f->crew - 1;
	f->gold -= (long)n * f->gold / f->crew;
	f->crew -= n;
}

void
sim_mutiny_duel(struct sim *s, int weapon)
{
	struct sword_setup in;

	memset(&in, 0, sizeof(in));
	in.kind = SWORD_DUEL;
	in.you_men = in.foe_men = 1;
	in.you_factor = in.foe_factor = 9;
	in.title = 2;				/* Mutineer */
	in.nation = 5;
	in.extra = 1;
	in.weapon = weapon;
	sim_sword_start(s, &in);
	s->sword.scene = SCENE_DECK;
}

void
sim_mutiny_end(struct sim *s)
{
	sim_duel_end(s);
	sim_reputation(s, -4);
}

void
sim_career_end(const struct sim *s, struct career_end *out)
{
	int years = s->year - s->start_year, k;
	double e, h;

	memset(out, 0, sizeof(*out));
	for (k = 0; k < 4; k++)
		out->ranks += s->rank[k];
	out->points = s->fortune / 10 + (long)s->acres * 50 +
	    (long)out->ranks * 500 + (long)s->reputation * 200 +
	    (long)(s->married + s->relative - s->injuries) * 1000;
	out->score = (int)((double)out->points / 330.0 * (double)(25 -
	    years) / 20.0);
	out->rating = hof_rating(out->score);
	out->member = out->ranks * 5 / 2 + 4;
	if (out->member > 11)
		out->member = 11;
	k = (int)sqrt(((double)s->acres * 100.0 + (double)s->fortune) /
	    500.0);
	out->wealth = (k > 6 ? 6 : k) + 2;
	k = s->reputation / 4 + 1;
	out->repute = (k < 0 ? 0 : k > 6 ? 6 : k) + 2;
	/* Family, wounds and the years make the old captain's mood. */
	e = s->relative / 8.0 + (s->married / 8.0 + 1.0) - s->injuries / 6.0;
	h = e * ((double)(25 - years) / 20.0);
	if (h < -1.0)
		h = -1.0;
	if (s->player.expedition >= 0)
		h = 2.0;
	k = (int)(h * 3.0);
	out->mood = (k < 0 ? 0 : k > 5 ? 5 : k) + 1;
	out->age = years + 25;
}

int
sim_crew_check(const struct sim *s)
{
	const struct sim_fleet *f = &s->fleet;

	if (f->nships > 0 && f->crew < f->nships * 8)
		return (f->nships == 1 ? CHECK_LAST : CHECK_ABANDON);
	if (sim_hold_space(s) < 0)
		return (CHECK_HOLD);
	return (CHECK_OK);
}

void
sim_abandon_ship(struct sim *s, int ship)
{
	if (s->fleet.nships > 1)
		sim_drop_ship(s, ship, 1);
}

void
sim_abandon_last(struct sim *s)
{
	/* In a town reached by sea the fort's path (VA 0x1f2a1 with
	 * from_fort), elsewhere marooned. */
	if (s->player_town >= 0 && !s->fleet.ashore)
		sim_imprison(s, JAIL_FORT, s->player_town);
	else
		sim_imprison(s, JAIL_MAROONED, -1);
}
