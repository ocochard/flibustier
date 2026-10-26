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
