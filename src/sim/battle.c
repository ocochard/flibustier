/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Sea battles, following the "Sea battles" section of
 * aidd_docs/memory/internal/game-rules.md.
 */

#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define FIELD_CENTRE	240	/* pixels; the ships start mirrored here */
#define ESCAPE_RANGE	240	/* pixels apart on an axis: out of range */
#define SUNSET_TICKS	4500
#define SAIL_TICKS	160	/* to change sails */
#define TURN_TICKS	8	/* steering applies every 8 ticks */
#define AI_TICKS	64	/* the enemy reconsiders this often */
#define WIND_TICKS	512	/* the wind may shift this often */
#define SINK_LEVEL	4	/* damage / 16 that sinks a ship */
#define MAX_SPEED	40
#define SHOT_SPEED	0x80
#define MAX_SHIPS	8
#define FP		256	/* positions are in 1/256 pixels */
#define FIELD		480	/* the battle field's side, in pixels */
#define SET_OPEN	4	/* open sea: no coast near */
#define PIECE_OPEN	4
#define SET_FORT	5
#define COAST_PROBE	19	/* pixels the coast is looked for */
#define SCROLL_LO	40	/* the field scrolls beyond these */
#define SCROLL_HI	400
#define SCROLL_STEP	120
#define FORT_LO		10	/* a fort battle ends beyond these */
#define FORT_HI		430
#define FORT_CLASS	8	/* the fort's class in the hit formula */
#define FORT_TYPE	9	/* its type for labels ("Fort", CSTN 8.9) */
#define CENTRE_X	40	/* a ship's centre from its position */
#define CENTRE_Y	30
#define FORT_BOX	32	/* its box, not quartered (VA 0xe3f0) */
#define SCRAPE_TICKS	20	/* after the shallows hurt a ship */
#define SCRAPE_DAMAGE	16

struct sideinfo {
	int	half_w, half_h;
};

static int
clampi(int v, int lo, int hi)
{
	return (v < lo ? lo : v > hi ? hi : v);
}

static int
sign(int v)
{
	return (v > 0) - (v < 0);
}

/* Signed difference of two headings, -128..127, as a signed byte. */
static int
heading_diff(int a, int b)
{
	return ((int)(signed char)(uint8_t)(a - b));
}

/*
 * The battle's level: the difficulty, one easier with the gunnery
 * ability.  Named captains add to it; they are not modelled yet.
 */
static int
battle_level(const struct sim *s)
{
	return (clampi(s->player.difficulty -
	    (s->player.ability == ABILITY_GUNNERY), 0, BATTLE_LEVELS - 1));
}

static int
px(int32_t v)
{
	return (v / FP);
}

static void
box(const struct sim *s, const struct battle_ship *b, struct sideinfo *bx)
{
	int t = b->type < BATTLE_TYPES ? b->type : BATTLE_TYPES - 1;

	if (s->battle.fort && b == &s->battle.foe) {
		bx->half_w = bx->half_h = FORT_BOX;
		return;
	}
	bx->half_w = s->world->half_w[t];
	bx->half_h = s->world->half_h[t];
}

/*
 * The battle's screens (VA 0x2a658), from where the fleet is: open sea
 * when it sits on the shallows; otherwise the coast found nearest, up
 * to 19 pixels north, east, south or west (table at VA 0x413f8), gives
 * the set, and open sea when there is none.  The set offers four
 * screens along the coast, one picked at random, and open sea.
 */
static void
choose_screens(struct sim *s)
{
	static const int dir[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 },
	    { -1, 0 } };
	struct sim_battle *bt = &s->battle;
	int x = s->fleet.x, y = s->fleet.y, step, d;

	bt->piece = rng_below(&s->rng, 4);
	bt->set = SET_OPEN;
	if (world_terrain(s->world, x, y) != TERRAIN_SHALLOWS)
		for (step = 1; step <= COAST_PROBE && bt->set == SET_OPEN;
		    step++)
			for (d = 0; d < 4; d++)
				if (world_terrain(s->world, x + dir[d][0] *
				    step, y + dir[d][1] * step) ==
				    TERRAIN_LAND) {
					bt->set = d;
					break;
				}
	bt->scroll_x = bt->scroll_y = 0;
	bt->screen = s->world->battle_sets[bt->set][bt->piece];
}

