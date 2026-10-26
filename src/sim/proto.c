/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proto.h"

#define STATE_VERSION	17
#define MAX_ADVANCE	(100 * 365)

static const char *nation_names[4] = { "spain", "england", "france", "holland" };

static const struct {
	const char	*name;
	int		 code;
} news_names[] = {
	{ "amnesty", NEWS_AMNESTY }, { "indians", NEWS_INDIANS },
	{ "gold_mine", NEWS_GOLD_MINE }, { "malaria", NEWS_MALARIA },
	{ "governor", NEWS_GOVERNOR }, { "plunder", NEWS_PLUNDER },
	{ "war", NEWS_WAR }, { "peace", NEWS_PEACE },
	{ "alliance", NEWS_ALLIANCE }, { "capture", NEWS_CAPTURE },
};

/* Town fields addressable by name in state and "set" commands. */
#define FIELD(f)	{ #f, offsetof(struct sim_town, f) }
static const struct {
	const char	*name;
	size_t		 off;
} town_fields[] = {
	FIELD(flags), FIELD(x), FIELD(y), FIELD(nation), FIELD(forts),
	FIELD(soldiers), FIELD(citizens), FIELD(gold), FIELD(wealth),
	FIELD(standing), FIELD(governor), FIELD(family), FIELD(id),
	FIELD(sea_x), FIELD(sea_y), FIELD(fort_x), FIELD(fort_y),
	FIELD(ship_x), FIELD(ship_y), FIELD(ship_heading), FIELD(sea_heading),
	FIELD(barmaid), FIELD(garrison), FIELD(land_x), FIELD(land_y),
	FIELD(party_x), FIELD(party_y),
};
#define NFIELDS	(sizeof(town_fields) / sizeof(town_fields[0]))

static uint16_t *
town_field(struct sim_town *t, size_t i)
{
	return ((uint16_t *)((char *)t + town_fields[i].off));
}

static int
nation_index(const char *name)
{
	int i;

	for (i = 0; i < 4; i++)
		if (strcmp(name, nation_names[i]) == 0)
			return (i);
	return (-1);
}

static const char *
news_name(int code)
{
	size_t i;

	for (i = 0; i < sizeof(news_names) / sizeof(news_names[0]); i++)
		if (news_names[i].code == code)
			return (news_names[i].name);
	return ("unknown");
}

static int
news_code(const char *name)
{
	size_t i;

	for (i = 0; i < sizeof(news_names) / sizeof(news_names[0]); i++)
		if (strcmp(news_names[i].name, name) == 0)
			return (news_names[i].code);
	return (-1);
}

static void
jb_hex64(struct jbuf *b, uint64_t v)
{
	char tmp[24];

	snprintf(tmp, sizeof(tmp), "\"%016llx\"", (unsigned long long)v);
	jb_raw(b, tmp);
}

/* One news item, with names resolved for readers. */
static void
news_json(const struct sim *s, const struct sim_news *n, struct jbuf *b)
{
	char text[64];

	jb_raw(b, "{\"day\":");
	jb_int(b, n->day);
	jb_raw(b, ",\"event\":");
	jb_str(b, news_name(n->code));
	jb_raw(b, ",\"a\":");
	jb_int(b, n->a);
	jb_raw(b, ",\"b\":");
	jb_int(b, n->b);
	switch (n->code) {
	case NEWS_WAR:
	case NEWS_PEACE:
	case NEWS_ALLIANCE:
		snprintf(text, sizeof(text), "%s %s %s",
		    nation_names[n->a & 3], n->code == NEWS_WAR ?
		    "at war with" : n->code == NEWS_PEACE ? "at peace with" :
		    "allied with", nation_names[n->b & 3]);
		jb_raw(b, ",\"text\":");
		jb_str(b, text);
		break;
	case NEWS_AMNESTY:
		jb_raw(b, ",\"nation\":");
		jb_str(b, nation_names[n->a & 3]);
		break;
	case NEWS_CAPTURE:
		jb_raw(b, ",\"nation\":");
		jb_str(b, nation_names[n->a & 3]);
		if (n->b >= 0 && (size_t)n->b < s->ntowns) {
			jb_raw(b, ",\"town\":");
			jb_str(b, s->towns[n->b].name);
		}
		break;
	default:
		if (n->a >= 0 && (size_t)n->a < s->ntowns) {
			jb_raw(b, ",\"town\":");
			jb_str(b, s->towns[n->a].name);
		}
	}
	jb_raw(b, "}");
}

static const char *stage_names[] = { "none", "sighted", "identified",
    "colours" };
static const char *outcome_names[] = { "none", "boarding", "escaped",
    "sunset", "surrender", "player_sunk", "enemy_sunk", "repelled",
    "defeated", "beached", "landed" };
#define NOUTCOMES	(sizeof(outcome_names) / sizeof(outcome_names[0]))
static const char *sword_results[] = { "none", "won", "repelled", "lost" };

static void
sword_side_json(const struct sword_side *f, struct jbuf *out)
{
	jb_raw(out, "{\"x\":");
	jb_int(out, f->x);
	jb_raw(out, ",\"move\":");
	jb_int(out, f->move);
	jb_raw(out, ",\"frame\":");
	jb_int(out, f->frame);
	jb_raw(out, ",\"men\":");
	jb_int(out, f->men);
	jb_raw(out, ",\"bar\":");
	jb_int(out, f->bar);
	jb_raw(out, ",\"weapon\":");
	jb_int(out, f->weapon);
	jb_raw(out, ",\"guard\":");
	jb_int(out, f->guard);
	jb_raw(out, ",\"wounds\":[");
	jb_int(out, f->wounds[0]);
	jb_raw(out, ",");
	jb_int(out, f->wounds[1]);
	jb_raw(out, ",");
	jb_int(out, f->wounds[2]);
	jb_raw(out, "]}");
}

static int
sword_side_from_json(struct sword_side *f, const struct json *j)
{
	const struct json *w = json_get(j, "wounds");
	size_t i;

	memset(f, 0, sizeof(*f));
	if (j == NULL || w == NULL || w->type != JSON_ARRAY || w->count != 3)
		return (-1);
	f->x = (int)json_get_int(j, "x", 0);
	f->move = (int)json_get_int(j, "move", 0);
	f->frame = (int)json_get_int(j, "frame", 0);
	f->men = (int)json_get_int(j, "men", 0);
	f->bar = (int)json_get_int(j, "bar", 0);
	f->weapon = (int)json_get_int(j, "weapon", 0);
	f->guard = (int)json_get_int(j, "guard", 0);
	for (i = 0; i < 3; i++) {
		if (w->items[i]->type != JSON_INT)
			return (-1);
		f->wounds[i] = (int)w->items[i]->num;
	}
	return (f->move < -1 || f->move >= SWORD_MOVES || f->frame < 0 ||
	    f->frame > 15 || f->weapon < 0 || f->weapon >= SWORD_WEAPONS ?
	    -1 : 0);
}

static void
sword_json(const struct sim_sword *sw, struct jbuf *out)
{
	jb_raw(out, "{\"active\":");
	jb_int(out, sw->active);
	jb_raw(out, ",\"kind\":");
	jb_int(out, sw->kind);
	jb_raw(out, ",\"level\":");
	jb_int(out, sw->level);
	jb_raw(out, ",\"title\":");
	jb_int(out, sw->title);
	jb_raw(out, ",\"nation\":");
	jb_int(out, sw->nation);
	jb_raw(out, ",\"counter\":");
	jb_int(out, sw->counter);
	jb_raw(out, ",\"result\":");
	jb_str(out, sword_results[sw->result & 3]);
	jb_raw(out, ",\"beaten\":");
	jb_int(out, sw->beaten);
	jb_raw(out, ",\"input\":");
	jb_int(out, sw->input);
	jb_raw(out, ",\"script\":");
	jb_int(out, sw->script);
	jb_raw(out, ",\"step\":");
	jb_int(out, sw->step);
	jb_raw(out, ",\"you_hits\":");
	jb_int(out, sw->you_hits);
	jb_raw(out, ",\"foe_hits\":");
	jb_int(out, sw->foe_hits);
	jb_raw(out, ",\"men_start\":");
	jb_int(out, sw->men_start);
	jb_raw(out, ",\"scene\":");
	jb_int(out, sw->scene);
	jb_raw(out, ",\"you\":");
	sword_side_json(&sw->you, out);
	jb_raw(out, ",\"foe\":");
	sword_side_json(&sw->foe, out);
	jb_raw(out, "}");
}

static int
sword_from_json(struct sim_sword *sw, const struct json *j)
{
	const char *r = json_get_str(j, "result");
	int i;

	memset(sw, 0, sizeof(*sw));
	if (j == NULL || r == NULL)
		return (-1);
	sw->active = json_get_int(j, "active", 0) != 0;
	sw->kind = (int)json_get_int(j, "kind", 0);
	sw->level = (int)json_get_int(j, "level", 0);
	sw->title = (int)json_get_int(j, "title", 0);
	sw->nation = (int)json_get_int(j, "nation", 0);
	sw->counter = (uint32_t)json_get_int(j, "counter", 0);
	for (i = 0; i < 4; i++)
		if (strcmp(r, sword_results[i]) == 0)
			sw->result = i;
	sw->beaten = (int)json_get_int(j, "beaten", 0);
	sw->input = (int)json_get_int(j, "input", 0);
	sw->script = (int)json_get_int(j, "script", 0);
	sw->step = (int)json_get_int(j, "step", 0);
	sw->you_hits = (int)json_get_int(j, "you_hits", 0);
	sw->foe_hits = (int)json_get_int(j, "foe_hits", 0);
	sw->men_start = (int)json_get_int(j, "men_start", 0);
	sw->scene = (int)json_get_int(j, "scene", 3);
	if (sword_side_from_json(&sw->you, json_get(j, "you")) != 0 ||
	    sword_side_from_json(&sw->foe, json_get(j, "foe")) != 0)
		return (-1);
	return (sw->level < 0 || sw->level >= BATTLE_LEVELS ||
	    sw->input < 0 || sw->input >= SWM_ADVANCE || sw->script < 0 ||
	    sw->script >= SWORD_SCRIPTS || sw->step < 0 ||
	    sw->step >= SWORD_STEPS ? -1 : 0);
}

