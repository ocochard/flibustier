/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim.h"

#define DAYS_PER_MONTH		30
#define DAYS_PER_YEAR		360
#define CONQUEST_RANGE		150	/* Manhattan distance, map cells */
#define DIPLOMACY_EVENTS	28	/* draws below this change relations */
#define EVENT_DRAW		64	/* town event draw; >= towns: none */
#define GOVERNOR_FIXED		0x40	/* field 11: governor never replaced */
#define FLAG_EVENT_NEW		0x80	/* moved to FLAG_EVENT_OLD by an event */
#define FLAG_EVENT_OLD		0x20
#define STANDING_FLOOR		(-3)	/* after an amnesty */
#define TICKS_PER_DAY		64
#define WIND_TICKS		128	/* the wind may shift this often */
#define WIND_START		0xC0	/* blowing west: the trade winds */
#define SPEED_MIN		4
#define SPEED_MAX		12
#define EDGE_MIN		0x41	/* playable world, in pixels */
#define EDGE_MAX_X		0x27bf
#define EDGE_MAX_Y		0x1dbf
#define ARRIVAL_CELLS		1	/* a town counts as reached within */

enum town_event {
	EV_INDIANS,
	EV_GOLD_MINE,
	EV_MALARIA,
	EV_GOVERNOR,
	EV_PLUNDER,
	EV_COUNT
};

static void
log_news(struct sim *s, int code, int a, int b)
{
	struct sim_news *n = &s->news[s->nnews % SIM_NEWS_MAX];

	n->day = s->day;
	n->code = code;
	n->a = a;
	n->b = b;
	s->nnews++;
}

size_t
sim_news_kept(const struct sim *s)
{
	return (s->nnews < SIM_NEWS_MAX ? s->nnews : SIM_NEWS_MAX);
}

const struct sim_news *
sim_news_at(const struct sim *s, size_t i)
{
	size_t first;

	if (i >= sim_news_kept(s))
		return (NULL);
	first = s->nnews - sim_news_kept(s);
	return (&s->news[(first + i) % SIM_NEWS_MAX]);
}

/* Nearest town within "reach" cells of the fleet, or -1. */
static int
town_near(const struct sim *s, int reach)
{
	int cx = s->fleet.x / WORLD_TILE, cy = s->fleet.y / WORLD_TILE;
	int best = -1, bestd = 0, d;
	size_t i;

	for (i = 0; i < s->ntowns; i++) {
		if (abs(s->towns[i].x - cx) > reach ||
		    abs(s->towns[i].y - cy) > reach)
			continue;
		d = abs(s->towns[i].x - cx) + abs(s->towns[i].y - cy);
		if (best < 0 || d < bestd) {
			best = (int)i;
			bestd = d;
		}
	}
	return (best);
}

void
sim_new_game(struct sim *s, const struct era_towns *era,
    const struct scenario *scn, uint64_t seed)
{
	const struct town *t;
	size_t i;

	memset(s, 0, sizeof(*s));
	rng_seed(&s->rng, seed, 0x5354414c4f554fULL);
	s->era_year = era->year;
	s->year = era->year;
	s->diplomacy_range = SIM_DIPLOMACY_RANGE;
	s->player_town = SIM_NO_TOWN;
	s->player.expedition = -1;
	snprintf(s->player.title, sizeof(s->player.title), "%s", scn->name);
	memcpy(s->relations, scn->relations, sizeof(s->relations));
	s->fleet.nships = scn->nships > 0 ? scn->nships : 1;
	memcpy(s->fleet.ships, scn->ships, sizeof(s->fleet.ships));
	s->fleet.crew = scn->crew;
	s->fleet.guns = scn->guns;
	s->fleet.gold = scn->gold;
	{
		int k = rng_below(&s->rng, SCENARIO_STARTS);

		s->fleet.x = scn->start[k][0] * WORLD_TILE;
		s->fleet.y = scn->start[k][1] * WORLD_TILE;
	}
	s->fleet.leaving = -1;
	s->wind = WIND_START;
	s->ntowns = era->count;
	for (i = 0; i < era->count; i++) {
		t = &era->towns[i];
		memcpy(s->towns[i].name, t->name, sizeof(s->towns[i].name));
		s->towns[i].flags = (uint16_t)t->flags;
		s->towns[i].x = (uint16_t)t->x;
		s->towns[i].y = (uint16_t)t->y;
		s->towns[i].nation = (uint16_t)t->nation;
		s->towns[i].forts = (uint16_t)t->forts;
		s->towns[i].soldiers = (uint16_t)t->soldiers;
		s->towns[i].citizens = (uint16_t)t->citizens;
		s->towns[i].gold = (uint16_t)t->gold;
		s->towns[i].wealth = (uint16_t)t->wealth;
		s->towns[i].standing = t->raw[9];
		s->towns[i].governor = t->raw[11];
		s->towns[i].id = (uint16_t)t->id;
		s->towns[i].sea_x = t->raw[26];
		s->towns[i].sea_y = t->raw[27];
	}
	/* The start position is in a town; the voyage starts on the water
	 * outside its harbour. */
	s->fleet.leaving = town_near(s, ARRIVAL_CELLS + 3);
	if (s->fleet.leaving >= 0 && s->towns[s->fleet.leaving].sea_x != 0) {
		s->fleet.x = s->towns[s->fleet.leaving].sea_x * WORLD_TILE + 8;
		s->fleet.y = s->towns[s->fleet.leaving].sea_y * WORLD_TILE + 8;
	}
}