/*
 * Scrolling (VA 0x95ac): the player beyond 40..400 on an axis (or else
 * the enemy) shifts the field by 120 pixels.  Along a coast the screens
 * follow on from the first one, four around; away from it, open sea.
 */
static int
scroll(struct sim *s)
{
	struct sim_battle *bt = &s->battle;
	int v[2][2], d[2], k, i;
	int32_t *pos[2][4];

	/* The limits apply to the ships' corners, as positioned. */
	v[0][0] = px(bt->you.x) - CENTRE_X;
	v[0][1] = px(bt->you.y) - CENTRE_Y;
	v[1][0] = px(bt->foe.x) - CENTRE_X;
	v[1][1] = px(bt->foe.y) - CENTRE_Y;
	for (k = 0; k < 2; k++) {
		d[k] = 0;
		for (i = 0; i < 2 && d[k] == 0; i++)
			d[k] = v[i][k] > SCROLL_HI ? 1 : v[i][k] < SCROLL_LO ?
			    -1 : 0;
	}
	if (d[0] == 0 && d[1] == 0)
		return (0);
	pos[0][0] = &bt->you.x;
	pos[0][1] = &bt->foe.x;
	pos[0][2] = &bt->you.shot.x;
	pos[0][3] = &bt->foe.shot.x;
	pos[1][0] = &bt->you.y;
	pos[1][1] = &bt->foe.y;
	pos[1][2] = &bt->you.shot.y;
	pos[1][3] = &bt->foe.shot.y;
	for (k = 0; k < 2; k++) {
		if (d[k] == 0)
			continue;
		for (i = 0; i < 4; i++)
			*pos[k][i] -= d[k] * SCROLL_STEP * FP;
		*(k == 0 ? &bt->scroll_x : &bt->scroll_y) += d[k];
	}
	if (bt->set == SET_OPEN)
		k = PIECE_OPEN;
	else if ((bt->set & 1) == 0)	/* a coast along x */
		k = bt->scroll_y != 0 ? PIECE_OPEN :
		    (bt->scroll_x + bt->piece) & 3;
	else
		k = bt->scroll_x != 0 ? PIECE_OPEN :
		    (bt->scroll_y + bt->piece) & 3;
	bt->screen = s->world->battle_sets[bt->set][k];
	return (1);
}

/* The flagship fights; the other ships keep 8 men each. */
static void
flagship(struct sim *s)
{
	struct sim_battle *bt = &s->battle;
	struct sim_fleet *f = &s->fleet;
	int cls;

	bt->you.type = f->ships[0];
	cls = f->ships[0] & 7;
	bt->you.damage = f->damage[0];
	bt->you.crew = f->crew + 8 - 8 * f->nships;
	if (bt->you.crew > (cls + 2) * 32)
		bt->you.crew = (cls + 2) * 32;
	if (bt->you.crew < 1)
		bt->you.crew = 1;
	bt->crew_ashore = f->crew - bt->you.crew;
	bt->you.guns = f->guns;
	if (bt->you.guns > bt->you.crew / 4)
		bt->you.guns = bt->you.crew / 4;
	if (bt->you.guns > (cls + 2) * 4)
		bt->you.guns = (cls + 2) * 4;
	bt->guns_ashore = f->guns - bt->you.guns;
	bt->you.turn_rate = 3 - (cls + 2) / 4;
}

void
sim_battle_command(struct sim *s, int ship)
{
	struct sim_fleet *f = &s->fleet;
	uint8_t t;

	if (!s->battle.active || s->battle.tick != 0 || ship <= 0 ||
	    ship >= f->nships)
		return;
	t = f->ships[0];
	f->ships[0] = f->ships[ship];
	f->ships[ship] = t;
	t = f->damage[0];
	f->damage[0] = f->damage[ship];
	f->damage[ship] = t;
	flagship(s);
}

