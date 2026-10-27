/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * The relatives' villains and the governors' missions: the "Villains"
 * and "Missions" sections of aidd_docs/memory/internal/game-rules.md
 * (VA 0x12178, 0x18913, 0x12e88, 0x1f8f7).
 */

#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define RELATIVES	4
#define DRAWS_MAX	4096	/* the original draws for ever */

static int8_t
clamp8(int v)
{
	return ((int8_t)(v < -128 ? -128 : v > 127 ? 127 : v));
}

/* Town field 9 is a signed word. */
static void
town_word9(struct sim_town *t, int d)
{
	t->standing = (uint16_t)(int16_t)((int16_t)t->standing + d);
}

int
sim_villain_appear(struct sim *s)
{
	struct sim_villain *v;
	int k, t, tries = 0;

	/* None in a famous expedition (VA 0x12549: bit 0 of VA 0x45e84,
	 * set by its choice, VA 0x15bc6). */
	if (s->relative >= RELATIVES || s->player.expedition >= 0 ||
	    s->villains[SIM_VILLAINS - 1].active || s->ntowns == 0)
		return (-1);
	for (k = 0; s->villains[k].active; k++)
		;
	/* A Spanish town. */
	do
		t = rng_below(&s->rng, (int)s->ntowns);
	while (s->towns[t].nation != NATION_SPAIN && ++tries < DRAWS_MAX);
	v = &s->villains[k];
	v->active = 1;
	v->town = t;
	v->trail[0] = v->trail[1] = v->trail[2] = v->trail[3] = -1;
	v->surname = rng_below(&s->rng, 16);
	v->rank = rng_below(&s->rng, 8);
	v->nation = rng_below(&s->rng, 4);
	sim_log_news(s, NEWS_VILLAIN, k, t);
	return (k);
}

int
sim_villain_at(const struct sim *s, int town)
{
	int k;

	if (town < 0)
		return (-1);
	for (k = 0; k < SIM_VILLAINS; k++)
		if (s->villains[k].active && s->villains[k].town == town)
			return (k);
	return (-1);
}

void
sim_villain_duel(struct sim *s, int villain, int weapon)
{
	struct sword_setup in;

	if (villain < 0 || villain >= SIM_VILLAINS || s->ntowns == 0)
		return;
	/* He flees to any town, whatever the fight's outcome. */
	s->villains[villain].town = rng_below(&s->rng, (int)s->ntowns);
	s->villain_duel = villain;
	memset(&in, 0, sizeof(in));
	in.kind = SWORD_DUEL;
	in.you_men = in.foe_men = 1;
	in.you_factor = in.foe_factor = 9;
	in.title = 0;
	in.nation = -2;			/* labelled with the villain's name */
	in.extra = 0;
	in.weapon = weapon;
	sim_sword_start(s, &in);
	s->sword.scene = SCENE_ALLEY;
}

int
sim_villain_duel_end(struct sim *s)
{
	int k = s->villain_duel, won = s->sword.result == SWORD_WON;

	sim_duel_end(s);
	s->villain_duel = -1;
	if (!won || k < 0)
		return (0);
	s->villains[k].active = 0;
	sim_map_piece(s, MAP_RELATIVE);
	return (1);
}

int
sim_mission_report(struct sim *s)
{
	struct sim_mission *m = &s->mission;
	struct sim_town *t;
	int r, n;

	if (m->type == MISSION_NONE || s->player_town < 0 ||
	    m->giver != s->player_town)
		return (0);
	t = &s->towns[s->player_town];
	if (m->type < 0) {
		r = m->type == -MISSION_PIRATE ? 2 : 1;
		n = m->nation & 3;
		s->standing[n] = clamp8(s->standing[n] + 1);
		town_word9(t, m->type);		/* less by the type */
	} else {
		r = -1;
		n = t->nation & 3;
		s->standing[n] = clamp8(s->standing[n] - 1);
		town_word9(t, -1);
		sim_log_news(s, NEWS_DISPLEASED, 0, 0);
	}
	m->type = MISSION_NONE;
	return (r);
}

/* Relations between two nations, read from the game's 4 x 4 table as
 * a flat array (a town's nation 4 reads the next row). */
static int
relation(const struct sim *s, int a, int b)
{
	int i = a * 4 + b;

	return (i >= 0 && i < 16 ? s->relations[i / 4][i % 4] : 0);
}