static void
new_governor(struct sim *s, struct sim_town *t)
{
	t->standing = 0;
	if (!(t->governor & GOVERNOR_FIXED))
		t->governor = (uint16_t)rng_below(&s->rng, 16);
}

static void
town_growth(struct sim *s)
{
	struct sim_town *t;
	size_t i;
	int r, income, drift;

	for (i = 0; i < s->ntowns; i++) {
		t = &s->towns[i];
		if (rng_below(&s->rng, 4) != 0)
			continue;
		t->wealth = (uint16_t)(t->wealth +
		    (rng_below(&s->rng, 255 - t->wealth) >> 5));
		r = rng_below(&s->rng, t->wealth);
		t->citizens = (uint16_t)(t->citizens +
		    (((uint32_t)s->month * 24 + (uint32_t)r) >> 8 & 0xFF));
		income = (uint16_t)((uint32_t)r * t->citizens) >> 9;
		if (t->nation == NATION_SPAIN)
			income >>= 1;
		t->gold = (uint16_t)(t->gold + income);
		drift = (int16_t)((income + t->citizens) / 2 - t->soldiers);
		if (drift >= 1 && t->soldiers <= 254)
			t->soldiers++;
		else if (drift < 0 && t->soldiers > 0)
			t->soldiers--;
	}
}

/* Draw a town, avoiding the player's town when there is a choice. */
static int
draw_target(struct sim *s)
{
	int b;

	do
		b = rng_below(&s->rng, (int)s->ntowns);
	while (b == s->player_town && s->ntowns > 1);
	return (b);
}

static void
conquest(struct sim *s)
{
	struct sim_town *a, *b;
	int ia, ib, dist;

	if (s->ntowns == 0)
		return;
	ia = rng_below(&s->rng, (int)s->ntowns);
	ib = draw_target(s);
	a = &s->towns[ia];
	b = &s->towns[ib];
	if (s->relations[a->nation & 3][b->nation & 3] != -1 ||
	    (uint32_t)b->soldiers * 2 >= a->soldiers)
		return;
	dist = abs((int)a->x - (int)b->x) + abs((int)a->y - (int)b->y);
	if (dist >= CONQUEST_RANGE || ib == s->player_town)
		return;
	b->nation = a->nation;
	new_governor(s, b);
	log_news(s, NEWS_CAPTURE, a->nation, ib);
}

static void
diplomacy(struct sim *s)
{
	int r, a, b, step, rel;

	r = rng_below(&s->rng, s->diplomacy_range);
	if (r >= DIPLOMACY_EVENTS)
		return;
	a = r & 3;
	b = r >> 2 & 3;
	step = (r & 16) ? 1 : -1;
	if (a == b || s->relations[a][b] == step)
		return;
	rel = s->relations[a][b] + step;
	s->relations[a][b] = s->relations[b][a] = (int8_t)rel;
	if (rel == 0 && step == -1)
		return;		/* an alliance lapsing into peace is silent */
	log_news(s, NEWS_PEACE + rel, a, b);
	if (rel > 0) {
		log_news(s, NEWS_AMNESTY, a, 0);
		if (s->standing[a] < STANDING_FLOOR)
			s->standing[a] = STANDING_FLOOR;
	}
}