void
sim_battle_start(struct sim *s)
{
	struct sim_battle *bt = &s->battle;
	const struct sim_encounter *e = &s->encounter;
	int off;

	memset(bt, 0, sizeof(*bt));
	bt->active = 1;
	bt->wind = s->wind;
	bt->wind_strength = rng_below(&s->rng, 3);
	choose_screens(s);

	flagship(s);

	bt->foe.type = e->type;
	bt->foe.guns = e->guns;
	bt->foe.crew = e->crew * 4;
	bt->foe.role = e->role;
	bt->foe.turn_rate = 3 - ((e->type & 7) + 2) / 4;

	off = rng_below(&s->rng, 16) + 32;
	if (rng_below(&s->rng, 2))
		off = -off;
	bt->you.x = (FIELD_CENTRE + off + CENTRE_X) * FP;
	bt->foe.x = (FIELD_CENTRE - off + CENTRE_X) * FP;
	off = rng_below(&s->rng, 16) + 32;
	if (rng_below(&s->rng, 2))
		off = -off;
	bt->you.y = (FIELD_CENTRE + off + CENTRE_Y) * FP;
	bt->foe.y = (FIELD_CENTRE - off + CENTRE_Y) * FP;

	/* The player faces the enemy's side; the enemy heads within 56
	 * degrees (40 / 256 turn) of the player's heading. */
	if (bt->foe.x < bt->you.x)
		bt->you.heading = bt->you.y == bt->foe.y ? 0xC0 :
		    bt->foe.y < bt->you.y ? 0xE0 : 0xA0;
	else
		bt->you.heading = bt->you.y == bt->foe.y ? 0x40 :
		    bt->foe.y < bt->you.y ? 0x20 : 0x60;
	bt->foe.heading = (uint8_t)(rng_below(&s->rng, 81) +
	    bt->you.heading - 40);
	bt->foe.target = bt->foe.heading;
	bt->decision = 1;
}

void
sim_fort_battle_start(struct sim *s, int town)
{
	struct sim_battle *bt = &s->battle;
	struct sim_encounter *e = &s->encounter;
	const struct sim_town *t;

	if (town < 0 || (size_t)town >= s->ntowns)
		return;
	t = &s->towns[town];
	/* The fort as an encounter (VA 0x18b9c): half the garrison, 12
	 * guns a fort. */
	memset(e, 0, sizeof(*e));
	e->type = FORT_TYPE;
	e->nation = t->nation;
	e->crew = (t->soldiers + 1) / 2;
	e->guns = (t->forts & 7) * 12;
	e->role = 0;
	e->town = town;
	memset(bt, 0, sizeof(*bt));
	bt->active = 1;
	bt->fort = 1;
	bt->wind = s->wind;
	bt->wind_strength = rng_below(&s->rng, 3);
	bt->set = SET_FORT;
	bt->screen = t->id < BATTLE_SCREENS ? t->id : 0;
	flagship(s);
	bt->foe.type = FORT_TYPE;
	bt->foe.guns = e->guns;
	bt->foe.crew = e->crew * 4;
	bt->foe.role = 0;
	/* On the town's screen (VA 0xd16c). */
	bt->you.x = (t->ship_x * BATTLE_TILE + 16 + CENTRE_X) * FP;
	bt->you.y = (t->ship_y * BATTLE_TILE + 16 + CENTRE_Y) * FP;
	bt->you.heading = (uint8_t)t->ship_heading;
	bt->foe.x = (t->fort_x * BATTLE_TILE + FORT_BOX / 2) * FP;
	bt->foe.y = (t->fort_y * BATTLE_TILE + FORT_BOX / 2) * FP;
	bt->foe.heading = (uint8_t)rng_below(&s->rng, 256);
	bt->decision = 1;
}

/* Terrain under field pixel (x, y): the colour of the battle tile drawn
 * there; open sea off the field. */
static int
field_terrain(const struct sim *s, int x, int y)
{
	const struct sim_battle *bt = &s->battle;
	int tile;

	if (x < 0 || y < 0 || x >= FIELD || y >= FIELD)
		return (TERRAIN_SEA);
	tile = s->world->battle_screens[bt->screen][(y / BATTLE_TILE) *
	    BATTLE_SCREEN + x / BATTLE_TILE];
	return (s->world->colour_class[s->world->battle_tiles[tile *
	    BATTLE_TILE * BATTLE_TILE + (y % BATTLE_TILE) * BATTLE_TILE +
	    x % BATTLE_TILE]]);
}

/*
 * The shallows (VA 0x81fb, 0x8732): once a ship's cooldown has run out,
 * a roll of rand(16) at or above its class (the table at VA 0x327ce
 * holds 0..7) costs 16 damage and starts 20 ticks of cooldown.  Returns
 * 1 when it hurt.
 */
static int
scrape(struct sim *s, struct battle_ship *b, int *cooldown)
{
	if (*cooldown > 0) {
		(*cooldown)--;
		return (0);
	}
	if ((b->type & 7) > rng_below(&s->rng, 16))
		return (0);
	b->damage += SCRAPE_DAMAGE;
	*cooldown = SCRAPE_TICKS;
	return (1);
}