int
sim_mission_draw(struct sim *s)
{
	struct sim_mission *o = &s->mission_offer;
	const struct sim_town *here;
	int type, i, t = 0, c, gov, tries = 0;

	o->type = MISSION_NONE;
	if (s->mission.type != MISSION_NONE || s->player_town < 0 ||
	    s->ntowns < 2)
		return (0);
	here = &s->towns[s->player_town];
	gov = here->nation & 3;
	do {
		type = rng_below(&s->rng, 5) + 1;
		for (i = 0; i < 10; i++) {
			t = rng_below(&s->rng, (int)s->ntowns);
			if (t == s->player_town)
				continue;
			c = relation(s, gov, s->towns[t].nation);
			if ((type & 1) == 0) {
				if (c < 0)	/* papers, the son: an enemy */
					break;
			} else if ((type == MISSION_PIRATE &&
			    s->towns[t].nation == gov &&
			    !(s->towns[t].flags & 0x40)) ||
			    (type != MISSION_PIRATE && c >= 0))
				break;
		}
	} while (i == 10 && ++tries < DRAWS_MAX);
	if (i == 10 || s->standing[gov] >> 2 < type || type == 5 ||
	    (int16_t)here->standing < 0)
		return (0);
	o->type = type;
	o->giver = s->player_town;
	o->dest = t;
	o->nation = gov;
	o->pirate = 0;
	return (type);
}

void
sim_mission_accept(struct sim *s)
{
	struct sim_mission *o = &s->mission_offer;
	int r;

	if (o->type == MISSION_NONE)
		return;
	s->mission = *o;
	if (o->type == MISSION_PIRATE) {
		r = rng_below(&s->rng, 4);
		s->mission.pirate = ((o->nation << 2 | r) << 3) | 0x8000;
		sim_log_news(s, NEWS_PIRATE_OFF, s->mission.pirate, o->dest);
	} else
		sim_log_news(s, NEWS_LETTER + o->type - 1, o->dest, 0);
	o->type = MISSION_NONE;
}

void
sim_mission_done(struct sim *s)
{
	struct sim_mission *m = &s->mission;

	if (m->type <= 0)
		return;
	if (m->type == MISSION_PIRATE)
		sim_log_news(s, NEWS_PIRATE_TAKEN, m->pirate, m->dest);
	else
		sim_log_news(s, NEWS_DELIVERED_LETTER + m->type - 1, m->dest, 0);
	m->type = -m->type;
}

int
sim_mission_contact(const struct sim *s, int town)
{
	return (town >= 0 && (s->mission.type == MISSION_LETTER ||
	    s->mission.type == MISSION_PAPERS) && s->mission.dest == town);
}

/* Hold a captain (VA 0xdb7b): the first free slot, pirates marked. */
static void
hold_captain(struct sim *s, int id, int nation)
{
	int k;

	for (k = 0; k < SIM_PRISONERS; k++)
		if (s->prisoners[k] == 0) {
			s->prisoners[k] = (nation & 4) << 6 | (id & 0xFF);
			return;
		}
}

void
sim_hostage_answer(struct sim *s, int answer)
{
	if (s->hostage == 0)
		return;
	if (answer == 2)
		hold_captain(s, s->hostage, s->encounter.nation);
	s->hostage = 0;
}

void
sim_capture_captain(struct sim *s)
{
	int id = s->encounter.captain;

	if (id == 0 || (id & 0x8000))
		return;
	if ((s->encounter.nation & 3) == 0)	/* Spaniards and pirates */
		s->hostage = id;
	else
		hold_captain(s, id, s->encounter.nation);
}

int
sim_ransom_offer(struct sim *s, int *tries)
{
	int k, id;

	s->ransom_slot = -1;
	while (*tries < 4) {
		(*tries)++;
		k = rng_below(&s->rng, SIM_PRISONERS);
		id = s->prisoners[k] & 0xFF;
		if (id == 0)
			continue;
		s->ransom_gold = (long)((id & 7) + 1) * 50 *
		    (rng_below(&s->rng, 3) + 1) * 10;
		s->ransom_slot = k;
		return (k);
	}
	return (-1);
}

void
sim_ransom_take(struct sim *s)
{
	int k = s->ransom_slot;

	if (k < 0 || k >= SIM_PRISONERS || s->prisoners[k] == 0)
		return;
	s->fleet.gold += s->ransom_gold;
	s->prisoners[k] = 0;
	s->ransom_slot = -1;
}

int
sim_name_captain(struct sim *s, int nation)
{
	int surname, bit, id;

	do {
		if (s->surnames == 0x1F)
			s->surnames = 0;
		surname = rng_below(&s->rng, 5);
		while (s->surnames & (bit = 1 << surname))
			surname = surname < 4 ? surname + 1 : 0;
		s->surnames |= bit;
		id = surname * 8 + rng_below(&s->rng, 8) * 2 + nation * 64;
	} while (id == 0);
	return (id);
}