static void
town_event(struct sim *s)
{
	struct sim_town *t;
	int it, e;

	it = rng_below(&s->rng, EVENT_DRAW);
	if (it >= (int)s->ntowns)
		return;
	t = &s->towns[it];
	do
		e = rng_below(&s->rng, EV_COUNT);
	while (e == EV_GOVERNOR && it == s->player_town);
	log_news(s, NEWS_INDIANS + e, it, 0);
	if (t->flags & FLAG_EVENT_NEW)
		t->flags |= FLAG_EVENT_OLD;
	else
		t->flags &= (uint16_t)~FLAG_EVENT_OLD;
	t->flags &= (uint16_t)~FLAG_EVENT_NEW;
	switch (e) {
	case EV_INDIANS:
		t->soldiers >>= 1;
		break;
	case EV_GOLD_MINE:
		t->wealth = t->wealth + 64 > 254 ? 255 : t->wealth + 64;
		t->gold = t->gold + 12 > 254 ? 255 : t->gold + 12;
		break;
	case EV_MALARIA:
		t->soldiers >>= 1;
		t->citizens >>= 1;
		break;
	case EV_GOVERNOR:
		new_governor(s, t);
		break;
	case EV_PLUNDER:
		t->gold = 0;
		t->wealth = (uint16_t)(t->wealth - (t->wealth >> 2));
		break;
	}
}

void
sim_world_update(struct sim *s)
{
	town_growth(s);
	conquest(s);
	diplomacy(s);
	town_event(s);
	/* Treasure fleets are not specified yet; see game-rules.md. */
}

/*
 * One step of at most a month, as the game handles time: each month
 * boundary crossed runs a world update with the new month number (which
 * reaches 12 at the year end), then the day of the year wraps once.
 * News is dated at the boundary that produced it.
 */
static void
advance_step(struct sim *s, int days)
{
	int32_t start_day = s->day;
	int start_doy = s->day_of_year, m;

	s->day_of_year += days;
	for (m = start_doy / DAYS_PER_MONTH + 1;
	    m <= s->day_of_year / DAYS_PER_MONTH; m++) {
		s->month = m;
		s->day = start_day + (m * DAYS_PER_MONTH - start_doy);
		sim_world_update(s);
	}
	s->day = start_day + days;
	if (s->day_of_year >= DAYS_PER_YEAR) {
		s->day_of_year -= DAYS_PER_YEAR;
		s->year++;
	}
	s->month = s->day_of_year / DAYS_PER_MONTH;
}

void
sim_advance(struct sim *s, int32_t days)
{
	int step;

	while (days > 0) {
		step = days > DAYS_PER_MONTH ? DAYS_PER_MONTH : (int)days;
		advance_step(s, step);
		days -= step;
	}
}

/* Signed sine of a heading, from the game's half-circle table. */
static int
sine(const struct world *w, int heading)
{
	heading &= 0xFF;
	return (heading < 0x80 ? w->sine[heading] : -w->sine[heading - 0x80]);
}

/* Division by 256 rounding down, also for negative values. */
static int
div256(int v)
{
	return (v >= 0 ? v / 256 : -((-v + 255) / 256));
}

static int
ship_class(const struct sim_fleet *f)
{
	int i, c = 0;

	for (i = 0; i < f->nships && i < SCENARIO_SHIPS; i++)
		if ((f->ships[i] & 7) > c)
			c = f->ships[i] & 7;
	return (c);
}

/* Speed the ship heads for on its course, before the 4..12 clamp. */
static int
target_speed(const struct sim *s)
{
	int sector, d, bonus;

	sector = ((s->fleet.heading + 8 - s->wind) >> 4) & 15;
	d = s->player.difficulty - (s->player.ability == ABILITY_NAVIGATION);
	d = d < 0 ? 0 : d > 4 ? 4 : d;
	bonus = (5 - d) >> 1;
	return ((s->world->speed[ship_class(&s->fleet)][sector] + bonus) * 3);
}