/* Do the quarter-size boxes of the two ships touch (VA 0x9c84)? */
static int
touching(const struct sim *s)
{
	const struct sim_battle *bt = &s->battle;
	struct sideinfo by, be;

	box(s, &bt->you, &by);
	box(s, &bt->foe, &be);
	if (!bt->fort) {
		be.half_w >>= 2;
		be.half_h >>= 2;
	}
	return (abs(px(bt->foe.x) - px(bt->you.x)) <= be.half_w +
	    (by.half_w >> 2) && abs(px(bt->foe.y) - px(bt->you.y)) <=
	    be.half_h + (by.half_h >> 2));
}

/* Speed a ship heads for (game-rules.md, "Each tick"). */
static int
ship_speed(const struct sim *s, const struct battle_ship *b, int enemy)
{
	const struct sim_battle *bt = &s->battle;
	int sector, v, base;

	sector = ((b->heading + 8 - bt->wind) >> 4) & 15;
	v = s->world->speed[b->type & 7][sector];
	if (enemy && v == battle_level(s))
		v--;		/* as traced; purpose unknown */
	base = v * 4 - b->damage / 4;
	if (b->full_sails)
		base += base >= 0 ? base / 2 : -((-base + 1) / 2);
	v = sim_div16(base * 8, 8 - (3 - b->turn_rate) * bt->wind_strength);
	return (v > MAX_SPEED ? MAX_SPEED : v);
}

static void
move_ship(const struct sim *s, struct battle_ship *b, int half)
{
	int dx = sim_sine_mul(s->world, b->heading, b->speed) * 4;
	int dy = sim_sine_mul(s->world, b->heading + 0x40, b->speed) * 4;

	if (half) {		/* the player before a fort (VA 0x7f6c) */
		dx >>= 1;
		dy >>= 1;
	}
	b->x += dx;
	b->y -= dy;
}

/* Flight time to the other ship: d / 2 ticks, d at least 16. */
static int
flight_ticks(const struct sim_battle *bt)
{
	int dx = abs(px(bt->you.x) - px(bt->foe.x));
	int dy = abs(px(bt->you.y) - px(bt->foe.y));
	int d = (dx > dy ? dx : dy) + (dx > dy ? dy : dx) / 4;

	return ((d < 16 ? 16 : d) / 2);
}

static void
fire(struct sim_battle *bt, struct battle_ship *b, int bearing, int enemy)
{
	int side = sign(enemy ? heading_diff(bearing, b->heading) :
	    heading_diff(b->heading, bearing));

	b->shot.heading = (uint8_t)(b->heading + side * 0x40);
	b->shot.x = b->x;
	b->shot.y = b->y;
	b->shot.ticks = flight_ticks(bt);
}

/* Does the shot land inside the target's box, widened by "steps"
 * quarter sizes? */
static int
shot_hits(const struct sim *s, const struct battle_shot *sh,
    const struct battle_ship *t, int steps)
{
	struct sideinfo bx;
	int w, h;

	box(s, t, &bx);
	w = bx.half_w + steps * (bx.half_w >> 2);
	h = bx.half_h + steps * (bx.half_h >> 2);
	return (abs(px(sh->x) - px(t->x)) <= w &&
	    abs(px(sh->y) - px(t->y)) <= h);
}

/* A hit by guns "g" on ship "t" (game-rules.md, damage of a hit). */
static void
hit(struct sim *s, struct battle_ship *t, int g, int bonus)
{
	int fort = s->battle.fort && t == &s->battle.foe;
	int c = fort ? FORT_CLASS : t->type & 7, dmg;

	dmg = sim_div16(g * 4, c + 2);
	if (!t->full_sails)
		dmg >>= 1;
	if (dmg == 0)
		dmg = 1;
	if (bonus && !fort)
		dmg += dmg >> 1;
	if (fort)		/* a fort is not sunk (VA 0x8630) */
		s->battle.fort_damage += dmg;
	else
		t->damage += dmg;
	t->crew -= ((rng_below(&s->rng, 64)) + 4 * sim_div16(t->crew * g,
	    c + 3)) >> 6;
	t->guns -= ((rng_below(&s->rng, 64)) + sim_div16(t->guns * 4 * g,
	    c + 3)) >> 6;
	if (t->crew < 0)
		t->crew = 0;
	if (t->guns < 0)
		t->guns = 0;
}