/* Battle fields, by name, as offsets in struct battle_ship. */
#define BFIELD(f)	{ #f, offsetof(struct battle_ship, f) }
static const struct {
	const char	*name;
	size_t		 off;
} ship_ints[] = {
	BFIELD(speed), BFIELD(type), BFIELD(guns), BFIELD(crew),
	BFIELD(damage), BFIELD(full_sails), BFIELD(sail_timer),
	BFIELD(reload), BFIELD(turn_rate), BFIELD(role),
};
#define NSHIPINTS	(sizeof(ship_ints) / sizeof(ship_ints[0]))

static void
battle_ship_json(const struct battle_ship *b, struct jbuf *out)
{
	size_t i;

	jb_raw(out, "{\"x\":");
	jb_int(out, b->x);
	jb_raw(out, ",\"y\":");
	jb_int(out, b->y);
	jb_raw(out, ",\"heading\":");
	jb_int(out, b->heading);
	jb_raw(out, ",\"target\":");
	jb_int(out, b->target);
	for (i = 0; i < NSHIPINTS; i++) {
		jb_raw(out, ",\"");
		jb_raw(out, ship_ints[i].name);
		jb_raw(out, "\":");
		jb_int(out, *(const int *)((const char *)b + ship_ints[i].off));
	}
	jb_raw(out, ",\"shot\":[");
	jb_int(out, b->shot.ticks);
	jb_raw(out, ",");
	jb_int(out, b->shot.x);
	jb_raw(out, ",");
	jb_int(out, b->shot.y);
	jb_raw(out, ",");
	jb_int(out, b->shot.heading);
	jb_raw(out, "]}");
}

static int
battle_ship_from_json(struct battle_ship *b, const struct json *j)
{
	const struct json *shot = json_get(j, "shot");
	size_t i;

	memset(b, 0, sizeof(*b));
	if (j == NULL || shot == NULL || shot->type != JSON_ARRAY ||
	    shot->count != 4)
		return (-1);
	b->x = (int32_t)json_get_int(j, "x", 0);
	b->y = (int32_t)json_get_int(j, "y", 0);
	b->heading = (uint8_t)json_get_int(j, "heading", 0);
	b->target = (uint8_t)json_get_int(j, "target", 0);
	for (i = 0; i < NSHIPINTS; i++)
		*(int *)((char *)b + ship_ints[i].off) =
		    (int)json_get_int(j, ship_ints[i].name, 0);
	for (i = 0; i < 4; i++)
		if (shot->items[i]->type != JSON_INT)
			return (-1);
	b->shot.ticks = (int)shot->items[0]->num;
	b->shot.x = (int32_t)shot->items[1]->num;
	b->shot.y = (int32_t)shot->items[2]->num;
	b->shot.heading = (uint8_t)shot->items[3]->num;
	return (b->type < 0 || b->type > 9 ? -1 : 0);
}

static void
battle_json(const struct sim_battle *bt, struct jbuf *out)
{
	jb_raw(out, "{\"active\":");
	jb_int(out, bt->active);
	jb_raw(out, ",\"tick\":");
	jb_int(out, bt->tick);
	jb_raw(out, ",\"wind\":");
	jb_int(out, bt->wind);
	jb_raw(out, ",\"wind_strength\":");
	jb_int(out, bt->wind_strength);
	jb_raw(out, ",\"decision\":");
	jb_int(out, bt->decision);
	jb_raw(out, ",\"outcome\":");
	jb_str(out, outcome_names[(size_t)bt->outcome % NOUTCOMES]);
	jb_raw(out, ",\"crew_ashore\":");
	jb_int(out, bt->crew_ashore);
	jb_raw(out, ",\"guns_ashore\":");
	jb_int(out, bt->guns_ashore);
	jb_raw(out, ",\"fort\":");
	jb_int(out, bt->fort);
	jb_raw(out, ",\"fort_damage\":");
	jb_int(out, bt->fort_damage);
	jb_raw(out, ",\"set\":");
	jb_int(out, bt->set);
	jb_raw(out, ",\"piece\":");
	jb_int(out, bt->piece);
	jb_raw(out, ",\"scroll_x\":");
	jb_int(out, bt->scroll_x);
	jb_raw(out, ",\"scroll_y\":");
	jb_int(out, bt->scroll_y);
	jb_raw(out, ",\"screen\":");
	jb_int(out, bt->screen);
	jb_raw(out, ",\"you_scrape\":");
	jb_int(out, bt->you_scrape);
	jb_raw(out, ",\"foe_scrape\":");
	jb_int(out, bt->foe_scrape);
	jb_raw(out, ",\"you\":");
	battle_ship_json(&bt->you, out);
	jb_raw(out, ",\"foe\":");
	battle_ship_json(&bt->foe, out);
	jb_raw(out, "}");
}

static int
battle_from_json(struct sim_battle *bt, const struct json *j)
{
	const char *o = json_get_str(j, "outcome");
	int i;

	memset(bt, 0, sizeof(*bt));
	if (j == NULL || o == NULL)
		return (-1);
	bt->active = json_get_int(j, "active", 0) != 0;
	bt->tick = (uint32_t)json_get_int(j, "tick", 0);
	bt->wind = (uint8_t)json_get_int(j, "wind", 0);
	bt->wind_strength = (int)json_get_int(j, "wind_strength", 0);
	bt->decision = (int)json_get_int(j, "decision", 0);
	for (i = 0; i < (int)NOUTCOMES; i++)
		if (strcmp(o, outcome_names[i]) == 0)
			bt->outcome = i;
	bt->crew_ashore = (int)json_get_int(j, "crew_ashore", 0);
	bt->guns_ashore = (int)json_get_int(j, "guns_ashore", 0);
	bt->fort = (int)json_get_int(j, "fort", 0);
	bt->fort_damage = (int)json_get_int(j, "fort_damage", 0);
	bt->set = (int)json_get_int(j, "set", 4);
	bt->piece = (int)json_get_int(j, "piece", 0);
	bt->scroll_x = (int)json_get_int(j, "scroll_x", 0);
	bt->scroll_y = (int)json_get_int(j, "scroll_y", 0);
	bt->screen = (int)json_get_int(j, "screen", 104);
	if (bt->screen < 0 || bt->screen >= BATTLE_SCREENS)
		bt->screen = 104;
	bt->you_scrape = (int)json_get_int(j, "you_scrape", 0);
	bt->foe_scrape = (int)json_get_int(j, "foe_scrape", 0);
	if (battle_ship_from_json(&bt->you, json_get(j, "you")) != 0 ||
	    battle_ship_from_json(&bt->foe, json_get(j, "foe")) != 0)
		return (-1);
	return (bt->wind_strength < 0 || bt->wind_strength > 2 ? -1 : 0);
}

static void
encounter_json(const struct sim_encounter *e, struct jbuf *b)
{
	jb_raw(b, "{\"stage\":");
	jb_str(b, stage_names[e->stage & 3]);
	jb_raw(b, ",\"type\":");
	jb_int(b, e->type);
	jb_raw(b, ",\"nation\":");
	jb_int(b, e->nation);
	jb_raw(b, ",\"crew\":");
	jb_int(b, e->crew);
	jb_raw(b, ",\"guns\":");
	jb_int(b, e->guns);
	jb_raw(b, ",\"role\":");
	jb_int(b, e->role);
	jb_raw(b, ",\"cargo\":");
	jb_int(b, e->cargo);
	jb_raw(b, ",\"gold\":");
	jb_int(b, e->gold);
	jb_raw(b, ",\"hostile\":");
	jb_int(b, e->hostile);
	jb_raw(b, ",\"town\":");
	jb_int(b, e->town);
	jb_raw(b, "}");
}

static int
encounter_from_json(struct sim_encounter *e, const struct json *j)
{
	const char *stage = json_get_str(j, "stage");
	int i;

	memset(e, 0, sizeof(*e));
	if (stage == NULL)
		return (-1);
	for (i = 0; i < 4; i++)
		if (strcmp(stage, stage_names[i]) == 0)
			e->stage = i;
	e->type = (int)json_get_int(j, "type", -1);
	e->nation = (int)json_get_int(j, "nation", -1);
	e->crew = (int)json_get_int(j, "crew", 0);
	e->guns = (int)json_get_int(j, "guns", 0);
	e->role = (int)json_get_int(j, "role", 0);
	e->cargo = (int)json_get_int(j, "cargo", 0);
	e->gold = (int)json_get_int(j, "gold", 0);
	e->hostile = (int)json_get_int(j, "hostile", 0);
	e->town = (int)json_get_int(j, "town", -1);
	return (e->type < 0 || e->type > 8 || e->nation < 0 ||
	    e->nation > NATION_PIRATE ? -1 : 0);
}

