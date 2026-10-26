/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Town services, following the "Towns" section of
 * aidd_docs/memory/internal/game-rules.md.
 */

#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define PURSE_MAX	50000	/* the shipwright's gold */
#define PURSE_STEP	600
#define SHIP_PRICE	50	/* gold per class step and tenth */
#define DAMAGE_LEVELS	3	/* prices count damage levels up to 3 */

static struct sim_town *
current(struct sim *s)
{
	return (s->player_town >= 0 && (size_t)s->player_town < s->ntowns ?
	    &s->towns[s->player_town] : NULL);
}

int
sim_shipwright_refuses(const struct sim *s)
{
	const struct sim_town *t;

	if (s->player_town < 0 || (size_t)s->player_town >= s->ntowns)
		return (1);
	t = &s->towns[s->player_town];
	/* Spanish towns, unless the Spanish rank (not modelled: 0) and
	 * the high byte of the diplomacy range reach the town's wealth. */
	return (t->nation == NATION_SPAIN &&
	    0 * 25 + ((s->diplomacy_range >> 8) & 0xFF) < t->wealth);
}

/* The shipwright's gold, set once per stay: seasonal, by wealth and
 * citizens (VA 0x219b8). */
int
sim_shipwright_gold(struct sim *s)
{
	struct sim_town *t = current(s);
	long x;

	if (t == NULL)
		return (0);
	if (s->wright_gold < 0) {
		x = (long)((abs(6 - s->month) + 6) * t->wealth) / 6;
		x = ((long)(t->citizens + 10) * x / 150) * PURSE_STEP;
		s->wright_gold = (int)(x > PURSE_MAX ? PURSE_MAX : x);
	}
	return (s->wright_gold);
}

static int
damage_level(const struct sim_fleet *f, int i)
{
	int d = f->damage[i] / 16;

	return (d > DAMAGE_LEVELS ? DAMAGE_LEVELS : d);
}

int
sim_repair_cost(const struct sim *s, int i)
{
	const struct sim_fleet *f = &s->fleet;

	if (i < 0 || i >= f->nships)
		return (0);
	return (SHIP_PRICE * ((f->ships[i] & 0xF) + 1) * damage_level(f, i));
}

int
sim_ship_price(const struct sim *s, int i)
{
	const struct sim_fleet *f = &s->fleet;

	if (i < 0 || i >= f->nships)
		return (0);
	return (SHIP_PRICE * ((f->ships[i] & 0xF) + 1) *
	    (10 - damage_level(f, i)));
}

int
sim_repair(struct sim *s, unsigned mask)
{
	struct sim_fleet *f = &s->fleet;
	long total = 0;
	int i;

	for (i = 0; i < f->nships; i++)
		if (mask & (1u << i))
			total += sim_repair_cost(s, i);
	if (total == 0)
		return (REPAIR_NOTHING);
	if (total >= f->gold)
		return (REPAIR_TOO_DEAR);
	sim_shipwright_gold(s);
	f->gold -= total;
	s->wright_gold += (int)total;
	for (i = 0; i < f->nships; i++)
		if (mask & (1u << i))
			f->damage[i] = 0;
	return (REPAIR_DONE);
}

int
sim_sell_ships(struct sim *s, unsigned mask)
{
	struct sim_fleet *f = &s->fleet;
	int i = 0, k, price, sold = 0;

	if (f->nships < 2)
		return (SELL_ONLY_SHIP);
	sim_shipwright_gold(s);
	/* In fleet order; one ship always stays, and the shipwright stops
	 * when his gold runs out (CSTR 319). */
	for (; mask != 0 && i < f->nships && f->nships > 1; mask >>= 1) {
		if (!(mask & 1u)) {
			i++;
			continue;
		}
		price = sim_ship_price(s, i);
		if (price > s->wright_gold)
			return (SELL_SHORT);
		f->capacity -= sim_ship_room(f->ships[i]);
		for (k = i; k + 1 < f->nships; k++) {
			f->ships[k] = f->ships[k + 1];
			f->damage[k] = f->damage[k + 1];
		}
		f->nships--;
		f->gold += price;
		s->wright_gold -= price;
		sold++;
	}
	return (sold > 0 ? SELL_DONE : SELL_NOTHING);
}

/* Merchant: greetings by purse (CSTR 257-260, or 291-294 for family). */
#define GREET_POOR		257
#define GREET_FAMILY		291
#define FAMILY_BITS		0xE0	/* town word 12: married in */
#define WEALTH_MOVE		20	/* gold that moves the town's wealth */

int
sim_hold_space(const struct sim *s)
{
	const struct sim_fleet *f = &s->fleet;

	return (f->capacity * 10 - f->guns / 2 - f->food - f->goods -
	    f->sugar);
}

static int
family(const struct sim_town *t)
{
	return ((t->family & FAMILY_BITS) != 0);
}

int
sim_merchant_refuses(const struct sim *s)
{
	const struct sim_town *t;

	if (s->player_town < 0 || (size_t)s->player_town >= s->ntowns)
		return (1);
	t = &s->towns[s->player_town];
	return (!family(t) && sim_shipwright_refuses(s));
}