static int
fly_shot(struct sim *s, struct battle_ship *from, struct battle_ship *to,
    int steps, int bonus)
{
	struct battle_shot *sh = &from->shot;

	if (sh->ticks == 0)
		return (0);
	sh->x += sim_sine_mul(s->world, sh->heading, SHOT_SPEED) * 4;
	sh->y -= sim_sine_mul(s->world, sh->heading + 0x40, SHOT_SPEED) * 4;
	if (--sh->ticks > 0)
		return (0);
	if (!shot_hits(s, sh, to, steps))
		return (0);
	hit(s, to, from->guns, bonus);
	return (1);
}

/* The enemy's choice every 64 ticks (game-rules.md, enemy decisions). */
static void
decide(struct sim *s, int bearing)
{
	struct sim_battle *bt = &s->battle;
	struct battle_ship *y = &bt->you, *e = &bt->foe;
	int u = 0, eff = (e->crew * e->role) >> 3, h, d;
	struct sideinfo by, be;

	if (e->damage < (y->damage >> 1) + 1)
		u += 9;
	if (e->damage < y->damage * 2)
		u += 9;
	if ((y->guns >> 1) + 1 < e->guns)
		u += 3;
	if (y->guns * 2 < e->guns)
		u += 3;
	if ((y->crew >> 1) + 1 < eff)
		u += 1;
	if (y->crew * 2 < eff)
		u += 1;
	box(s, y, &by);
	box(s, e, &be);
	if (u == 0 && abs(px(e->x) - px(y->x)) <= 2 * (be.half_w + by.half_w)
	    && abs(px(e->y) - px(y->y)) <= 2 * (be.half_h + by.half_h)) {
		bt->outcome = BAT_SURRENDER;
		return;
	}
	bt->decision = s->world->battle_ai[u] % 3;
	e->full_sails = (bt->decision & 1) ^ 1;
	h = bt->decision * 0x40 + bearing;
	if (bt->decision == 1) {
		if (abs(heading_diff(h, e->heading)) > 0x40)
			h += 0x80;
		if (abs(heading_diff(h, bt->wind)) > 0x5F)
			h += 0x80;
	} else {
		/* Never steer more than 0x50 from downwind. */
		d = clampi(heading_diff(h, bt->wind) + 0x50, 0, 0xA0);
		h = d - 0x50 + bt->wind;
	}
	e->target = (uint8_t)(h & 0xF0);
}

static int
sunk(const struct battle_ship *b)
{
	return (b->damage / 16 >= SINK_LEVEL);
}