void
sim_to_json(const struct sim *s, struct jbuf *b)
{
	size_t i, f;
	int a, c;

	jb_raw(b, "{\"version\":");
	jb_int(b, STATE_VERSION);
	jb_raw(b, ",\"rng\":[");
	jb_hex64(b, s->rng.state);
	jb_raw(b, ",");
	jb_hex64(b, s->rng.inc);
	jb_raw(b, "],\"player\":{\"name\":");
	jb_str(b, s->player.name);
	jb_raw(b, ",\"title\":");
	jb_str(b, s->player.title);
	jb_raw(b, ",\"nation\":");
	jb_int(b, s->player.nation);
	jb_raw(b, ",\"difficulty\":");
	jb_int(b, s->player.difficulty);
	jb_raw(b, ",\"ability\":");
	jb_int(b, s->player.ability);
	jb_raw(b, ",\"expedition\":");
	jb_int(b, s->player.expedition);
	jb_raw(b, "},\"fleet\":{\"ships\":[");
	for (a = 0; a < s->fleet.nships && a < SCENARIO_SHIPS; a++) {
		if (a > 0)
			jb_raw(b, ",");
		jb_int(b, s->fleet.ships[a]);
	}
	jb_raw(b, "],\"damage\":[");
	for (a = 0; a < s->fleet.nships && a < SCENARIO_SHIPS; a++) {
		if (a > 0)
			jb_raw(b, ",");
		jb_int(b, s->fleet.damage[a]);
	}
	jb_raw(b, "],\"gunnery\":");
	jb_int(b, s->fleet.gunnery);
	jb_raw(b, ",\"food\":");
	jb_int(b, s->fleet.food);
	jb_raw(b, ",\"goods\":");
	jb_int(b, s->fleet.goods);
	jb_raw(b, ",\"sugar\":");
	jb_int(b, s->fleet.sugar);
	jb_raw(b, ",\"capacity\":");
	jb_int(b, s->fleet.capacity);
	jb_raw(b, ",\"crew\":");
	jb_int(b, s->fleet.crew);
	jb_raw(b, ",\"guns\":");
	jb_int(b, s->fleet.guns);
	jb_raw(b, ",\"gold\":");
	jb_int(b, s->fleet.gold);
	jb_raw(b, ",\"x\":");
	jb_int(b, s->fleet.x);
	jb_raw(b, ",\"y\":");
	jb_int(b, s->fleet.y);
	jb_raw(b, ",\"fx\":");
	jb_int(b, s->fleet.fx);
	jb_raw(b, ",\"fy\":");
	jb_int(b, s->fleet.fy);
	jb_raw(b, ",\"heading\":");
	jb_int(b, s->fleet.heading);
	jb_raw(b, ",\"speed\":");
	jb_int(b, s->fleet.speed);
	jb_raw(b, ",\"ticks\":");
	jb_int(b, s->fleet.ticks);
	jb_raw(b, ",\"leaving\":");
	jb_int(b, s->fleet.leaving);
	jb_raw(b, ",\"map_pieces\":");
	jb_int(b, s->fleet.map_pieces);
	jb_raw(b, ",\"ashore\":");
	jb_int(b, s->fleet.ashore);
	jb_raw(b, ",\"ship_x\":");
	jb_int(b, s->fleet.ship_x);
	jb_raw(b, ",\"ship_y\":");
	jb_int(b, s->fleet.ship_y);
	jb_raw(b, ",\"land_x\":");
	jb_int(b, s->fleet.land_x);
	jb_raw(b, ",\"land_y\":");
	jb_int(b, s->fleet.land_y);
	jb_raw(b, "},\"wind\":");
	jb_int(b, s->wind);
	jb_raw(b, ",\"encounters\":");
	jb_int(b, s->encounters);
	jb_raw(b, ",\"heat\":");
	jb_int(b, s->heat);
	jb_raw(b, ",\"heat_town\":");
	jb_int(b, s->heat_town);
	jb_raw(b, ",\"encounter\":");
	encounter_json(&s->encounter, b);
	jb_raw(b, ",\"battle\":");
	battle_json(&s->battle, b);
	jb_raw(b, ",\"sword\":");
	sword_json(&s->sword, b);
	jb_raw(b, ",\"practice\":");
	jb_int(b, s->practice);
	jb_raw(b, ",\"era\":");
	jb_int(b, s->era_year);
	jb_raw(b, ",\"year\":");
	jb_int(b, s->year);
	jb_raw(b, ",\"day_of_year\":");
	jb_int(b, s->day_of_year);
	jb_raw(b, ",\"month\":");
	jb_int(b, s->month);
	jb_raw(b, ",\"day\":");
	jb_int(b, s->day);
	jb_raw(b, ",\"diplomacy_range\":");
	jb_int(b, s->diplomacy_range);
	jb_raw(b, ",\"player_town\":");
	jb_int(b, s->player_town);
	jb_raw(b, ",\"visit_town\":");
	jb_int(b, s->visit_town);
	jb_raw(b, ",\"visit_flags\":");
	jb_int(b, s->visit_flags);
	jb_raw(b, ",\"wright_gold\":");
	jb_int(b, s->wright_gold);
	jb_raw(b, ",\"wright_cannon\":");
	jb_int(b, s->wright_cannon);
	jb_raw(b, ",\"merchant_gold\":");
	jb_int(b, s->merchant_gold);
	jb_raw(b, ",\"reputation\":");
	jb_int(b, s->reputation);
	jb_raw(b, ",\"acres\":");
	jb_int(b, s->acres);
	jb_raw(b, ",\"fortune\":");
	jb_int(b, s->fortune);
	jb_raw(b, ",\"rank\":[");
	for (i = 0; i < 5; i++) {
		if (i > 0)
			jb_raw(b, ",");
		jb_int(b, s->rank[i]);
	}
	jb_raw(b, "]");
	jb_raw(b, ",\"tavern_spots\":");
	jb_int(b, s->tavern_spots);
	jb_raw(b, ",\"recruits\":");
	jb_int(b, s->recruits);
	jb_raw(b, ",\"news_town\":");
	jb_int(b, s->news_town);
	jb_raw(b, ",\"news_price\":");
	jb_int(b, s->news_price);
	jb_raw(b, ",\"barmaid_mood\":");
	jb_int(b, s->barmaid_mood);
	jb_raw(b, ",\"market\":{\"open\":");
	jb_int(b, s->market.open);
	for (i = 0; i < GOODS; i++) {
		jb_raw(b, i == 0 ? ",\"stock\":[" : ",");
		jb_int(b, s->market.stock[i]);
	}
	for (i = 0; i < GOODS; i++) {
		jb_raw(b, i == 0 ? "],\"price\":[" : ",");
		jb_int(b, s->market.price[i]);
	}
	jb_raw(b, "],\"gold_at_open\":");
	jb_int(b, s->market.gold_at_open);
	jb_raw(b, "}");
	jb_raw(b, ",\"relations\":[");
	for (a = 0; a < 4; a++) {
		jb_raw(b, a > 0 ? ",[" : "[");
		for (c = 0; c < 4; c++) {
			if (c > 0)
				jb_raw(b, ",");
			jb_int(b, s->relations[a][c]);
		}
		jb_raw(b, "]");
	}
	jb_raw(b, "],\"standing\":[");
	for (a = 0; a < 4; a++) {
		if (a > 0)
			jb_raw(b, ",");
		jb_int(b, s->standing[a]);
	}
	jb_raw(b, "],\"towns\":[");
	for (i = 0; i < s->ntowns; i++) {
		jb_raw(b, i > 0 ? ",{\"name\":" : "{\"name\":");
		jb_str(b, s->towns[i].name);
		for (f = 0; f < NFIELDS; f++) {
			jb_raw(b, ",\"");
			jb_raw(b, town_fields[f].name);
			jb_raw(b, "\":");
			jb_int(b, *town_field((struct sim_town *)&s->towns[i], f));
		}
		jb_raw(b, "}");
	}
	jb_raw(b, "],\"news_total\":");
	jb_int(b, (int64_t)s->nnews);
	jb_raw(b, ",\"news\":[");
	for (i = 0; i < sim_news_kept(s); i++) {
		if (i > 0)
			jb_raw(b, ",");
		news_json(s, sim_news_at(s, i), b);
	}
	jb_raw(b, "]}");
}

static int
get_hex64(const struct json *j, uint64_t *out)
{
	char *end;

	if (j == NULL || j->type != JSON_STRING)
		return (-1);
	*out = strtoull(j->str, &end, 16);
	return (*end == '\0' && end != j->str ? 0 : -1);
}

static int
get_array(const struct json *obj, const char *key, size_t n,
    int64_t lo, int64_t hi, int64_t *out)
{
	const struct json *a = json_get(obj, key);
	size_t i;

	if (a == NULL || a->type != JSON_ARRAY || a->count != n)
		return (-1);
	for (i = 0; i < n; i++) {
		if (a->items[i]->type != JSON_INT || a->items[i]->num < lo ||
		    a->items[i]->num > hi)
			return (-1);
		out[i] = a->items[i]->num;
	}
	return (0);
}

