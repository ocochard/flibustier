/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Land battles, before an assault on foot: the "Land battles" section
 * of aidd_docs/memory/internal/game-rules.md (VA 0x5a80, 0x5d40, 0x72ac).
 */

#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define CELL		32	/* a land battle cell, in pixels */
#define FIELD		481	/* a unit past this is off the field */
#define MORALE_MAX	79
#define CLOSE		32	/* closer than this, units fight */
#define SIGHT		31	/* units in cover are seen from this near */
#define LONG_IDLE	999	/* passes without a fight: the attack fails */

/* Ground at field pixel (x, y), GROUND_* (VA 0x7147, 0x7197). */
static int
terrain(const struct sim *s, int x, int y)
{
	const struct sim_land *l = &s->land;

	if (x < 0 || y < 0 || x >= BATTLE_SCREEN * CELL ||
	    y >= BATTLE_SCREEN * CELL)
		return (GROUND_CLEAR);
	return (s->world->land_ground[s->world->land_screens[l->screen]
	    [(y / CELL) * BATTLE_SCREEN + x / CELL]]);
}

static int
type(const struct land_unit *u)
{
	return (u->flags & 7);
}

int
sim_land_battle_needed(const struct sim *s, int town)
{
	const struct sim_town *t;

	if (town < 0 || (size_t)town >= s->ntowns)
		return (0);
	t = &s->towns[town];
	return (s->fleet.crew <= t->soldiers * 15 && t->forts != 0 &&
	    t->soldiers > 9);
}

void
sim_land_start(struct sim *s, int town)
{
	struct sim_land *l = &s->land;
	const struct sim_town *t;
	struct land_unit *u;
	char used[48];
	int men, enemy, forts, i, k, d, fx, fy, px, py;

	if (town < 0 || (size_t)town >= s->ntowns || s->world == NULL)
		return;
	t = &s->towns[town];
	memset(l, 0, sizeof(*l));
	memset(used, 0, sizeof(used));
	l->active = 1;
	l->town = town;
	l->screen = t->id < LAND_SCREENS ? t->id : 0;
	fx = t->land_x * CELL;
	fy = t->land_y * CELL;
	px = t->party_x * CELL;
	py = t->party_y * CELL;
	men = s->fleet.crew;
	enemy = t->garrison * 128 + t->soldiers * 10 + 5;
	forts = t->forts;
	/* The party: half the crew, then the rest, halved again above 100;
	 * the second and third a cell to either side (VA 0x72ac). */
	for (i = 1; i <= 3; i++) {
		u = &l->u[i];
		u->men = i == 1 ? (men + 1) >> 1 : men > 100 && i != 3 ?
		    men >> 1 : men;
		if (u->men == 0)
			continue;
		d = i == 2 ? 1 : i == 3 ? -1 : 0;
		u->x = px - d * CELL;
		u->y = py - d * CELL;
		u->muskets = rng_below(&s->rng, 6) + 10;
		u->morale = MORALE_MAX;
		u->flags = LAND_PLAYER;
		men -= u->men;
	}
	/* The town: halves of its men, the first by the fort (at half
	 * strength) when there are forts, horsemen without muskets as the
	 * last otherwise; the others on the clear cells nearest the town. */
	for (i = 7; i >= 4; i--) {
		u = &l->u[i];
		u->men = enemy >> 1;
		if (i == 4 || u->men < 50)
			u->men = enemy;
		if (u->men == 0)
			continue;
		u->muskets = (s->player.difficulty + 1) * 2 +
		    rng_below(&s->rng, 6);
		u->flags = LAND_SOLDIERS;
		if (forts == 0) {
			if (i == 4) {
				u->flags = LAND_HORSE;
				u->muskets = 0;
			}
		} else {
			u->flags = LAND_FORT;
			forts = 0;
			u->men >>= 1;
			l->fort = i;
		}
		if (type(u) == LAND_FORT) {
			u->x = fx;
			u->y = fy;
		} else
			for (k = 0; k < 48; k++) {
				d = ((k & 7) + 1) >> 1;
				u->x = fx + (k & 1 ? -d : d) * CELL;
				d = (((k >> 3) & 7) + 1) >> 1;
				u->y = fy + ((k >> 3) & 1 ? -d : d) * CELL;
				if (terrain(s, u->x, u->y) == 0 && !used[k]) {
					used[k] = 1;
					break;
				}
			}
		u->morale = MORALE_MAX;
		enemy -= u->men;
	}
}