int
sim_daughter_meet(struct sim *s, struct daughter *d)
{
	struct sim_town *t;
	int nation, charm;

	memset(d, 0, sizeof(*d));
	if (s->player_town < 0 || (size_t)s->player_town >= s->ntowns)
		return (0);
	t = &s->towns[s->player_town];
	nation = t->nation & 3;
	d->town = s->player_town;
	d->niece = t->governor & GOV_NIECE;
	if (t->governor & (GOV_WIFE | GOV_INFORMANT | 0x20))
		return (2);
	charm = ((int)(s->fortune / 499) + s->rank[nation] * 2 +
	    s->player.difficulty) / (d->niece + 1);
	if (charm < 1 || s->married != 0)
		return (0);
	charm /= 2;
	if (s->player.ability == ABILITY_CHARM)
		charm *= 2;
	d->charm = charm;
	d->suitor = (s->player_town & 3) * 8 + nation * 32 + d->niece * 2 + 1;
	return (1);
}

int
sim_daughter_talk(struct sim *s, const struct daughter *d)
{
	if (d->charm < 1 || rng_below(&s->rng, d->charm) <= 2)
		return (0);
	s->towns[d->town].governor |= GOV_INFORMANT;
	s->romances++;
	return (1);
}

int
sim_daughter_propose(const struct daughter *d)
{
	return (d->charm < 4 ? 142 + d->charm / 2 : 144);
}

void
sim_daughter_duel(struct sim *s, const struct daughter *d, int weapon)
{
	struct sword_setup in;

	memset(&in, 0, sizeof(in));
	in.kind = SWORD_DUEL;
	in.you_men = in.foe_men = 1;
	in.you_factor = in.foe_factor = 9;
	in.title = d->suitor;
	in.nation = -1;			/* labelled with the suitor's name */
	in.extra = 0;
	in.weapon = weapon;
	sim_sword_start(s, &in);
	s->sword.scene = SCENE_ALLEY;
}

int
sim_daughter_duel_end(struct sim *s, const struct daughter *d)
{
	struct sim_town *t = &s->towns[d->town];
	int won = s->sword.result == SWORD_WON;

	sim_duel_end(s);
	if (!won)
		return (0);
	s->married = d->niece + 2;
	t->governor |= GOV_WIFE;
	t->standing = (uint16_t)((int16_t)t->standing + 2);
	return (1);
}

static int
cells_apart(const struct sim_town *a, const struct sim_town *b)
{
	return (abs(a->x - b->x) + abs(a->y - b->y));
}

void
sim_villains_move(struct sim *s)
{
	struct sim_villain *v;
	const struct sim_town *t, *here, *alarm;
	int k, i, best, score, top, r;

	if (s->ntowns < 2)
		return;
	for (k = 0; k < SIM_VILLAINS; k++) {
		v = &s->villains[k];
		if (!v->active || v->town < 0 || (size_t)v->town >= s->ntowns)
			continue;
		here = &s->towns[v->town];
		alarm = &s->towns[s->alert[NATION_SPAIN] >= 0 &&
		    (size_t)s->alert[NATION_SPAIN] < s->ntowns ?
		    s->alert[NATION_SPAIN] : 0];
		best = v->town;
		top = -999;
		/* The original's loop stops one town short of the last. */
		for (i = 0; i < (int)s->ntowns - 1; i++) {
			t = &s->towns[i];
			if (t->nation != NATION_SPAIN || t->garrison != 0)
				continue;
			r = rng_below(&s->rng, s->player.difficulty + 5);
			if (cells_apart(t, here) >= r * r)
				continue;
			score = 4 * (int16_t)t->standing -
			    (((int)t->gold - (int)t->soldiers) >> 3) +
			    cells_apart(t, alarm) - 255;
			if (score > top) {
				best = i;
				top = score;
			}
		}
		if (best == v->town)
			continue;
		for (i = 3; i > 0; i--)
			v->trail[i] = v->trail[i - 1];
		v->trail[0] = v->town;
		s->towns[v->town].garrison = 0;
		v->town = best;
		s->towns[best].garrison = 1;
	}
}

int
sim_villain_news(const struct sim *s, int town, int out[SIM_VILLAINS])
{
	const struct sim_villain *v;
	int k, i, n = 0;

	for (k = 0; k < SIM_VILLAINS; k++) {
		v = &s->villains[k];
		if (!v->active || town < 0)
			continue;
		if (v->town == town) {
			out[n++] = k;
			continue;
		}
		for (i = 0; i < 4; i++)
			if (v->trail[i] == town) {
				out[n++] = k;
				break;
			}
	}
	return (n);
}

int
sim_villain_whereabouts(struct sim *s, int villain, int town, int *where)
{
	const struct sim_villain *v = &s->villains[villain & 3];
	int i;

	*where = v->town;
	if (v->town != town)
		/* He left "town": for the next town of his trail. */
		for (i = 0; i < 4; i++)
			if (v->trail[i] == town) {
				*where = i > 0 ? v->trail[i - 1] : v->town;
				break;
			}
	sim_log_news(s, NEWS_VILLAIN, villain, *where);
	return (v->town == town ? 108 : 107);
}
