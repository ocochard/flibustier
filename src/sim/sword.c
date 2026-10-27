/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Sword fights, following the "Sword fights" section of
 * aidd_docs/memory/internal/game-rules.md.  The tables come from the
 * player's executable (struct sword_tables).
 */

#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define CREW_YOU_X	340	/* start positions on the line */
#define CREW_FOE_X	220
#define DUEL_YOU_X	320
#define DUEL_FOE_X	240
#define FAR		59	/* beyond: attacks become advances */
#define CLOSE		20	/* the opponent fights from its script */
#define NO_STEP		9	/* steps apply above this distance */
#define CLASH_YOU	36
#define CLASH_FOE	35
#define PUSH		4	/* a hit pushes the target back */
#define BAR_TOTAL	47
#define BAR_SPAN	31
#define BAR_MIN		8
#define MELEE_TICKS	16
#define MAX_MEN		9999
#define FALL_END	8	/* a fall ends the fight after this frame */
#define BEATEN_FOE	1
#define BEATEN_YOU	3

static int
clampi(int v, int lo, int hi)
{
	return (v < lo ? lo : v > hi ? hi : v);
}

static int
is_lunge(int move)
{
	return (move >= SWM_LUNGE && move < SWM_LUNGE + 3);
}

static int
row(const struct sword_side *f)
{
	return ((f->move & (SWORD_MOVES - 1)) * SWORD_FRAMES +
	    clampi(f->frame, 0, SWORD_FRAMES - 1));
}

/* One step of the strength bar toward the player, or the opponent. */
static void
bar_to_you(struct sim_sword *sw)
{
	if (sw->foe.bar > 0) {
		sw->you.bar++;
		if (--sw->foe.bar < 1)
			sw->beaten = BEATEN_FOE;
	}
}

static void
bar_to_foe(struct sim_sword *sw)
{
	if (sw->you.bar > 0) {
		sw->foe.bar++;
		if (--sw->you.bar < 1)
			sw->beaten = BEATEN_YOU;
	}
}

void
sim_sword_start(struct sim *s, const struct sword_setup *in)
{
	struct sim_sword *sw = &s->sword;
	long you, foe;

	memset(sw, 0, sizeof(*sw));
	sw->active = 1;
	sw->kind = in->kind;
	sw->title = in->title;
	sw->nation = in->nation;
	sw->level = clampi(s->player.difficulty + in->extra -
	    (s->player.ability == ABILITY_FENCING), 0, BATTLE_LEVELS - 1);
	sw->you.men = in->you_men;
	sw->foe.men = in->foe_men;
	sw->men_start = in->you_men;
	sw->you.weapon = clampi(in->weapon, 0, SWORD_WEAPONS - 1);
	sw->foe.weapon = in->kind == SWORD_CREW ?
	    rng_below(&s->rng, 2) + 1 : 0;

	you = (long)in->you_men * in->you_factor;
	foe = (long)in->foe_men * in->foe_factor;
	sw->you.bar = you + foe != 0 ?
	    (int)(BAR_SPAN * you / (you + foe)) + BAR_MIN :
	    BAR_TOTAL / 2;
	sw->foe.bar = BAR_TOTAL - sw->you.bar;

	sw->you.x = in->kind == SWORD_CREW ? CREW_YOU_X : DUEL_YOU_X;
	sw->foe.x = in->kind == SWORD_CREW ? CREW_FOE_X : DUEL_FOE_X;
	sw->you.move = -1;		/* the first input counts at once */
	sw->foe.move = SWM_GUARD;
	sw->script = rng_below(&s->rng, SWORD_SCRIPTS);
	sw->counter = 1;
}

void
sim_sword_input(struct sim *s, int move)
{
	if (move >= 0 && move < SWM_ADVANCE)
		s->sword.input = move;
}

/* The script bank of a level: 0, 1, then 2 for the rest. */
static int
bank(int level)
{
	return (level < 2 ? level : 2);
}

/* A script step of 0x90-0x93: a move chosen against the player's line. */
static int
counter_move(struct sim *s, int code)
{
	const struct sword_tables *t = &s->world->sword;
	int line = t->line[s->sword.you.move & (SWORD_MOVES - 1)], v;

	if (code == 0x90)
		return (line);
	if (code == 0x91)
		return (line + SWM_BACK);
	v = rng_below(&s->rng, 2) + 1 + line;	/* one of the other lines */
	if (v > 2)
		v = 0;
	return (v + (code == 0x92 ? SWM_SLASH : SWM_LUNGE));
}