/* The distance the game uses: the longer side plus half the shorter. */
static int
distance(const struct land_unit *a, const struct land_unit *b)
{
	int dx = abs(a->x - b->x), dy = abs(a->y - b->y);

	return (dx < dy ? dx / 2 + dy : dy / 2 + dx);
}

/* Every 16 passes, staggered, a unit fights its nearest foe (VA 0x664c)
 * and, for the town, chooses where to go. */
static int
fight(struct sim *s, int i, int engaged)
{
	struct sim_land *l = &s->land;
	struct land_unit *me = &l->u[i], *it;
	int raw, best = 0, bestd = 9999, j, d, v, kills, tt, q, ev = 0;

	raw = me->men * me->morale / 80;
	if (type(me) == LAND_HORSE)
		raw <<= 2;
	for (j = i < 4 ? 4 : 1; j <= (i < 4 ? 7 : 3); j++) {
		it = &l->u[j];
		if (it->flags == 0)
			continue;
		d = distance(me, it);
		if (!(it->flags & LAND_EXPOSED) && d > SIGHT)
			d = 9999;
		if (d < bestd) {
			bestd = d;
			best = j;
		}
	}
	if (i == l->selected && best != 0)
		l->shown = best;
	it = &l->u[best];
	if (best != 0 && bestd < CLOSE) {
		/* Men and morale fight; the muskets weigh only in the
		 * town's choice below.  Each fight sounds at once (VA
		 * 0x5a80): the player's men, or the town's. */
		engaged = -1;
		l->idle = 0;
		ev |= LEV_FIRE;
		l->sound = i < 4 ? FM_OUR_MEN : FM_THEIR_MEN;
		bestd = SIGHT;
		v = raw << 5;
		tt = terrain(s, it->x, it->y);
		if (tt == 1)
			v >>= 1;
		if (tt == 3)
			v <<= 1;
		if (type(it) == LAND_FORT) {	/* the fort is reached */
			v >>= 2;
			l->result = 2;
		}
		kills = rng_below(&s->rng, (v + 16) / bestd);
		it->men -= (kills >> 1) + 1;
		it->men = it->men < 0 ? 0 : it->men > 999 ? 999 : it->men;
		v = it->men == 0 ? 0 : it->morale - (kills << 7) / it->men;
		it->morale = v < 0 ? 0 : v > 255 ? 255 : v;
		if (it->morale == 0)
			it->men -= it->men >> 3;
		if (best == l->selected)
			ev |= LEV_SELECTED;
		if (it->men == 0 && type(it) != LAND_FORT)
			it->flags = 0;
	}
	if (i >= 4) {
		me->tx = 0;
		if (best != 0) {
			if (type(me) == LAND_FORT) {
				l->rally_x = me->x;
				l->rally_y = me->y;
				l->rally_unit = i;
			} else {
				d = type(me) == LAND_HORSE ? 2 : 1;
				v = type(me) == LAND_HORSE ? me->men << 1 :
				    me->men;
				if (it->men * 2 < v)
					d = -1;
				if (type(me) != LAND_HORSE) {
					q = ((me->men * me->muskets) >> 4) *
					    me->morale / 65;
					if (terrain(s, it->x, it->y) == 1)
						q >>= 1;
					if (q < (it->men * it->muskets) >> 4)
						d = 2;
				}
				if (!l->ahead)
					d = 99;
				if (d < bestd >> 4) {
					me->tx = it->x;
					me->ty = it->y;
				} else if (d != bestd >> 4) {
					me->tx = l->rally_x;
					me->ty = l->rally_y;
					me->flags = l->rally_unit << 4 |
					    (me->flags & 0x8f);
				}
			}
		}
	}
	/* Shaken and close: away from the foe. */
	if (best != 0 && me->morale < 16 && bestd < CLOSE &&
	    type(me) != LAND_FORT) {
		me->tx = me->x * 2 - it->x;
		me->ty = me->y * 2 - it->y;
		me->flags &= 7;
	}
	if (engaged < 0)
		me->flags |= LAND_EXPOSED;
	return (ev);
}