int
sim_battle_tick(struct sim *s, int turn, int fire_now, int sails)
{
	struct sim_battle *bt = &s->battle;
	struct battle_ship *y = &bt->you, *e = &bt->foe;
	int ev = 0, bearing, lx, ly;

	if (!bt->active || bt->outcome != BAT_NONE || s->world == NULL)
		return (0);
	bt->tick++;

	/* Steering, sails and reloading. */
	if (bt->tick % TURN_TICKS == 0 && turn != 0)
		y->heading = (uint8_t)(y->heading + 16 * sign(turn));
	if (y->sail_timer > 0) {
		if (--y->sail_timer == 0) {
			y->full_sails = !y->full_sails;
			ev |= BEV_SAILS;
		}
	} else {
		if (sails != 0 && (sails > 0) != y->full_sails)
			y->sail_timer = SAIL_TICKS;
		else if (y->reload > 0 && --y->reload == 0)
			ev |= BEV_RELOADED;
	}

	/* Bearing from the enemy to where the player will be next tick. */
	lx = px(y->x + sim_sine_mul(s->world, y->heading, y->speed) * 4) -
	    px(e->x);
	ly = px(y->y - sim_sine_mul(s->world, y->heading + 0x40, y->speed) *
	    4) - px(e->y);
	bearing = sim_bearing(lx, ly);

	if (fire_now && y->reload == 0 && y->guns > 0 && y->shot.ticks == 0) {
		fire(bt, y, bearing, 0);
		y->reload = abs(144 - 8 * clampi(s->fleet.gunnery / 2 + 4, 0,
		    16));
		ev |= BEV_FIRED;
	}
	if (fly_shot(s, y, e, s->world->hit_steps[battle_level(s)],
	    battle_level(s) == 0))
		ev |= BEV_HIT_FOE;

	y->speed = ship_speed(s, y, 0);
	move_ship(s, y, bt->fort);
	/* The player's ship on the shallows or on land (VA 0x81f0): land
	 * beaches it, unless the enemy is alongside. */
	switch (field_terrain(s, px(y->x), px(y->y))) {
	case TERRAIN_SHALLOWS:
		if (scrape(s, y, &bt->you_scrape))
			ev |= BEV_AGROUND;
		break;
	case TERRAIN_LAND:
		bt->outcome = !touching(s) ? BAT_BEACHED : bt->fort ?
		    BAT_LANDED : BAT_BOARDING;
		return (ev);
	default:
		break;
	}

	/* A fort neither decides nor moves (VA 0x8914); before each shot
	 * it turns its broadside to the player (VA 0x8eb5). */
	if (bt->fort) {
		if (e->reload > 0)
			e->reload--;
		if (e->shot.ticks == 0 && e->guns > 0 && e->reload == 0) {
			e->heading = (uint8_t)(rng_below(&s->rng, 8) +
			    bearing + 0x3C);
			fire(bt, e, bearing, 1);
			e->reload = abs(144 - 8 * e->role);
			ev |= BEV_FOE_FIRED;
		}
		if (fly_shot(s, e, y, 0, 0)) {
			ev |= BEV_HIT_YOU;
			y->crew = clampi(y->crew, 1, 999);
		}
		goto wind;
	}
	/* The enemy: decisions, steering, firing, moving. */
	if (bt->tick % AI_TICKS == 0)
		decide(s, bearing);
	if (bt->outcome != BAT_NONE)
		return (ev);
	if (bt->tick % TURN_TICKS == 0 && e->heading != e->target)
		e->heading = (uint8_t)(e->heading + e->turn_rate *
		    sign(heading_diff(e->target, e->heading)));
	if (e->reload > 0)
		e->reload--;
	if (e->shot.ticks == 0 && e->guns > 0 && e->reload == 0 &&
	    abs(abs(heading_diff(bearing, e->heading)) - 0x40) < 6) {
		fire(bt, e, bearing, 1);
		e->reload = abs(144 - 8 * e->role);
		ev |= BEV_FOE_FIRED;
	}
	if (fly_shot(s, e, y, 0, 0)) {
		ev |= BEV_HIT_YOU;
		y->crew = clampi(y->crew, 1, 999);
	}
	e->speed = ship_speed(s, e, 1);
	move_ship(s, e, 0);
	if (field_terrain(s, px(e->x), px(e->y)) == TERRAIN_SHALLOWS &&
	    scrape(s, e, &bt->foe_scrape))
		ev |= BEV_FOE_AGROUND;

	if (scroll(s))
		ev |= BEV_SCROLLED;

	/* The wind may shift. */
wind:
	if (bt->tick % WIND_TICKS == 0) {
		int w = (uint8_t)((rng_below(&s->rng, 3) - 1) * 16 + bt->wind);

		if (w != bt->wind) {
			bt->wind = (uint8_t)w;
			bt->wind_strength = clampi(bt->wind_strength +
			    rng_below(&s->rng, 3) - 1, 0, 2);
			ev |= BEV_WIND;
		}
	}

	/* How it may end. */
	if (sunk(y))
		bt->outcome = BAT_PLAYER_SUNK;
	else if (bt->fort) {
		/* Out of the fort's reach (VA 0x88e0). */
		int cx = px(y->x) - CENTRE_X, cy = px(y->y) - CENTRE_Y;

		if (cx < FORT_LO || cx > FORT_HI || cy < FORT_LO ||
		    cy > FORT_HI)
			bt->outcome = BAT_ESCAPED;
	} else if (sunk(e))
		bt->outcome = BAT_ENEMY_SUNK;
	else {
		if (touching(s))
			bt->outcome = BAT_BOARDING;
		else if (abs(px(e->x) - px(y->x)) > ESCAPE_RANGE ||
		    abs(px(e->y) - px(y->y)) > ESCAPE_RANGE)
			bt->outcome = BAT_ESCAPED;
	}
	if (bt->outcome == BAT_NONE && bt->tick >= SUNSET_TICKS)
		bt->outcome = BAT_SUNSET;
	return (ev);
}

