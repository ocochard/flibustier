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

#define SCENE_ALLEY	0	/* sword fight pictures (VA 0x3361c) */
#define SCENE_FORT	1	/* 2 inland: the same picture */

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
	if (t == NULL)
		return;
	if (!(s->tavern_spots & TAVERN_CREW))
		goto people;
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
people:
	/* No personal enemy nor mission is modelled: no mystery man. */
	s->tavern_spots &= ~TAVERN_MYSTERY;
	/* The traveller (VA 0x110c7): a price and a town not known yet,
	 * tried until one is found or the price comes out at 50. */
	if (s->tavern_spots & TAVERN_NEWS) {
		do {
			s->visit_flags |= VISIT_KEPT;
			s->news_price = (rng_below(&s->rng, 10) + 5) * 10;
			s->news_town = rng_below(&s->rng, (int)s->ntowns);
		} while ((s->towns[s->news_town].flags & 0x80) &&
		    s->news_price != 50);
		if (s->news_price == 50 || s->news_town == s->player_town)
			s->tavern_spots &= ~TAVERN_NEWS;
	}
	/* The map seller (VA 0x112b1): one time in five. */
	if ((s->tavern_spots & TAVERN_MAPS) && rng_below(&s->rng, 5) != 0)
		s->tavern_spots &= ~TAVERN_MAPS;
}

int
sim_buy_map(struct sim *s)
{
	if (s->fleet.gold < MAP_PRICE)
		return (0);
	s->fleet.gold -= MAP_PRICE;
	s->fleet.map_pieces++;
	s->tavern_spots &= ~TAVERN_MAPS;
	return (1);
}

/* Information on a town: current again; the town the player is in
 * loses its "obsolete" mark too (VA 0x111f0). */
static void
learn_town(struct sim *s, int town)
{
	struct sim_town *t = current(s);

	s->towns[town].flags |= 0x80;
	if (t != NULL)
		t->flags = (uint16_t)(t->flags & ~0x20);
}

int
sim_buy_news(struct sim *s)
{
	if (s->fleet.gold < s->news_price || s->news_town < 0 ||
	    (size_t)s->news_town >= s->ntowns)
		return (-1);
	s->fleet.gold -= s->news_price;
	learn_town(s, s->news_town);
	s->tavern_spots &= ~TAVERN_NEWS;
	return (s->news_town);
}

int
sim_barmaid_meet(struct sim *s)
{
	struct sim_town *t = current(s);

	s->barmaid_mood = rng_below(&s->rng, 6);
	if (t == NULL || t->barmaid > 10)
		return (2);
	return (t->barmaid == 0 && s->barmaid_mood != 0 ? 0 : 1);
}

int
sim_barmaid(struct sim *s, int answer)
{
	struct sim_town *t = current(s);

	if (t == NULL)
		return (0);
	if (answer == BARMAID_DRINK) {
		if (s->fleet.gold > 0)
			s->fleet.gold--;
		return (0);
	}
	if (answer != BARMAID_COMPANY)
		return (0);
	if (s->fleet.gold < 5)
		return (-1);
	s->fleet.gold -= 5;
	t->barmaid++;
	return (t->barmaid >= 10);
}