static int
player_from_json(struct sim_player *p, const struct json *j, char *err,
    size_t errlen)
{
	const char *name = json_get_str(j, "name");
	const char *title = json_get_str(j, "title");

	memset(p, 0, sizeof(*p));
	if (name == NULL || title == NULL || strlen(name) >= sizeof(p->name) ||
	    strlen(title) >= sizeof(p->title)) {
		snprintf(err, errlen, "bad player name or title");
		return (-1);
	}
	strcpy(p->name, name);
	strcpy(p->title, title);
	p->nation = (int)json_get_int(j, "nation", -1);
	p->difficulty = (int)json_get_int(j, "difficulty", -1);
	p->ability = (int)json_get_int(j, "ability", -1);
	p->expedition = (int)json_get_int(j, "expedition", -2);
	if (p->nation < 0 || p->nation > 3 || p->difficulty < 0 ||
	    p->difficulty > 3 || p->ability < 0 || p->ability > 4 ||
	    p->expedition < -1 || p->expedition > 5) {
		snprintf(err, errlen, "bad player");
		return (-1);
	}
	return (0);
}

static int
fleet_from_json(struct sim_fleet *f, const struct json *j, char *err,
    size_t errlen)
{
	const struct json *ships = json_get(j, "ships");
	size_t i;

	memset(f, 0, sizeof(*f));
	if (ships == NULL || ships->type != JSON_ARRAY || ships->count == 0 ||
	    ships->count > SCENARIO_SHIPS) {
		snprintf(err, errlen, "bad fleet ships");
		return (-1);
	}
	f->nships = (int)ships->count;
	for (i = 0; i < ships->count; i++) {
		if (ships->items[i]->type != JSON_INT ||
		    ships->items[i]->num < 0 || ships->items[i]->num > 9) {
			snprintf(err, errlen, "bad ship type");
			return (-1);
		}
		f->ships[i] = (uint8_t)ships->items[i]->num;
	}
	{
		const struct json *dm = json_get(j, "damage");

		if (dm == NULL || dm->type != JSON_ARRAY ||
		    dm->count != ships->count) {
			snprintf(err, errlen, "bad fleet damage");
			return (-1);
		}
		for (i = 0; i < dm->count; i++) {
			if (dm->items[i]->type != JSON_INT ||
			    dm->items[i]->num < 0 || dm->items[i]->num > 255) {
				snprintf(err, errlen, "bad fleet damage");
				return (-1);
			}
			f->damage[i] = (uint8_t)dm->items[i]->num;
		}
	}
	f->gunnery = (int)json_get_int(j, "gunnery", 0);
	f->food = (int)json_get_int(j, "food", 0);
	f->goods = (int)json_get_int(j, "goods", 0);
	f->sugar = (int)json_get_int(j, "sugar", 0);
	f->capacity = (int)json_get_int(j, "capacity", 0);
	f->crew = (int)json_get_int(j, "crew", -1);
	f->guns = (int)json_get_int(j, "guns", -1);
	f->gold = (long)json_get_int(j, "gold", -1);
	f->x = (int32_t)json_get_int(j, "x", -1);
	f->y = (int32_t)json_get_int(j, "y", -1);
	f->fx = (uint8_t)json_get_int(j, "fx", 0);
	f->fy = (uint8_t)json_get_int(j, "fy", 0);
	f->heading = (uint8_t)json_get_int(j, "heading", 0);
	f->speed = (int)json_get_int(j, "speed", 0);
	f->ticks = (uint32_t)json_get_int(j, "ticks", 0);
	f->leaving = (int)json_get_int(j, "leaving", -1);
	f->map_pieces = (int)json_get_int(j, "map_pieces", 0);
	f->ashore = json_get_int(j, "ashore", 0) != 0;
	f->ship_x = (int32_t)json_get_int(j, "ship_x", 0);
	f->ship_y = (int32_t)json_get_int(j, "ship_y", 0);
	f->land_x = (int32_t)json_get_int(j, "land_x", 0);
	f->land_y = (int32_t)json_get_int(j, "land_y", 0);
	if (f->crew < 0 || f->guns < 0 || f->gold < 0 || f->x < 0 ||
	    f->x >= WORLD_PX_W || f->y < 0 || f->y >= WORLD_PX_H ||
	    f->fx > 7 || f->fy > 7 || f->speed < 0 || f->speed > 12) {
		snprintf(err, errlen, "bad fleet");
		return (-1);
	}
	return (0);
}

int
sim_from_json(struct sim *s, const struct json *j, char *err, size_t errlen)
{
	const struct json *rng, *towns, *t, *news, *n;
	struct sim tmp;
	int64_t v[4], total;
	size_t i, f, kept;
	const char *name;
	int a;

	memset(&tmp, 0, sizeof(tmp));
	if (json_get_int(j, "version", 0) != STATE_VERSION) {
		snprintf(err, errlen, "unsupported state version");
		return (-1);
	}
	rng = json_get(j, "rng");
	if (rng == NULL || rng->type != JSON_ARRAY || rng->count != 2 ||
	    get_hex64(rng->items[0], &tmp.rng.state) != 0 ||
	    get_hex64(rng->items[1], &tmp.rng.inc) != 0) {
		snprintf(err, errlen, "bad rng");
		return (-1);
	}
	if (player_from_json(&tmp.player, json_get(j, "player"), err,
	    errlen) != 0)
		return (-1);
	if (fleet_from_json(&tmp.fleet, json_get(j, "fleet"), err,
	    errlen) != 0)
		return (-1);
	tmp.wind = (uint8_t)json_get_int(j, "wind", 0);
	tmp.encounters = json_get_int(j, "encounters", 1) != 0;
	tmp.heat = (int)json_get_int(j, "heat", 0);
	tmp.heat_town = (int)json_get_int(j, "heat_town", -1);
	if (encounter_from_json(&tmp.encounter, json_get(j, "encounter")) !=
	    0) {
		snprintf(err, errlen, "bad encounter");
		return (-1);
	}
	if (battle_from_json(&tmp.battle, json_get(j, "battle")) != 0) {
		snprintf(err, errlen, "bad battle");
		return (-1);
	}
	if (sword_from_json(&tmp.sword, json_get(j, "sword")) != 0) {
		snprintf(err, errlen, "bad sword fight");
		return (-1);
	}
	tmp.practice = (int)json_get_int(j, "practice", 0);
	tmp.era_year = (int)json_get_int(j, "era", 0);
	tmp.year = (int)json_get_int(j, "year", 0);
	tmp.day_of_year = (int)json_get_int(j, "day_of_year", -1);
	tmp.month = (int)json_get_int(j, "month", -1);
	tmp.day = (int32_t)json_get_int(j, "day", -1);
	tmp.diplomacy_range = (int)json_get_int(j, "diplomacy_range", 0);
	tmp.player_town = (int)json_get_int(j, "player_town", -2);
	tmp.visit_town = (int)json_get_int(j, "visit_town", -1);
	tmp.visit_flags = (int)json_get_int(j, "visit_flags", 0);
	tmp.wright_gold = (int)json_get_int(j, "wright_gold", -1);
	tmp.wright_cannon = (int)json_get_int(j, "wright_cannon", 0);
	tmp.merchant_gold = (int)json_get_int(j, "merchant_gold", -1);
	tmp.reputation = (int)json_get_int(j, "reputation", 0);
	tmp.acres = (int)json_get_int(j, "acres", 0);
	tmp.fortune = (long)json_get_int(j, "fortune", 0);
	{
		const struct json *rk = json_get(j, "rank");
		int k;

		if (rk == NULL || rk->type != JSON_ARRAY || rk->count != 5) {
			snprintf(err, errlen, "bad rank");
			return (-1);
		}
		for (k = 0; k < 5; k++)
			tmp.rank[k] = (uint8_t)rk->items[k]->num;
	}
	tmp.tavern_spots = (unsigned)json_get_int(j, "tavern_spots", 0) &
	    TAVERN_ALL;
	tmp.recruits = (int)json_get_int(j, "recruits", 0);
	tmp.news_town = (int)json_get_int(j, "news_town", 0);
	if (tmp.news_town < 0 || (size_t)tmp.news_town >= TOWN_MAX)
		tmp.news_town = 0;
	tmp.news_price = (int)json_get_int(j, "news_price", 0);
	tmp.barmaid_mood = (int)json_get_int(j, "barmaid_mood", 0);
	{
		const struct json *mk = json_get(j, "market");
		const struct json *st = json_get(mk, "stock");
		const struct json *pr = json_get(mk, "price");
		int k;

		if (mk == NULL || st == NULL || pr == NULL ||
		    st->type != JSON_ARRAY || pr->type != JSON_ARRAY ||
		    st->count != GOODS || pr->count != GOODS) {
			snprintf(err, errlen, "bad market");
			return (-1);
		}
		tmp.market.open = json_get_int(mk, "open", 0) != 0;
		for (k = 0; k < GOODS; k++) {
			tmp.market.stock[k] = (int)st->items[k]->num;
			tmp.market.price[k] = (int)pr->items[k]->num;
		}
		tmp.market.gold_at_open = (long)json_get_int(mk,
		    "gold_at_open", 0);
	}
	if (tmp.day < 0 || tmp.day_of_year < 0 || tmp.day_of_year >= 360 ||
	    tmp.month < 0 || tmp.month > 12 || tmp.diplomacy_range <= 0) {
		snprintf(err, errlen, "bad calendar or diplomacy_range");
		return (-1);
	}
	for (a = 0; a < 4; a++) {
		const struct json *rel = json_get(j, "relations");

		if (rel == NULL || rel->type != JSON_ARRAY || rel->count != 4)
			goto bad_rel;
		if (rel->items[a]->type != JSON_ARRAY ||
		    rel->items[a]->count != 4)
			goto bad_rel;
		for (f = 0; f < 4; f++) {
			if (rel->items[a]->items[f]->type != JSON_INT ||
			    rel->items[a]->items[f]->num < -1 ||
			    rel->items[a]->items[f]->num > 1)
				goto bad_rel;
			tmp.relations[a][f] =
			    (int8_t)rel->items[a]->items[f]->num;
		}
	}
	if (get_array(j, "standing", 4, -128, 127, v) != 0) {
		snprintf(err, errlen, "bad standing");
		return (-1);
	}
	for (a = 0; a < 4; a++)
		tmp.standing[a] = (int8_t)v[a];
	tmp.standing[NATION_PIRATE] = -1;
	towns = json_get(j, "towns");
	if (towns == NULL || towns->type != JSON_ARRAY ||
	    towns->count > TOWN_MAX) {
		snprintf(err, errlen, "bad towns");
		return (-1);
	}
	tmp.ntowns = towns->count;
	for (i = 0; i < towns->count; i++) {
		t = towns->items[i];
		if ((name = json_get_str(t, "name")) == NULL ||
		    strlen(name) >= sizeof(tmp.towns[i].name)) {
			snprintf(err, errlen, "town %zu: bad name", i);
			return (-1);
		}
		strcpy(tmp.towns[i].name, name);
		for (f = 0; f < NFIELDS; f++) {
			int64_t fv = json_get_int(t, town_fields[f].name, -1);

			if (fv < 0 || fv > 0xFFFF) {
				snprintf(err, errlen, "town %zu: bad %s", i,
				    town_fields[f].name);
				return (-1);
			}
			*town_field(&tmp.towns[i], f) = (uint16_t)fv;
		}
	}
	if (tmp.visit_town < -1 || tmp.visit_town >= (int)tmp.ntowns) {
		snprintf(err, errlen, "bad visit_town");
		return (-1);
	}
	if (tmp.player_town < -1 || tmp.player_town >= (int)tmp.ntowns) {
		snprintf(err, errlen, "bad player_town");
		return (-1);
	}
	total = json_get_int(j, "news_total", -1);
	news = json_get(j, "news");
	if (total < 0 || news == NULL || news->type != JSON_ARRAY ||
	    news->count > SIM_NEWS_MAX || (int64_t)news->count > total) {
		snprintf(err, errlen, "bad news");
		return (-1);
	}
	/* Rebuild the ring so the oldest kept item is at the same place. */
	kept = news->count;
	tmp.nnews = (size_t)total - kept;
	for (i = 0; i < kept; i++) {
		n = news->items[i];
		if ((name = json_get_str(n, "event")) == NULL ||
		    news_code(name) < 0) {
			snprintf(err, errlen, "news %zu: bad event", i);
			return (-1);
		}
		tmp.news[tmp.nnews % SIM_NEWS_MAX].day =
		    (int32_t)json_get_int(n, "day", 0);
		tmp.news[tmp.nnews % SIM_NEWS_MAX].code = news_code(name);
		tmp.news[tmp.nnews % SIM_NEWS_MAX].a =
		    (int)json_get_int(n, "a", 0);
		tmp.news[tmp.nnews % SIM_NEWS_MAX].b =
		    (int)json_get_int(n, "b", 0);
		tmp.nnews++;
	}
	*s = tmp;
	return (0);
bad_rel:
	snprintf(err, errlen, "bad relations");
	return (-1);
}