/*
 * Lose ship "i" (VA 0x1f2a1).  The fleet loses its room in the hold
 * and, unless "keep", the ship's share of the gold, food, goods, sugar
 * and guns: r = its room / the fleet's, in single precision.  The last
 * ship leaves the player castaway and picked up (CSTR 161-162, or
 * 175 and 190 from prison): a pinnace, as an engine choice for now.
 */
void
sim_drop_ship(struct sim *s, int i, int keep)
{
	struct sim_fleet *f = &s->fleet;
	int room, k, v;
	float r;

	if (i < 0 || i >= f->nships)
		return;
	if (f->nships == 1) {
		f->ships[0] = 0;
		f->damage[0] = 0;
		f->capacity = sim_ship_room(0);
		if (f->crew < 8)
			f->crew = 8;
		return;
	}
	room = sim_ship_room(f->ships[i]);
	r = f->capacity > 0 ? (float)room / (float)f->capacity : 0.0f;
	f->capacity -= room;
	if (f->capacity < 0)
		f->capacity = 0;
	for (k = i; k + 1 < f->nships; k++) {
		f->ships[k] = f->ships[k + 1];
		f->damage[k] = f->damage[k + 1];
	}
	f->nships--;
	if (keep)
		return;
	f->gold -= (long)((float)f->gold * r);
	v = f->food << 8;			/* with its eaten fraction */
	v -= (int)((double)((float)v * r) + 0.5);
	f->food = v >> 8;
	f->goods -= (int)((double)((float)f->goods * r) + 0.5);
	f->sugar -= (int)((double)((float)f->sugar * r) + 0.5);
	f->guns -= (int)((double)((float)f->guns * r) + 0.5);
}

static void
capture(struct sim *s, int keep)
{
	struct sim_fleet *f = &s->fleet;
	const struct sim_battle *bt = &s->battle;

	/* Stand-in plunder until cargo and treasure are traced. */
	f->gold += (long)s->encounter.gold * 10;
	if (keep && f->nships < MAX_SHIPS) {
		f->ships[f->nships] = (uint8_t)bt->foe.type;
		f->capacity += sim_ship_room(bt->foe.type);
		f->damage[f->nships] = (uint8_t)clampi(bt->foe.damage, 0, 63);
		f->nships++;
		f->guns += bt->foe.guns;
	}
}

enum battle_outcome
sim_battle_finish(struct sim *s, int keep)
{
	struct sim_battle *bt = &s->battle;
	struct sim_fleet *f = &s->fleet;
	enum battle_outcome o = (enum battle_outcome)bt->outcome;

	if (!bt->active)
		return (BAT_NONE);
	/* Boarding: the sword fight decides, and its losses count. */
	if (o == BAT_BOARDING) {
		const struct sim_sword *sw = &s->sword;

		if (!sw->active || sw->result == SWORD_NONE)
			return (BAT_NONE);
		o = sw->result == SWORD_WON ? BAT_SURRENDER :
		    sw->result == SWORD_REPELLED ? BAT_REPELLED : BAT_DEFEATED;
		bt->you.crew = sw->you.men;
		s->sword.active = 0;
	}
	f->crew = bt->you.crew + bt->crew_ashore;
	f->guns = bt->you.guns + bt->guns_ashore;
	f->damage[0] = (uint8_t)clampi(bt->you.damage, 0, 63);
	switch (o) {
	case BAT_DEFEATED:	/* the enemy takes the flagship */
	case BAT_PLAYER_SUNK:
		f->crew = bt->crew_ashore;
		f->guns = bt->guns_ashore;
		sim_drop_ship(s, 0, 0);		/* VA 0xe81d */
		break;
	case BAT_SURRENDER:
		capture(s, keep);
		break;
	default:
		break;
	}
	/* The reputation (VA 0x45e76): a capture raises it, a boarding
	 * that fails lowers it by 2 (VA 0xdb7b, 0xe775). */
	if (o == BAT_SURRENDER) {
		sim_reputation(s, 1);
		/* A capture offends the ship's nation and pleases its
		 * enemies (VA 0xdb7b); pirates do not count. */
		sim_attack_nation(s, s->encounter.nation, -1);
		/* The pirate a governor wanted (VA 0xdb7b). */
		if (s->encounter.named)
			sim_mission_done(s);
	}
	else if (o == BAT_REPELLED || o == BAT_DEFEATED)
		sim_reputation(s, -2);
	bt->active = 0;
	return (o);
}
