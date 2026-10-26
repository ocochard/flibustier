/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Town services, following the "Towns" section of
 * aidd_docs/memory/internal/game-rules.md.
 */

#include <math.h>
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
	/* Spanish towns, unless the Spanish rank and the high byte of
	 * the diplomacy range reach the town's wealth. */
	return (t->nation == NATION_SPAIN &&
	    s->rank[NATION_SPAIN] * 25 + ((s->diplomacy_range >> 8) & 0xFF) <
	    t->wealth);
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

void
sim_attack_nation(struct sim *s, int nation, int d)
{
	int k, v;

	if (nation < 0 || nation > 3)
		return;
	v = s->standing[nation] + d;
	s->standing[nation] = (int8_t)(v < -128 ? -128 : v > 127 ? 127 : v);
	for (k = 0; k < 4; k++) {
		if (k == nation)
			continue;
		v = s->standing[k] + d * s->relations[nation][k];
		/* Through others, standing rises no higher than 1, and does
		 * not fall below 2 once above 1. */
		if (v >= 2 && s->standing[k] <= 1)
			v = 1;
		else if (v < 2 && s->standing[k] > 1)
			v = 2;
		s->standing[k] = (int8_t)v;
	}
}

#define RANK_MAX	9
#define ACRE_STEP	50

int
sim_governor_promote(struct sim *s, int *acres)
{
	struct sim_town *t = current(s);
	int n, r, surplus, land;

	*acres = 0;
	if (t == NULL || t->nation > 3)
		return (0);
	n = t->nation;
	r = s->rank[n] + 1;
	surplus = s->standing[n] - (r * r + 2);
	if (surplus < 0 || r > RANK_MAX)
		return (0);
	s->rank[n] = (uint8_t)r;
	s->standing[n] = (int8_t)(s->standing[n] - surplus);
	/* The game also logs it (code 2) in the personal log, which the
	 * engine does not keep yet. */
	land = surplus + t->standing;
	if (land > 0) {
		s->acres += land;
		*acres = land * ACRE_STEP;
	}
	return (r);
}

enum governor_offer
sim_governor_offer(const struct sim *s, long *price)
{
	const struct sim_town *t;
	int st;

	*price = 0;
	if (s->player_town < 0 || (size_t)s->player_town >= s->ntowns)
		return (OFFER_NONE);
	t = &s->towns[s->player_town];
	if (t->nation > 3)
		return (OFFER_NONE);
	st = s->standing[t->nation];
	if (st <= -4)
		return (OFFER_NONE);
	*price = (long)(4 - st * st * st) * 20 * 10;
	if (*price < 100)
		return (OFFER_DAUGHTER);
	return (st < 0 ? OFFER_PARDON : OFFER_MARQUE);
}

int
sim_governor_pay(struct sim *s)
{
	struct sim_town *t = current(s);
	long price;
	enum governor_offer o = sim_governor_offer(s, &price);
	int n;

	if (t == NULL || (o != OFFER_MARQUE && o != OFFER_PARDON) ||
	    price > s->fleet.gold)
		return (0);
	s->fleet.gold -= price;
	t->standing = 0;
	n = t->nation;
	s->standing[n] = (int8_t)(s->standing[n] + 2 > 2 ? 2 :
	    s->standing[n] + 2);
	return (1);
}

#define REFIT_SPELLS	4	/* the refit: four spells of 26 days */
#define REFIT_DAYS	26
#define NEW_FOOD	15

void
sim_divide_plunder(struct sim *s, struct plunder *out)
{
	struct sim_fleet *f = &s->fleet;
	double root;
	int crew = f->crew > 0 ? f->crew : 1, r;

	out->share = (int)((double)f->gold / crew * 0.8 + 0.5);
	root = sqrt((double)out->share);
	out->mood = (int)(root / 23.0);
	if (out->mood > 3)
		out->mood = 3;
	/* The crew's mood moves the reputation halfway (VA 0x181fb). */
	r = ((int)root + s->reputation + 1) / 2;
	s->reputation = r < -128 ? -128 : r > 127 ? 127 : r;
	out->portions = (s->player.difficulty & 3) * 2 + 2;
	out->yours = (long)(s->player.difficulty + 1) * f->gold / 20;
	s->fortune += out->yours;
}

void
sim_new_expedition(struct sim *s, int advance)
{
	struct sim_fleet *f = &s->fleet;
	long gold = f->gold;
	int type, i;

	if (advance && s->player.difficulty < 3)
		s->player.difficulty++;
	if (gold == 0) {
		s->fortune -= s->fortune / 10;
		gold = s->fortune;
	}
	f->gold = gold / 10;
	type = f->ships[0] & 7;
	f->ships[0] = (uint8_t)type;
	f->damage[0] = 0;
	f->nships = 1;
	f->capacity = sim_ship_room(type);
	f->guns = type * 2 + 6;
	f->food = NEW_FOOD;
	f->goods = f->sugar = 0;
	f->crew = s->reputation + 16;
	if (f->crew <= 0)
		f->crew = 1;
	s->visit_flags = 0;
	for (i = 0; i < REFIT_SPELLS; i++)
		sim_advance(s, REFIT_DAYS);
}

int
sim_trade_cannon(struct sim *s, int n)
{
	struct sim_fleet *f = &s->fleet;
	int done = 0;

	if (current(s) == NULL)
		return (0);
	sim_shipwright_gold(s);
	for (; n < 0; n++, done++) {		/* selling */
		if (f->guns <= 0 || CANNON_PRICE > s->wright_gold)
			break;
		f->guns--;
		s->wright_cannon++;
		s->wright_gold -= CANNON_PRICE;
		f->gold += CANNON_PRICE;
	}
	for (; n > 0; n--, done++) {		/* buying back */
		/* Only a hold exactly full refuses (VA 0x20bcd). */
		if (s->wright_cannon <= 0 || CANNON_PRICE > f->gold ||
		    sim_hold_space(s) == 0)
			break;
		f->guns++;
		s->wright_cannon--;
		s->wright_gold += CANNON_PRICE;
		f->gold -= CANNON_PRICE;
	}
	return (done);
}