int
sim_merchant_greeting(const struct sim *s)
{
	const struct sim_town *t = &s->towns[s->player_town];
	long g = s->fleet.gold;
	int step = g <= 2000 ? 0 : g <= 5000 ? 1 : g <= 20000 ? 2 : 3;

	return ((family(t) ? GREET_FAMILY : GREET_POOR) + step);
}

/* The town's appetite, from the season and its wealth (a double). */
static double
appetite(const struct sim *s, const struct sim_town *t)
{
	return ((abs(6 - s->month) + 6.0) * t->wealth / 6.0);
}

void
sim_merchant_open(struct sim *s)
{
	struct sim_town *t = current(s);
	struct sim_market *m = &s->market;
	double w;
	long x;
	int i, base, p;

	if (t == NULL)
		return;
	memset(m, 0, sizeof(*m));
	w = appetite(s, t);
	if (s->merchant_gold < 0) {
		x = (long)(w * (t->citizens + 10) / 150.0) * PURSE_STEP;
		s->merchant_gold = (int)(x > PURSE_MAX ? PURSE_MAX : x);
	}
	for (i = 0; i < GOODS; i++) {
		base = (int16_t)(w * (t->citizens + 10) / 150.0);
		m->stock[i] = base + (base > 0 ?
		    (int)rng_below(&s->rng, (uint32_t)base) : 0);
		p = (int16_t)(w / 50.0 + 3.0 - (double)(i / 2));
		if (t->flags & (1u << (3 - i)))		/* a local product */
			p = p / 2 + 1;
		if (i == 2 && (t->flags & (1u << 4)))	/* sugar wanted */
			p *= 2;
		m->price[i] = (int16_t)(p * 10);
		/* As traced: the next good starts from this price. */
		w = m->price[i];
	}
	m->price[0] = (int16_t)(m->price[0] * 1.5);
	m->price[1] = (int16_t)(m->price[1] * 1.5);
	m->price[2] = (int16_t)((int16_t)(m->price[2] * 0.5 +
	    m->price[2]) << 1);
	if (family(t))
		for (i = 0; i < GOODS; i++) {
			double v = m->price[i] * 0.75;

			m->price[i] = (int16_t)(v < 1.0 ? 1.0 : v);
		}
	m->gold_at_open = s->fleet.gold;
	m->open = 1;
}

static int *
cargo(struct sim_fleet *f, int good)
{
	return (good == 0 ? &f->food : good == 1 ? &f->goods : &f->sugar);
}

int
sim_trade(struct sim *s, int good, int n)
{
	struct sim_market *m = &s->market;
	struct sim_fleet *f = &s->fleet;
	int *held, done = 0, price;

	if (!m->open || good < 0 || good >= GOODS)
		return (0);
	held = cargo(f, good);
	price = m->price[good];
	for (; n > 0; n--, done++) {		/* buying */
		if (m->stock[good] <= 0 || price > f->gold ||
		    sim_hold_space(s) <= 0)
			break;
		(*held)++;
		m->stock[good]--;
		s->merchant_gold += price;
		f->gold -= price;
	}
	for (; n < 0; n++, done++) {		/* selling */
		if (*held <= 0 || price > s->merchant_gold)
			break;
		(*held)--;
		m->stock[good]++;
		s->merchant_gold -= price;
		f->gold += price;
	}
	return (done);
}

void
sim_merchant_close(struct sim *s)
{
	struct sim_town *t = current(s);
	long moved;
	int w;

	if (!s->market.open || t == NULL)
		return;
	moved = labs(s->fleet.gold - s->market.gold_at_open);
	if (moved > WEALTH_MOVE) {
		w = t->wealth * 19 / 20 + 12;
		t->wealth = (uint16_t)(w > 255 ? 255 : w);
	}
	s->market.open = 0;
}

/*
 * Recruits (VA 0x10e2a): by reputation, difficulty and the town's
 * people; at least a few when the crew is small, at most what the
 * fleet can hold.
 */
void
sim_tavern_open(struct sim *s)
{
	struct sim_town *t = current(s);
	struct sim_fleet *f = &s->fleet;
	int n, least, most;

	s->recruits = 0;
	if (t == NULL || !(s->tavern_spots & TAVERN_CREW))
		return;
	n = ((s->reputation + 7 - s->player.difficulty) * (t->citizens +
	    (t->citizens > 0 ? (int)rng_below(&s->rng, t->citizens) : 0))) /
	    9;
	least = (4 - s->player.difficulty) * 6;
	if (f->crew < least * 4 && n < least)
		n = least;
	most = (f->nships * 2 + f->capacity) * 16;
	if (f->crew + n > most)
		n = most - f->crew;
	if (n < 0)
		n = 1;
	s->recruits = n;
}

void
sim_recruit(struct sim *s, int accept)
{
	if (accept)
		s->fleet.crew += s->recruits;
	s->recruits = 0;
	s->tavern_spots &= ~TAVERN_CREW;
}
