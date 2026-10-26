/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Prison and being marooned: the "Prison" section of
 * aidd_docs/memory/internal/game-rules.md (VA 0x16647, 0x1f803,
 * 0x1be71, 0x1c056).
 */

#include <string.h>

#include "internal.h"

#define SPELL_DAYS	27
#define COAST_NONE	0x80

/* A map cell's shore side: 0x11, 0x22, 0x33, or 0. */
static int
coast_at(const struct world *w, int x, int y)
{
	int v;

	if (x < 0 || y < 0 || x >= WORLD_CELLS_W || y >= WORLD_CELLS_H)
		return (0);
	v = w->coast[w->map[y * WORLD_CELLS_W + x]];
	return (v == COAST_NONE ? 0 : v);
}

/*
 * The nearest shore (VA 0x1be71): rings of growing size around (px,
 * py), each read as the original does (its last two runs stay on one
 * row), then a step off the shore and a heading away from it.
 */
static void
put_ashore(struct sim *s, int px, int py)
{
	const struct world *w = s->world;
	int r = 1, n = 3, k, x = px, y = py, c = 0, h = 0;

	while (c == 0 && r < WORLD_CELLS_W) {
		x = px + r;
		y = py - r;
		for (k = 0; k < n && !(c = coast_at(w, x, y)); k++)
			x--;
		if (c == 0) {
			x = px - r;
			y = py + r;
			for (k = 0; k < n && !(c = coast_at(w, x, y)); k++)
				x++;
		}
		if (c == 0) {
			x = px - r;
			y = py - r + 1;
			for (k = 0; k < n - 2 && !(c = coast_at(w, x, y)); k++)
				x++;
		}
		if (c == 0) {
			x = px + r;
			y = py - r + 1;
			for (k = 0; k < n - 2 && !(c = coast_at(w, x, y)); k++)
				x++;
		}
		r++;
		n += 2;
	}
	if (c == 0x11) {
		x++;
		h = 0x40;
	} else if (c == 0x22) {
		y--;
		h = 0x80;
	} else if (c == 0x33) {
		x--;
		h = 0xC0;
	}
	s->fleet.x = x * WORLD_TILE + 8;
	s->fleet.y = y * WORLD_TILE + 8;
	s->fleet.fx = s->fleet.fy = 4;
	s->fleet.heading = (uint8_t)h;
}

/* Out of a town's prison (VA 0x1c056): onto its water, or for the
 * inland towns from a nudge toward the coast to the nearest shore. */
static void
leave_prison(struct sim *s, int town)
{
	static const struct {
		int	id, dx, dy;
	} inland[] = {
		{ 0x2d, 50, 0 }, { 0x33, 20, 0 }, { 0x2b, 0, -20 },
		{ 7, 0, 16 }, { 0x1e, 0, -20 }, { 0x31, 0, -10 },
	};
	const struct sim_town *t = &s->towns[town];
	int x = t->x, y = t->y;
	size_t i;

	if (t->sea_x != 0 && t->sea_y != 0) {
		sim_harbour_mouth(s, town);
		return;
	}
	for (i = 0; i < sizeof(inland) / sizeof(inland[0]); i++)
		if (inland[i].id == t->id) {
			x += inland[i].dx;
			y += inland[i].dy;
		}
	put_ashore(s, x, y);
}

int
sim_health(const struct sim *s)
{
	int h = (s->year - s->start_year + s->injuries) /
	    (3 + (s->player.ability == ABILITY_MEDICINE));

	return (h < 0 ? 0 : h > 3 ? 3 : h);
}

void
sim_imprison(struct sim *s, int cause, int town)
{
	struct sim_jail *out = &s->jail;
	int k;

	memset(out, 0, sizeof(*out));
	out->fresh = 1;
	out->cause = cause;
	out->town = town;
	if (cause == JAIL_TOWN || cause == JAIL_SHIP)
		s->injuries++;
	/* The career may end here (VA 0x1f1d7). */
	if (s->player.expedition >= 0) {
		out->end = 1;
		return;
	}
	if (sim_health(s) == 3) {
		out->end = 2;
		return;
	}
	s->player_town = SIM_NO_TOWN;
	s->fleet.ashore = 0;
	s->fleet.speed = 0;
	s->encounter.stage = ENC_NONE;
	s->battle.active = 0;
	if (s->world != NULL) {
		if ((cause == JAIL_TOWN || cause == JAIL_FORT) && town >= 0 &&
		    (size_t)town < s->ntowns)
			leave_prison(s, town);
		else if (cause == JAIL_MAROONED)
			put_ashore(s, s->fleet.x / WORLD_TILE,
			    s->fleet.y / WORLD_TILE);
	}
	/* By a town's water as when leaving it: not straight back in. */
	s->fleet.leaving = (cause == JAIL_TOWN || cause == JAIL_FORT) ?
	    town : -1;
	/* The months behind bars: the crew stays quiet. */
	s->months = -16;
	out->spells = s->reputation / 2 + 7;
	if (out->spells < 0)
		out->spells = 0;
	for (k = 0; k < out->spells; k++)
		sim_advance(s, SPELL_DAYS);
	sim_fresh_crew(s, 0);
	s->mission.type = MISSION_NONE;
	out->health = sim_health(s);
	if (s->player.difficulty > 0 && s->player.difficulty < 4)
		s->player.difficulty--;
}