static void
shift_wind(struct sim *s)
{
	int w = s->wind >> 4, r = rng_below(&s->rng, 8);

	/* An arithmetic shift of a small signed value: floor(x / 2). */
	s->wind = (uint8_t)(s->wind + 16 * ((r - w + 10 >= 0) ?
	    (r - w + 10) / 2 : -((-(r - w + 10) + 1) / 2)));
}

/* Cell distance (the larger axis) from the fleet to a town: to the town
 * itself or to the water outside its harbour, whichever is nearer. */
static int
town_distance(const struct sim *s, const struct sim_town *t)
{
	int cx = s->fleet.x / WORLD_TILE, cy = s->fleet.y / WORLD_TILE;
	int d, dx, dy;

	dx = abs(t->x - cx);
	dy = abs(t->y - cy);
	d = dx > dy ? dx : dy;
	if (t->sea_x != 0) {
		dx = abs(t->sea_x - cx);
		dy = abs(t->sea_y - cy);
		if ((dx > dy ? dx : dy) < d)
			d = dx > dy ? dx : dy;
	}
	return (d);
}

/* Town the fleet has reached, other than the one it is leaving; -1 if
 * none.  The departure town counts again once the fleet is clear. */
static int
town_at(struct sim *s)
{
	size_t i;
	int near = -1, d;

	for (i = 0; i < s->ntowns; i++) {
		d = town_distance(s, &s->towns[i]);
		if ((int)i == s->fleet.leaving) {
			if (d > ARRIVAL_CELLS + 1)
				s->fleet.leaving = -1;	/* clear of it */
			continue;
		}
		if (d <= ARRIVAL_CELLS && near < 0)
			near = (int)i;
	}
	return (near);
}

enum sail_stop
sim_sail(struct sim *s, int turn, int ticks, int *town)
{
	struct sim_fleet *f = &s->fleet;
	int32_t x8, y8, nx, ny;
	int target, step, t;

	*town = -1;
	if (s->world == NULL)
		return (SAIL_TICKS);
	turn = turn < 0 ? -1 : turn > 0 ? 1 : 0;
	while (ticks-- > 0) {
		f->ticks++;
		if (turn != 0 && (f->ticks & 4) == 0)
			f->heading = (uint8_t)(f->heading + 16 * turn);
		target = target_speed(s);
		step = target > f->speed ? 2 : target < f->speed ? -2 : 0;
		f->speed += step;
		f->speed = f->speed < SPEED_MIN ? SPEED_MIN :
		    f->speed > SPEED_MAX ? SPEED_MAX : f->speed;
		x8 = f->x * 8 + f->fx + div256(sine(s->world, f->heading) *
		    f->speed);
		y8 = f->y * 8 + f->fy - div256(sine(s->world, f->heading +
		    0x40) * f->speed);
		nx = x8 >> 3;
		ny = y8 >> 3;
		/* Bounce back into the world at its edges. */
		if (nx > EDGE_MAX_X || nx < EDGE_MIN || ny > EDGE_MAX_Y ||
		    ny < EDGE_MIN) {
			f->heading = nx > EDGE_MAX_X ? 0xC0 : nx < EDGE_MIN ?
			    0x40 : ny > EDGE_MAX_Y ? 0x00 : 0x80;
			x8 = (nx > EDGE_MAX_X ? EDGE_MAX_X : nx < EDGE_MIN ?
			    EDGE_MIN : nx) * 8;
			y8 = (ny > EDGE_MAX_Y ? EDGE_MAX_Y : ny < EDGE_MIN ?
			    EDGE_MIN : ny) * 8;
			nx = x8 >> 3;
			ny = y8 >> 3;
		}
		/* Time passes whether or not the ship can move. */
		if (f->ticks % WIND_TICKS == 0)
			shift_wind(s);
		if (f->ticks % TICKS_PER_DAY == 0)
			sim_advance(s, 1);
		/* Ships never sail onto land. */
		if (world_terrain(s->world, nx, ny) == TERRAIN_LAND) {
			f->speed = 0;
			return (SAIL_LAND);
		}
		f->x = nx;
		f->y = ny;
		f->fx = (uint8_t)(x8 & 7);
		f->fy = (uint8_t)(y8 & 7);
		if ((t = town_at(s)) >= 0) {
			*town = t;
			f->speed = 0;
			f->leaving = t;
			return (SAIL_TOWN);
		}
	}
	return (SAIL_TICKS);
}