void
session_free(struct session *ss)
{
	free(ss->world);
	ss->world = NULL;
	ss->sim.world = NULL;
}

/* Attach the world of the state's era, loading it when it changed. */
static int
attach_world(struct session *ss)
{
	int era;

	ss->sim.world = NULL;
	if (ss->load_world == NULL)
		return (0);
	for (era = 0; era < EXE_ERAS; era++)
		if (town_era_years[era] == ss->sim.era_year)
			break;
	if (era == EXE_ERAS)
		return (-1);
	if (ss->world == NULL || ss->world->era != era) {
		if (ss->world == NULL &&
		    (ss->world = malloc(sizeof(*ss->world))) == NULL)
			return (-1);
		if (ss->load_world(ss->load_arg, era, ss->world) != 0) {
			free(ss->world);
			ss->world = NULL;
			return (-1);
		}
	}
	ss->sim.world = ss->world;
	return (0);
}

static void
reply_error(struct jbuf *out, const char *msg)
{
	jb_raw(out, "{\"ok\":false,\"error\":");
	jb_str(out, msg);
	jb_raw(out, "}");
}

static int
era_index(int year)
{
	int e;

	for (e = 0; e < EXE_ERAS; e++)
		if (town_era_years[e] == year)
			return (e);
	return (-1);
}

/* Scenario nation order (England, France, Holland, Spain) from a town
 * nation (Spain, England, France, Holland). */
static enum scenario_nation
scenario_nation(int nation)
{
	return (nation == NATION_SPAIN ? SCN_SPAIN :
	    (enum scenario_nation)(nation - 1));
}

static void
cmd_new_game(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	struct era_towns towns;
	struct scenario scn;
	const char *nation;
	int era, nat;
	int64_t seed;

	era = era_index((int)json_get_int(cmd, "era", 0));
	nation = json_get_str(cmd, "nation");
	nat = nation != NULL ? nation_index(nation) : NATION_ENGLAND;
	seed = json_get_int(cmd, "seed", 1);
	if (era < 0 || nat < 0) {
		reply_error(out, "new_game needs \"era\" (1560..1680) and an "
		    "optional \"nation\" (spain, england, france, holland)");
		return;
	}
	if (ss->load(ss->load_arg, era, scenario_career(era,
	    scenario_nation(nat)), &towns, &scn) != 0) {
		reply_error(out, "cannot load the era data");
		return;
	}
	if (scn.name[0] == '\0') {
		reply_error(out, "no career for this nation in this era");
		return;
	}
	sim_new_game(&ss->sim, &towns, &scn, (uint64_t)seed);
	ss->sim.player.nation = nat;
	ss->started = 1;
	if (attach_world(ss) != 0) {
		reply_error(out, "cannot load the era's world");
		return;
	}
	jb_raw(out, "{\"ok\":true,\"scenario\":");
	jb_str(out, scn.name);
	jb_raw(out, ",\"towns\":");
	jb_int(out, (int64_t)ss->sim.ntowns);
	jb_raw(out, "}");
}

static void
cmd_advance(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	int64_t days = json_get_int(cmd, "days", 0);
	size_t before, i, first;

	if (days < 1 || days > MAX_ADVANCE) {
		reply_error(out, "advance needs \"days\" (1..36500)");
		return;
	}
	before = ss->sim.nnews;
	sim_advance(&ss->sim, (int32_t)days);
	jb_raw(out, "{\"ok\":true,\"day\":");
	jb_int(out, ss->sim.day);
	jb_raw(out, ",\"news\":[");
	/* New items, as far as the ring still holds them. */
	first = ss->sim.nnews - sim_news_kept(&ss->sim);
	for (i = before > first ? before : first; i < ss->sim.nnews; i++) {
		if (i > (before > first ? before : first))
			jb_raw(out, ",");
		news_json(&ss->sim, sim_news_at(&ss->sim, i - first), out);
	}
	jb_raw(out, "]}");
}

static int
find_town(const struct sim *s, const struct json *ref)
{
	size_t i;

	if (ref == NULL)
		return (-1);
	if (ref->type == JSON_INT)
		return (ref->num >= 0 && ref->num < (int64_t)s->ntowns ?
		    (int)ref->num : -1);
	if (ref->type == JSON_STRING)
		for (i = 0; i < s->ntowns; i++)
			if (strcmp(s->towns[i].name, ref->str) == 0)
				return ((int)i);
	return (-1);
}

/* Debug and test hooks: change one value of the state. */
static void
cmd_set(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	const struct json *rel = json_get(cmd, "relation");
	const struct json *val = json_get(cmd, "value");
	const char *field = json_get_str(cmd, "field");
	struct sim *s = &ss->sim;
	int64_t v;
	size_t f;
	int t, a, b;

	if (val == NULL || val->type != JSON_INT) {
		reply_error(out, "set needs an integer \"value\"");
		return;
	}
	v = val->num;
	if (rel != NULL) {
		if (rel->type != JSON_ARRAY || rel->count != 2 ||
		    rel->items[0]->type != JSON_INT ||
		    rel->items[1]->type != JSON_INT || v < -1 || v > 1) {
			reply_error(out, "relation needs [a, b] and -1..1");
			return;
		}
		a = (int)rel->items[0]->num;
		b = (int)rel->items[1]->num;
		if (a < 0 || a > 3 || b < 0 || b > 3) {
			reply_error(out, "nations are 0..3");
			return;
		}
		s->relations[a][b] = s->relations[b][a] = (int8_t)v;
	} else if (field != NULL && strcmp(field, "player_town") == 0) {
		if (v < -1 || v >= (int64_t)s->ntowns) {
			reply_error(out, "bad town index");
			return;
		}
		s->player_town = (int)v;
	} else if (field != NULL && strcmp(field, "encounters") == 0) {
		s->encounters = v != 0;
	} else if (field != NULL && strcmp(field, "diplomacy_range") == 0) {
		if (v < 1 || v > 0xFFFF) {
			reply_error(out, "diplomacy_range is 1..65535");
			return;
		}
		s->diplomacy_range = (int)v;
	} else {
		if ((t = find_town(s, json_get(cmd, "town"))) < 0) {
			reply_error(out, "unknown town");
			return;
		}
		for (f = 0; f < NFIELDS; f++)
			if (field != NULL &&
			    strcmp(field, town_fields[f].name) == 0)
				break;
		if (f == NFIELDS || v < 0 || v > 0xFFFF) {
			reply_error(out, "unknown field or value out of range");
			return;
		}
		*town_field(&s->towns[t], f) = (uint16_t)v;
	}
	jb_raw(out, "{\"ok\":true}");
}

