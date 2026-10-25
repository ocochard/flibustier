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