/* The opponent's next move, when its move ends (VA 0x280ef). */
static void
foe_choose(struct sim *s)
{
	const struct sword_tables *t = &s->world->sword;
	struct sim_sword *sw = &s->sword;
	int m;

	if (sw->foe.move == SWM_FALL)
		return;
	if ((unsigned)(sw->you.x - sw->foe.x) < CLOSE) {
		if ((rng_below(&s->rng, 16)) > t->caution[sw->level]) {
			sw->step = (sw->step + 1) & (SWORD_STEPS - 1);
			if (sw->step == 0 && ((sw->level == 3 &&
			    (sw->you_hits != 0 || sw->foe_hits != 0)) ||
			    (sw->level == 2 && sw->you_hits != 0) ||
			    (sw->level == 1 && sw->you_hits > 2))) {
				sw->script = rng_below(&s->rng, SWORD_SCRIPTS);
				sw->you_hits = 0;
			}
			m = t->scripts[bank(sw->level)][sw->script][sw->step];
			if (m >= 0x80)
				m = counter_move(s, m);
		} else
			m = SWM_GUARD + 1;
	} else
		m = t->far_ai[rng_below(&s->rng, 8)];
	sw->foe.move = m & 0xFF;
	sw->foe.frame = 0;
}

/* The player's next move, when the current one ends. */
static void
you_choose(struct sim *s)
{
	const struct sword_tables *t = &s->world->sword;
	struct sim_sword *sw = &s->sword;
	int m = sw->input;

	if (sw->you.x - sw->foe.x > FAR)
		m = t->far_move[m];
	if (sw->foe.move == SWM_FALL)
		m = SWM_GUARD + 1;
	sw->you.move = m;
	sw->you.frame = 0;
	if (t->once[m])
		sw->input = SWM_GUARD;
}

/* Count a hit by the body area the strike aims at. */
static void
wound(struct sword_side *target, int area)
{
	target->wounds[area >= 1 && area <= 2 ? area - 1 : 2]++;
}

/* The crew melee every 16 ticks: a roll weighted by men and bar. */
static int
melee(struct sim *s)
{
	struct sim_sword *sw = &s->sword;
	int16_t pv, ov;
	int pr, orl, lost;

	pv = (int16_t)((((unsigned)(uint16_t)(sw->you.men + 8)) >> 2) *
	    sw->you.bar);
	ov = (int16_t)((((unsigned)(uint16_t)(sw->foe.men + 8)) >> 2) *
	    sw->foe.bar);
	pr = pv != 0 ? rng_below(&s->rng, (uint32_t)abs(pv)) : 0;
	orl = ov != 0 ? rng_below(&s->rng, (uint32_t)abs(ov)) : 0;
	lost = (((unsigned)(uint16_t)(sw->you.men + sw->foe.men)) >> 6) + 1;
	if (orl < pr) {
		sw->foe.men = clampi(sw->foe.men - lost, 1, MAX_MEN);
		if (sw->foe.men == 1)
			sw->beaten = BEATEN_FOE;
		bar_to_you(sw);
		return (SEV_FOE_MEN);
	}
	sw->you.men = clampi(sw->you.men - lost, 1, MAX_MEN);
	if (sw->you.men == 1)
		sw->beaten = BEATEN_YOU;
	bar_to_foe(sw);
	return (SEV_YOU_MEN);
}