static void
cmd_sail(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	static const char *stops[] = { "ticks", "land", "town",
	    "encounter", "boarded" };
	int64_t ticks = json_get_int(cmd, "ticks", 64);
	int64_t turn = json_get_int(cmd, "turn", 0);
	const struct sim_fleet *f = &ss->sim.fleet;
	enum sail_stop stop;
	int town;

	if (ss->sim.world == NULL) {
		reply_error(out, "sailing needs the era's world");
		return;
	}
	if (ticks < 1 || ticks > 64 * 365 || turn < -1 || turn > 1) {
		reply_error(out, "sail needs \"ticks\" (1..23360) and "
		    "\"turn\" (-1, 0, 1)");
		return;
	}
	stop = sim_sail(&ss->sim, (int)turn, (int)ticks, &town);
	jb_raw(out, "{\"ok\":true,\"stop\":");
	jb_str(out, stops[stop]);
	if (stop == SAIL_TOWN) {
		jb_raw(out, ",\"town\":");
		jb_str(out, ss->sim.towns[town].name);
	} else if (stop == SAIL_ENCOUNTER) {
		jb_raw(out, ",\"encounter\":");
		encounter_json(&ss->sim.encounter, out);
	}
	jb_raw(out, ",\"x\":");
	jb_int(out, f->x);
	jb_raw(out, ",\"y\":");
	jb_int(out, f->y);
	jb_raw(out, ",\"heading\":");
	jb_int(out, f->heading);
	jb_raw(out, ",\"speed\":");
	jb_int(out, f->speed);
	jb_raw(out, ",\"wind\":");
	jb_int(out, ss->sim.wind);
	jb_raw(out, ",\"day\":");
	jb_int(out, ss->sim.day);
	jb_raw(out, "}");
}

static void
cmd_encounter(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	static const char *actions[] = { "investigate", "ignore", "battle",
	    "hail" };
	static const char *results[] = { "continue", "escaped", "news",
	    "battle", "overtaken", "unanswered" };
	const char *a = json_get_str(cmd, "action");
	enum encounter_result r;
	int i;

	if (ss->sim.encounter.stage == ENC_NONE) {
		reply_error(out, "no encounter");
		return;
	}
	for (i = 0; i < 4 && a != NULL; i++)
		if (strcmp(a, actions[i]) == 0)
			break;
	if (a == NULL || i == 4) {
		reply_error(out, "encounter needs \"action\": investigate, "
		    "ignore, battle or hail");
		return;
	}
	r = sim_encounter_act(&ss->sim, (enum encounter_action)i);
	jb_raw(out, "{\"ok\":true,\"result\":");
	jb_str(out, results[r]);
	jb_raw(out, ",\"encounter\":");
	/* After the last dialog the stage is "none"; the ship stays known. */
	encounter_json(&ss->sim.encounter, out);
	jb_raw(out, "}");
}

/* Fight for up to "ticks" ticks with the same inputs; firing only on the
 * first tick.  Stops when the battle is decided. */
static void
cmd_battle(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	int64_t ticks = json_get_int(cmd, "ticks", 1);
	int64_t turn = json_get_int(cmd, "turn", 0);
	int64_t fire = json_get_int(cmd, "fire", 0);
	int64_t sails = json_get_int(cmd, "sails", 0);
	int ev = 0, i;

	if (!ss->sim.battle.active || ss->sim.world == NULL) {
		reply_error(out, "no battle");
		return;
	}
	if (ticks < 1 || ticks > 10000 || turn < -1 || turn > 1 ||
	    sails < -1 || sails > 1) {
		reply_error(out, "battle needs \"ticks\" (1..10000), "
		    "\"turn\" and \"sails\" (-1, 0, 1), \"fire\" (0, 1)");
		return;
	}
	for (i = 0; i < ticks && ss->sim.battle.outcome == BAT_NONE; i++)
		ev |= sim_battle_tick(&ss->sim, (int)turn, i == 0 && fire != 0,
		    (int)sails);
	jb_raw(out, "{\"ok\":true,\"events\":");
	jb_int(out, ev);
	jb_raw(out, ",\"battle\":");
	battle_json(&ss->sim.battle, out);
	jb_raw(out, "}");
}

static void
cmd_finish(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	enum battle_outcome o;

	if (!ss->sim.battle.active || ss->sim.battle.outcome == BAT_NONE) {
		reply_error(out, "no finished battle");
		return;
	}
	if (ss->sim.battle.outcome == BAT_BOARDING &&
	    ss->sim.sword.result == SWORD_NONE) {
		reply_error(out, "board and fight first");
		return;
	}
	o = sim_battle_finish(&ss->sim, json_get_int(cmd, "keep", 0) != 0);
	jb_raw(out, "{\"ok\":true,\"outcome\":");
	jb_str(out, outcome_names[o]);
	jb_raw(out, ",\"fleet_ships\":");
	jb_int(out, ss->sim.fleet.nships);
	jb_raw(out, ",\"crew\":");
	jb_int(out, ss->sim.fleet.crew);
	jb_raw(out, ",\"gold\":");
	jb_int(out, ss->sim.fleet.gold);
	jb_raw(out, "}");
}

/* "enter" the town the fleet just reached (sail stopped at it), or
 * "leave" the town the player is in. */
static void
cmd_town(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	const char *action = json_get_str(cmd, "action");
	struct sim *s = &ss->sim;

	static const char *arrivals[] = { "enter", "fired", "sunk",
	    "caught" };
	int arrival = -1, disgraced = -1;

	if (action != NULL && (strcmp(action, "enter") == 0 ||
	    strcmp(action, "sail_in") == 0 || strcmp(action, "sneak") == 0 ||
	    strcmp(action, "guard") == 0)) {
		if (s->fleet.leaving < 0 || s->player_town >= 0) {
			reply_error(out, "no town reached");
			return;
		}
		if (action[0] == 'e')
			sim_enter_town(s, s->fleet.leaving);
		else if (action[1] == 'a')
			arrival = sim_sail_in(s, s->fleet.leaving);
		else if (action[1] == 'n')
			arrival = sim_sneak_in(s, s->fleet.leaving);
		else {
			int64_t w = json_get_int(cmd, "weapon", 2);

			if (w < 0 || w >= SWORD_WEAPONS) {
				reply_error(out, "guard needs \"weapon\" 0..2");
				return;
			}
			sim_guard_duel(s, s->fleet.leaving, (int)w);
		}
	} else if (action != NULL && strcmp(action, "duel_end") == 0) {
		if (!s->sword.active || s->sword.kind != SWORD_DUEL ||
		    s->sword.result == SWORD_NONE) {
			reply_error(out, "no finished duel");
			return;
		}
		disgraced = sim_duel_end(s);
	} else if (action != NULL && strcmp(action, "leave") == 0) {
		if (s->player_town < 0) {
			reply_error(out, "not in a town");
			return;
		}
		sim_leave_town(s);
	} else {
		reply_error(out, "town needs \"action\": enter, sail_in, sneak, "
		    "guard, duel_end or leave");
		return;
	}
	jb_raw(out, "{\"ok\":true,\"player_town\":");
	jb_int(out, s->player_town);
	jb_raw(out, ",\"visit_flags\":");
	jb_int(out, s->visit_flags);
	if (arrival >= 0) {
		jb_raw(out, ",\"arrival\":");
		jb_str(out, arrivals[arrival]);
		jb_raw(out, ",\"fleet_ships\":");
		jb_int(out, s->fleet.nships);
	}
	if (disgraced >= 0) {
		jb_raw(out, ",\"disgraced\":");
		jb_int(out, disgraced);
		jb_raw(out, ",\"reputation\":");
		jb_int(out, s->reputation);
	}
	jb_raw(out, "}");
}

/*
 * The shipwright: "prices" lists each ship's repair cost and price;
 * "repair" and "sell" take "ships", a list of fleet indices.
 */
