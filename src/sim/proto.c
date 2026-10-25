/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proto.h"

#define STATE_VERSION	1
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
	FIELD(standing), FIELD(governor), FIELD(id),
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
	jb_raw(b, "],\"era\":");
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
	tmp.era_year = (int)json_get_int(j, "era", 0);
	tmp.year = (int)json_get_int(j, "year", 0);
	tmp.day_of_year = (int)json_get_int(j, "day_of_year", -1);
	tmp.month = (int)json_get_int(j, "month", -1);
	tmp.day = (int32_t)json_get_int(j, "day", -1);
	tmp.diplomacy_range = (int)json_get_int(j, "diplomacy_range", 0);
	tmp.player_town = (int)json_get_int(j, "player_town", -2);
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
	ss->started = 1;
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
	else
		reply_error(out, "unknown command");
	json_free(cmd);
	return (quit);
}
