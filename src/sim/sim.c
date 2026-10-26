/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "internal.h"

#define DAYS_PER_MONTH		30
#define DAYS_PER_YEAR		360
#define CONQUEST_RANGE		150	/* Manhattan distance, map cells */
#define DIPLOMACY_EVENTS	28	/* draws below this change relations */
#define EVENT_DRAW		64	/* town event draw; >= towns: none */
#define GOVERNOR_FIXED		0x40	/* field 11: governor never replaced */
#define FLAG_KNOWN		0x80	/* the player has current information */
#define FLAG_OBSOLETE		0x20	/* an event made it out of date */
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
#define SIGHTING_TICKS		32	/* how often a sail may appear */
#define HEAT_IGNORED		16	/* added when an encounter is left */
#define FAR_FROM_TOWNS		12	/* cells: ships from any nation */

enum town_event {
	EV_INDIANS,
	EV_GOLD_MINE,
	EV_MALARIA,
	EV_GOVERNOR,
	EV_PLUNDER,
	EV_COUNT
};

#define log_news	sim_log_news

void
sim_log_news(struct sim *s, int code, int a, int b)
{
	struct sim_news *n = &s->news[s->nnews % SIM_NEWS_MAX];

	n->day = s->day;
	n->date = s->year * DAYS_PER_YEAR + s->day_of_year;
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

/*
 * Out of a town onto its water (VA 0x19b9e): the cell of fields 26 and
 * 27, heading field 28 x 32, which points away from the town.
 */
void
sim_harbour_mouth(struct sim *s, int town)
{
	const struct sim_town *t = &s->towns[town];

	if (t->sea_x == 0)
		return;
	s->fleet.x = t->sea_x * WORLD_TILE + 8;
	s->fleet.y = t->sea_y * WORLD_TILE + 8;
	s->fleet.heading = (uint8_t)((t->sea_heading & 7) * 32);
	s->fleet.speed = 0;
}

/*
 * The career starts on day rand(150) + 65 of the era's year (VA
 * 0x132f8), and its log already holds the news of the year so far (VA
 * 0x13679): for each pair of nations, at war or allied, that; at peace,
 * a random town event (codes 11-15).  Six entries, a sixth of the time
 * since New Year apart, the first half a step in.
 */
void
sim_restore_towns(struct sim *s, const struct era_towns *era)
{
	static const struct {
		size_t	off;
		int	raw;
	} later[] = {
		{ offsetof(struct sim_town, barmaid), 13 },
		{ offsetof(struct sim_town, garrison), 10 },
		{ offsetof(struct sim_town, land_x), 15 },
		{ offsetof(struct sim_town, land_y), 16 },
		{ offsetof(struct sim_town, party_x), 22 },
		{ offsetof(struct sim_town, party_y), 23 },
	};
	uint16_t *v;
	size_t i, k;

	for (i = 0; i < s->ntowns && i < era->count; i++)
		for (k = 0; k < sizeof(later) / sizeof(later[0]); k++) {
			v = (uint16_t *)((char *)&s->towns[i] + later[k].off);
			if (*v == TOWN_UNSET)
				*v = (uint16_t)era->towns[i].raw[later[k].raw];
		}
}

static void
start_log(struct sim *s)
{
	int step, date, i, j, r;
	struct sim_news *n;

	s->day_of_year = rng_below(&s->rng, 150) + 65;
	s->month = s->day_of_year / DAYS_PER_MONTH;
	step = s->day_of_year / 6;
	date = step / 2;
	for (i = 0; i < 3; i++)
		for (j = i + 1; j < 4; j++) {
			n = &s->news[s->nnews % SIM_NEWS_MAX];
			r = s->relations[i][j];
			if (r == 0) {
				n->code = NEWS_INDIANS + rng_below(&s->rng, 5);
				n->a = rng_below(&s->rng, (int)s->ntowns);
				n->b = 0;
			} else {
				n->code = NEWS_PEACE + r;	/* war, alliance */
				n->a = i;
				n->b = j;
			}
			n->date = s->year * DAYS_PER_YEAR + date;
			n->day = date - s->day_of_year;
			s->nnews++;
			date += step;
		}
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
	s->villain_duel = -1;
	s->ransom_slot = -1;
	s->player_town = SIM_NO_TOWN;
	s->visit_town = SIM_NO_TOWN;
	s->wright_gold = -1;
	s->merchant_gold = -1;
	s->player.expedition = -1;
	snprintf(s->player.title, sizeof(s->player.title), "%s", scn->name);
	memcpy(s->relations, scn->relations, sizeof(s->relations));
	s->fleet.nships = scn->nships > 0 ? scn->nships : 1;
	memcpy(s->fleet.ships, scn->ships, sizeof(s->fleet.ships));
	s->fleet.crew = scn->crew;
	s->fleet.guns = scn->guns;
	s->fleet.gold = scn->gold;
	s->fleet.food = scn->food;
	s->fleet.goods = scn->goods;
	s->fleet.sugar = scn->sugar;
	s->fleet.capacity = scn->capacity;
	{
		int k = rng_below(&s->rng, SCENARIO_STARTS);

		s->fleet.x = scn->start[k][0] * WORLD_TILE;
		s->fleet.y = scn->start[k][1] * WORLD_TILE;
	}
	s->fleet.leaving = -1;
	s->wind = WIND_START;
	s->standing[NATION_PIRATE] = -1;	/* pirates are always hostile */
	s->encounters = 1;
	s->heat_town = -1;
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
		s->towns[i].family = t->raw[12];
		s->towns[i].id = (uint16_t)t->id;
		s->towns[i].fort_x = t->raw[17];
		s->towns[i].fort_y = t->raw[18];
		s->towns[i].ship_x = t->raw[19];
		s->towns[i].ship_y = t->raw[20];
		s->towns[i].ship_heading = t->raw[21];
		s->towns[i].sea_x = t->raw[26];
		s->towns[i].sea_y = t->raw[27];
		s->towns[i].sea_heading = t->raw[28];
		s->towns[i].barmaid = TOWN_UNSET;
		s->towns[i].garrison = TOWN_UNSET;
		s->towns[i].land_x = TOWN_UNSET;
		s->towns[i].land_y = TOWN_UNSET;
		s->towns[i].party_x = TOWN_UNSET;
		s->towns[i].party_y = TOWN_UNSET;
	}
	sim_restore_towns(s, era);
	/* The start position is in a town; the voyage starts on the water
	 * outside its harbour. */
	s->fleet.leaving = town_near(s, ARRIVAL_CELLS + 3);
	if (s->fleet.leaving >= 0)
		sim_harbour_mouth(s, s->fleet.leaving);
	start_log(s);
	/* The expeditions start months at sea (scenario byte 0x58). */
	s->months = (int8_t)scn->raw[0x58];
	s->start_year = s->year;
	s->fleet.mood = sim_crew_mood(s);
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
	if (t->flags & FLAG_KNOWN)
		t->flags |= FLAG_OBSOLETE;
	else
		t->flags &= (uint16_t)~FLAG_OBSOLETE;
	t->flags &= (uint16_t)~FLAG_KNOWN;
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
	int start_doy = s->day_of_year, m, months = 0;

	s->day_of_year += days;
	for (m = start_doy / DAYS_PER_MONTH + 1;
	    m <= s->day_of_year / DAYS_PER_MONTH; m++) {
		s->month = m;
		s->day = start_day + (m * DAYS_PER_MONTH - start_doy);
		/* A month longer since the plunder was divided, two with
		 * a fortune aboard (VA 0xceb8). */
		s->months = (int8_t)(s->months + (s->fleet.gold > 0x7FFF ?
		    2 : 1));
		sim_world_update(s);
		s->fleet.mood = sim_crew_mood(s);
		months++;
	}
	/* At sea (not in town nor ashore), the crew speaks (VA 0x1dd0e). */
	if (months > 0 && s->player_town < 0 && !s->fleet.ashore &&
	    s->fleet.crew != 1 && s->crew_event == CREW_NONE) {
		if (s->fleet.mood < -3) {
			s->crew_event = CREW_MUTINY;
			sim_crew_leave(s, s->fleet.crew / 3);
		} else if (s->fleet.mood < 0)
			s->crew_event = s->months < 12 ? CREW_RESTLESS :
			    CREW_ANXIOUS;
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

static int	sighting(struct sim *s);

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

int
sim_sine_mul(const struct world *w, int heading, int v)
{
	return (div256(sine(w, heading) * v));
}

int
sim_div16(int a, int b)
{
	if (a == 0 || b == 0 || a < b)
		return (0);
	return (a / b);
}

/*
 * The game's bearing: the angle of the smaller axis over the larger one
 * from a polynomial approximation of the arc tangent, in 1/65536 turn,
 * placed in its octant, then scaled to 256 per turn.
 */
int
sim_bearing(int dx, int dy)
{
	int p1 = dx, p2 = -dy;	/* east and north components */
	int a, b, swap, ratio, t;
	unsigned ang;

	if (p1 == 0)
		return (p2 < 1 ? 0x80 : 0);
	if (p2 == 0)
		return (p1 < 1 ? 0xC0 : 0x40);
	a = abs(p1);
	b = abs(p2);
	swap = b < a;		/* the ratio is then north over east */
	ratio = swap ? (b << 14) / a : (a << 14) / b;
	t = abs(ratio - 0x1333);
	ang = (unsigned)(((0x2800 - (t * 0xb00 >> 14)) * ratio) >> 14) &
	    0xFFFF;
	if (p1 < 1) {
		if (p2 < 1)
			ang = swap ? 0xC000 - ang : ang + 0x8000;
		else
			ang = swap ? ang + 0xC000 : 0x10000 - ang;
	} else {
		if (p2 < 1)
			ang = swap ? ang + 0x4000 : 0x8000 - ang;
		else
			ang = swap ? 0x4000 - ang : ang;
	}
	return ((int)((ang & 0xFFFF) >> 8));
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
	if (s->encounter.stage != ENC_NONE || s->battle.active)
		return (SAIL_ENCOUNTER);	/* answer or fight it first */
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
		if (f->ticks % TICKS_PER_DAY == 0) {
			sim_advance(s, 1);
			if (s->crew_event != CREW_NONE)
				return (SAIL_CREW);
		}
		/* Ships never sail onto land; the player may go ashore. */
		if (world_terrain(s->world, nx, ny) == TERRAIN_LAND) {
			f->speed = 0;
			f->land_x = nx;
			f->land_y = ny;
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
		if (s->encounters && (f->ticks + 4) % SIGHTING_TICKS == 0 &&
		    sighting(s)) {
			f->speed = 0;
			return (SAIL_ENCOUNTER);
		}
	}
	return (SAIL_TICKS);
}

#define BOARD_REACH	11	/* pixels from the landing point */

void
sim_go_ashore(struct sim *s)
{
	struct sim_fleet *f = &s->fleet;

	if (f->ashore || s->world == NULL ||
	    world_terrain(s->world, f->land_x, f->land_y) != TERRAIN_LAND)
		return;
	f->ashore = 1;
	f->ship_x = f->x;
	f->ship_y = f->y;
	f->x = f->land_x;
	f->y = f->land_y;
	f->fx = f->fy = 4;
	f->speed = 0;
	/* A party landing next to the town the ship just left (a failed
	 * attack, say) may walk into it (an engine choice). */
	f->leaving = -1;
}

void
sim_turn_back(struct sim *s)
{
	s->fleet.heading = (uint8_t)(s->fleet.heading + 0x80);
}

/* Land within a ship's length ahead (16 pixels, checked every 4). */
static int
shore_ahead(const struct sim *s)
{
	const struct sim_fleet *f = &s->fleet;
	int d;

	for (d = 4; d <= 16; d += 4)
		if (world_terrain(s->world, f->x + sim_sine_mul(s->world,
		    f->heading, d), f->y - sim_sine_mul(s->world,
		    f->heading + 0x40, d)) == TERRAIN_LAND)
			return (1);
	return (0);
}

/* The heading nearest "away from the landing point" with open water
 * ahead, turning 16 at a time either side. */
static void
clear_heading(struct sim *s)
{
	struct sim_fleet *f = &s->fleet;
	int away = (sim_bearing(f->x - f->land_x, f->y - f->land_y) + 8) &
	    0xF0, k;

	for (k = 0; k <= 128; k += 16) {
		f->heading = (uint8_t)(away + k);
		if (!shore_ahead(s))
			return;
		f->heading = (uint8_t)(away - k);
		if (!shore_ahead(s))
			return;
	}
	f->heading = (uint8_t)away;
}

/* The ship's heading on boarding, by the party's direction (VA 0x367ac;
 * bits 1 west, 2 east, 4 north, 8 south). */
static uint8_t
boarding_heading(int dx, int dy)
{
	static const uint8_t h[16] = { 0, 192, 64, 0, 0, 224, 32, 0, 128,
	    160, 96, 0, 0, 0, 0, 0 };

	return (h[(dx < 0 ? 1 : dx > 0 ? 2 : 0) |
	    (dy < 0 ? 4 : dy > 0 ? 8 : 0)]);
}

enum sail_stop
sim_march(struct sim *s, int dx, int dy, int ticks, int *town)
{
	struct sim_fleet *f = &s->fleet;
	int32_t x8, y8, nx, ny;
	int t;

	*town = -1;
	if (s->world == NULL || !f->ashore)
		return (SAIL_TICKS);
	dx = dx < 0 ? -1 : dx > 0 ? 1 : 0;
	dy = dy < 0 ? -1 : dy > 0 ? 1 : 0;
	while (ticks-- > 0) {
		f->ticks++;
		if (f->ticks % WIND_TICKS == 0)
			shift_wind(s);
		if (f->ticks % TICKS_PER_DAY == 0)
			sim_advance(s, 1);
		if (dx == 0 && dy == 0)
			continue;
		x8 = f->x * 8 + f->fx + dx * 4;
		y8 = f->y * 8 + f->fy + dy * 4;
		nx = x8 >> 3;
		ny = y8 >> 3;
		if (nx > EDGE_MAX_X || nx < EDGE_MIN || ny > EDGE_MAX_Y ||
		    ny < EDGE_MIN)
			continue;
		if (world_terrain(s->world, nx, ny) != TERRAIN_LAND) {
			/* Water: aboard again near the landing point. */
			if (abs(nx - f->land_x) < BOARD_REACH &&
			    abs(ny - f->land_y) < BOARD_REACH) {
				f->ashore = 0;
				f->x = f->ship_x;
				f->y = f->ship_y;
				f->fx = f->fy = 4;
				f->heading = boarding_heading(dx, dy);
				f->speed = 0;
				/* The party's way can face the shore again,
				 * and each step onto it would ask once more
				 * whether to disembark: the engine then
				 * heads away from where the party landed. */
				if (shore_ahead(s))
					clear_heading(s);
				return (SAIL_BOARDED);
			}
			continue;
		}
		f->x = nx;
		f->y = ny;
		f->fx = (uint8_t)(x8 & 7);
		f->fy = (uint8_t)(y8 & 7);
		if ((t = town_at(s)) >= 0) {
			*town = t;
			f->leaving = t;
			return (SAIL_TOWN);
		}
	}
	return (SAIL_TICKS);
}

/* Nearest town by |dx| + |dy| in cells, and that distance (at least 1). */
static int
nearest_town(const struct sim *s, int *dist)
{
	int cx = s->fleet.x / WORLD_TILE, cy = s->fleet.y / WORLD_TILE;
	int best = -1, d;
	size_t i;

	*dist = 0;
	for (i = 0; i < s->ntowns; i++) {
		d = abs(s->towns[i].x - cx) + abs(s->towns[i].y - cy);
		if (best < 0 || d < *dist) {
			best = (int)i;
			*dist = d;
		}
	}
	if (*dist < 1)
		*dist = 1;
	return (best);
}

/* Pick the ship met, from the era's records. */
static void
pick_ship(struct sim *s, int town, int dist)
{
	struct sim_encounter *e = &s->encounter;
	const uint8_t *rec;
	int nation = s->towns[town].nation & 3, k = 0, range, r;

	range = s->fleet.crew / 50 + s->heat / 6 - s->towns[town].standing;
	if (range >= 1) {
		k = rng_below(&s->rng, range) / 3;
		if (k > ENCOUNTER_SHIPS - 1)
			k = ENCOUNTER_SHIPS - 1;
	}
	if (rng_below(&s->rng, 15) < 4 || dist > FAR_FROM_TOWNS) {
		r = rng_below(&s->rng, ENCOUNTER_NATIONS * ENCOUNTER_SHIPS);
		k = r / 4;
		nation = r % 4;
	}
	rec = s->world->encounter[nation][k];
	memset(e, 0, sizeof(*e));
	e->stage = ENC_SIGHTED;
	e->type = rec[0] & 7;
	/* Shown as a fast galleon in the early eras (assumed: the game
	 * tests a value below 4 that looks like the era). */
	if (e->type == 5 && s->world->era < 4)
		e->type = 8;
	e->nation = rec[1] <= NATION_PIRATE ? rec[1] : NATION_PIRATE;
	e->crew = rec[2];
	e->guns = rec[3];
	e->role = rec[4];
	e->cargo = rec[5];
	e->gold = rec[6];
	e->hostile = e->role > 5 && s->standing[e->nation] < 0;
	e->town = town;
	/* Off the town a governor sent the player to: the pirate he
	 * hunts (VA 0xd1fc). */
	e->named = s->mission.type == MISSION_PIRATE &&
	    s->mission.dest == town && e->nation == NATION_PIRATE;
	/* A hostile warship or pirate has a named captain; the hunted
	 * pirate is the mission's (VA 0xd1fc). */
	if (e->named)
		e->captain = s->mission.pirate;
	else if (e->hostile)
		e->captain = sim_name_captain(s, e->nation == NATION_PIRATE &&
		    town >= 0 ? s->towns[town].nation & 3 : e->nation & 3);
}

/* Every 32 ticks: maybe a sail on the horizon (game-rules.md). */
static int
sighting(struct sim *s)
{
	int dist, town, r, chance;

	if ((town = nearest_town(s, &dist)) < 0)
		return (0);
	if (town != s->heat_town) {
		s->heat = 0;
		s->heat_town = town;
	}
	if (dist > 63)
		dist = 63;
	r = rng_below(&s->rng, 0x10000);
	if (dist < 8) {
		s->heat += 8 - dist;
		chance = (r & 7) + (s->heat >> 2) < 2;
	} else if (dist < 16)
		chance = (r & 15) < 2;
	else if (dist < 32)
		chance = (r & 31) < 4;
	else
		chance = (r & 63) < 4;
	if (!chance)
		return (0);
	pick_ship(s, town, dist);
	return (1);
}

static enum encounter_result
end_encounter(struct sim *s, enum encounter_result r)
{
	if (r == RES_ESCAPED || r == RES_NEWS)
		s->heat += HEAT_IGNORED;
	s->encounter.stage = ENC_NONE;
	return (r);
}

/* The dialogs end in a sea battle against the encounter's ship. */
static enum encounter_result
to_battle(struct sim *s, enum encounter_result r)
{
	sim_battle_start(s);
	return (end_encounter(s, r));
}

enum encounter_result
sim_encounter_act(struct sim *s, enum encounter_action action)
{
	struct sim_encounter *e = &s->encounter;

	if (s->battle.active)
		return (RES_BATTLE);	/* fight it out first */
	switch (e->stage) {
	case ENC_SIGHTED:
		if (action == ACT_IGNORE)
			return (end_encounter(s, RES_ESCAPED));
		e->stage = ENC_IDENTIFIED;
		return (RES_CONTINUE);
	case ENC_IDENTIFIED:
		/* A hunter, or the pirate hunted, cannot be avoided once
		 * she is seen. */
		if (action == ACT_IGNORE && !e->hostile && !e->named)
			return (end_encounter(s, RES_ESCAPED));
		e->stage = ENC_COLOURS;
		return (RES_CONTINUE);
	case ENC_COLOURS:
		if (action != ACT_BATTLE && e->hostile &&
		    rng_below(&s->rng, 3) < s->player.difficulty)
			return (to_battle(s, RES_OVERTAKEN));
		if (action == ACT_BATTLE)
			return (to_battle(s, RES_BATTLE));
		if (action == ACT_HAIL && s->standing[e->nation] < -3)
			return (to_battle(s, RES_UNANSWERED));
		if (action == ACT_HAIL)
			return (end_encounter(s, RES_NEWS));
		return (end_encounter(s, RES_ESCAPED));
	default:
		return (RES_ESCAPED);
	}
}

/*
 * In town (VA 0x18fa9 and the ways in): the visit flags survive only a
 * return to the same town, and then only bits 0 and 1; the town's
 * information becomes current.
 */
void
sim_arrive(struct sim *s, int town)
{
	if (town < 0 || (size_t)town >= s->ntowns)
		return;
	if (town != s->visit_town) {
		s->visit_flags = 0;
		s->wright_gold = -1;
		s->wright_cannon = 0;
		s->merchant_gold = -1;
	} else
		s->visit_flags &= VISIT_GOVERNOR | VISIT_KEPT;
	s->visit_town = town;
}

void
sim_enter_town(struct sim *s, int town)
{
	struct sim_town *t;

	if (town < 0 || (size_t)town >= s->ntowns)
		return;
	sim_arrive(s, town);
	s->player_town = town;
	s->tavern_spots = TAVERN_ALL;		/* VA 0x18fa9 */
	t = &s->towns[town];
	t->flags = (uint16_t)((t->flags | FLAG_KNOWN) & ~FLAG_OBSOLETE);
}

void
sim_leave_town(struct sim *s)
{
	int n;

	/* Roll call (VA 0x17efb): (3 - mood) x crew / 15 men desert,
	 * when more than one. */
	s->deserted = 0;
	n = (int16_t)((3 - s->fleet.mood) * s->fleet.crew) / 15;
	if (n >= s->fleet.crew)
		n = s->fleet.crew - 1;
	if (n > 1) {
		sim_crew_leave(s, n);
		s->deserted = n;
	}
	/* One prisoner may jump overboard when the ship is in port. */
	s->escaped = 0;
	n = rng_below(&s->rng, SIM_PRISONERS);
	if (s->prisoners[n] != 0 && !s->fleet.ashore) {
		s->escaped = s->prisoners[n] & 0xFF;
		s->prisoners[n] = 0;
	}
	if (s->player_town >= 0 && (size_t)s->player_town < s->ntowns) {
		if (s->fleet.ashore) {
			/* On foot: out on the town's own cell (VA 0x19bd0). */
			s->fleet.x = s->towns[s->player_town].x * WORLD_TILE + 8;
			s->fleet.y = s->towns[s->player_town].y * WORLD_TILE + 8;
		} else
			sim_harbour_mouth(s, s->player_town);
		s->fleet.leaving = s->player_town;
	}
	s->player_town = SIM_NO_TOWN;
}