static void
cmd_shipwright(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	static const char *repairs[] = { "done", "nothing", "too_dear" };
	static const char *sales[] = { "done", "nothing", "only_ship",
	    "short" };
	const char *action = json_get_str(cmd, "action");
	const struct json *ships = json_get(cmd, "ships");
	struct sim *s = &ss->sim;
	unsigned mask = 0;
	size_t i;
	int r;

	if (s->player_town < 0) {
		reply_error(out, "not in a town");
		return;
	}
	if (sim_shipwright_refuses(s)) {
		reply_error(out, "we do not trade with pirates");
		return;
	}
	if (ships != NULL) {
		if (ships->type != JSON_ARRAY) {
			reply_error(out, "\"ships\" is a list of indices");
			return;
		}
		for (i = 0; i < ships->count; i++) {
			if (ships->items[i]->type != JSON_INT ||
			    ships->items[i]->num < 0 ||
			    ships->items[i]->num >= s->fleet.nships) {
				reply_error(out, "bad ship index");
				return;
			}
			mask |= 1u << ships->items[i]->num;
		}
	}
	if (action != NULL && strcmp(action, "repair") == 0) {
		r = sim_repair(s, mask);
		jb_raw(out, "{\"ok\":true,\"result\":");
		jb_str(out, repairs[r]);
	} else if (action != NULL && strcmp(action, "sell") == 0) {
		r = sim_sell_ships(s, mask);
		jb_raw(out, "{\"ok\":true,\"result\":");
		jb_str(out, sales[r]);
	} else if (action != NULL && (strcmp(action, "sell_cannon") == 0 ||
	    strcmp(action, "buy_cannon") == 0)) {
		int64_t count = json_get_int(cmd, "count", 1);

		if (count < 1 || count > 1000) {
			reply_error(out, "cannon needs \"count\" (1..1000)");
			return;
		}
		r = sim_trade_cannon(s, action[0] == 's' ? -(int)count :
		    (int)count);
		jb_raw(out, "{\"ok\":true,\"traded\":");
		jb_int(out, r);
		jb_raw(out, ",\"guns\":");
		jb_int(out, s->fleet.guns);
	} else if (action != NULL && strcmp(action, "prices") == 0) {
		jb_raw(out, "{\"ok\":true,\"repair\":[");
		for (r = 0; r < s->fleet.nships; r++) {
			if (r > 0)
				jb_raw(out, ",");
			jb_int(out, sim_repair_cost(s, r));
		}
		jb_raw(out, "],\"price\":[");
		for (r = 0; r < s->fleet.nships; r++) {
			if (r > 0)
				jb_raw(out, ",");
			jb_int(out, sim_ship_price(s, r));
		}
		jb_raw(out, "]");
	} else {
		reply_error(out, "shipwright needs \"action\": prices, repair, "
		    "sell, sell_cannon or buy_cannon");
		return;
	}
	jb_raw(out, ",\"gold\":");
	jb_int(out, s->fleet.gold);
	jb_raw(out, ",\"shipwright_gold\":");
	jb_int(out, sim_shipwright_gold(s));
	jb_raw(out, "}");
}

/*
 * The merchant: "open" rolls this visit's stock and prices; "buy" and
 * "sell" take "good" (food, goods, sugar) and "count"; "close" ends the
 * visit.
 */
static void
cmd_merchant(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	static const char *goods[GOODS] = { "food", "goods", "sugar" };
	const char *action = json_get_str(cmd, "action");
	const char *good = json_get_str(cmd, "good");
	int64_t count = json_get_int(cmd, "count", 1);
	struct sim *s = &ss->sim;
	int g = -1, done = 0, i;

	if (s->player_town < 0) {
		reply_error(out, "not in a town");
		return;
	}
	if (action == NULL) {
		reply_error(out, "merchant needs \"action\": open, buy, sell "
		    "or close");
		return;
	}
	if (strcmp(action, "open") == 0) {
		if (sim_merchant_refuses(s)) {
			reply_error(out, "we do not trade with pirates");
			return;
		}
		sim_merchant_open(s);
	} else if (strcmp(action, "close") == 0) {
		sim_merchant_close(s);
	} else if (strcmp(action, "buy") == 0 || strcmp(action, "sell") == 0) {
		for (i = 0; good != NULL && i < GOODS; i++)
			if (strcmp(good, goods[i]) == 0)
				g = i;
		if (!s->market.open || g < 0 || count < 1 || count > 10000) {
			reply_error(out, "buy and sell need an open shop, "
			    "\"good\" (food, goods, sugar) and \"count\"");
			return;
		}
		done = sim_trade(s, g, action[0] == 'b' ? (int)count :
		    -(int)count);
	} else {
		reply_error(out, "unknown merchant action");
		return;
	}
	jb_raw(out, "{\"ok\":true,\"traded\":");
	jb_int(out, done);
	jb_raw(out, ",\"gold\":");
	jb_int(out, s->fleet.gold);
	jb_raw(out, ",\"merchant_gold\":");
	jb_int(out, s->merchant_gold);
	jb_raw(out, ",\"hold_space\":");
	jb_int(out, sim_hold_space(s));
	for (i = 0; i < GOODS; i++) {
		jb_raw(out, i == 0 ? ",\"stock\":[" : ",");
		jb_int(out, s->market.stock[i]);
	}
	for (i = 0; i < GOODS; i++) {
		jb_raw(out, i == 0 ? "],\"price\":[" : ",");
		jb_int(out, s->market.price[i]);
	}
	jb_raw(out, "]}");
}

/* The tavern: "open" counts the recruits; "recruit" with "accept". */
static void
cmd_tavern(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	const char *action = json_get_str(cmd, "action");
	struct sim *s = &ss->sim;

	if (s->player_town < 0) {
		reply_error(out, "not in a town");
		return;
	}
	if (action != NULL && strcmp(action, "open") == 0)
		sim_tavern_open(s);
	else if (action != NULL && strcmp(action, "recruit") == 0) {
		if (!(s->tavern_spots & TAVERN_CREW)) {
			reply_error(out, "no one to recruit");
			return;
		}
		sim_recruit(s, json_get_int(cmd, "accept", 1) != 0);
	} else {
		reply_error(out, "tavern needs \"action\": open or recruit");
		return;
	}
	jb_raw(out, "{\"ok\":true,\"recruits\":");
	jb_int(out, s->recruits);
	jb_raw(out, ",\"crew\":");
	jb_int(out, s->fleet.crew);
	jb_raw(out, ",\"tavern_spots\":");
	jb_int(out, s->tavern_spots);
	jb_raw(out, "}");
}

/*
 * The governor: "visit" (once a stay) promotes the player when earned
 * and states his offer; "pay" buys the Letter of Marque or the pardon.
 */
static void
cmd_governor(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	static const char *offers[] = { "none", "marque", "pardon",
	    "daughter" };
	const char *action = json_get_str(cmd, "action");
	struct sim *s = &ss->sim;
	int promoted = 0, acres = 0, paid = 0;
	long price;
	enum governor_offer o;

	if (s->player_town < 0) {
		reply_error(out, "not in a town");
		return;
	}
	if (action != NULL && strcmp(action, "visit") == 0) {
		if ((s->visit_flags & VISIT_GOVERNOR) ||
		    s->towns[s->player_town].citizens < 6) {
			reply_error(out, "the governor will not receive you");
			return;
		}
		s->visit_flags |= VISIT_GOVERNOR;
		promoted = sim_governor_promote(s, &acres);
	} else if (action != NULL && strcmp(action, "pay") == 0)
		paid = sim_governor_pay(s);
	else {
		reply_error(out, "governor needs \"action\": visit or pay");
		return;
	}
	o = sim_governor_offer(s, &price);
	jb_raw(out, "{\"ok\":true,\"promoted\":");
	jb_int(out, promoted);
	jb_raw(out, ",\"acres\":");
	jb_int(out, acres);
	jb_raw(out, ",\"paid\":");
	jb_int(out, paid);
	jb_raw(out, ",\"offer\":");
	jb_str(out, offers[o]);
	jb_raw(out, ",\"price\":");
	jb_int(out, price);
	jb_raw(out, ",\"gold\":");
	jb_int(out, s->fleet.gold);
	jb_raw(out, "}");
}

/* The land battle at the town reached: "start", "tick" (dx, dy,
 * button, ticks), "end". */
static void
cmd_land(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	const char *action = json_get_str(cmd, "action");
	struct sim *s = &ss->sim;
	struct sim_land *l = &s->land;
	int64_t i, n = json_get_int(cmd, "ticks", 1);
	int won = -1;

	if (action == NULL) {
		reply_error(out, "land needs \"action\": start, tick or end");
		return;
	}
	if (strcmp(action, "start") == 0)
		sim_land_start(s, s->fleet.leaving);
	else if (strcmp(action, "tick") == 0) {
		if (n < 1 || n > 10000) {
			reply_error(out, "land tick needs \"ticks\" 1..10000");
			return;
		}
		for (i = 0; i < n && l->result == 0; i++)
			sim_land_tick(s, (int)json_get_int(cmd, "dx", 0),
			    (int)json_get_int(cmd, "dy", 0),
			    (int)json_get_int(cmd, "button", 0));
	} else if (strcmp(action, "end") == 0)
		won = sim_land_end(s);
	else {
		reply_error(out, "land needs \"action\": start, tick or end");
		return;
	}
	jb_raw(out, "{\"ok\":true,\"active\":");
	jb_int(out, l->active);
	jb_raw(out, ",\"pass\":");
	jb_int(out, l->pass);
	jb_raw(out, ",\"result\":");
	jb_int(out, l->result);
	jb_raw(out, ",\"won\":");
	jb_int(out, won);
	jb_raw(out, ",\"units\":[");
	for (i = 1; i < LAND_UNITS; i++) {
		jb_raw(out, i > 1 ? ",[" : "[");
		jb_int(out, l->u[i].x);
		jb_raw(out, ",");
		jb_int(out, l->u[i].y);
		jb_raw(out, ",");
		jb_int(out, l->u[i].men);
		jb_raw(out, ",");
		jb_int(out, l->u[i].flags);
		jb_raw(out, ",");
		jb_int(out, l->u[i].morale);
		jb_raw(out, "]");
	}
	jb_raw(out, "]}");
}

/* After "land": "ashore" 1 goes ashore, 0 turns the ship about. */
static void
cmd_shore(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	if (json_get_int(cmd, "ashore", 1))
		sim_go_ashore(&ss->sim);
	else
		sim_turn_back(&ss->sim);
	jb_raw(out, "{\"ok\":true,\"ashore\":");
	jb_int(out, ss->sim.fleet.ashore);
	jb_raw(out, "}");
}