int
sim_barmaid_report(struct sim *s, int *town)
{
	int r = 0;

	*town = -1;
	if (s->tavern_spots & TAVERN_NEWS) {
		learn_town(s, s->news_town);
		*town = s->news_town;
		s->tavern_spots &= ~TAVERN_NEWS;
		r = 1;
	} else if (s->tavern_spots & TAVERN_MAPS) {
		s->fleet.map_pieces++;
		s->tavern_spots &= ~TAVERN_MAPS;
		r = 2;
	}
	s->tavern_spots &= ~TAVERN_MAID;
	return (r);
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

/*
 * Sailing in (VA 0x19415): x = (standing with the town's nation + the
 * town's standing - crew / 100) x rand(32767) x 2 / 65536.  Above -0.5,
 * or without forts, the fleet is in.  Otherwise the fort fires, and at
 * -1 or below a random ship is sunk, its cargo share lost.
 */
enum arrival
sim_sail_in(struct sim *s, int town)
{
	const struct sim_town *t;
	double x;
	int n;

	if (town < 0 || (size_t)town >= s->ntowns)
		return (ARRIVE_FIRED);
	t = &s->towns[town];
	n = t->nation <= NATION_PIRATE ? t->nation : NATION_PIRATE;
	x = (double)(s->standing[n] + (int16_t)t->standing) -
	    s->fleet.crew / 100.0;
	x = x * (double)(rng_below(&s->rng, 0x7FFF) * 2) / 65536.0;
	if (x > -0.5 || t->forts == 0) {
		sim_enter_town(s, town);
		return (ARRIVE_ENTER);
	}
	if (x > -1.0 || s->fleet.ashore)	/* a ship only by sea */
		return (ARRIVE_FIRED);
	sim_drop_ship(s, rng_below(&s->rng, s->fleet.nships), 0);
	return (ARRIVE_SUNK);
}

/*
 * Sneaking in (VA 0x195f5): the stay is marked (visit flags 2 and 8);
 * the player gets in when the town's standing x the reputation is above
 * -rand(50), and is otherwise recognized by a guard.
 */
enum arrival
sim_sneak_in(struct sim *s, int town)
{
	const struct sim_town *t;

	if (town < 0 || (size_t)town >= s->ntowns)
		return (ARRIVE_CAUGHT);
	t = &s->towns[town];
	sim_arrive(s, town);
	s->visit_flags |= VISIT_KEPT | VISIT_SNEAKED;
	if ((int16_t)t->standing * s->reputation >
	    -(int)rng_below(&s->rng, 50)) {
		sim_enter_town(s, town);
		s->visit_flags |= VISIT_KEPT | VISIT_SNEAKED;
		return (ARRIVE_ENTER);
	}
	return (ARRIVE_CAUGHT);
}

void
sim_guard_duel(struct sim *s, int town, int weapon)
{
	struct sword_setup in;

	memset(&in, 0, sizeof(in));
	in.kind = SWORD_DUEL;
	in.you_men = in.foe_men = 1;
	in.you_factor = in.foe_factor = 9;
	in.title = 0;				/* a guard */
	in.nation = town >= 0 && (size_t)town < s->ntowns ?
	    s->towns[town].nation : NATION_PIRATE;
	in.extra = 1;
	in.weapon = weapon;
	sim_sword_start(s, &in);
	s->sword.scene = SCENE_ALLEY;
}

int
sim_duel_end(struct sim *s)
{
	struct sim_sword *sw = &s->sword;
	int disgraced = sw->result != SWORD_WON;

	sw->active = 0;
	if (disgraced)
		s->reputation = -1;		/* VA 0x16384 */
	return (disgraced);
}


void
sim_fort_battle_garrison(struct sim *s, int town)
{
	struct sim_town *t;

	if (town < 0 || (size_t)town >= s->ntowns)
		return;
	t = &s->towns[town];
	t->soldiers = (uint16_t)((t->soldiers + 1) / 2 +
	    (s->battle.foe.crew > 0 ? s->battle.foe.crew : 0) / 10);
}

void
sim_assault_start(struct sim *s, int town, int aboard, int weapon)
{
	const struct sim_town *t;
	struct sword_setup in;

	if (town < 0 || (size_t)town >= s->ntowns)
		return;
	t = &s->towns[town];
	memset(&in, 0, sizeof(in));
	in.kind = SWORD_CREW;
	in.you_men = s->fleet.crew - aboard;
	if (in.you_men < 1)
		in.you_men = 1;
	/* rand(3) + 3 + the crew's mood / 2; the mood (VA 0x45faa) is not
	 * modelled yet and counts 0. */
	in.you_factor = rng_below(&s->rng, 3) + 3;
	in.foe_men = t->soldiers * 10 + 5;
	in.foe_factor = t->forts + 5;
	in.title = 4;
	in.nation = t->nation;
	in.extra = 0;
	in.weapon = weapon;
	sim_sword_start(s, &in);
	s->sword.scene = t->forts > 0 ? SCENE_FORT : SCENE_ALLEY;
}

int
sim_assault_end(struct sim *s, int town)
{
	struct sim_sword *sw = &s->sword;
	struct sim_town *t;
	int r;

	if (town < 0 || (size_t)town >= s->ntowns)
		return (0);
	t = &s->towns[town];
	r = sw->result == SWORD_WON ? 1 : sw->result == SWORD_LOST ? -1 : 0;
	sw->active = 0;
	if (r == 1) {
		t->soldiers = (uint16_t)((sw->foe.men + 9) / 10);
		s->fleet.crew -= sw->men_start - sw->you.men;
		return (1);
	}
	/* Repelled: the men who fell (VA 0x16384). Defeated: prison (VA
	 * 0x16647), whose consequences are not traced yet. */
	s->fleet.crew -= sw->men_start - sw->you.men;
	if (s->fleet.crew < 1)
		s->fleet.crew = 1;
	s->reputation = s->reputation > -128 ? s->reputation - 1 : -128;
	sim_attack_nation(s, t->nation, -1);
	return (r);
}

void
sim_sack(struct sim *s, int town, struct sack *out)
{
	struct sim_town *t;
	long gold;
	int i, c;

	memset(out, 0, sizeof(*out));
	out->hidden = -1;
	if (town < 0 || (size_t)town >= s->ntowns)
		return;
	t = &s->towns[town];
	t->wealth = t->wealth < 21 ? 0 : (uint16_t)(t->wealth - 20);
	t->flags = (uint16_t)((t->flags | 0x80) & ~0x20);
	/* Warned: some, much or most of the gold is hidden. */
	if (s->heat >= 12) {
		out->hidden = (s->heat - 12) / 15;
		if (out->hidden > 2)
			out->hidden = 2;
	}
	for (i = 0; i < 4; i++) {
		c = rng_below(&s->rng, t->wealth);
		if (i < 3)
			out->goods[i] = c;
	}
	gold = (long)t->gold * 1500 / (s->heat + 15);
	t->gold = gold / 50 > t->gold ? 0 : (uint16_t)(t->gold - gold / 50);
	t->wealth = t->wealth < 20 ? 0 : (uint16_t)(t->wealth - 20);
	sim_attack_nation(s, t->nation, -1);
	if (gold > 99) {
		sim_attack_nation(s, t->nation, -2);
		s->reputation = s->reputation < 126 ? s->reputation + 2 : 127;
	}
	t->standing = (uint16_t)((int16_t)t->standing - 3);
	c = t->citizens * 10;
	if (s->fleet.crew < c + rng_below(&s->rng, c))
		sim_log_news(s, NEWS_PLUNDERED, town, 0);
	else
		out->conquest = 1;
	out->cannon = t->forts * 6;
	out->gold = gold * 10;
	s->fleet.gold += out->gold;
}

void
sim_install_governor(struct sim *s, int town, int nation)
{
	struct sim_town *t;

	if (town < 0 || (size_t)town >= s->ntowns || nation < 0 ||
	    nation > 3)
		return;
	t = &s->towns[town];
	t->standing = 2;
	t->governor = (uint16_t)rng_below(&s->rng, 16);
	if (nation != t->nation) {
		s->standing[nation] = (int8_t)(s->standing[nation] < 125 ?
		    s->standing[nation] + 3 : 127);
		sim_attack_nation(s, t->nation, -2);
		t->nation = (uint16_t)nation;
	}
	sim_log_news(s, NEWS_TAKEN, town, nation);
}

void
sim_plunder_open(struct sim *s, const int goods[3])
{
	int i;

	for (i = 0; i < GOODS; i++) {
		s->market.stock[i] = goods[i];
		s->market.price[i] = 0;
	}
	s->market.gold_at_open = s->fleet.gold;
	s->merchant_gold = 0;
	s->market.open = 1;
}

void
sim_plunder_close(struct sim *s)
{
	s->market.open = 0;
	s->merchant_gold = -1;
}
