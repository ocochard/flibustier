/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Town services, following the "Towns" section of
 * aidd_docs/memory/internal/game-rules.md.
 */

#include <stdlib.h>

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