int
sim_land_tick(struct sim *s, int dx, int dy, int button)
{
	struct sim_land *l = &s->land;
	struct land_unit *u;
	int i, k, t, sp, ev = 0, engaged, mine = 0, open = 0, theirs = 0;
	int a;

	if (!l->active || l->result != 0 || s->world == NULL)
		return (0);
	l->sound = 0;
	if (l->pass == 0) {			/* VA 0x7104 */
		l->selected = 1;
		l->shown = 4;
		l->rally_x = l->u[4].x;
		l->rally_y = l->u[4].y;
	}
	/* Every 4 passes, the count (VA 0x5d80). */
	if ((l->pass & 3) == 0) {
		for (i = 1; i < LAND_UNITS; i++) {
			if (l->u[i].flags == 0)
				continue;
			if (i < 4) {
				mine += l->u[i].men;
				if (l->u[i].flags & LAND_EXPOSED)
					open += l->u[i].men;
			} else
				theirs += l->u[i].men;
		}
		if (theirs == 0)
			l->result = 2;
		if (mine < 2)
			l->result = 3;
		if ((l->pass & 0x3f) == 0) {
			l->ahead = theirs - open / 2 > 0;
			if (l->idle > LONG_IDLE)
				l->result = 3;
		}
	}
	l->pass++;
	l->idle++;
	dx = dx < 0 ? -1 : dx > 0 ? 1 : 0;
	dy = dy < 0 ? -1 : dy > 0 ? 1 : 0;
	/* A new press selects the next unit of the party. */
	if (!l->held && button) {
		for (k = 0; k < 7; k++) {
			l->selected = (l->selected + 1) & 3;
			if (l->selected == 0)
				l->selected = 1;
			if (l->u[l->selected].flags != 0)
				break;
		}
		ev |= LEV_SELECTED;
		l->held = 1;
	}
	if (l->held)
		l->held = button;
	/* The fort reached by the selected unit (VA 0x7885). */
	if (l->fort != 0 && abs(l->u[l->selected].x - l->u[l->fort].x) < CLOSE &&
	    abs(l->u[l->selected].y - l->u[l->fort].y) < CLOSE)
		l->result = 2;
	for (i = 1; i < LAND_UNITS; i++) {
		u = &l->u[i];
		if (u->flags == 0)
			continue;
		t = terrain(s, u->x, u->y);
		if (t != 1)
			u->flags |= LAND_EXPOSED;
		k = t + 1;
		if (i >= 4)
			k &= 3;
		sp = (9 / (k + 2)) * 2;
		if (t == 0)
			sp += u->flags & 4;
		if (u->morale > 16)
			sp += sp / 2;
		engaged = 0;
		if ((i == l->selected) + button - (u->tx != 0) - (i & 4) < 1) {
			if (u->tx != 0) {
				a = sim_bearing(u->tx - u->x, u->ty - u->y);
				u->x += sim_sine_mul(s->world, a, sp);
				u->y -= sim_sine_mul(s->world, a + 0x40, sp);
				if (abs(u->tx - u->x) + abs(u->ty - u->y) < 8) {
					u->tx = 0;
					if (u->flags & 0x70) {	/* joins a unit */
						k = (u->flags >> 4) & 7;
						l->u[k].men += u->men;
						u->men = 0;
						u->flags = 0;
						continue;
					}
				}
			}
		} else {
			u->x += sp * dx / 2;
			u->y += sp * dy / 2;
			engaged = abs(dx) + abs(dy);
		}
		if (u->x < 0 || u->y < 0 || u->x >= FIELD || u->y >= FIELD) {
			u->flags = 0;		/* off the field */
			continue;
		}
		if (type(u) == LAND_FORT)
			u->flags = LAND_FORT | LAND_EXPOSED;
		if ((int)(l->pass & 7) == i) {
			u->flags &= 0x7f;
			u->morale += 3;
			if (u->morale > MORALE_MAX)
				u->morale = MORALE_MAX;
		}
		if ((int)(l->pass & 15) == i * 2)
			ev |= fight(s, i, engaged);
	}
	return (ev);
}

int
sim_land_end(struct sim *s)
{
	struct sim_land *l = &s->land;
	int i, mine = 0, theirs = 0;

	if (!l->active)
		return (0);
	l->active = 0;
	/* Every unit counts, those that left the field too (VA 0x6be0). */
	for (i = 1; i < LAND_UNITS; i++)
		*(i < 4 ? &mine : &theirs) += l->u[i].men;
	s->fleet.crew = mine < 1 ? 1 : mine;
	s->towns[l->town].soldiers = (uint16_t)(theirs / 10);
	s->heat += (int)(l->pass / 99);
	/* Beaten with no one left: prison (VA 0x18d35). */
	if (l->result != 2 && s->fleet.crew < 2)
		sim_imprison(s, JAIL_TOWN, l->town);
	return (l->result == 2);
}

void
sim_log_failure(struct sim *s, int town)
{
	sim_log_news(s, NEWS_NOT_TAKEN, town, 0);
}