/* On foot: "dx", "dy" (-1..1) for "ticks". */
static void
cmd_march(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	static const char *stops[] = { "ticks", "land", "town",
	    "encounter", "boarded" };
	int64_t ticks = json_get_int(cmd, "ticks", 64);
	enum sail_stop stop;
	int town;

	if (!ss->sim.fleet.ashore || ticks < 1 || ticks > 64 * 365) {
		reply_error(out, "march needs a party ashore and \"ticks\" "
		    "(1..23360)");
		return;
	}
	stop = sim_march(&ss->sim, (int)json_get_int(cmd, "dx", 0),
	    (int)json_get_int(cmd, "dy", 0), (int)ticks, &town);
	jb_raw(out, "{\"ok\":true,\"stop\":");
	jb_str(out, stops[stop]);
	if (stop == SAIL_TOWN) {
		jb_raw(out, ",\"town\":");
		jb_str(out, ss->sim.towns[town].name);
	}
	jb_raw(out, ",\"x\":");
	jb_int(out, ss->sim.fleet.x);
	jb_raw(out, ",\"y\":");
	jb_int(out, ss->sim.fleet.y);
	jb_raw(out, ",\"day\":");
	jb_int(out, ss->sim.day);
	jb_raw(out, "}");
}

/*
 * Storming the town the fleet has reached: "start" (weapon, aboard),
 * then sword ticks, "end", then "sack", and "governor" (nation) when the
 * sack allows a conquest.
 */
static void
cmd_assault(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	const char *action = json_get_str(cmd, "action");
	struct sim *s = &ss->sim;
	int town = s->fleet.leaving, r = 0;
	struct sack k;

	if (town < 0 || (size_t)town >= s->ntowns || action == NULL) {
		reply_error(out, "assault needs a town reached and \"action\"");
		return;
	}
	memset(&k, 0, sizeof(k));
	k.hidden = -1;
	if (strcmp(action, "start") == 0)
		sim_assault_start(s, town, (int)json_get_int(cmd, "aboard", 0),
		    (int)json_get_int(cmd, "weapon", 2) % SWORD_WEAPONS);
	else if (strcmp(action, "end") == 0) {
		if (!s->sword.active || s->sword.result == SWORD_NONE) {
			reply_error(out, "no finished assault");
			return;
		}
		r = sim_assault_end(s, town);
	} else if (strcmp(action, "sack") == 0)
		sim_sack(s, town, &k);
	else if (strcmp(action, "governor") == 0) {
		int64_t n = json_get_int(cmd, "nation", -1);

		if (n < 0 || n > 3) {
			reply_error(out, "governor needs \"nation\" 0..3");
			return;
		}
		sim_install_governor(s, town, (int)n);
	} else {
		reply_error(out, "assault needs \"action\": start, end, sack "
		    "or governor");
		return;
	}
	jb_raw(out, "{\"ok\":true,\"result\":");
	jb_int(out, r);
	jb_raw(out, ",\"gold\":");
	jb_int(out, k.gold);
	jb_raw(out, ",\"hidden\":");
	jb_int(out, k.hidden);
	jb_raw(out, ",\"goods\":[");
	jb_int(out, k.goods[0]);
	jb_raw(out, ",");
	jb_int(out, k.goods[1]);
	jb_raw(out, ",");
	jb_int(out, k.goods[2]);
	jb_raw(out, "],\"cannon\":");
	jb_int(out, k.cannon);
	jb_raw(out, ",\"conquest\":");
	jb_int(out, k.conquest);
	jb_raw(out, ",\"crew\":");
	jb_int(out, s->fleet.crew);
	jb_raw(out, "}");
}

/* The bank: "divide" the plunder, then "continue" ("advance": 0 or 1). */
static void
cmd_bank(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	const char *action = json_get_str(cmd, "action");
	struct sim *s = &ss->sim;
	struct plunder p;

	memset(&p, 0, sizeof(p));
	if (s->player_town < 0) {
		reply_error(out, "not in a town");
		return;
	}
	if (action != NULL && strcmp(action, "divide") == 0)
		sim_divide_plunder(s, &p);
	else if (action != NULL && strcmp(action, "continue") == 0)
		sim_new_expedition(s, json_get_int(cmd, "advance", 0) != 0);
	else {
		reply_error(out, "bank needs \"action\": divide or continue");
		return;
	}
	jb_raw(out, "{\"ok\":true,\"share\":");
	jb_int(out, p.share);
	jb_raw(out, ",\"mood\":");
	jb_int(out, p.mood);
	jb_raw(out, ",\"yours\":");
	jb_int(out, p.yours);
	jb_raw(out, ",\"fortune\":");
	jb_int(out, s->fortune);
	jb_raw(out, ",\"gold\":");
	jb_int(out, s->fleet.gold);
	jb_raw(out, ",\"crew\":");
	jb_int(out, s->fleet.crew);
	jb_raw(out, ",\"difficulty\":");
	jb_int(out, s->player.difficulty);
	jb_raw(out, "}");
}

static void
cmd_board(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	int64_t weapon = json_get_int(cmd, "weapon", 2);

	if (!ss->sim.battle.active || ss->sim.battle.outcome != BAT_BOARDING ||
	    ss->sim.sword.active) {
		reply_error(out, "no boarding");
		return;
	}
	if (weapon < 0 || weapon >= SWORD_WEAPONS) {
		reply_error(out, "board needs \"weapon\" (0 rapier, "
		    "1 longsword, 2 cutlass)");
		return;
	}
	sim_board(&ss->sim, (int)weapon);
	jb_raw(out, "{\"ok\":true,\"sword\":");
	sword_json(&ss->sim.sword, out);
	jb_raw(out, "}");
}

/* Fight up to "ticks" ticks; "input" (0-11), when given, is set first. */
static void
cmd_sword(struct session *ss, const struct json *cmd, struct jbuf *out)
{
	int64_t ticks = json_get_int(cmd, "ticks", 1);
	int64_t input = json_get_int(cmd, "input", -1);
	int ev = 0, i;

	if (!ss->sim.sword.active || ss->sim.world == NULL) {
		reply_error(out, "no sword fight");
		return;
	}
	if (ticks < 1 || ticks > 10000 || input < -1 || input >= SWM_ADVANCE) {
		reply_error(out, "sword needs \"ticks\" (1..10000) and "
		    "\"input\" (0..11)");
		return;
	}
	if (input >= 0)
		sim_sword_input(&ss->sim, (int)input);
	for (i = 0; i < ticks && ss->sim.sword.result == SWORD_NONE; i++)
		ev |= sim_sword_tick(&ss->sim);
	jb_raw(out, "{\"ok\":true,\"events\":");
	jb_int(out, ev);
	jb_raw(out, ",\"sword\":");
	sword_json(&ss->sim.sword, out);
	jb_raw(out, "}");
}

int
proto_handle(struct session *ss, const char *line, struct jbuf *out)
{
	struct json *cmd;
	const char *name;
	char err[128];
	int quit = 0;

	if ((cmd = json_parse(line, err, sizeof(err))) == NULL) {
		reply_error(out, err);
		return (0);
	}
	if ((name = json_get_str(cmd, "cmd")) == NULL)
		reply_error(out, "missing \"cmd\"");
	else if (strcmp(name, "new_game") == 0)
		cmd_new_game(ss, cmd, out);
	else if (strcmp(name, "quit") == 0) {
		jb_raw(out, "{\"ok\":true}");
		quit = 1;
	} else if (strcmp(name, "load") == 0) {
		if (sim_from_json(&ss->sim, json_get(cmd, "state"), err,
		    sizeof(err)) != 0)
			reply_error(out, err);
		else if (attach_world(ss) != 0)
			reply_error(out, "cannot load the era's world");
		else {
			ss->started = 1;
			jb_raw(out, "{\"ok\":true}");
		}
	} else if (!ss->started)
		reply_error(out, "no game: send new_game or load first");
	else if (strcmp(name, "advance") == 0)
		cmd_advance(ss, cmd, out);
	else if (strcmp(name, "state") == 0) {
		jb_raw(out, "{\"ok\":true,\"state\":");
		sim_to_json(&ss->sim, out);
		jb_raw(out, "}");
	} else if (strcmp(name, "set") == 0)
		cmd_set(ss, cmd, out);
	else if (strcmp(name, "sail") == 0)
		cmd_sail(ss, cmd, out);
	else if (strcmp(name, "encounter") == 0)
		cmd_encounter(ss, cmd, out);
	else if (strcmp(name, "battle") == 0)
		cmd_battle(ss, cmd, out);
	else if (strcmp(name, "finish") == 0)
		cmd_finish(ss, cmd, out);
	else if (strcmp(name, "town") == 0)
		cmd_town(ss, cmd, out);
	else if (strcmp(name, "shipwright") == 0)
		cmd_shipwright(ss, cmd, out);
	else if (strcmp(name, "merchant") == 0)
		cmd_merchant(ss, cmd, out);
	else if (strcmp(name, "tavern") == 0)
		cmd_tavern(ss, cmd, out);
	else if (strcmp(name, "governor") == 0)
		cmd_governor(ss, cmd, out);
	else if (strcmp(name, "land") == 0)
		cmd_land(ss, cmd, out);
	else if (strcmp(name, "shore") == 0)
		cmd_shore(ss, cmd, out);
	else if (strcmp(name, "march") == 0)
		cmd_march(ss, cmd, out);
	else if (strcmp(name, "assault") == 0)
		cmd_assault(ss, cmd, out);
	else if (strcmp(name, "bank") == 0)
		cmd_bank(ss, cmd, out);
	else if (strcmp(name, "board") == 0)
		cmd_board(ss, cmd, out);
	else if (strcmp(name, "sword") == 0)
		cmd_sword(ss, cmd, out);
	else
		reply_error(out, "unknown command");
	json_free(cmd);
	return (quit);
}