int
sim_sword_tick(struct sim *s)
{
	struct sim_sword *sw = &s->sword;
	const struct sword_tables *t;
	struct sword_side *y = &sw->you, *e = &sw->foe;
	int ev = 0, dist, idx, strike, i, passes;

	if (!sw->active || sw->result != SWORD_NONE || s->world == NULL)
		return (0);
	t = &s->world->sword;
	sw->counter++;
	sw->sound = 0;
	sw->effect = -1;
	dist = y->x - e->x;

	/* The player's frame. */
	if (y->move < 0 || ++y->frame >= t->frames[y->move])
		you_choose(s);
	idx = row(y);
	y->guard = t->guard[idx];
	strike = t->strike[idx];
	if (strike != 0) {
		y->guard = 0;
		sw->sound = FM_SWING;
		if ((strike & e->guard) == 0) {
			sw->sound = FM_MISS;
			if (dist <= t->reach_level[sw->level] +
			    t->reach_weapon[y->weapon] &&
			    t->hit[sw->level] <= rng_below(&s->rng, 256) +
			    s->practice) {
				ev |= SEV_HIT_FOE;
				sw->sound = FM_BLOW;
				sw->effect = 7;
				e->x -= PUSH;
				passes = y->weapon * 2;
				for (i = 0; i <= passes; i++) {
					bar_to_you(sw);
					if (!is_lunge(y->move))
						bar_to_you(sw);
				}
				sw->you_hits++;
				wound(e, t->area[y->move]);
				if (sw->beaten == BEATEN_FOE &&
				    e->move != SWM_FALL) {
					e->move = SWM_FALL;
					e->frame = 0;
					sw->effect = 3;	/* the last man falls */
				} else if (dist < CLASH_YOU) {
					/* The original falls into the clash
					 * test (VA 0x279c0): close in, the
					 * blow sounds as blades meeting. */
					ev |= SEV_CLASH;
					sw->sound = FM_SWING;
					sw->effect = 6;
				}
			}
		} else if (dist < CLASH_YOU) {
			ev |= SEV_CLASH;
			sw->sound = FM_SWING;
			sw->effect = 6;
		}
	}
	if (dist > NO_STEP)
		y->x += t->step_back[idx] - t->step_in[idx];
	if (y->x >= t->player_edge)
		sw->result = SWORD_REPELLED;
	dist = y->x - e->x;

	/* The opponent's frame. */
	if (++e->frame >= t->frames[e->move & (SWORD_MOVES - 1)])
		foe_choose(s);
	idx = row(e);
	e->guard = t->guard[idx];
	strike = t->strike[idx];
	if (strike != 0) {
		e->guard = 0;
		sw->sound = FM_BLOW;	/* his swing, a miss sounds the same */
		if ((strike & y->guard) == 0) {
			if (dist <= t->foe_reach[e->weapon] &&
			    t->foe_hit[sw->level] <= rng_below(&s->rng, 256)) {
				ev |= SEV_HIT_YOU;
				sw->effect = 8;
				y->x += PUSH;
				for (i = 0; i <= e->weapon; i++) {
					bar_to_foe(sw);
					if (!is_lunge(e->move))
						bar_to_foe(sw);
				}
				sw->foe_hits++;
				wound(y, t->area[e->move]);
				if (sw->beaten == BEATEN_YOU) {
					y->move = SWM_FALL;
					y->frame = 0;
				}
			}
		} else if (dist < CLASH_FOE) {
			ev |= SEV_CLASH;
			sw->sound = FM_SWING;
			sw->effect = 6;
		}
	}
	if (dist > NO_STEP)
		e->x += t->step_in[idx] - t->step_back[idx];
	if (e->x <= t->foe_edge)
		sw->result = SWORD_WON;

	if (sw->kind == SWORD_CREW && (sw->counter & (MELEE_TICKS - 1)) == 0) {
		i = melee(s);
		ev |= i;
		sw->sound = i == SEV_FOE_MEN ? FM_THEIR_MEN : FM_OUR_MEN;
	}
	if (sw->counter == 1) {		/* nothing on the first tick */
		sw->sound = 0;
		sw->effect = -1;
	}

	if (y->move == SWM_FALL && y->frame > FALL_END)
		sw->result = SWORD_LOST;
	if (e->move == SWM_FALL && e->frame > FALL_END)
		sw->result = SWORD_WON;
	return (ev);
}

void
sim_board(struct sim *s, int weapon)
{
	const struct sim_battle *bt = &s->battle;
	struct sword_setup in;

	memset(&in, 0, sizeof(in));
	in.kind = SWORD_CREW;
	in.you_men = bt->you.crew;
	in.you_factor = 7 - bt->you.damage / 16;
	in.foe_men = bt->foe.crew;
	in.foe_factor = bt->foe.role - bt->foe.damage / 16;
	in.title = 4;			/* "Captain" */
	in.nation = s->encounter.nation;
	in.extra = 0;			/* named captains are not modelled */
	in.weapon = weapon;
	sim_sword_start(s, &in);
	s->sword.scene = 3;			/* the deck */
}

void
sim_practice_start(struct sim *s, int weapon)
{
	struct sword_setup in;

	memset(&in, 0, sizeof(in));
	in.kind = SWORD_PRACTICE;
	in.you_men = in.foe_men = 200;
	in.you_factor = in.foe_factor = 32;
	in.title = 5;				/* the first mate */
	in.nation = 5;
	in.weapon = weapon;
	sim_sword_start(s, &in);
	s->sword.scene = 3;
}

void
sim_practice_end(struct sim *s)
{
	s->sword.active = 0;
	if (s->practice < 30)
		s->practice += 5;
}
