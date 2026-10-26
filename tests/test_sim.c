/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Simulation and protocol tests on a synthetic era; no game data needed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim/hof.h"
#include "sim/json.h"
#include "sim/proto.h"
#include "sim/rng.h"
#include "sim/sim.h"
#include "sim/internal.h"

static int failures;

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,	\
		    __LINE__, #cond);					\
		failures++;						\
	}								\
} while (0)

/* Four towns: two Spanish, one English next to them, one far French. */
static void
make_era(struct era_towns *era, struct scenario *scn)
{
	static const struct {
		const char	*name;
		int		 x, y, nation, soldiers;
	} t[] = {
		{ "Alpha", 100, 100, NATION_SPAIN, 40 },
		{ "Beta", 120, 110, NATION_SPAIN, 10 },
		{ "Gamma", 110, 130, NATION_ENGLAND, 60 },
		{ "Delta", 600, 400, NATION_FRANCE, 20 },
	};
	size_t i;

	memset(era, 0, sizeof(*era));
	memset(scn, 0, sizeof(*scn));
	era->year = 1660;
	era->count = sizeof(t) / sizeof(t[0]);
	for (i = 0; i < era->count; i++) {
		snprintf(era->towns[i].name, sizeof(era->towns[i].name), "%s",
		    t[i].name);
		era->towns[i].x = t[i].x;
		era->towns[i].y = t[i].y;
		era->towns[i].nation = t[i].nation;
		era->towns[i].soldiers = t[i].soldiers;
		era->towns[i].citizens = 30;
		era->towns[i].gold = 20;
		era->towns[i].wealth = 120;
	}
	snprintf(scn->name, sizeof(scn->name), "Test Captain");
	scn->relations[NATION_SPAIN][NATION_ENGLAND] = -1;
	scn->relations[NATION_ENGLAND][NATION_SPAIN] = -1;
	/* Delta is at war with England too, but out of range. */
	scn->relations[NATION_FRANCE][NATION_ENGLAND] = -1;
	scn->relations[NATION_ENGLAND][NATION_FRANCE] = -1;
}

static int
loader(void *arg, int era, int scenario, struct era_towns *towns,
    struct scenario *scn)
{
	(void)arg;
	(void)era;
	(void)scenario;
	make_era(towns, scn);
	return (0);
}

static void
test_rng(void)
{
	struct rng a, b;
	int i, v, seen[7] = { 0 };

	rng_seed(&a, 42, 1);
	rng_seed(&b, 42, 1);
	for (i = 0; i < 1000; i++)
		CHECK(rng_next(&a) == rng_next(&b));
	rng_seed(&b, 43, 1);
	CHECK(rng_next(&a) != rng_next(&b));
	for (i = 0; i < 7000; i++) {
		v = rng_below(&a, 7);
		CHECK(v >= 0 && v < 7);
		if (v >= 0 && v < 7)
			seen[v]++;
	}
	for (i = 0; i < 7; i++)
		CHECK(seen[i] > 800 && seen[i] < 1200);
	CHECK(rng_below(&a, 0) == 0 && rng_below(&a, -5) == 0);
}

static void
test_json(void)
{
	struct json *j;
	char err[128];

	j = json_parse(" {\"a\": [1, -2, {\"b\": \"x\\u00e9\\n\"}], "
	    "\"t\": true, \"n\": null} ", err, sizeof(err));
	CHECK(j != NULL);
	if (j != NULL) {
		CHECK(json_get(j, "a")->count == 3);
		CHECK(json_get(j, "a")->items[1]->num == -2);
		CHECK(strcmp(json_get_str(json_get(j, "a")->items[2], "b"),
		    "x\xc3\xa9\n") == 0);
		CHECK(json_get_int(j, "missing", 7) == 7);
		json_free(j);
	}
	CHECK(json_parse("{\"a\":1,}", err, sizeof(err)) == NULL);
	CHECK(json_parse("[1.5]", err, sizeof(err)) == NULL);
	CHECK(json_parse("\"open", err, sizeof(err)) == NULL);
	CHECK(json_parse("{} x", err, sizeof(err)) == NULL);
	CHECK(json_parse("99999999999999999999", err, sizeof(err)) == NULL);
}

/* News by absolute index (0 = first ever logged); must still be kept. */
static const struct sim_news *
news_abs(const struct sim *s, size_t k)
{
	return (sim_news_at(s, k - (s->nnews - sim_news_kept(s))));
}

/* Reset the conquest test conditions. */
static void
pin_war(struct sim *s)
{
	s->relations[NATION_SPAIN][NATION_ENGLAND] = -1;
	s->relations[NATION_ENGLAND][NATION_SPAIN] = -1;
	s->relations[NATION_FRANCE][NATION_ENGLAND] = -1;
	s->relations[NATION_ENGLAND][NATION_FRANCE] = -1;
	s->towns[1].soldiers = 0;
	s->towns[2].soldiers = 60;
	s->towns[3].soldiers = 0;
}

static void
test_rules(void)
{
	struct era_towns era;
	struct scenario scn;
	struct sim s;
	const struct sim_news *n;
	size_t k, before;
	int m, a, b, captured = 0, wrong = 0;

	make_era(&era, &scn);

	/*
	 * Conquest, with the conditions pinned before every update so that
	 * diplomacy and growth cannot drift them: England (Gamma, strong) is
	 * at war with Spain (Beta, empty, in range) and France (Delta, weak,
	 * out of range).
	 */
	sim_new_game(&s, &era, &scn, 7);
	s.diplomacy_range = 0xFFFF;
	for (m = 0; m < 400; m++) {
		pin_war(&s);
		before = s.nnews;
		sim_world_update(&s);
		for (k = before; k < s.nnews; k++) {
			n = news_abs(&s, k);
			if (n->code != NEWS_CAPTURE)
				continue;
			captured += n->b == 1;
			wrong += n->b == 3;	/* Delta is out of range */
		}
		s.towns[1].nation = NATION_SPAIN;	/* re-arm the target */
	}
	CHECK(captured > 0);
	CHECK(wrong == 0);
	for (a = 0; a < 4; a++)
		for (b = 0; b < 4; b++)
			CHECK(s.relations[a][b] == s.relations[b][a]);

	/* Same setup with the player in Beta: never captured, never given a
	 * new governor. */
	sim_new_game(&s, &era, &scn, 11);
	s.diplomacy_range = 0xFFFF;
	s.player_town = 1;
	captured = wrong = 0;
	for (m = 0; m < 2000; m++) {
		pin_war(&s);
		before = s.nnews;
		sim_world_update(&s);
		for (k = before; k < s.nnews; k++) {
			n = news_abs(&s, k);
			if ((n->code == NEWS_CAPTURE && n->b == 1) ||
			    (n->code == NEWS_GOVERNOR && n->a == 1))
				wrong++;
			captured += n->code == NEWS_GOVERNOR;
		}
	}
	CHECK(wrong == 0);
	CHECK(captured > 0);	/* governors do change elsewhere */
	CHECK(s.towns[1].nation == NATION_SPAIN);

	/* Diplomacy keeps relations symmetric and within -1..1. */
	sim_new_game(&s, &era, &scn, 13);
	s.diplomacy_range = 28;
	for (m = 0; m < 500; m++) {
		sim_world_update(&s);
		for (a = 0; a < 4; a++)
			for (b = 0; b < 4; b++) {
				CHECK(s.relations[a][b] == s.relations[b][a]);
				CHECK(s.relations[a][b] >= -1 &&
				    s.relations[a][b] <= 1);
			}
	}

	/* With a range of 28 every draw changes relations or is a no-op. */
	sim_new_game(&s, &era, &scn, 3);
	s.diplomacy_range = 28;
	for (m = 0; m < 200; m++)
		sim_world_update(&s);
	CHECK(s.nnews > 50);

	/* Time: an update per 30-day month, the year turns every 360 days. */
	sim_new_game(&s, &era, &scn, 5);
	/* The career starts mid-year with six entries in the log (VA
	 * 0x132f8, 0x13679): spread from New Year, war or alliance by the
	 * pair's relation, a town event at peace. */
	CHECK(s.day_of_year >= 65 && s.day_of_year < 215 && s.nnews == 6);
	for (k = 0; k < 6; k++) {
		const struct sim_news *e = news_abs(&s, k);

		CHECK(e->day < 0 && e->date >= 1660 * 360 &&
		    e->date < 1660 * 360 + s.day_of_year);
		CHECK(k == 0 || e->date > news_abs(&s, k - 1)->date);
		CHECK(e->code == NEWS_WAR || e->code == NEWS_ALLIANCE ||
		    (e->code >= NEWS_INDIANS && e->code <= NEWS_PLUNDER));
	}
	CHECK(news_abs(&s, 0)->date - 1660 * 360 == s.day_of_year / 12);
	s.day_of_year = s.month = 0;	/* the calendar from New Year */
	before = s.nnews;
	s.diplomacy_range = 28;	/* every draw is a diplomatic event */
	sim_advance(&s, 29);
	CHECK(s.month == 0 && s.day == 29 && s.year == 1660);
	sim_advance(&s, 1);
	CHECK(s.month == 1 && s.day == 30 && s.day_of_year == 30);
	sim_advance(&s, 95);
	CHECK(s.month == 4 && s.day == 125 && s.day_of_year == 125);
	sim_advance(&s, 360);
	CHECK(s.year == 1661 && s.day_of_year == 125 && s.day == 485);
	sim_advance(&s, 0);
	sim_advance(&s, -10);
	CHECK(s.day == 485);
	/* News is dated at its month boundary, not at the end. */
	for (k = before; k < s.nnews; k++)
		CHECK(news_abs(&s, k)->day % 30 == 0);
	CHECK(s.nnews > before);
}

/* Run a command script in a fresh session; returns all output lines. */
static char *
run(const char *const *cmds, size_t n)
{
	struct session ss;
	struct jbuf all = { 0 }, out;
	size_t i;

	memset(&ss, 0, sizeof(ss));
	ss.load = loader;
	for (i = 0; i < n; i++) {
		memset(&out, 0, sizeof(out));
		proto_handle(&ss, cmds[i], &out);
		jb_raw(&all, out.s != NULL ? out.s : "");
		jb_raw(&all, "\n");
		jb_free(&out);
	}
	return (all.s);
}

static void
test_protocol(void)
{
	static const char *const script[] = {
		"{\"cmd\":\"new_game\",\"era\":1660,\"seed\":99}",
		"{\"cmd\":\"set\",\"field\":\"diplomacy_range\",\"value\":40}",
		"{\"cmd\":\"advance\",\"days\":3650}",
		"{\"cmd\":\"state\"}",
	};
	static const char *const other_seed[] = {
		"{\"cmd\":\"new_game\",\"era\":1660,\"seed\":100}",
		"{\"cmd\":\"set\",\"field\":\"diplomacy_range\",\"value\":40}",
		"{\"cmd\":\"advance\",\"days\":3650}",
		"{\"cmd\":\"state\"}",
	};
	static const char *const errors[] = {
		"{\"cmd\":\"advance\",\"days\":30}",
		"not json",
		"{\"cmd\":\"new_game\",\"era\":1234}",
		"{\"cmd\":\"new_game\",\"era\":1660,\"nation\":\"venice\"}",
		"{\"cmd\":\"new_game\",\"era\":1660}",
		"{\"cmd\":\"fly\"}",
		"{\"cmd\":\"set\",\"town\":\"Nowhere\",\"field\":\"gold\","
		    "\"value\":1}",
		"{\"cmd\":\"set\",\"town\":\"Alpha\",\"field\":\"gold\","
		    "\"value\":70000}",
		"{\"cmd\":\"advance\",\"days\":0}",
		"{\"cmd\":\"load\",\"state\":{\"version\":9}}",
		"{\"cmd\":\"quit\"}",
	};
	char *a, *b, *c, *line, *err;
	size_t nlines;

	/* Same script, same output; another seed, another history. */
	a = run(script, 4);
	b = run(script, 4);
	c = run(other_seed, 4);
	CHECK(a != NULL && b != NULL && c != NULL);
	if (a != NULL && b != NULL && c != NULL) {
		CHECK(strcmp(a, b) == 0);
		CHECK(strcmp(a, c) != 0);
		CHECK(strstr(a, "\"scenario\":\"Test Captain\"") != NULL);
		CHECK(strstr(a, "\"day\":3650") != NULL);
	}
	free(b);
	free(c);

	err = run(errors, sizeof(errors) / sizeof(errors[0]));
	CHECK(err != NULL);
	for (nlines = 0, line = err; line != NULL && *line != '\0'; nlines++) {
		/* Every line but the last two (valid game start, quit) fails. */
		if (nlines != 4 && nlines != 10)
			CHECK(strncmp(line, "{\"ok\":false", 11) == 0);
		line = strchr(line, '\n');
		if (line != NULL)
			line++;
	}
	CHECK(nlines == sizeof(errors) / sizeof(errors[0]));
	free(err);
	free(a);
}

/* A saved state reloads to the same state and replays identically. */
static void
test_snapshot(void)
{
	struct session s1, s2;
	struct jbuf st1 = { 0 }, st2 = { 0 }, cmd = { 0 }, out = { 0 };
	struct jbuf r1 = { 0 }, r2 = { 0 };

	memset(&s1, 0, sizeof(s1));
	s1.load = loader;
	proto_handle(&s1, "{\"cmd\":\"new_game\",\"era\":1600,\"seed\":5}",
	    &out);
	jb_free(&out);
	sim_advance(&s1.sim, 30 * 80);	/* fill and wrap the news ring */
	CHECK(s1.sim.nnews > 0);
	sim_to_json(&s1.sim, &st1);

	jb_raw(&cmd, "{\"cmd\":\"load\",\"state\":");
	jb_raw(&cmd, st1.s);
	jb_raw(&cmd, "}");
	memset(&s2, 0, sizeof(s2));
	s2.load = loader;
	proto_handle(&s2, cmd.s, &out);
	CHECK(out.s != NULL && strcmp(out.s, "{\"ok\":true}") == 0);
	jb_free(&out);
	sim_to_json(&s2.sim, &st2);
	CHECK(st1.s != NULL && st2.s != NULL && strcmp(st1.s, st2.s) == 0);

	proto_handle(&s1, "{\"cmd\":\"advance\",\"days\":900}", &r1);
	proto_handle(&s2, "{\"cmd\":\"advance\",\"days\":900}", &r2);
	CHECK(r1.s != NULL && r2.s != NULL && strcmp(r1.s, r2.s) == 0);

	jb_free(&st1);
	jb_free(&st2);
	jb_free(&cmd);
	jb_free(&r1);
	jb_free(&r2);
}

static void
test_hof(void)
{
	static const char *path = "/tmp/stmalo-test-hof.txt";
	struct hof h, back;
	struct hof_entry e;
	char err[128];
	FILE *fp;
	int i;

	/* The original's rating scale: 0 is best, 21 worst. */
	CHECK(hof_rating(100) == 0 && hof_rating(90) == 0);
	CHECK(hof_rating(5) == 17 && hof_rating(1) == 18);
	CHECK(hof_rating(0) == 19 && hof_rating(-100) == 21);

	memset(&h, 0, sizeof(h));
	memset(&e, 0, sizeof(e));
	for (i = 0; i < 12; i++) {
		e.score = (i * 7) % 12;	/* 0, 7, 2, 9, 4, 11, 6, 1, 8, 3... */
		snprintf(e.name, sizeof(e.name), "Captain\t%d", i);
		snprintf(e.title, sizeof(e.title), "Title %d", i);
		hof_add(&h, &e);
	}
	CHECK(h.count == HOF_ENTRIES);
	for (i = 1; i < (int)h.count; i++)
		CHECK(h.entry[i - 1].score >= h.entry[i].score);
	CHECK(h.entry[0].score == 11 && h.entry[9].score == 2);
	CHECK(strchr(h.entry[0].name, '\t') == NULL);	/* tabs removed */
	e.score = 2;	/* ties the last entry: does not displace it */
	CHECK(hof_add(&h, &e) == -1);
	e.score = 5;
	CHECK(hof_add(&h, &e) == 7 && h.entry[9].score == 3);

	CHECK(hof_save(path, &h, err, sizeof(err)) == 0);
	CHECK(hof_load(path, &back, err, sizeof(err)) == 0);
	CHECK(memcmp(&h, &back, sizeof(h)) == 0);

	/* Hand edited out of order: sorted on load. */
	if ((fp = fopen(path, "w")) != NULL) {
		fputs("# stmalo hall of fame 1\n3\t17\tA\tX\n9\t0\tB\tY\n",
		    fp);
		fclose(fp);
	}
	CHECK(hof_load(path, &back, err, sizeof(err)) == 0);
	CHECK(back.count == 2 && strcmp(back.entry[0].name, "B") == 0);
	if ((fp = fopen(path, "w")) != NULL) {
		fputs("12 no tabs here\n", fp);
		fclose(fp);
	}
	CHECK(hof_load(path, &back, err, sizeof(err)) == -1);
	remove(path);
	/* No file yet: an empty Hall of Fame. */
	CHECK(hof_load(path, &back, err, sizeof(err)) == 0 && back.count == 0);
}

/*
 * Synthetic sword fight tables, simple enough to reason about: every
 * move has 6 frames (the fall 10); guards hold one line each; slashes
 * strike on frame 3, lunges on frame 2; the opponent always hits and
 * the player always does (thresholds 0), and a cautious opponent (15)
 * only stands on guard 1.  Tests adjust them as needed.
 */
static void
make_sword(struct sword_tables *t)
{
	int m, f;

	memset(t, 0, sizeof(*t));
	for (m = 0; m < SWORD_MOVES; m++)
		t->frames[m] = 6;
	t->frames[SWM_FALL] = 10;
	for (m = 0; m < BATTLE_LEVELS; m++) {
		t->reach_level[m] = 10;
		t->caution[m] = 15;
	}
	t->reach_weapon[0] = 40;
	t->reach_weapon[1] = 30;
	t->reach_weapon[2] = 20;
	t->foe_reach[0] = t->foe_reach[1] = t->foe_reach[2] = 30;
	t->player_edge = 450;
	t->foe_edge = 50;
	for (m = 0; m < 12; m++) {
		static const uint8_t lines[3] = { 2, 1, 4 };

		t->far_move[m] = m < 3 ? m : m < 6 ? SWM_RETREAT : SWM_ADVANCE;
		t->once[m] = m < 3 || m >= 6;
		t->line[m] = (uint8_t)(m % 3);
		t->area[m] = m >= 6 ? (uint8_t)(m % 3 == 2 ? 1 : m % 3 + 2) :
		    0;
		for (f = 0; f < SWORD_FRAMES; f++)
			if (m < 6)
				t->guard[m * 6 + f] = lines[m % 3];
		if (m >= 6 && m < 9) {
			t->strike[m * 6 + 3] = lines[m % 3];
			t->step_in[m * 6 + 5] = 4;
		} else if (m >= 9) {
			t->strike[m * 6 + 2] = lines[m % 3];
			t->step_in[m * 6 + 3] = 4;
		}
		if (m >= 3 && m < 6)
			t->step_back[m * 6] = 14;
	}
	t->step_in[SWM_ADVANCE * 6 + 5] = 14;
	t->step_back[SWM_RETREAT * 6] = 14;
	for (f = 0; f < SWORD_FRAMES; f++)
		t->guard[SWM_FALL * 6 + f] = 7;
	memset(t->foe_hit, 255, sizeof(t->foe_hit));	/* rarely */
	memset(t->far_ai, SWM_GUARD, sizeof(t->far_ai));
	memset(t->scripts, 0x90, sizeof(t->scripts));	/* parry */
}

/*
 * A synthetic world: all sea, except a land block of cells around
 * (110..129, 40..49).  Triangle "sine" and a flat speed table, enough to
 * check the rules' mechanics.
 */
static struct world test_world;

static void
make_world(struct world *w)
{
	int x, y, i;

	memset(w, 0, sizeof(*w));
	w->era = 4;
	memset(w->map_terrain, TERRAIN_SEA, sizeof(w->map_terrain));
	memset(w->battle_terrain, TERRAIN_SEA, sizeof(w->battle_terrain));
	/* Every land tile reads as a shore with the sea to its west. */
	memset(w->coast, 0x80, sizeof(w->coast));
	w->coast[1] = 0x33;
	/* Treasure spots: three cells east of each town in turn. */
	for (i = 0; i < TREASURE_SPOTS; i++) {
		static const int16_t town[4][2] = { { 100, 100 },
		    { 120, 110 }, { 110, 130 }, { 600, 400 } };

		w->treasure[i][0] = (int16_t)(town[i % 4][0] + 3);
		w->treasure[i][1] = town[i % 4][1];
	}
	memset(w->map_terrain + 1 * WORLD_TILE * WORLD_TILE, TERRAIN_LAND,
	    WORLD_TILE * WORLD_TILE);	/* tile 1: land */
	for (y = 40; y < 50; y++)
		for (x = 110; x < 130; x++)
			w->map[y * WORLD_CELLS_W + x] = 1;
	/* Battle tiles: 1 land as on the map, 2 shallows, on the row under
	 * the land (tile 2 looks like sea when sailing). */
	memset(w->battle_terrain + 1 * BATTLE_TILE * BATTLE_TILE,
	    TERRAIN_LAND, BATTLE_TILE * BATTLE_TILE);
	memset(w->battle_terrain + 2 * BATTLE_TILE * BATTLE_TILE,
	    TERRAIN_SHALLOWS, BATTLE_TILE * BATTLE_TILE);
	/* Battle screen 1: land on rows 0-4, shallows on row 5; its set
	 * 0 (coast to the north) offers it. */
	for (y = 0; y < BATTLE_SCREEN; y++)
		for (x = 0; x < BATTLE_SCREEN; x++)
			w->battle_screens[1][y * BATTLE_SCREEN + x] =
			    y < 5 ? 1 : y == 5 ? 2 : 0;
	for (x = 0; x < 4; x++)
		w->battle_sets[0][x] = 1;
	for (i = 0; i < 128; i++)
		w->sine[i] = (uint8_t)(i < 64 ? i * 4 : i == 64 ? 255 :
		    (128 - i) * 4);
	for (y = 0; y < SHIP_CLASSES; y++)
		for (x = 0; x < WIND_SECTORS; x++)
			w->speed[y][x] = x == 8 ? -1 : 7;
	/* Per nation: merchantmen, then a pirate sloop as record 1. */
	for (y = 0; y < ENCOUNTER_NATIONS; y++)
		for (x = 0; x < ENCOUNTER_SHIPS; x++) {
			uint8_t *r = w->encounter[y][x];

			r[0] = x == 1 ? 1 : 4;
			r[1] = x == 1 ? NATION_PIRATE : (uint8_t)y;
			r[2] = 20;
			r[3] = 8;
			r[4] = x == 1 ? 7 : 4;
		}
	/* Battles: always broadside; hit boxes as the game's. */
	memset(w->battle_ai, 1, sizeof(w->battle_ai));
	for (i = 0; i < BATTLE_TYPES; i++) {
		w->half_w[i] = (int16_t)(20 + 10 * (i / 3 < 3 ? i / 3 : 3));
		w->half_h[i] = (int16_t)(10 + 10 * (i / 3 < 3 ? i / 3 : 3));
	}
	w->half_w[9] = 40;
	w->half_h[9] = 10;
	w->hit_steps[0] = 3;
	w->hit_steps[1] = 2;
	w->hit_steps[2] = w->hit_steps[3] = w->hit_steps[4] = 1;
	make_sword(&w->sword);
}

static int
world_cb(void *arg, int era, struct world *w)
{
	(void)arg;
	make_world(w);
	w->era = era;
	return (0);
}

/* A game on the synthetic world, fleet at cell (cx, cy) heading "h". */
static void
sea_game(struct sim *s, int cx, int cy, int heading)
{
	struct era_towns era;
	struct scenario scn;

	make_era(&era, &scn);
	scn.nships = 1;
	sim_new_game(s, &era, &scn, 21);
	s->day_of_year = s->month = 0;	/* mechanics from New Year */
	s->world = &test_world;
	s->fleet.x = cx * WORLD_TILE + 8;
	s->fleet.y = cy * WORLD_TILE + 8;
	s->fleet.heading = (uint8_t)heading;
	s->fleet.leaving = -1;
	s->encounters = 0;	/* mechanics only; encounters have tests */
}

static void
test_sailing(void)
{
	struct sim s;
	int town, x0, y0;
	uint8_t w0;

	make_world(&test_world);

	/* North: y falls, x stays; speed ramps up and settles at 12. */
	sea_game(&s, 120, 100, 0x00);
	x0 = s.fleet.x;
	y0 = s.fleet.y;
	CHECK(sim_sail(&s, 0, 10, &town) == SAIL_TICKS);
	CHECK(s.fleet.x == x0 && s.fleet.y < y0 && s.fleet.speed == 12);
	/* East: x grows. */
	sea_game(&s, 120, 100, 0x40);
	x0 = s.fleet.x;
	CHECK(sim_sail(&s, 0, 10, &town) == SAIL_TICKS && s.fleet.x > x0);
	/* Heading east into a wind blowing west is sector 8, the slowest:
	 * (-1 + 2) x 3 = 3, clamped up to 4. */
	sea_game(&s, 120, 100, 0x40);
	s.wind = 0xC0;
	CHECK(sim_sail(&s, 0, 10, &town) == SAIL_TICKS && s.fleet.speed == 4);

	/* Turning: +16 on each tick where bit 2 of the counter is clear, so
	 * 8 of 16 consecutive ticks. */
	sea_game(&s, 300, 300, 0x00);
	s.fleet.ticks = 0;
	CHECK(sim_sail(&s, 1, 16, &town) == SAIL_TICKS);
	CHECK(s.fleet.heading == 0x80);

	/* Land stops the ship on the water, before the coast. */
	sea_game(&s, 120, 60, 0x00);
	CHECK(sim_sail(&s, 0, 2000, &town) == SAIL_LAND);
	CHECK(world_terrain(&test_world, s.fleet.x, s.fleet.y) == TERRAIN_SEA);
	CHECK(s.fleet.y / WORLD_TILE == 50 && s.fleet.speed == 0);

	/* World edge: the ship turns back in. */
	sea_game(&s, 4, 300, 0xC0);
	CHECK(sim_sail(&s, 0, 200, &town) == SAIL_TICKS);
	CHECK(s.fleet.heading == 0x40 && s.fleet.x >= 0x41);

	/* Time: a day per 64 ticks; the wind may only shift every 128. */
	sea_game(&s, 300, 300, 0x00);
	s.fleet.ticks = 0;
	w0 = s.wind;
	sim_sail(&s, 0, 127, &town);
	CHECK(s.day == 1 && s.wind == w0);
	sim_sail(&s, 0, 64 * 30, &town);
	CHECK(s.day == 31 && s.month == 1);

	/* Arrival at a town; not at the one just left. Gamma is at cell
	 * (110, 130): start beside it, leaving it, sail away and back. */
	sea_game(&s, 110, 133, 0x00);
	s.fleet.leaving = 2;
	CHECK(sim_sail(&s, 0, 10, &town) == SAIL_TICKS);	/* in harbour */
	sea_game(&s, 110, 140, 0x00);
	CHECK(sim_sail(&s, 0, 2000, &town) == SAIL_TOWN && town == 2);
	CHECK(s.fleet.leaving == 2 && s.fleet.speed == 0);
}

/* A saved fleet reloads and sails on identically. */
static void
test_sail_snapshot(void)
{
	struct session s1, s2;
	struct jbuf st = { 0 }, cmd = { 0 }, out = { 0 }, r1 = { 0 },
	    r2 = { 0 };

	memset(&s1, 0, sizeof(s1));
	s1.load = loader;
	s1.load_world = world_cb;
	proto_handle(&s1, "{\"cmd\":\"new_game\",\"era\":1660}", &out);
	jb_free(&out);
	s1.sim.fleet.x = 300 * WORLD_TILE;	/* open sea */
	s1.sim.fleet.y = 300 * WORLD_TILE;
	s1.sim.encounters = 0;
	proto_handle(&s1, "{\"cmd\":\"sail\",\"turn\":1,\"ticks\":300}",
	    &out);
	CHECK(out.s != NULL && strstr(out.s, "\"stop\":\"ticks\"") != NULL);
	jb_free(&out);
	sim_to_json(&s1.sim, &st);
	jb_raw(&cmd, "{\"cmd\":\"load\",\"state\":");
	jb_raw(&cmd, st.s);
	jb_raw(&cmd, "}");
	memset(&s2, 0, sizeof(s2));
	s2.load = loader;
	s2.load_world = world_cb;
	proto_handle(&s2, cmd.s, &out);
	CHECK(out.s != NULL && strcmp(out.s, "{\"ok\":true}") == 0);
	jb_free(&out);
	proto_handle(&s1, "{\"cmd\":\"sail\",\"turn\":-1,\"ticks\":500}",
	    &r1);
	proto_handle(&s2, "{\"cmd\":\"sail\",\"turn\":-1,\"ticks\":500}",
	    &r2);
	CHECK(r1.s != NULL && r2.s != NULL && strcmp(r1.s, r2.s) == 0);
	/* Without a world, sailing is refused. */
	s2.load_world = NULL;
	session_free(&s2);
	proto_handle(&s2, "{\"cmd\":\"sail\"}", &out);
	CHECK(out.s != NULL && strstr(out.s, "\"ok\":false") != NULL);
	jb_free(&out);
	session_free(&s1);
	jb_free(&st);
	jb_free(&cmd);
	jb_free(&r1);
	jb_free(&r2);
}

/* Encounters: sightings, the dialogs, and their outcomes. */
static void
test_encounters(void)
{
	struct sim s;
	int town, i, sighted = 0, far = 0, hostile = 0;
	enum encounter_result r;

	/* Far from towns a sail appears in 1 of 16 checks; near them, the
	 * lingering counter makes them rarer and rarer. */
	sea_game(&s, 300, 300, 0x00);	/* the towns are 150+ cells away */
	s.encounters = 1;
	for (i = 0; i < 400; i++) {
		s.encounter.stage = ENC_NONE;
		s.fleet.x = 300 * WORLD_TILE;
		s.fleet.y = 300 * WORLD_TILE;
		if (sim_sail(&s, 0, 32, &town) == SAIL_ENCOUNTER) {
			far++;
			hostile += s.encounter.hostile;
		}
	}
	CHECK(far > 10 && far < 50);	/* about 400 / 16 = 25 */
	CHECK(hostile > 0);	/* pirates are among the records */

	/* Near a town (2 cells from Beta), a first check adds 6 to the
	 * counter: (r & 7) + 1 < 2, 1 in 8.  Lingering shuts sightings off. */
	sea_game(&s, 120, 112, 0x00);
	s.encounters = 1;
	for (i = 0; i < 400; i++) {
		s.encounter.stage = ENC_NONE;
		s.heat = 0;
		s.fleet.x = 120 * WORLD_TILE + 8;
		s.fleet.y = 112 * WORLD_TILE + 8;
		if (sim_sail(&s, 0, 32, &town) == SAIL_ENCOUNTER)
			sighted++;
	}
	CHECK(sighted > 25 && sighted < 80);	/* about 400 / 8 = 50 */
	for (i = 0, sighted = 0; i < 400; i++) {
		s.encounter.stage = ENC_NONE;
		s.fleet.x = 120 * WORLD_TILE + 8;
		s.fleet.y = 112 * WORLD_TILE + 8;
		if (i > 2 && sim_sail(&s, 0, 32, &town) == SAIL_ENCOUNTER)
			sighted++;
		else if (i <= 2)
			sim_sail(&s, 0, 32, &town);
	}
	CHECK(sighted == 0 && s.heat > 100);

	/* While an encounter is open, sailing waits for an answer. */
	sea_game(&s, 300, 300, 0x00);
	s.encounter.stage = ENC_SIGHTED;
	s.encounter.nation = 1;
	CHECK(sim_sail(&s, 0, 10, &town) == SAIL_ENCOUNTER);
	CHECK(s.fleet.ticks == 0);

	/* A peaceful ship can be left at any step. */
	s.encounter.hostile = 0;
	CHECK(sim_encounter_act(&s, ACT_IGNORE) == RES_ESCAPED);
	CHECK(s.encounter.stage == ENC_NONE && s.heat == 16);
	s.encounter.stage = ENC_SIGHTED;
	CHECK(sim_encounter_act(&s, ACT_INVESTIGATE) == RES_CONTINUE);
	CHECK(s.encounter.stage == ENC_IDENTIFIED);
	CHECK(sim_encounter_act(&s, ACT_INVESTIGATE) == RES_CONTINUE);
	CHECK(s.encounter.stage == ENC_COLOURS);
	CHECK(sim_encounter_act(&s, ACT_HAIL) == RES_NEWS);
	/* A disliked nation does not answer. */
	s.encounter.stage = ENC_COLOURS;
	s.standing[1] = -4;
	CHECK(sim_encounter_act(&s, ACT_HAIL) == RES_UNANSWERED);
	CHECK(s.battle.active);		/* she opens fire */
	s.battle.active = 0;
	s.encounter.stage = ENC_COLOURS;
	CHECK(sim_encounter_act(&s, ACT_BATTLE) == RES_BATTLE);
	CHECK(s.battle.active && s.encounter.stage == ENC_NONE);
	CHECK(sim_sail(&s, 0, 10, &town) == SAIL_ENCOUNTER);	/* fight! */
	s.battle.active = 0;

	/* A hunter cannot be avoided once seen; at the hardest level she
	 * catches a fleeing player 2 times in 3. */
	s.encounter.stage = ENC_IDENTIFIED;
	s.encounter.hostile = 1;
	CHECK(sim_encounter_act(&s, ACT_IGNORE) == RES_CONTINUE);
	s.player.difficulty = 3;
	for (i = 0, hostile = 0; i < 300; i++) {
		s.encounter.stage = ENC_COLOURS;
		r = sim_encounter_act(&s, ACT_IGNORE);
		hostile += r == RES_OVERTAKEN;
		s.battle.active = 0;
	}
	CHECK(hostile == 300);	/* rand(3) < 3 always */
	s.player.difficulty = 0;
	for (i = 0, hostile = 0; i < 300; i++) {
		s.encounter.stage = ENC_COLOURS;
		hostile += sim_encounter_act(&s, ACT_IGNORE) == RES_OVERTAKEN;
	}
	CHECK(hostile == 0);	/* rand(3) < 0 never */
}

/* A battle against a merchantman (type 4, 8 guns, 80 men). */
static void
battle_game(struct sim *s)
{
	sea_game(s, 300, 300, 0x00);
	s->fleet.ships[0] = 1;		/* a sloop */
	s->fleet.crew = 40;
	s->fleet.guns = 12;
	memset(&s->encounter, 0, sizeof(s->encounter));
	s->encounter.type = 4;
	s->encounter.nation = 1;
	s->encounter.guns = 8;
	s->encounter.crew = 20;
	s->encounter.role = 4;
	s->encounter.gold = 30;
	sim_battle_start(s);
}

/* Sea battles: setup, gunnery, damage, outcomes and their effects. */
static void
test_battles(void)
{
	struct sim s, s2;
	struct sim_battle *bt = &s.battle;
	int i, ev, hits, gx, gy;
	long gold;

	make_world(&test_world);

	/* Setup: guns limited by crew/4 and by the class ((1 + 2) * 4). */
	battle_game(&s);
	CHECK(bt->active && bt->outcome == BAT_NONE);
	CHECK(bt->you.crew == 40 && bt->you.guns == 10);
	CHECK(bt->guns_ashore == 2 && bt->crew_ashore == 0);
	CHECK(bt->foe.crew == 80 && bt->foe.guns == 8 && bt->foe.type == 4);
	CHECK(bt->you.turn_rate == 3 && bt->foe.turn_rate == 2);
	gx = abs(bt->you.x - bt->foe.x) / 256;
	gy = abs(bt->you.y - bt->foe.y) / 256;
	CHECK(gx >= 64 && gx <= 94 && gy >= 64 && gy <= 94);
	CHECK(!bt->you.full_sails && !bt->foe.full_sails);
	CHECK(abs((signed char)(uint8_t)(bt->foe.heading - bt->you.heading))
	    <= 40);

	/* Firing: one shot, then a reload of |144 - 8 x (mood / 2 + 4)|
	 * ticks: 112 with a neutral crew. */
	s.fleet.mood = 0;
	ev = sim_battle_tick(&s, 0, 1, 0);
	CHECK((ev & BEV_FIRED) && bt->you.reload == 112);
	CHECK(bt->you.shot.ticks > 0);
	CHECK((sim_battle_tick(&s, 0, 1, 0) & BEV_FIRED) == 0);

	/* Sails take 160 ticks to change; the guns wait meanwhile. */
	battle_game(&s);
	CHECK(sim_battle_tick(&s, 0, 0, 1) == 0 && bt->you.sail_timer == 160);
	for (i = 0, ev = 0; i < 160 && bt->outcome == BAT_NONE; i++)
		ev |= sim_battle_tick(&s, 0, 0, 0);
	CHECK((ev & BEV_SAILS) && bt->you.full_sails);

	/* Steering: 16 per 8 ticks. */
	battle_game(&s);
	gx = bt->you.heading;
	for (i = 0; i < 8; i++)
		sim_battle_tick(&s, 1, 0, 0);
	CHECK(bt->you.heading == ((gx + 16) & 0xFF));

	/* A shot at point blank hits: the hull takes guns x 4 / (class + 2),
	 * halved under battle sails, and half again at difficulty 0. */
	battle_game(&s);
	bt->foe.x = bt->you.x + 60 * 256;	/* abeam, to starboard */
	bt->foe.y = bt->you.y;
	bt->you.heading = 0;
	bt->foe.heading = 0;
	bt->foe.target = 0;
	bt->foe.guns = 0;			/* she cannot answer */
	sim_battle_tick(&s, 0, 1, 0);
	for (i = 0, hits = 0; i < 40 && bt->outcome == BAT_NONE; i++)
		hits += (sim_battle_tick(&s, 0, 0, 0) & BEV_HIT_FOE) != 0;
	CHECK(hits == 1);
	CHECK(s.player.difficulty == 0 && bt->foe.damage == 4);	/* 6, 3 */
	CHECK(bt->foe.crew < 80);
	/* At difficulty 1 the bonus is gone, unless gunnery brings the
	 * level back to 0. */
	for (i = 0; i < 2; i++) {
		int j;

		battle_game(&s);
		s.player.difficulty = 1;
		s.player.ability = i == 0 ? ABILITY_FENCING : ABILITY_GUNNERY;
		bt->foe.x = bt->you.x + 60 * 256;
		bt->foe.y = bt->you.y;
		bt->you.heading = bt->foe.heading = bt->foe.target = 0;
		bt->foe.guns = 0;
		sim_battle_tick(&s, 0, 1, 0);
		for (j = 0; j < 40 && bt->outcome == BAT_NONE; j++)
			sim_battle_tick(&s, 0, 0, 0);
		CHECK(bt->foe.damage == (i == 0 ? 3 : 4));
	}
	battle_game(&s);
	bt->foe.x = bt->you.x + 60 * 256;
	bt->foe.y = bt->you.y;
	bt->you.heading = bt->foe.heading = bt->foe.target = 0;
	bt->foe.guns = 0;
	bt->foe.damage = 4;

	/* Sinking at damage 64; the player keeps her prize money only from
	 * surrenders. */
	bt->foe.damage = 63;
	bt->foe.full_sails = 1;
	sim_battle_tick(&s, 0, 1, 0);
	for (i = 0; i < 300 && bt->outcome == BAT_NONE; i++)
		sim_battle_tick(&s, 0, 1, 0);
	CHECK(bt->outcome == BAT_ENEMY_SUNK);
	gold = s.fleet.gold;
	CHECK(sim_battle_finish(&s, 1) == BAT_ENEMY_SUNK);
	CHECK(!bt->active && s.fleet.nships == 1 && s.fleet.gold == gold);
	CHECK(s.fleet.guns == bt->you.guns + 2);

	/* Escape: more than 240 pixels apart on an axis. */
	battle_game(&s);
	bt->foe.x = bt->you.x + 239 * 256;
	bt->foe.y = bt->you.y;
	bt->you.heading = 0xC0;		/* west, away from her */
	bt->you.full_sails = 1;
	for (i = 0; i < 400 && bt->outcome == BAT_NONE; i++)
		sim_battle_tick(&s, 0, 0, 0);
	CHECK(bt->outcome == BAT_ESCAPED);

	/* Boarding: the ships touch; the sword fight decides.  Nothing is
	 * applied before it is fought. */
	battle_game(&s);
	bt->foe.x = bt->you.x;
	bt->foe.y = bt->you.y;
	sim_battle_tick(&s, 0, 0, 0);
	CHECK(bt->outcome == BAT_BOARDING);
	CHECK(sim_battle_finish(&s, 1) == BAT_NONE && bt->active);
	sim_board(&s, 2);
	CHECK(s.sword.active && s.sword.kind == SWORD_CREW);
	CHECK(s.sword.you.men == 40 && s.sword.foe.men == 80);
	CHECK(s.sword.you.bar == 31 * (40 * 7) / (40 * 7 + 80 * 4) + 8);
	CHECK(sim_battle_finish(&s, 1) == BAT_NONE);	/* still fighting */
	s.sword.result = SWORD_WON;
	s.sword.you.men = 30;			/* 10 fell */
	gold = s.fleet.gold;
	CHECK(sim_battle_finish(&s, 1) == BAT_SURRENDER);
	CHECK(s.fleet.nships == 2 && s.fleet.ships[1] == 4);
	CHECK(s.fleet.gold == gold + 300 && s.fleet.crew == 30);
	CHECK(!s.sword.active && !bt->active);
	/* Driven back: no prize, the losses stay. */
	battle_game(&s);
	bt->outcome = BAT_BOARDING;
	sim_board(&s, 0);
	s.sword.result = SWORD_REPELLED;
	s.sword.you.men = 25;
	CHECK(sim_battle_finish(&s, 1) == BAT_REPELLED);
	CHECK(s.fleet.nships == 1 && s.fleet.crew == 25);
	/* Lost: prison aboard (an injury), then a fresh start in the
	 * same type of ship. */
	battle_game(&s);
	bt->outcome = BAT_BOARDING;
	sim_board(&s, 0);
	s.sword.result = SWORD_LOST;
	int ship0 = s.fleet.ships[0] & 7;
	CHECK(sim_battle_finish(&s, 1) == BAT_DEFEATED);
	CHECK(s.fleet.nships == 1 && s.fleet.ships[0] == ship0 &&
	    s.jail.fresh && s.jail.cause == JAIL_SHIP && s.injuries == 1);

	/* Surrender: an outmatched enemy alongside strikes her colours at
	 * the next decision. */
	battle_game(&s);
	bt->foe.x = bt->you.x + 50 * 256;
	bt->foe.y = bt->you.y + 30 * 256;
	bt->foe.damage = 40;
	bt->foe.guns = 2;
	bt->foe.crew = 10;
	bt->foe.speed = 0;
	for (i = 0; i < 64 && bt->outcome == BAT_NONE; i++) {
		bt->foe.x = bt->you.x + 50 * 256;
		bt->foe.y = bt->you.y + 30 * 256;
		sim_battle_tick(&s, 0, 0, 0);
	}
	CHECK(bt->outcome == BAT_SURRENDER && bt->tick == 64);
	CHECK(sim_battle_finish(&s, 0) == BAT_SURRENDER);
	CHECK(s.fleet.nships == 1);	/* plundered, not kept */

	/* Exact limits, with both ships sailing north side by side at the
	 * same speed: boarding when the quarter boxes touch (5 + 7 = 12
	 * pixels across), escape beyond 240 pixels. */
	for (i = 0; i < 4; i++) {
		static const int gap[4] = { 12, 13, 240, 241 };
		static const int want[4] = { BAT_BOARDING, BAT_NONE, BAT_NONE,
		    BAT_ESCAPED };

		battle_game(&s);
		bt->wind_strength = 0;
		bt->you.heading = bt->foe.heading = bt->foe.target = 0;
		bt->foe.x = bt->you.x + gap[i] * 256;
		bt->foe.y = bt->you.y;
		sim_battle_tick(&s, 0, 0, 0);
		CHECK(bt->you.speed == bt->foe.speed);
		CHECK(bt->outcome == want[i]);
	}

	/* Sunset after 4500 ticks. */
	battle_game(&s);
	bt->tick = 4499;
	bt->foe.x = bt->you.x + 100 * 256;
	sim_battle_tick(&s, 0, 0, 0);
	CHECK(bt->outcome == BAT_SUNSET);

	/* Deterministic: the same start and inputs give the same battle. */
	battle_game(&s);
	battle_game(&s2);
	for (i = 0; i < 2000; i++) {
		sim_battle_tick(&s, i % 3 - 1, i % 50 == 0, i == 700);
		sim_battle_tick(&s2, i % 3 - 1, i % 50 == 0, i == 700);
	}
	CHECK(memcmp(&s.battle, &s2.battle, sizeof(s.battle)) == 0);
	CHECK(bt->tick > 0);
}

/* A battle through the protocol: snapshots mid-fight replay the same. */
static void
test_battle_protocol(void)
{
	struct session s1, s2;
	struct jbuf st = { 0 }, cmd = { 0 }, out = { 0 }, r1 = { 0 },
	    r2 = { 0 };

	memset(&s1, 0, sizeof(s1));
	s1.load = loader;
	s1.load_world = world_cb;
	proto_handle(&s1, "{\"cmd\":\"new_game\",\"era\":1660}", &out);
	jb_free(&out);
	proto_handle(&s1, "{\"cmd\":\"battle\"}", &out);
	CHECK(out.s != NULL && strstr(out.s, "no battle") != NULL);
	jb_free(&out);
	s1.sim.encounter.stage = ENC_COLOURS;
	s1.sim.encounter.type = 4;
	s1.sim.encounter.guns = 8;
	s1.sim.encounter.crew = 20;
	s1.sim.encounter.role = 4;
	proto_handle(&s1, "{\"cmd\":\"encounter\",\"action\":\"battle\"}",
	    &out);
	CHECK(out.s != NULL && strstr(out.s, "\"battle\"") != NULL);
	jb_free(&out);
	proto_handle(&s1, "{\"cmd\":\"finish\"}", &out);
	CHECK(out.s != NULL && strstr(out.s, "no finished battle") != NULL);
	jb_free(&out);
	proto_handle(&s1, "{\"cmd\":\"battle\",\"ticks\":100,\"fire\":1,"
	    "\"turn\":1}", &out);
	CHECK(out.s != NULL && strstr(out.s, "\"tick\":") != NULL &&
	    strstr(out.s, "\"events\":") != NULL);
	jb_free(&out);
	proto_handle(&s1, "{\"cmd\":\"battle\",\"turn\":2}", &out);
	CHECK(out.s != NULL && strstr(out.s, "\"ok\":false") != NULL);
	jb_free(&out);

	sim_to_json(&s1.sim, &st);
	jb_raw(&cmd, "{\"cmd\":\"load\",\"state\":");
	jb_raw(&cmd, st.s);
	jb_raw(&cmd, "}");
	memset(&s2, 0, sizeof(s2));
	s2.load = loader;
	s2.load_world = world_cb;
	proto_handle(&s2, cmd.s, &out);
	CHECK(out.s != NULL && strcmp(out.s, "{\"ok\":true}") == 0);
	jb_free(&out);
	CHECK(memcmp(&s1.sim.battle, &s2.sim.battle,
	    sizeof(s1.sim.battle)) == 0);
	proto_handle(&s1, "{\"cmd\":\"battle\",\"ticks\":3000,\"fire\":1}",
	    &r1);
	proto_handle(&s2, "{\"cmd\":\"battle\",\"ticks\":3000,\"fire\":1}",
	    &r2);
	CHECK(r1.s != NULL && r2.s != NULL && strcmp(r1.s, r2.s) == 0);
	if (s1.sim.battle.outcome != BAT_NONE) {
		proto_handle(&s1, "{\"cmd\":\"finish\",\"keep\":1}", &out);
		CHECK(out.s != NULL && strstr(out.s, "\"outcome\":") != NULL);
		CHECK(!s1.sim.battle.active);
		jb_free(&out);
	}
	session_free(&s1);
	session_free(&s2);
	jb_free(&st);
	jb_free(&cmd);
	jb_free(&r1);
	jb_free(&r2);
}

/* A crew fight at "dist" pixels: 40 men (factor 7) against 80 (4). */
static void
sword_game(struct sim *s, int dist)
{
	struct sword_setup in;

	sea_game(s, 300, 300, 0);
	memset(&in, 0, sizeof(in));
	in.kind = SWORD_CREW;
	in.you_men = 40;
	in.you_factor = 7;
	in.foe_men = 80;
	in.foe_factor = 4;
	in.title = 4;
	in.nation = 1;
	in.weapon = 2;			/* cutlass */
	sim_sword_start(s, &in);
	s->sword.foe.x = s->sword.you.x - dist;
}

/* Sword fights: setup, moves, hits, the bar, the melee and the end. */
static void
test_sword(void)
{
	struct sim s, s2;
	struct sim_sword *sw = &s.sword;
	int i, ev, x;

	make_world(&test_world);

	/* Setup: the bar from men x factor, start positions, level. */
	sword_game(&s, 120);
	CHECK(sw->active && sw->result == SWORD_NONE);
	CHECK(sw->you.bar == 31 * 280 / 600 + 8 && sw->foe.bar == 47 - 22);
	CHECK(sw->you.x == 340 && sw->foe.x == 220);
	CHECK(sw->foe.weapon == 1 || sw->foe.weapon == 2);
	CHECK(sw->you.move == -1 && sw->level == 0);
	{
		struct sword_setup in;

		memset(&in, 0, sizeof(in));
		in.you_men = in.foe_men = in.you_factor = in.foe_factor = 1;
		s.player.difficulty = 2;
		s.player.ability = ABILITY_FENCING;
		sim_sword_start(&s, &in);
		CHECK(sw->level == 1);
		in.extra = 9;
		sim_sword_start(&s, &in);
		CHECK(sw->level == 4);		/* clamped */
		CHECK(sw->foe.weapon == 1 || sw->foe.weapon == 2);
		in.kind = SWORD_DUEL;
		sim_sword_start(&s, &in);
		CHECK(sw->foe.weapon == 0 && sw->you.x == 320 &&
		    sw->foe.x == 240);
	}

	/* The opponent starts on the middle guard.  A high slash hits on
	 * frame 3: the cutlass moves the bar 5 x 2 steps, pushes back 4, and
	 * wounds the head. */
	sword_game(&s, 25);
	sim_sword_input(&s, SWM_SLASH + 2);
	x = sw->foe.x;
	for (i = 0, ev = 0; i < 4; i++)
		ev |= sim_sword_tick(&s);
	CHECK((ev & SEV_HIT_FOE) && sw->you.move == SWM_SLASH + 2);
	CHECK(sw->you.bar == 32 && sw->foe.bar == 15);
	CHECK(sw->foe.x == x - 4 && sw->foe.wounds[0] == 1);
	CHECK(sw->input == SWM_GUARD);		/* taken once */
	/* The middle line is guarded: a clash, no hit. */
	sword_game(&s, 25);
	sim_sword_input(&s, SWM_SLASH);
	for (i = 0, ev = 0; i < 4; i++)
		ev |= sim_sword_tick(&s);
	CHECK((ev & SEV_CLASH) && !(ev & SEV_HIT_FOE));
	/* Out of reach (10 + 20): a miss. */
	sword_game(&s, 31);
	sim_sword_input(&s, SWM_SLASH + 2);
	for (i = 0, ev = 0; i < 4; i++)
		ev |= sim_sword_tick(&s);
	CHECK(!(ev & SEV_HIT_FOE) && sw->foe.bar == 25);
	/* A lunge strikes on frame 2 and moves the bar one step a pass. */
	sword_game(&s, 25);
	sim_sword_input(&s, SWM_LUNGE + 1);
	sim_sword_tick(&s);
	sim_sword_tick(&s);
	CHECK(sim_sword_tick(&s) & SEV_HIT_FOE);	/* frame 2: tick 3 */
	CHECK(sw->foe.bar == 20 && sw->foe.wounds[2] == 1);

	/* At a distance an attack becomes an advance of 14. */
	sword_game(&s, 100);
	sim_sword_input(&s, SWM_SLASH);
	x = sw->you.x;
	for (i = 0; i < 6; i++)
		sim_sword_tick(&s);
	CHECK(sw->you.move == SWM_ADVANCE && sw->you.x == x - 14);
	/* The limit is 59: an attack there stays an attack. */
	sword_game(&s, 59);
	sim_sword_input(&s, SWM_SLASH);
	sim_sword_tick(&s);
	CHECK(sw->you.move == SWM_SLASH);
	sword_game(&s, 60);
	sim_sword_input(&s, SWM_SLASH);
	sim_sword_tick(&s);
	CHECK(sw->you.move == SWM_ADVANCE);
	/* A lopsided bar: 31 x 100 / 101 + 8. */
	{
		struct sword_setup in;

		memset(&in, 0, sizeof(in));
		in.you_men = 100;
		in.foe_men = in.you_factor = in.foe_factor = 1;
		sim_sword_start(&s, &in);
		CHECK(sw->you.bar == 38 && sw->foe.bar == 9);
	}
	/* Up close, a cautious opponent stands on guard 1. */
	sword_game(&s, 15);
	for (i = 0; i < 6; i++)
		sim_sword_tick(&s);
	CHECK(sw->foe.move == SWM_GUARD + 1);
	/* Stepping back repeats while the input stays. */
	sword_game(&s, 100);
	sim_sword_input(&s, SWM_BACK);
	x = sw->you.x;
	for (i = 0; i < 12; i++)
		sim_sword_tick(&s);
	CHECK(sw->you.x == x + 28 && sw->input == SWM_BACK);

	/* The hit that empties the opponent's bar fells him; he lies
	 * after frame 8 and the player wins. */
	sword_game(&s, 25);
	sw->foe.bar = 3;
	sim_sword_input(&s, SWM_SLASH + 2);
	for (i = 0; i < 4; i++)
		sim_sword_tick(&s);
	CHECK(sw->beaten == 1 && sw->foe.move == SWM_FALL);
	for (i = 0; i < 20 && sw->result == SWORD_NONE; i++)
		sim_sword_tick(&s);
	CHECK(sw->result == SWORD_WON && sw->foe.frame == 9);

	/* Edges: the opponent driven to x 50 loses; the player at 450 is
	 * driven back. */
	sword_game(&s, 100);
	sw->foe.x = 64;			/* backs off 14 on tick 6: 50 */
	sw->you.x = 164;
	memset(test_world.sword.far_ai, SWM_RETREAT, 8);
	for (i = 0; i < 6; i++)
		sim_sword_tick(&s);
	CHECK(sw->foe.x == 50 && sw->result == SWORD_WON);
	memset(test_world.sword.far_ai, SWM_GUARD, 8);
	sword_game(&s, 100);
	sw->you.x = 436;		/* steps back 14 on tick 1: 450 */
	sw->foe.x = 300;
	sim_sword_input(&s, SWM_BACK);
	sim_sword_tick(&s);
	CHECK(sw->you.x == 450 && sw->result == SWORD_REPELLED);

	/* The crew melee runs on the 15th tick (counter 16): someone loses
	 * ((40 + 80) >> 6) + 1 = 2 men and the bar moves one step. */
	sword_game(&s, 100);
	for (i = 0, ev = 0; i < 14; i++)
		ev |= sim_sword_tick(&s);
	CHECK(!(ev & (SEV_FOE_MEN | SEV_YOU_MEN)));
	ev = sim_sword_tick(&s);
	CHECK((ev & SEV_FOE_MEN) ? sw->foe.men == 78 && sw->you.bar == 23 :
	    (ev & SEV_YOU_MEN) && sw->you.men == 38 && sw->you.bar == 21);
	/* None in duels. */
	sword_game(&s, 100);
	sw->kind = SWORD_DUEL;
	for (i = 0, ev = 0; i < 40; i++)
		ev |= sim_sword_tick(&s);
	CHECK(!(ev & (SEV_FOE_MEN | SEV_YOU_MEN)));

	/* The opponent's script: up close and not cautious, 0x90 parries
	 * the player's line (here low, after a low slash). */
	memset(test_world.sword.caution, 0, sizeof(test_world.sword.caution));
	sword_game(&s, 15);
	sim_sword_input(&s, SWM_SLASH + 1);
	for (i = 0; i < 6; i++)
		sim_sword_tick(&s);
	CHECK(sw->foe.move == SWM_GUARD + 1);
	/* 0x92 slashes in one of the other lines: the player now stands
	 * on guard 0 (middle), so low or high. */
	memset(test_world.sword.scripts, 0x92, sizeof(test_world.sword.scripts));
	for (i = 0; i < 6; i++)
		sim_sword_tick(&s);
	CHECK(sw->you.move == SWM_GUARD);
	CHECK(sw->foe.move == SWM_SLASH + 1 || sw->foe.move == SWM_SLASH + 2);
	{
		int seen[3] = { 0, 0, 0 };

		for (x = 0; x < 40; x++) {
			sword_game(&s, 15);
			s.rng.state += (uint64_t)x * 7919;
			for (i = 0; i < 12; i++)
				sim_sword_tick(&s);
			if (sw->foe.move >= SWM_SLASH &&
			    sw->foe.move < SWM_SLASH + 3)
				seen[sw->foe.move - SWM_SLASH]++;
		}
		CHECK(seen[0] == 0 && seen[1] > 0 && seen[2] > 0);
	}
	memset(test_world.sword.scripts, 0x90, sizeof(test_world.sword.scripts));
	memset(test_world.sword.caution, 15, sizeof(test_world.sword.caution));

	/* Deterministic. */
	sword_game(&s, 40);
	sword_game(&s2, 40);
	for (i = 0; i < 300; i++) {
		if (i % 7 == 0) {
			sim_sword_input(&s, i % 12);
			sim_sword_input(&s2, i % 12);
		}
		sim_sword_tick(&s);
		sim_sword_tick(&s2);
	}
	CHECK(memcmp(&s.sword, &s2.sword, sizeof(s.sword)) == 0);
}

/* Boarding through the protocol, and a snapshot taken mid-fight. */
static void
test_sword_protocol(void)
{
	struct session s1, s2;
	struct jbuf st = { 0 }, cmd = { 0 }, out = { 0 }, r1 = { 0 },
	    r2 = { 0 };

	memset(&s1, 0, sizeof(s1));
	s1.load = loader;
	s1.load_world = world_cb;
	proto_handle(&s1, "{\"cmd\":\"new_game\",\"era\":1660}", &out);
	jb_free(&out);
	proto_handle(&s1, "{\"cmd\":\"board\"}", &out);
	CHECK(out.s != NULL && strstr(out.s, "no boarding") != NULL);
	jb_free(&out);
	s1.sim.encounter.type = 4;
	s1.sim.encounter.guns = 8;
	s1.sim.encounter.crew = 20;
	s1.sim.encounter.role = 4;
	sim_battle_start(&s1.sim);
	s1.sim.battle.outcome = BAT_BOARDING;
	proto_handle(&s1, "{\"cmd\":\"finish\"}", &out);
	CHECK(out.s != NULL && strstr(out.s, "board and fight") != NULL);
	jb_free(&out);
	proto_handle(&s1, "{\"cmd\":\"board\",\"weapon\":3}", &out);
	CHECK(out.s != NULL && strstr(out.s, "\"ok\":false") != NULL);
	jb_free(&out);
	proto_handle(&s1, "{\"cmd\":\"board\",\"weapon\":0}", &out);
	CHECK(out.s != NULL && strstr(out.s, "\"sword\":{\"active\":1") !=
	    NULL);
	jb_free(&out);
	proto_handle(&s1, "{\"cmd\":\"sword\",\"ticks\":30,\"input\":6}",
	    &out);
	CHECK(out.s != NULL && strstr(out.s, "\"events\":") != NULL);
	jb_free(&out);

	sim_to_json(&s1.sim, &st);
	jb_raw(&cmd, "{\"cmd\":\"load\",\"state\":");
	jb_raw(&cmd, st.s);
	jb_raw(&cmd, "}");
	memset(&s2, 0, sizeof(s2));
	s2.load = loader;
	s2.load_world = world_cb;
	proto_handle(&s2, cmd.s, &out);
	CHECK(out.s != NULL && strcmp(out.s, "{\"ok\":true}") == 0);
	jb_free(&out);
	CHECK(memcmp(&s1.sim.sword, &s2.sim.sword,
	    sizeof(s1.sim.sword)) == 0);
	proto_handle(&s1, "{\"cmd\":\"sword\",\"ticks\":3000,\"input\":9}",
	    &r1);
	proto_handle(&s2, "{\"cmd\":\"sword\",\"ticks\":3000,\"input\":9}",
	    &r2);
	CHECK(r1.s != NULL && r2.s != NULL && strcmp(r1.s, r2.s) == 0);
	if (s1.sim.sword.result != SWORD_NONE) {
		proto_handle(&s1, "{\"cmd\":\"finish\",\"keep\":1}", &out);
		CHECK(out.s != NULL && strstr(out.s, "\"outcome\":") != NULL);
		CHECK(!s1.sim.battle.active && !s1.sim.sword.active);
		jb_free(&out);
	}
	session_free(&s1);
	session_free(&s2);
	jb_free(&st);
	jb_free(&cmd);
	jb_free(&r1);
	jb_free(&r2);
}

/* Town visits: the visit flags and what the player knows. */
static void
test_towns(void)
{
	struct sim s;
	struct session ss;
	struct jbuf out = { 0 };

	sea_game(&s, 300, 300, 0);
	s.towns[1].flags = 0x20;		/* obsolete information */
	sim_enter_town(&s, 1);
	CHECK(s.player_town == 1 && s.visit_town == 1);
	CHECK((s.towns[1].flags & 0x80) && !(s.towns[1].flags & 0x20));
	s.visit_flags = VISIT_GOVERNOR | VISIT_KEPT | VISIT_SNEAKED;
	sim_leave_town(&s);
	CHECK(s.player_town == -1);
	sim_enter_town(&s, 1);		/* the same town: bits 0-1 kept */
	CHECK(s.visit_flags == (VISIT_GOVERNOR | VISIT_KEPT));
	sim_leave_town(&s);
	sim_enter_town(&s, 0);		/* another: all cleared */
	CHECK(s.visit_flags == 0 && s.player_town == 0);
	sim_enter_town(&s, 99);		/* no such town: ignored */
	CHECK(s.player_town == 0);

	memset(&ss, 0, sizeof(ss));
	ss.load = loader;
	ss.load_world = world_cb;
	proto_handle(&ss, "{\"cmd\":\"new_game\",\"era\":1660}", &out);
	jb_free(&out);
	proto_handle(&ss, "{\"cmd\":\"town\",\"action\":\"leave\"}", &out);
	CHECK(out.s != NULL && strstr(out.s, "not in a town") != NULL);
	jb_free(&out);
	ss.sim.fleet.leaving = 2;
	proto_handle(&ss, "{\"cmd\":\"town\",\"action\":\"enter\"}", &out);
	CHECK(out.s != NULL && strstr(out.s, "\"player_town\":2") != NULL);
	jb_free(&out);
	proto_handle(&ss, "{\"cmd\":\"town\",\"action\":\"leave\"}", &out);
	CHECK(out.s != NULL && strstr(out.s, "\"player_town\":-1") != NULL);
	jb_free(&out);
	session_free(&ss);
}

/* The shipwright: his gold, the refusal, prices, repairs and sales. */
static void
test_shipwright(void)
{
	struct sim s;
	struct sim_town *t;
	long gold;

	sea_game(&s, 300, 300, 0);
	t = &s.towns[1];
	t->nation = NATION_ENGLAND;
	t->wealth = 120;
	t->citizens = 20;
	s.month = 0;				/* January: |6 - 0| = 6 */
	sim_enter_town(&s, 1);
	CHECK(!sim_shipwright_refuses(&s));
	/* ((6 + 6) x 120 / 6) = 240; (30 x 240 / 150) x 600 = 28800. */
	CHECK(sim_shipwright_gold(&s) == 28800);
	s.month = 6;				/* set once per stay */
	CHECK(sim_shipwright_gold(&s) == 28800);
	sim_leave_town(&s);
	sim_enter_town(&s, 0);
	sim_leave_town(&s);
	sim_enter_town(&s, 1);			/* a new stay: July */
	CHECK(sim_shipwright_gold(&s) == ((30 * (6 * 120 / 6)) / 150) * 600);
	t->wealth = 255;
	t->citizens = 200;
	sim_enter_town(&s, 0);
	sim_enter_town(&s, 1);
	CHECK(sim_shipwright_gold(&s) == 50000);	/* the cap */
	/* Spanish towns refuse while the rank falls short of the wealth. */
	t->nation = NATION_SPAIN;
	CHECK(sim_shipwright_refuses(&s));
	t->wealth = 0;
	CHECK(!sim_shipwright_refuses(&s));
	t->nation = NATION_ENGLAND;

	/* Prices: 50 x (class + 1) x level, and x (10 - level). */
	s.fleet.nships = 3;
	s.fleet.ships[0] = 1;			/* sloop, undamaged */
	s.fleet.damage[0] = 0;
	s.fleet.ships[1] = 4;			/* merchantman, level 2 */
	s.fleet.damage[1] = 40;
	s.fleet.ships[2] = 0;			/* pinnace, level 3 (capped) */
	s.fleet.damage[2] = 63;
	CHECK(sim_repair_cost(&s, 0) == 0 && sim_ship_price(&s, 0) == 1000);
	CHECK(sim_repair_cost(&s, 1) == 500 && sim_ship_price(&s, 1) == 2000);
	CHECK(sim_repair_cost(&s, 2) == 150 && sim_ship_price(&s, 2) == 350);

	/* Repairs: nothing to do, too dear (strictly less than the gold),
	 * then paid to the shipwright. */
	CHECK(sim_repair(&s, 1u) == REPAIR_NOTHING);
	s.fleet.gold = 650;
	CHECK(sim_repair(&s, 6u) == REPAIR_TOO_DEAR && s.fleet.damage[1] == 40);
	s.fleet.gold = 651;
	gold = sim_shipwright_gold(&s);
	CHECK(sim_repair(&s, 6u) == REPAIR_DONE);
	CHECK(s.fleet.gold == 1 && s.fleet.damage[1] == 0 &&
	    s.fleet.damage[2] == 0 && sim_shipwright_gold(&s) == gold + 650);

	/* Sales: in fleet order, one ship stays, his gold may run out. */
	CHECK(sim_sell_ships(&s, 7u) == SELL_DONE);
	CHECK(s.fleet.nships == 1 && s.fleet.ships[0] == 0);	/* kept */
	CHECK(s.fleet.gold == 1 + 1000 + 2500);
	CHECK(sim_sell_ships(&s, 1u) == SELL_ONLY_SHIP);
	s.fleet.nships = 2;
	s.fleet.ships[1] = 8;
	s.fleet.damage[1] = 0;
	s.wright_gold = 4499;			/* a fast galleon: 4500 */
	CHECK(sim_sell_ships(&s, 2u) == SELL_SHORT && s.fleet.nships == 2);
	s.wright_gold = 4500;
	CHECK(sim_sell_ships(&s, 2u) == SELL_DONE && s.wright_gold == 0);
	/* Levels stop at 3, as the game's damage nibble is read. */
	s.fleet.damage[0] = 200;
	CHECK(sim_repair_cost(&s, 0) == 50 * 1 * 3);
}

/* The merchant: prices, stock, trading limits, the town's wealth. */
static void
test_merchant(void)
{
	struct sim s;
	struct sim_town *t;
	struct sim_market *m = &s.market;

	sea_game(&s, 300, 300, 0);
	t = &s.towns[1];
	t->nation = NATION_ENGLAND;
	t->wealth = 120;
	t->citizens = 20;
	t->flags = 0;
	t->family = 0;
	s.month = 0;
	sim_enter_town(&s, 1);
	CHECK(!sim_merchant_refuses(&s));
	sim_merchant_open(&s);
	CHECK(m->open);
	/* W = 12 x 120 / 6 = 240: gold 48 x 600; food 7 -> 70, then each
	 * good starts from the last price: goods 4 -> 40, sugar 2 -> 20;
	 * then x 1.5, and sugar x 2. */
	CHECK(s.merchant_gold == 28800);
	CHECK(m->price[0] == 105 && m->price[1] == 60 && m->price[2] == 60);
	CHECK(m->stock[0] >= 48 && m->stock[0] < 96);
	CHECK(m->stock[1] >= 14 && m->stock[1] < 28);
	CHECK(m->stock[2] >= 8 && m->stock[2] < 16);
	/* A local product is cheap (bit 3: food): 7 / 2 + 1 = 4. */
	t->flags = 0x08;
	sim_merchant_open(&s);
	CHECK(m->price[0] == 60);
	/* Wanted sugar (bit 4) doubles it again. */
	t->flags = 0x10;
	sim_merchant_open(&s);
	CHECK(m->price[2] == 120);
	/* Family pays three quarters. */
	t->flags = 0;
	t->family = 0x40;
	sim_merchant_open(&s);
	CHECK(m->price[0] == 78 && m->price[1] == 45);
	CHECK(sim_merchant_greeting(&s) == 291 + (s.fleet.gold > 2000));
	t->family = 0;
	/* Spanish towns: the family is welcome, others are not. */
	t->nation = NATION_SPAIN;
	CHECK(sim_merchant_refuses(&s));
	t->family = 0x40;
	CHECK(!sim_merchant_refuses(&s));
	t->family = 0;
	t->nation = NATION_ENGLAND;

	/* Trading: one price both ways, limited by stock, gold, hold space
	 * and the merchant's gold. */
	sim_merchant_open(&s);
	s.fleet.gold = 1000;
	s.fleet.capacity = 4;
	s.fleet.guns = 8;
	s.fleet.food = s.fleet.goods = s.fleet.sugar = 0;
	CHECK(sim_hold_space(&s) == 36);
	CHECK(sim_trade(&s, 1, 5) == 5);
	CHECK(s.fleet.goods == 5 && s.fleet.gold == 700 &&
	    s.merchant_gold == 28800 + 300);
	CHECK(sim_trade(&s, 1, 100) == 11);	/* 700 / 60: gold runs out */
	CHECK(s.fleet.gold == 40);
	s.fleet.gold = 100000;
	CHECK(sim_trade(&s, 0, 1000) == 20);	/* the hold is full */
	CHECK(sim_hold_space(&s) == 0);
	CHECK(sim_trade(&s, 1, -3) == 3 && s.fleet.goods == 13);
	s.merchant_gold = 100;
	CHECK(sim_trade(&s, 1, -5) == 1);	/* he runs out of gold */
	m->stock[2] = 0;
	s.fleet.food = 0;
	CHECK(sim_trade(&s, 2, 1) == 0);	/* no sugar to buy */
	CHECK(sim_trade(&s, 0, -1) == 0);	/* no food to sell */
	/* Closing: more than 20 gold moved, wealth x 19 / 20 + 12. */
	sim_merchant_close(&s);
	CHECK(!m->open && t->wealth == 120 * 19 / 20 + 12);
	sim_merchant_open(&s);
	sim_merchant_close(&s);			/* nothing traded */
	CHECK(t->wealth == 126);
	sim_merchant_open(&s);
	s.fleet.gold += 20;			/* 20 is not enough */
	sim_merchant_close(&s);
	CHECK(t->wealth == 126);
	/* Greetings by purse: up to 2000, 5000, 20000, then more. */
	s.fleet.gold = 2000;
	CHECK(sim_merchant_greeting(&s) == 257);
	s.fleet.gold = 2001;
	CHECK(sim_merchant_greeting(&s) == 258);
	s.fleet.gold = 5001;
	CHECK(sim_merchant_greeting(&s) == 259);
	s.fleet.gold = 5000;
	CHECK(sim_merchant_greeting(&s) == 258);
	s.fleet.gold = 20000;
	CHECK(sim_merchant_greeting(&s) == 259);
	s.fleet.gold = 20001;
	CHECK(sim_merchant_greeting(&s) == 260);
	t->wealth = 250;
	sim_merchant_open(&s);
	s.fleet.gold += 21;
	sim_merchant_close(&s);
	CHECK(t->wealth == 249);		/* 237 + 12 */
}

/* The tavern: who is there, the recruits, the reputation. */
static void
test_tavern(void)
{
	struct sim s;
	struct sim_town *t;
	int i, lo = 1000, hi = 0;

	sea_game(&s, 300, 300, 0);
	t = &s.towns[1];
	sim_enter_town(&s, 1);
	CHECK(s.tavern_spots == TAVERN_ALL);
	s.player.difficulty = 1;
	s.fleet.nships = 1;
	s.fleet.capacity = 4;		/* room for (2 + 4) x 16 = 96 men */
	/* No citizens: a small crew still gets (4 - 1) x 6 = 18 men. */
	t->citizens = 0;
	s.fleet.crew = 10;
	sim_tavern_open(&s);
	CHECK(s.recruits == 18);
	s.fleet.crew = 71;		/* under 72: still 18 */
	sim_tavern_open(&s);
	CHECK(s.recruits == 18);
	s.fleet.crew = 80;		/* 18 would pass 96: 16 */
	sim_tavern_open(&s);
	CHECK(s.recruits == 0);		/* not small: 0 */
	s.fleet.crew = 100;		/* over the limit: at least one */
	sim_tavern_open(&s);
	CHECK(s.recruits == 1);
	/* With people: (0 + 7 - 1) x (20 + rand(20)) / 9 = 13..25. */
	t->citizens = 20;
	s.fleet.crew = 72;
	for (i = 0; i < 200; i++) {
		sim_tavern_open(&s);
		lo = s.recruits < lo ? s.recruits : lo;
		hi = s.recruits > hi ? s.recruits : hi;
	}
	CHECK(lo == 13 && hi == 24);	/* 6 x 39 / 9 = 26 caps at 24 */
	s.reputation = 3;		/* a better name, more men */
	s.fleet.crew = 10;
	sim_tavern_open(&s);
	CHECK(s.recruits >= 9 * 20 / 9);
	/* Signing them on; then they are gone until the next arrival. */
	i = s.recruits;
	sim_recruit(&s, 1);
	CHECK(s.fleet.crew == 10 + i && !(s.tavern_spots & TAVERN_CREW));
	sim_tavern_open(&s);
	CHECK(s.recruits == 0);
	sim_leave_town(&s);
	sim_enter_town(&s, 1);
	CHECK(s.tavern_spots == TAVERN_ALL);

	/* Reputation: a capture +1, a failed boarding -2. */
	battle_game(&s);
	s.reputation = 0;
	s.battle.outcome = BAT_SURRENDER;
	sim_battle_finish(&s, 0);
	CHECK(s.reputation == 1);
	battle_game(&s);
	s.battle.outcome = BAT_BOARDING;
	sim_board(&s, 0);
	s.sword.result = SWORD_REPELLED;
	sim_battle_finish(&s, 0);
	CHECK(s.reputation == -2);	/* a fresh game: 0 - 2 */
}

/* The governor: standing, promotions, the Letter of Marque. */
static void
test_governor(void)
{
	struct sim s;
	struct sim_town *t;
	long price;
	int acres;

	sea_game(&s, 300, 300, 0);
	memset(s.standing, 0, 4);
	memset(s.relations, 0, sizeof(s.relations));
	s.relations[NATION_SPAIN][NATION_ENGLAND] = -1;	/* war */
	s.relations[NATION_SPAIN][NATION_FRANCE] = 1;	/* alliance */
	/* Attacking Spain: Spain -1, its ally France -1, its enemy England
	 * +1, but no higher than 1 through others. */
	sim_attack_nation(&s, NATION_SPAIN, -1);
	CHECK(s.standing[NATION_SPAIN] == -1 && s.standing[NATION_FRANCE] == -1);
	CHECK(s.standing[NATION_ENGLAND] == 1 && s.standing[NATION_HOLLAND] == 0);
	sim_attack_nation(&s, NATION_SPAIN, -1);
	CHECK(s.standing[NATION_ENGLAND] == 1);		/* capped */
	s.standing[NATION_ENGLAND] = 2;			/* above 1: free */
	sim_attack_nation(&s, NATION_SPAIN, -1);
	CHECK(s.standing[NATION_ENGLAND] == 3);
	s.standing[NATION_FRANCE] = 3;			/* not below 2 */
	sim_attack_nation(&s, NATION_SPAIN, -2);
	CHECK(s.standing[NATION_FRANCE] == 2);
	sim_attack_nation(&s, NATION_PIRATE, -1);	/* ignored */

	/* Offers: price (4 - s^3) x 200; at 2 and over, the daughter. */
	t = &s.towns[1];
	t->nation = NATION_ENGLAND;
	sim_enter_town(&s, 1);
	s.standing[NATION_ENGLAND] = 0;
	CHECK(sim_governor_offer(&s, &price) == OFFER_MARQUE && price == 800);
	s.standing[NATION_ENGLAND] = 1;
	CHECK(sim_governor_offer(&s, &price) == OFFER_MARQUE && price == 600);
	s.standing[NATION_ENGLAND] = -2;
	CHECK(sim_governor_offer(&s, &price) == OFFER_PARDON && price == 2400);
	s.standing[NATION_ENGLAND] = -4;
	CHECK(sim_governor_offer(&s, &price) == OFFER_NONE);
	s.standing[NATION_ENGLAND] = 2;
	CHECK(sim_governor_offer(&s, &price) == OFFER_DAUGHTER);
	/* Paying: +2, no higher than 2; the town's favour is spent. */
	s.standing[NATION_ENGLAND] = 1;
	t->standing = 5;
	s.fleet.gold = 599;
	CHECK(!sim_governor_pay(&s));
	s.fleet.gold = 600;
	CHECK(sim_governor_pay(&s) && s.fleet.gold == 0);
	CHECK(s.standing[NATION_ENGLAND] == 2 && t->standing == 0);

	/* Promotion: rank r + 1 needs (r + 1)^2 + 2; the surplus and the
	 * town's favour become acres (x 50). */
	s.rank[NATION_ENGLAND] = 0;
	s.standing[NATION_ENGLAND] = 2;
	CHECK(sim_governor_promote(&s, &acres) == 0);	/* 3 needed */
	s.standing[NATION_ENGLAND] = 5;
	t->standing = 1;
	CHECK(sim_governor_promote(&s, &acres) == 1);
	CHECK(s.rank[NATION_ENGLAND] == 1 && s.standing[NATION_ENGLAND] == 3);
	CHECK(acres == (2 + 1) * 50 && s.acres == 3);
	s.standing[NATION_ENGLAND] = 6;			/* rank 2: 6 */
	t->standing = 0;
	CHECK(sim_governor_promote(&s, &acres) == 2 && acres == 0);
	s.rank[NATION_ENGLAND] = 9;			/* the top */
	s.standing[NATION_ENGLAND] = 120;
	CHECK(sim_governor_promote(&s, &acres) == 0 &&
	    s.rank[NATION_ENGLAND] == 9);
	/* Ranks open Spanish doors: rank x 25 against the wealth. */
	t->nation = NATION_SPAIN;
	t->wealth = 50;
	s.rank[NATION_SPAIN] = 1;
	CHECK(sim_shipwright_refuses(&s));
	s.rank[NATION_SPAIN] = 2;
	CHECK(!sim_shipwright_refuses(&s));
}

/* Sea battles at the coast: the shallows scrape, land beaches. */
static void
test_coast(void)
{
	struct sim s;
	int i, hurt = 0;

	/* Below the land block (cells y 40-49), the coast is to the north:
	 * set 0, whose screen has land on rows 0..159, shallows below. */
	sea_game(&s, 120, 50, 0);
	sim_battle_start(&s);
	CHECK(s.battle.set == 0 && s.battle.screen == 1);
	s.battle.foe.x = 420 * 256;		/* in range, not alongside */
	s.battle.foe.y = 300 * 256;
	/* A pinnace (class 0) in the shallows: hurt at once, then 20 ticks
	 * of grace. */
	s.battle.you.type = 0;
	s.battle.you.damage = 0;
	s.battle.you.x = 240 * 256;
	s.battle.you.y = 176 * 256;
	s.battle.you.heading = 0x40;		/* along the shoal */
	CHECK(sim_battle_tick(&s, 0, 0, 0) & BEV_AGROUND);
	CHECK(s.battle.you.damage == 16 && s.battle.you_scrape == 20);
	for (i = 0; i < 5; i++)
		CHECK(!(sim_battle_tick(&s, 0, 0, 0) & BEV_AGROUND));
	CHECK(s.battle.you.damage == 16 && s.battle.you_scrape == 15);
	/* A galleon (class 7) is hurt on rolls of 7..15 only. */
	s.battle.you.type = 7;
	for (i = 0; i < 400 && s.battle.outcome == BAT_NONE; i++) {
		s.battle.you.y = 176 * 256;
		s.battle.you.x = 240 * 256;
		s.battle.you.damage = 0;
		s.battle.you_scrape = 0;
		hurt += (sim_battle_tick(&s, 0, 0, 0) & BEV_AGROUND) != 0;
	}
	CHECK(hurt > 150 && hurt < 300);	/* 9/16 of 400 */
	/* Land: beached; with the enemy alongside, a boarding. */
	s.battle.outcome = BAT_NONE;
	s.battle.you.x = 240 * 256;
	s.battle.you.y = 100 * 256;
	sim_battle_tick(&s, 0, 0, 0);
	CHECK(s.battle.outcome == BAT_BEACHED);
	s.battle.outcome = BAT_NONE;
	s.battle.you.y = 100 * 256;
	s.battle.foe.x = s.battle.you.x;
	s.battle.foe.y = s.battle.you.y;
	sim_battle_tick(&s, 0, 0, 0);
	CHECK(s.battle.outcome == BAT_BOARDING);
	/* Off the field is open sea. */
	s.battle.outcome = BAT_NONE;
	s.battle.you.damage = s.battle.foe.damage = 0;
	s.battle.you.x = -50 * 256;
	s.battle.you.y = 100 * 256;
	s.battle.foe.x = -50 * 256;
	s.battle.foe.y = 300 * 256;
	sim_battle_tick(&s, 0, 0, 0);
	CHECK(s.battle.outcome == BAT_NONE);
}

/* Battle screens: the set by the coast, scrolling along and off it. */
static void
test_screens(void)
{
	struct sim s;
	int i;

	/* Far from land: open sea, set 4. */
	sea_game(&s, 300, 300, 0);
	for (i = 0; i < 5; i++)
		test_world.battle_sets[4][i] = (uint8_t)(100 + i);
	sim_battle_start(&s);
	CHECK(s.battle.set == 4 && s.battle.screen == 100 + s.battle.piece);
	/* Along the north coast: the next piece when scrolling east, open
	 * sea once scrolled south. */
	sea_game(&s, 120, 50, 0);
	for (i = 0; i < 5; i++)
		test_world.battle_sets[0][i] = (uint8_t)(i < 4 ? 10 + i : 104);
	sim_battle_start(&s);
	CHECK(s.battle.set == 0);
	s.battle.you.x = (420 + 40) * 256;	/* corner past 400 */
	s.battle.you.y = (200 + 30) * 256;
	s.battle.foe.x = (300 + 40) * 256;
	s.battle.foe.y = (200 + 30) * 256;
	s.battle.you.heading = 0x40;
	CHECK(sim_battle_tick(&s, 0, 0, 0) & BEV_SCROLLED);
	CHECK(s.battle.scroll_x == 1 && s.battle.scroll_y == 0);
	CHECK(s.battle.screen == 10 + ((1 + s.battle.piece) & 3));
	CHECK(s.battle.you.x / 256 < 420 + 40 - 100);
	s.battle.you.y = (420 + 30) * 256;
	s.battle.foe.y = s.battle.you.y - 60 * 256;
	sim_battle_tick(&s, 0, 0, 0);
	CHECK(s.battle.scroll_y == 1 && s.battle.screen == 104);
	for (i = 0; i < 5; i++)
		test_world.battle_sets[0][i] = test_world.battle_sets[4][i] = 0;
	for (i = 0; i < 4; i++)
		test_world.battle_sets[0][i] = 1;
}

/* The fort battle: the town's screen, a fort that stays and shoots,
 * hits counted apart, the edge and the landing. */
static void
test_fort_battle(void)
{
	struct sim s;
	struct sim_town *t;
	int i, fx, fy, shots = 0;

	sea_game(&s, 300, 300, 0);
	t = &s.towns[1];
	t->id = 1;			/* screen 1: land on rows 0-4 */
	t->fort_x = 7;
	t->fort_y = 3;
	t->ship_x = 6;
	t->ship_y = 10;
	t->ship_heading = 0;
	t->soldiers = 41;
	t->forts = 2;
	sim_fort_battle_start(&s, 1);
	CHECK(s.battle.fort && s.battle.screen == 1 && s.battle.set == 5);
	CHECK(s.battle.foe.guns == 24 && s.battle.foe.crew == 21 * 4);
	CHECK(s.battle.you.x / 256 == 6 * 32 + 16 + 40 &&
	    s.battle.you.y / 256 == 10 * 32 + 16 + 30);
	CHECK(s.battle.foe.x / 256 == 7 * 32 + 16 &&
	    s.battle.foe.y / 256 == 3 * 32 + 16);
	fx = s.battle.foe.x;
	fy = s.battle.foe.y;
	for (i = 0; i < 400 && s.battle.outcome == BAT_NONE; i++) {
		s.battle.you.x = (6 * 32 + 16 + 40) * 256;
		s.battle.you.y = (10 * 32 + 16 + 30) * 256;
		s.battle.you.damage = 0;
		shots += (sim_battle_tick(&s, 0, 0, 0) & BEV_FOE_FIRED) != 0;
	}
	CHECK(s.battle.outcome == BAT_NONE && shots >= 2);
	CHECK(s.battle.foe.x == fx && s.battle.foe.y == fy);
	/* The player's shots count against the fort, which never sinks. */
	s.battle.foe.damage = 0;
	s.battle.you.shot.ticks = 1;
	s.battle.you.shot.x = fx;
	s.battle.you.shot.y = fy;
	sim_battle_tick(&s, 0, 0, 0);
	CHECK(s.battle.fort_damage > 0 && s.battle.foe.damage == 0);
	/* Past 430 on an axis: out of the fort's reach. */
	s.battle.you.x = (440 + 40) * 256;
	sim_battle_tick(&s, 0, 0, 0);
	CHECK(s.battle.outcome == BAT_ESCAPED);
	/* Onto the land by the fort: the landing; elsewhere, beached. */
	s.battle.outcome = BAT_NONE;
	s.battle.you.x = fx;
	s.battle.you.y = fy + 20 * 256;		/* land row, next to it */
	sim_battle_tick(&s, 0, 0, 0);
	CHECK(s.battle.outcome == BAT_LANDED);
	s.battle.outcome = BAT_NONE;
	s.battle.you.x = 13 * 32 * 256;
	s.battle.you.y = 2 * 32 * 256;
	sim_battle_tick(&s, 0, 0, 0);
	CHECK(s.battle.outcome == BAT_BEACHED);
}

/* Choosing the ship to command swaps it into the flagship's place. */
static void
test_command(void)
{
	struct sim s;

	sea_game(&s, 300, 300, 0);
	s.fleet.nships = 2;
	s.fleet.ships[0] = 1;			/* a sloop */
	s.fleet.ships[1] = 5;			/* a frigate */
	s.fleet.damage[0] = 0;
	s.fleet.damage[1] = 20;
	s.fleet.crew = 100;
	s.fleet.guns = 30;
	sim_battle_start(&s);
	CHECK(s.battle.you.type == 1 && s.battle.you.crew == 92);
	CHECK(s.battle.you.guns == 12);		/* (1 + 2) x 4 */
	sim_battle_command(&s, 1);
	CHECK(s.fleet.ships[0] == 5 && s.fleet.ships[1] == 1 &&
	    s.fleet.damage[0] == 20 && s.fleet.damage[1] == 0);
	CHECK(s.battle.you.type == 5 && s.battle.you.damage == 20 &&
	    s.battle.you.crew == 92 && s.battle.you.guns == 23);
	CHECK(s.battle.crew_ashore == 8 && s.battle.guns_ashore == 7);
	/* Not once the battle runs. */
	sim_battle_tick(&s, 0, 0, 0);
	sim_battle_command(&s, 1);
	CHECK(s.fleet.ships[0] == 5);
}

/* The land battle: the forces, moving, the fort reached, the count. */
static void
test_land_battle(void)
{
	struct sim s;
	struct sim_town *t;
	struct sim_land *l = &s.land;
	int i, x, ev = 0;

	memset(test_world.land_ground, GROUND_CLEAR,
	    sizeof(test_world.land_ground));
	sea_game(&s, 300, 300, 0);
	t = &s.towns[1];
	t->id = 0;
	t->forts = 1;
	t->soldiers = 10;
	t->garrison = 0;
	t->land_x = 7;
	t->land_y = 2;
	t->party_x = 7;
	t->party_y = 12;
	s.fleet.crew = 100;
	CHECK(sim_land_battle_needed(&s, 1));
	s.fleet.crew = 151;
	CHECK(!sim_land_battle_needed(&s, 1));
	s.fleet.crew = 100;
	sim_land_start(&s, 1);
	/* 100 men: 50 and 50; 105 defenders: the fort's 26, then 79. */
	CHECK(l->active && l->u[1].men == 50 && l->u[2].men == 50 &&
	    l->u[3].flags == 0);
	CHECK(l->u[1].x == 7 * 32 && l->u[1].y == 12 * 32 &&
	    l->u[2].x == 6 * 32 && l->u[2].y == 11 * 32);
	CHECK(l->u[7].flags == LAND_FORT && l->u[7].men == 26 &&
	    l->u[7].x == 7 * 32 && l->fort == 7);
	CHECK(l->u[6].flags == LAND_SOLDIERS && l->u[6].men == 79 &&
	    l->u[5].flags == 0 && l->u[4].flags == 0);
	CHECK(l->u[1].muskets >= 10 && l->u[1].muskets < 16 &&
	    l->u[1].morale == 79);
	/* Clear ground at full morale: 3 x 2 + 3 = 9, half a step a pass:
	 * 4 pixels. */
	x = l->u[1].x;
	sim_land_tick(&s, 1, 0, 0);
	CHECK(l->selected == 1 && l->u[1].x == x + 4);
	/* The button selects the next unit. */
	CHECK(sim_land_tick(&s, 0, 0, 1) & LEV_SELECTED);
	CHECK(l->selected == 2);
	sim_land_tick(&s, 0, 0, 0);
	/* Fights happen when close; marching on the fort wins. */
	l->u[2].x = l->u[7].x;
	l->u[2].y = l->u[7].y + 40;
	for (i = 0; i < 200 && l->result == 0; i++)
		ev |= sim_land_tick(&s, 0, -1, 0);
	CHECK(l->result == 2);
	CHECK(sim_land_end(&s) == 1 && !l->active);
	CHECK(s.fleet.crew >= 1 && s.fleet.crew <= 100 &&
	    t->soldiers <= 10);
	/* Without men the attack fails. */
	sim_land_start(&s, 1);
	for (i = 1; i < 4; i++)
		l->u[i].men = 0;
	sim_land_tick(&s, 0, 0, 0);
	CHECK(l->result == 3 && sim_land_end(&s) == 0 && s.jail.fresh &&
	    s.jail.cause == JAIL_TOWN && s.jail.town == 1);
	(void)ev;
}

/* A landing party: ashore, walking, stopped by water, back aboard. */
static void
test_landing(void)
{
	struct sim s;
	int town, i;
	enum sail_stop r = SAIL_TICKS;
	int32_t sx, sy;

	sea_game(&s, 120, 52, 0x00);		/* south of the island */
	for (i = 0; i < 400 && r != SAIL_LAND; i++)
		r = sim_sail(&s, 0, 1, &town);
	CHECK(r == SAIL_LAND && s.fleet.land_y / 16 == 49);
	/* No: the ship turns about. */
	sim_turn_back(&s);
	CHECK(s.fleet.heading == 0x80 && !s.fleet.ashore);
	s.fleet.heading = 0;
	for (i = 0, r = SAIL_TICKS; i < 400 && r != SAIL_LAND; i++)
		r = sim_sail(&s, 0, 1, &town);
	CHECK(r == SAIL_LAND);
	sx = s.fleet.x;
	sy = s.fleet.y;
	sim_go_ashore(&s);
	CHECK(s.fleet.ashore && s.fleet.ship_x == sx && s.fleet.ship_y == sy &&
	    s.fleet.x == s.fleet.land_x && s.fleet.y == s.fleet.land_y);
	/* North, half a pixel a tick, into the island. */
	CHECK(sim_march(&s, 0, -1, 40, &town) == SAIL_TICKS &&
	    s.fleet.y == s.fleet.land_y - 20 && s.fleet.ashore);
	/* East along the island, then south: water far from the landing
	 * point stops the party. */
	CHECK(sim_march(&s, 1, 0, 120, &town) == SAIL_TICKS);
	CHECK(sim_march(&s, 0, 1, 200, &town) == SAIL_TICKS &&
	    s.fleet.ashore && s.fleet.y / 16 == 49);
	/* Back west and south to the landing point: aboard, bows south. */
	CHECK(sim_march(&s, -1, 0, 120, &town) == SAIL_TICKS);
	CHECK(sim_march(&s, 0, 1, 200, &town) == SAIL_BOARDED &&
	    !s.fleet.ashore && s.fleet.x == sx && s.fleet.y == sy &&
	    s.fleet.heading == 128);
	/* A reef just south of the ship: bows south would run onto it and
	 * ask to disembark again, so the ship turns to open water. */
	sim_go_ashore(&s);
	{
		int cx = sx / 16, cy = sy / 16 + 1, k;

		test_world.map[cy * WORLD_CELLS_W + cx] = 1;
		CHECK(sim_march(&s, 0, 1, 200, &town) == SAIL_BOARDED &&
		    s.fleet.heading != 128);
		for (k = 4; k <= 16; k += 4)
			CHECK(world_terrain(&test_world, s.fleet.x +
			    sim_sine_mul(&test_world, s.fleet.heading, k),
			    s.fleet.y - sim_sine_mul(&test_world,
			    s.fleet.heading + 0x40, k)) != TERRAIN_LAND);
		test_world.map[cy * WORLD_CELLS_W + cx] = 0;
	}
	/* Time passes on foot too. */
	sim_go_ashore(&s);
	i = s.day;
	sim_march(&s, 0, 0, 64, &town);
	CHECK(s.day == i + 1);
	/* A town the ship just left (a failed attack) can be walked into
	 * by a party landing beside it. */
	{
		struct sim_town keep = s.towns[1];

		s.fleet.ashore = 0;
		s.fleet.x = sx;
		s.fleet.y = sy;
		s.fleet.land_x = sx;
		s.fleet.land_y = sy - 16;
		s.towns[1].x = (uint16_t)(sx / 16);
		s.towns[1].y = (uint16_t)(sy / 16 - 3);	/* 2 from the landing */
		s.fleet.leaving = 1;
		sim_go_ashore(&s);
		CHECK(s.fleet.leaving == -1);
		CHECK(sim_march(&s, 0, -1, 200, &town) == SAIL_TOWN &&
		    town == 1);
		s.towns[1] = keep;
	}
}

/* How many quarters of a treasure map are held. */
static int
quarters(unsigned pieces)
{
	return ((pieces & 1) + (pieces >> 1 & 1) + (pieces >> 2 & 1) +
	    (pieces >> 3 & 1));
}

/* The tavern's people: the traveller, the map seller, the barmaid. */
static void
test_tavern_people(void)
{
	struct sim s;
	int i, seen = 0, town, maps = 0;

	for (i = 0; i < 50; i++) {
		sea_game(&s, 300, 300, 0);
		rng_seed(&s.rng, (uint64_t)i + 1, 1);
		sim_enter_town(&s, 1);
		sim_tavern_open(&s);
		CHECK(!(s.tavern_spots & TAVERN_MYSTERY));
		if (s.tavern_spots & TAVERN_NEWS) {
			CHECK(s.news_price >= 60 && s.news_price <= 140 &&
			    s.news_town != 1 &&
			    !(s.towns[s.news_town].flags & 0x80));
			seen++;
		}
		maps += (s.tavern_spots & TAVERN_MAPS) != 0;
	}
	CHECK(seen > 30 && maps > 2 && maps < 25);
	/* Buying: news of the town, a map piece for 50. */
	sea_game(&s, 300, 300, 0);
	sim_enter_town(&s, 1);
	s.tavern_spots |= TAVERN_NEWS | TAVERN_MAPS;
	s.news_town = 2;
	s.news_price = 80;
	s.fleet.gold = 100;
	s.towns[2].flags &= ~0x80;
	CHECK(sim_buy_news(&s) == 2 && s.fleet.gold == 20 &&
	    (s.towns[2].flags & 0x80) && !(s.tavern_spots & TAVERN_NEWS));
	CHECK(sim_buy_map(&s) == 0 && s.fleet.gold == 20);
	s.fleet.gold = 60;
	s.player.difficulty = 1;
	CHECK(sim_buy_map(&s) == 1 && s.fleet.gold == 10 &&
	    quarters(s.maps[MAP_PIRATE].pieces) == 1 &&
	    (s.maps[MAP_PIRATE].pieces & MAP_SHOWN));
	/* The barmaid: company for 5, her heart at 10, then reports. */
	s.towns[1].barmaid = 9;
	s.fleet.gold = 7;
	CHECK(sim_barmaid(&s, BARMAID_COMPANY) == 1 && s.fleet.gold == 2);
	CHECK(sim_barmaid(&s, BARMAID_COMPANY) == -1);
	CHECK(sim_barmaid(&s, BARMAID_DRINK) == 0 && s.fleet.gold == 1);
	s.towns[1].barmaid = 11;
	CHECK(sim_barmaid_meet(&s) == 2);
	s.tavern_spots |= TAVERN_NEWS | TAVERN_MAID;
	s.news_town = 3;
	s.towns[3].flags &= ~0x80;
	CHECK(sim_barmaid_report(&s, &town) == 1 && town == 3 &&
	    (s.towns[3].flags & 0x80) && !(s.tavern_spots & TAVERN_MAID));
	s.tavern_spots |= TAVERN_MAPS;
	CHECK(sim_barmaid_report(&s, &town) == 2 &&
	    quarters(s.maps[MAP_PIRATE].pieces) == 2);
}

/* The assault on a town, the sack, a new governor. */
static void
test_assault(void)
{
	struct sim s;
	struct sim_town *t;
	struct sack k;
	int n;

	sea_game(&s, 300, 300, 0);
	t = &s.towns[1];
	t->soldiers = 6;
	t->forts = 2;
	t->wealth = 100;
	t->gold = 300;
	t->citizens = 3;
	t->standing = 0;
	n = t->nation;
	s.fleet.crew = 80;
	s.fleet.gold = 0;
	s.heat = 0;
	s.reputation = 0;
	memset(s.standing, 0, sizeof(s.standing));
	sim_assault_start(&s, 1, 10, 2);
	CHECK(s.sword.active && s.sword.kind == SWORD_CREW &&
	    s.sword.you.men == 70 && s.sword.foe.men == 65 &&
	    s.sword.title == 4 && s.sword.scene == 1);
	/* Won with 50 men left against 12: 2 soldiers stay, 20 men lost. */
	s.sword.result = SWORD_WON;
	s.sword.you.men = 50;
	s.sword.foe.men = 12;
	CHECK(sim_assault_end(&s, 1) == 1 && t->soldiers == 2 &&
	    s.fleet.crew == 60);
	/* The sack: 300 x 1500 / 15 = 30000, counted as 300000 gold; the
	 * town keeps 300 - 600 -> 0; wealth 100 - 20 - 20; standing -3;
	 * crew 60 against 30 + rand(30): a conquest. */
	sim_sack(&s, 1, &k);
	CHECK(k.gold == 300000 && s.fleet.gold == 300000 && t->gold == 0);
	CHECK(t->wealth == 60 && (int16_t)t->standing == -3 &&
	    k.cannon == 12 && k.hidden == -1);
	CHECK(k.goods[0] < 80 && k.goods[1] < 80 && k.goods[2] < 80);
	CHECK(s.standing[n] == -3 && s.reputation == 2 && k.conquest);
	/* The loot for nothing: "Take all" starts with the selected good,
	 * then the first left, until the hold is full. */
	{
		const int loot[3] = { 3, 5, 100 };

		s.fleet.capacity = 1;		/* 10 tons */
		s.fleet.guns = s.fleet.food = s.fleet.goods = s.fleet.sugar = 0;
		sim_plunder_open(&s, loot);
		CHECK(sim_take_all(&s, 1) == 10 && s.fleet.goods == 5 &&
		    s.fleet.food == 3 && s.fleet.sugar == 2 &&
		    sim_hold_space(&s) == 0);
		sim_plunder_close(&s);
	}
	sim_install_governor(&s, 1, (n + 1) & 3);
	CHECK(t->nation == ((n + 1) & 3) && t->standing == 2 &&
	    s.standing[(n + 1) & 3] >= 3);
	/* Warned towns hide their gold; a small crew only plunders. */
	sea_game(&s, 300, 300, 0);
	t = &s.towns[1];
	t->citizens = 50;
	t->gold = 10;
	s.heat = 40;
	s.fleet.crew = 20;
	sim_sack(&s, 1, &k);
	CHECK(k.hidden == 1 && !k.conquest && k.gold == 10 * 1500 / 55 * 10);
	CHECK(s.news[(s.nnews - 1) % SIM_NEWS_MAX].code == NEWS_PLUNDERED);
	/* Repelled: the fallen, and a blot on the record. */
	sea_game(&s, 300, 300, 0);
	t = &s.towns[1];
	t->forts = 0;
	s.fleet.crew = 50;
	s.reputation = 4;
	sim_assault_start(&s, 1, 0, 2);
	CHECK(s.sword.scene == 0);
	s.sword.result = SWORD_REPELLED;
	s.sword.you.men = 30;
	CHECK(sim_assault_end(&s, 1) == 0 && s.fleet.crew == 30 &&
	    s.reputation == 3);
}

/* Leaving a town: on its water, bows away from it (field 28). */
static void
test_leave_heading(void)
{
	struct sim s;

	sea_game(&s, 300, 300, 0);
	s.towns[1].sea_x = 120;
	s.towns[1].sea_y = 60;
	s.towns[1].sea_heading = 5;
	s.fleet.heading = 0;
	sim_enter_town(&s, 1);
	sim_leave_town(&s);
	CHECK(s.player_town == -1 && s.fleet.leaving == 1);
	CHECK(s.fleet.x == 120 * 16 + 8 && s.fleet.y == 60 * 16 + 8 &&
	    s.fleet.heading == 160 && s.fleet.speed == 0);
}

/* The arrival: the fort's welcome, a lost ship's cargo, sneaking in. */
static void
test_arrival(void)
{
	struct sim s;
	struct rng r;
	double x;
	int i, n, in = 0, fired = 0, sunk = 0, caught = 0;
	enum arrival a;

	/* A ship lost with its share: rooms 10 and 4 of 14. */
	sea_game(&s, 300, 300, 0);
	s.fleet.nships = 2;
	s.fleet.ships[0] = 4;
	s.fleet.ships[1] = 1;
	s.fleet.capacity = 14;
	s.fleet.gold = 1400;
	s.fleet.guns = 14;
	s.fleet.food = 7;
	s.fleet.goods = 20;
	s.fleet.sugar = 0;
	s.fleet.crew = 50;
	sim_drop_ship(&s, 1, 0);
	CHECK(s.fleet.nships == 1 && s.fleet.capacity == 10);
	CHECK(s.fleet.gold == 1000 && s.fleet.guns == 10 &&
	    s.fleet.food == 5 && s.fleet.goods == 14 && s.fleet.crew == 50);
	/* Kept cargo stays. */
	s.fleet.nships = 2;
	s.fleet.capacity = 14;
	sim_drop_ship(&s, 1, 1);
	CHECK(s.fleet.gold == 1000 && s.fleet.goods == 14);
	/* The last ship stays for the caller to maroon the player. */
	int ship0 = s.fleet.ships[0];
	sim_drop_ship(&s, 0, 0);
	CHECK(s.fleet.nships == 1 && s.fleet.ships[0] == ship0 &&
	    s.fleet.gold == 1000);

	/* Sailing in, replayed against the same random numbers. */
	for (i = 0; i < 200; i++) {
		sea_game(&s, 300, 300, 0);
		s.towns[1].forts = 2;
		s.towns[1].standing = (uint16_t)(int16_t)(i % 5 - 3);
		n = s.towns[1].nation;
		s.standing[n] = (int8_t)(i % 3 - 2);
		s.fleet.crew = 40 + i;
		s.fleet.nships = 2;
		s.fleet.ships[1] = 1;
		s.fleet.capacity = (s.fleet.ships[0] & 0xF) * 2 + 2 + 4;
		rng_seed(&s.rng, (uint64_t)i + 1, 1);
		r = s.rng;
		x = (double)(s.standing[n] + (int16_t)s.towns[1].standing) -
		    s.fleet.crew / 100.0;
		x = x * (rng_below(&r, 0x7FFF) * 2) / 65536.0;
		a = sim_sail_in(&s, 1);
		if (x > -0.5) {
			CHECK(a == ARRIVE_ENTER && s.player_town == 1);
			in++;
		} else if (x > -1.0) {
			CHECK(a == ARRIVE_FIRED && s.player_town == -1 &&
			    s.fleet.nships == 2);
			fired++;
		} else {
			CHECK(a == ARRIVE_SUNK && s.player_town == -1 &&
			    s.fleet.nships == 1);
			sunk++;
		}
	}
	CHECK(in > 0 && fired > 0 && sunk > 0);
	/* No forts: always in. */
	sea_game(&s, 300, 300, 0);
	s.towns[1].forts = 0;
	s.standing[s.towns[1].nation] = -100;
	CHECK(sim_sail_in(&s, 1) == ARRIVE_ENTER);

	/* Sneaking in: standing x reputation against -rand(50). */
	for (i = 0; i < 100; i++) {
		sea_game(&s, 300, 300, 0);
		s.towns[1].standing = (uint16_t)(int16_t)-(i % 7);
		s.reputation = i % 9;
		rng_seed(&s.rng, (uint64_t)i + 1, 1);
		r = s.rng;
		n = -(int16_t)s.towns[1].standing * s.reputation;
		a = sim_sneak_in(&s, 1);
		CHECK(s.visit_flags & VISIT_SNEAKED);
		if (-n > -rng_below(&r, 50)) {
			CHECK(a == ARRIVE_ENTER && s.player_town == 1);
		} else {
			CHECK(a == ARRIVE_CAUGHT && s.player_town == -1);
			caught++;
		}
	}
	CHECK(caught > 0 && caught < 100);
	/* The guard's duel: a loss disgraces. */
	sea_game(&s, 300, 300, 0);
	s.reputation = 6;
	sim_guard_duel(&s, 1, 0);
	CHECK(s.sword.active && s.sword.kind == SWORD_DUEL &&
	    s.sword.you.men == 1 && s.sword.foe.weapon == 0 &&
	    s.sword.title == 0);
	s.sword.result = SWORD_WON;
	CHECK(sim_duel_end(&s) == 0 && s.reputation == 6);
	sim_guard_duel(&s, 1, 0);
	s.sword.result = SWORD_REPELLED;
	CHECK(sim_duel_end(&s) == 1 && s.reputation == -1);
}

/* Cannon at the shipwright: 50 gold each, within both purses. */
static void
test_cannon(void)
{
	struct sim s;

	sea_game(&s, 300, 300, 0);
	sim_enter_town(&s, 1);
	s.fleet.guns = 6;
	s.fleet.gold = 0;
	s.fleet.capacity = 4;
	s.fleet.food = s.fleet.goods = s.fleet.sugar = 0;
	s.wright_gold = 120;
	/* Two sold: the shipwright's 120 pays for no third. */
	CHECK(sim_trade_cannon(&s, -3) == 2 && s.fleet.guns == 4 &&
	    s.fleet.gold == 100 && s.wright_gold == 20 &&
	    s.wright_cannon == 2);
	/* Bought back: his stock of 2 is the limit. */
	CHECK(sim_trade_cannon(&s, 5) == 2 && s.fleet.guns == 6 &&
	    s.fleet.gold == 0 && s.wright_cannon == 0);
	/* A hold exactly full refuses; an overfull one does not. */
	s.wright_cannon = 2;
	s.fleet.gold = 500;
	s.fleet.food = 37;		/* 40 - 37 - 6 / 2 = 0 */
	CHECK(sim_trade_cannon(&s, 1) == 0);
	s.fleet.food = 38;		/* -1 */
	CHECK(sim_trade_cannon(&s, 1) == 1 && s.fleet.guns == 7);
	/* None left to sell. */
	s.fleet.guns = 0;
	CHECK(sim_trade_cannon(&s, -1) == 0);
}

/* The bank: dividing the plunder, then another expedition. */
static void
test_bank(void)
{
	struct sim s;
	struct plunder p;
	int32_t day;

	sea_game(&s, 300, 300, 0);
	sim_enter_town(&s, 1);
	s.player.difficulty = 1;
	s.reputation = 0;
	s.fortune = 0;
	s.fleet.gold = 10000;
	s.fleet.crew = 40;
	/* 10000 / 40 x 0.8 + 0.5 = 200; sqrt 14 / 23: angry; the
	 * reputation (14 + 0 + 1) / 2 = 7; the captain 2 x 10000 / 20. */
	sim_divide_plunder(&s, &p);
	CHECK(p.share == 200 && p.mood == 0 && s.reputation == 7);
	CHECK(p.portions == 4 && p.yours == 1000 && s.fortune == 1000);
	/* Happy from sqrt(share) / 23 >= 3, that is a share of 4761. */
	s.fleet.gold = 4761 * 50 / 4;		/* share 4761.25 + 0.5 */
	s.fleet.crew = 10;
	sim_divide_plunder(&s, &p);
	CHECK(p.share == 4761 && p.mood == 3);
	s.fleet.gold = 6600;			/* a share of 528: angry */
	sim_divide_plunder(&s, &p);
	CHECK(p.share == 528 && p.mood == 0);
	s.fleet.crew = 8;
	s.fleet.gold = 5290;			/* 529: sqrt 23, unhappy */
	sim_divide_plunder(&s, &p);
	CHECK(p.share == 529 && p.mood == 1);

	/* Another expedition: the flagship alone, a tenth of the gold, a
	 * new crew of reputation + 16, 104 days of refit. */
	s.fleet.nships = 3;
	s.fleet.ships[0] = 4;
	s.fleet.damage[0] = 40;
	s.fleet.gold = 10000;
	s.fleet.goods = 30;
	s.reputation = 7;
	day = s.day;
	sim_new_expedition(&s, 0);
	CHECK(s.fleet.nships == 1 && s.fleet.ships[0] == 4 &&
	    s.fleet.damage[0] == 0);
	CHECK(s.fleet.gold == 1000 && s.fleet.capacity == 10 &&
	    s.fleet.guns == 14 && s.fleet.food == 15 && s.fleet.goods == 0);
	CHECK(s.fleet.crew == 23 && s.day == day + 104);
	/* Advancing the difficulty; no gold: a tenth of the fortune, less
	 * a tenth. */
	s.fleet.gold = 0;
	s.fortune = 5000;
	sim_new_expedition(&s, 1);
	CHECK(s.player.difficulty == 2 && s.fortune == 4500 &&
	    s.fleet.gold == 450);
	s.player.difficulty = 3;
	sim_new_expedition(&s, 1);
	CHECK(s.player.difficulty == 3);
}

/* Treasure maps: pieces, the spot, and digging. */
static void
test_treasure(void)
{
	struct sim s;
	struct sim_find found[SIM_MAPS];
	const struct sim_map *m = &s.maps[MAP_PIRATE];
	int k, day;
	long gold;

	sea_game(&s, 300, 300, 0);
	s.player_town = 1;
	s.player.difficulty = 1;
	/* Pieces one at a time; the spot is drawn once, away from the
	 * player's town (Beta) and the empty maps' town (Alpha). */
	sim_map_piece(&s, MAP_PIRATE);
	CHECK(quarters(m->pieces) == 1 && (m->pieces & MAP_SHOWN));
	CHECK(m->town == 2 || m->town == 3);
	CHECK(m->x == s.towns[m->town].x + 3 && m->y == s.towns[m->town].y);
	k = m->x;
	for (day = 0; day < 4; day++)
		sim_map_piece(&s, MAP_PIRATE);
	CHECK(m->pieces == (MAP_WHOLE | MAP_SHOWN) && m->x == k);
	/* The easiest level gets the whole map at once. */
	s.player.difficulty = 0;
	sim_map_piece(&s, MAP_RELATIVE);
	CHECK(s.maps[MAP_RELATIVE].pieces == (MAP_WHOLE | MAP_SHOWN));
	CHECK(s.maps[MAP_RELATIVE].town != m->town);
	/* Digging away from any spot: three days, nothing. */
	s.fleet.ashore = 1;
	day = s.day;
	gold = s.fleet.gold;
	CHECK(sim_search(&s, found) == 0 && s.day == day + 3 &&
	    s.fleet.gold == gold);
	/* At the pirate's spot (a cell off counts): 5000-13000 gold. */
	s.fleet.x = (m->x + 1) * WORLD_TILE + 8;
	s.fleet.y = (m->y - 1) * WORLD_TILE + 8;
	CHECK(sim_search(&s, found) == 1 && found[0].map == MAP_PIRATE &&
	    found[0].gold >= 5000 && found[0].gold <= 13000 &&
	    found[0].gold % 1000 == 0 && s.fleet.gold == gold +
	    found[0].gold && m->pieces == 0);
	CHECK(news_abs(&s, s.nnews - 1)->code == NEWS_TREASURE);
	/* The relative rescued tells of a lost treasure. */
	s.fleet.x = s.maps[MAP_RELATIVE].x * WORLD_TILE + 8;
	s.fleet.y = s.maps[MAP_RELATIVE].y * WORLD_TILE + 8;
	CHECK(sim_search(&s, found) == 1 && found[0].map == MAP_RELATIVE &&
	    found[0].value == 0 && s.relative == 1);
	CHECK(s.maps[MAP_LOST].pieces != 0 && s.maps[MAP_LOST].value == 0);
	CHECK(news_abs(&s, s.nnews - 1)->code == NEWS_RESCUED);
	gold = s.fleet.gold;
	s.fleet.x = s.maps[MAP_LOST].x * WORLD_TILE + 8;
	s.fleet.y = s.maps[MAP_LOST].y * WORLD_TILE + 8;
	CHECK(sim_search(&s, found) == 1 && found[0].map == MAP_LOST &&
	    found[0].gold == 100000 && s.fleet.gold == gold + 100000);
	CHECK(news_abs(&s, s.nnews - 1)->code == NEWS_LOST_TREASURE);
	/* The map seller stays away once the map is whole. */
	s.maps[MAP_PIRATE].pieces = MAP_WHOLE | MAP_SHOWN;
	for (k = 0; k < 20; k++) {
		s.tavern_spots = TAVERN_MAPS;
		sim_tavern_open(&s);
		CHECK(!(s.tavern_spots & TAVERN_MAPS));
	}
}

/* The relatives' villains: named after a promotion, fought in their
 * town, beaten for a piece of the relative's map. */
static void
test_villains(void)
{
	struct sim s;
	int k, v;

	sea_game(&s, 300, 300, 0);
	s.player.difficulty = 1;
	v = sim_villain_appear(&s);
	CHECK(v == 0 && s.villains[0].active &&
	    s.towns[s.villains[0].town].nation == NATION_SPAIN);
	CHECK(s.villains[0].surname < 16 && s.villains[0].rank < 8 &&
	    s.villains[0].nation < 4);
	CHECK(news_abs(&s, s.nnews - 1)->code == NEWS_VILLAIN &&
	    news_abs(&s, s.nnews - 1)->a == 0);
	for (k = 1; k < SIM_VILLAINS; k++)
		CHECK(sim_villain_appear(&s) == k);
	CHECK(sim_villain_appear(&s) == -1);	/* four at most */
	/* Met in his town: the mystery man is in the tavern. */
	s.player_town = s.villains[2].town;
	CHECK(sim_villain_at(&s, s.player_town) >= 0);
	v = sim_villain_at(&s, s.player_town);
	s.tavern_spots = TAVERN_MYSTERY;
	sim_tavern_open(&s);
	CHECK(s.tavern_spots & TAVERN_MYSTERY);
	/* Beaten in the duel: a piece of the relative's map. */
	sim_villain_duel(&s, v, 1);
	CHECK(s.sword.active && s.sword.kind == SWORD_DUEL &&
	    s.sword.nation == -2 && s.villain_duel == v);
	s.sword.result = SWORD_WON;
	CHECK(sim_villain_duel_end(&s) == 1 && !s.villains[v].active &&
	    s.villain_duel == -1 && quarters(s.maps[MAP_RELATIVE].pieces) == 1);
	/* A loss disgraces; the villain lives on elsewhere. */
	v = 0;
	sim_villain_duel(&s, v, 1);
	s.sword.result = SWORD_LOST;
	CHECK(sim_villain_duel_end(&s) == 0 && s.villains[v].active &&
	    s.reputation == -1);
	/* Nowhere to hide: no mystery man. */
	for (k = 0; k < SIM_VILLAINS; k++)
		s.villains[k].active = 0;
	s.tavern_spots = TAVERN_MYSTERY;
	sim_tavern_open(&s);
	CHECK(!(s.tavern_spots & TAVERN_MYSTERY));
	/* No more villains once the relatives are all found. */
	s.relative = 4;
	CHECK(sim_villain_appear(&s) == -1);
}

/* Governors' missions: offered, done, reported. */
static void
test_missions(void)
{
	struct sim s;
	struct sack sack;
	int k, type = 0, dest, st;

	sea_game(&s, 300, 300, 0);
	s.player_town = 0;			/* Alpha, Spanish */
	s.towns[0].standing = 0;
	s.standing[NATION_SPAIN] = 0;
	/* Standing 0 earns no mission. */
	for (k = 0; k < 50; k++)
		CHECK(sim_mission_draw(&s) == 0);
	s.standing[NATION_SPAIN] = 16;
	for (k = 0; k < 200 && type != MISSION_LETTER; k++)
		type = sim_mission_draw(&s);
	CHECK(type == MISSION_LETTER && s.mission_offer.dest != 0 &&
	    s.mission_offer.giver == 0 &&
	    s.relations[NATION_SPAIN][s.towns[s.mission_offer.dest].nation]
	    >= 0);
	dest = s.mission_offer.dest;
	sim_mission_accept(&s);
	CHECK(s.mission.type == MISSION_LETTER && s.mission.dest == dest &&
	    news_abs(&s, s.nnews - 1)->code == NEWS_LETTER);
	/* No second offer while one runs; the contact waits at dest. */
	CHECK(sim_mission_draw(&s) == 0);
	CHECK(sim_mission_contact(&s, dest) && !sim_mission_contact(&s, 0));
	s.player_town = dest;
	s.tavern_spots = TAVERN_MYSTERY;
	sim_tavern_open(&s);
	CHECK(s.tavern_spots & TAVERN_MYSTERY);
	sim_mission_done(&s);
	CHECK(s.mission.type == -MISSION_LETTER &&
	    news_abs(&s, s.nnews - 1)->code == NEWS_DELIVERED_LETTER);
	/* Back at the giver: +1 standing, his town's word 9 less 1. */
	CHECK(sim_mission_report(&s) == 0);	/* not the giver's town */
	s.player_town = 0;
	st = s.standing[NATION_SPAIN];
	CHECK(sim_mission_report(&s) == 1 && s.mission.type == 0 &&
	    s.standing[NATION_SPAIN] == st + 1 &&
	    (int16_t)s.towns[0].standing == -1);
	/* Back without doing it: a failure. */
	s.mission.type = MISSION_PAPERS;
	s.mission.giver = 0;
	s.mission.nation = NATION_SPAIN;
	st = s.standing[NATION_SPAIN];
	CHECK(sim_mission_report(&s) == -1 &&
	    s.standing[NATION_SPAIN] == st - 1 &&
	    (int16_t)s.towns[0].standing == -2 &&
	    news_abs(&s, s.nnews - 1)->code == NEWS_DISPLEASED);
	/* The son is freed in the sack of his prison town. */
	s.mission.type = MISSION_SON;
	s.mission.dest = 2;
	sim_sack(&s, 2, &sack);
	CHECK(sack.son_freed && s.mission.type == -MISSION_SON &&
	    (news_abs(&s, s.nnews - 1)->code == NEWS_SON_FREED ||
	    news_abs(&s, s.nnews - 2)->code == NEWS_SON_FREED));
	/* The pirate hunted, taken at sea. */
	s.mission.type = MISSION_PIRATE;
	s.mission.dest = 3;
	s.mission.pirate = 0x8000 | 5 << 3;
	memset(&s.battle, 0, sizeof(s.battle));
	s.battle.active = 1;
	s.battle.outcome = BAT_SURRENDER;
	s.battle.you.crew = 20;
	s.encounter.nation = NATION_PIRATE;
	s.encounter.named = 1;
	s.encounter.role = 7;
	k = s.fleet.mood;
	CHECK(sim_battle_finish(&s, 0) == BAT_SURRENDER &&
	    s.mission.type == -MISSION_PIRATE);
	CHECK(s.fleet.mood == k + 2);	/* a warship's capture cheers */
	s.player_town = 0;
	s.mission.giver = 0;
	CHECK(sim_mission_report(&s) == 2);
}

/* The crew's mood: its formula, the monthly talk at sea, mutiny and
 * desertion. */
static void
test_crew_mood(void)
{
	struct sim s;
	int town, k, stop = SAIL_TICKS;

	sea_game(&s, 300, 300, 0);
	s.player_town = -1;
	s.player.difficulty = 1;
	/* sqrt(10000 / 10 / 25) x 2 = 12, + 4 - 1. */
	s.fleet.gold = 10000;
	s.fleet.crew = 25;
	s.months = 0;
	CHECK(sim_crew_mood(&s) == 15);
	s.months = 3;
	CHECK(sim_crew_mood(&s) == 12);
	/* Leaving men take their share of the gold. */
	s.fleet.gold = 1000;
	s.fleet.crew = 10;
	sim_crew_leave(&s, 3);
	CHECK(s.fleet.gold == 700 && s.fleet.crew == 7);
	/* Broke for 5 months: restless, and sailing stops to say so. */
	s.fleet.gold = 0;
	s.fleet.crew = 30;
	s.months = 4;
	for (k = 0; k < 40 && stop != SAIL_CREW; k++)
		stop = sim_sail(&s, 0, 64, &town);
	CHECK(stop == SAIL_CREW && s.crew_event == CREW_RESTLESS &&
	    s.months == 5 && s.fleet.mood == -2);
	/* After 12 months the master speaks of dividing the plunder. */
	s.crew_event = CREW_NONE;
	s.months = 11;
	s.player.difficulty = 0;
	s.fleet.gold = 4000;		/* 2 x sqrt(13.3) = 7: mood -1 */
	sim_advance(&s, 30);
	CHECK(s.crew_event == CREW_ANXIOUS && s.fleet.mood == -1);
	/* Below -3: mutiny, a third leave, the leader duels. */
	s.crew_event = CREW_NONE;
	s.player.difficulty = 1;
	s.fleet.gold = 0;
	s.months = 7;
	sim_advance(&s, 30);
	CHECK(s.crew_event == CREW_MUTINY && s.fleet.crew == 20);
	sim_mutiny_duel(&s, 2);
	CHECK(s.sword.active && s.sword.title == 2 && s.sword.nation == 5 &&
	    s.sword.scene == 3);
	s.sword.result = SWORD_LOST;
	s.reputation = 3;
	sim_mutiny_end(&s);
	CHECK(s.reputation == -5 && !s.sword.active);
	/* In town the crew keeps quiet. */
	s.crew_event = CREW_NONE;
	s.player_town = 0;
	sim_advance(&s, 30);
	CHECK(s.crew_event == CREW_NONE);
	/* Leaving town: (3 - mood) x crew / 15 desert, when more than 1. */
	s.fleet.crew = 30;
	s.fleet.gold = 0;
	s.fleet.mood = -2;
	sim_leave_town(&s);
	CHECK(s.deserted == 10 && s.fleet.crew == 20);
	s.player_town = 0;
	s.fleet.crew = 30;
	s.fleet.mood = 2;
	sim_leave_town(&s);
	CHECK(s.deserted == 2 && s.fleet.crew == 28);
	s.player_town = 0;
	s.fleet.mood = 3;
	sim_leave_town(&s);
	CHECK(s.deserted == 0 && s.fleet.crew == 28);
}

/* Prison: the stay, the fresh start, the career's ends, health. */
static void
test_prison(void)
{
	struct sim s;
	int day;

	sea_game(&s, 300, 300, 0);
	s.player.expedition = -1;
	s.player.difficulty = 2;
	s.player.ability = ABILITY_FENCING;
	/* Health: (years + injuries) / 3, / 4 with medicine. */
	s.start_year = s.year - 4;
	s.injuries = 2;
	CHECK(sim_health(&s) == 2);
	s.player.ability = ABILITY_MEDICINE;
	CHECK(sim_health(&s) == 1);
	s.player.ability = ABILITY_FENCING;
	s.start_year = s.year;
	s.injuries = 0;
	/* Beaten in Beta: an injury, rep / 2 + 7 spells of 27 days, a
	 * fresh start from the fortune, the mission lost, one level down. */
	s.reputation = 4;
	s.fortune = 5000;
	s.fleet.gold = 9999;
	s.fleet.nships = 2;
	s.mission.type = MISSION_LETTER;
	s.player_town = 1;
	s.towns[1].sea_x = 125;
	s.towns[1].sea_y = 115;
	day = s.day;
	sim_imprison(&s, JAIL_TOWN, 1);
	CHECK(s.jail.fresh && !s.jail.end && s.jail.spells == 9 &&
	    s.day == day + 9 * 27 && s.injuries == 1);
	CHECK(s.fortune == 4500 && s.fleet.gold == 450 && s.fleet.nships == 1 &&
	    s.fleet.crew == 20 && s.months == 0 && s.mission.type == 0 &&
	    s.player.difficulty == 1 && s.player_town == -1);
	/* Out on the town's water. */
	CHECK(s.fleet.x / WORLD_TILE == s.towns[1].sea_x &&
	    s.fleet.y / WORLD_TILE == s.towns[1].sea_y);
	/* Marooned: by the nearest shore, an injury less. */
	s.fleet.x = 104 * WORLD_TILE + 8;	/* west of the test land */
	s.fleet.y = 45 * WORLD_TILE + 8;
	s.reputation = -20;			/* no time at all */
	day = s.day;
	sim_imprison(&s, JAIL_MAROONED, -1);
	CHECK(s.jail.spells == 0 && s.day == day && s.injuries == 1);
	CHECK(world_terrain(&test_world, s.fleet.x, s.fleet.y) !=
	    TERRAIN_LAND && s.fleet.x / WORLD_TILE == 109 &&
	    s.fleet.heading == 0xC0);
	/* A famous expedition ends in prison; so does health 3. */
	s.player.expedition = 2;
	sim_imprison(&s, JAIL_SHIP, -1);
	CHECK(s.jail.end == 1 && s.injuries == 2);
	s.player.expedition = -1;
	s.injuries = 8;
	sim_imprison(&s, JAIL_SHIP, -1);
	CHECK(s.jail.end == 2);
}

/* Named captains: held after a capture, ransomed, escaping. */
static void
test_prisoners(void)
{
	struct sim s;
	int k, id, tries, held = 0, slot;
	long gold;

	sea_game(&s, 300, 300, 0);
	/* Five surnames before one comes back; nation in bits 6-7. */
	for (k = 0; k < 5; k++) {
		id = sim_name_captain(&s, 2);
		CHECK(id != 0 && (id >> 6) == 2);
	}
	CHECK(s.surnames == 0x1F);
	/* An English captain taken: held at once. */
	memset(&s.battle, 0, sizeof(s.battle));
	s.battle.active = 1;
	s.battle.outcome = BAT_SURRENDER;
	s.battle.you.crew = 20;
	s.encounter.nation = NATION_ENGLAND;
	s.encounter.captain = 1 << 6 | 3 << 3 | 4;
	s.encounter.named = 0;
	CHECK(sim_battle_finish(&s, 0) == BAT_SURRENDER &&
	    s.prisoners[0] == (1 << 6 | 3 << 3 | 4) && s.hostage == 0);
	/* A pirate: a hostage first; held, marked as a pirate. */
	s.battle.active = 1;
	s.battle.outcome = BAT_SURRENDER;
	s.encounter.nation = NATION_PIRATE;
	s.encounter.captain = 2 << 6 | 1 << 3 | 2;
	CHECK(sim_battle_finish(&s, 0) == BAT_SURRENDER &&
	    s.hostage == (2 << 6 | 1 << 3 | 2) && s.prisoners[1] == 0);
	sim_hostage_answer(&s, 2);
	CHECK(s.hostage == 0 && s.prisoners[1] == (0x100 | 2 << 6 | 1 << 3 | 2));
	/* A Spaniard traded for news is let go. */
	s.hostage = 5 << 3;
	sim_hostage_answer(&s, 0);
	CHECK(s.hostage == 0 && s.prisoners[2] == 0);
	/* Ransom: up to 4 tries a visit; (rank + 1) x 500 x 1..3. */
	gold = s.fleet.gold;
	for (k = 0, slot = -1; k < 50 && slot < 0; k++) {
		tries = 0;
		slot = sim_ransom_offer(&s, &tries);
		CHECK(tries <= 4);
	}
	CHECK(slot == 0 || slot == 1);
	id = s.prisoners[slot] & 0xFF;
	CHECK(s.ransom_gold % ((id & 7) + 1) * 500 == 0 &&
	    s.ransom_gold >= ((id & 7) + 1) * 500 &&
	    s.ransom_gold <= ((id & 7) + 1) * 1500);
	sim_ransom_take(&s);
	CHECK(s.prisoners[slot] == 0 && s.fleet.gold == gold + s.ransom_gold);
	/* One jumps overboard at a roll call now and then. */
	s.prisoners[0] = s.prisoners[1] = 0;
	for (k = 0; k < SIM_PRISONERS; k++)
		s.prisoners[k] = 8 + k;
	s.fleet.mood = 20;
	for (k = 0, id = 0; k < 40 && id == 0; k++) {
		s.player_town = 0;
		sim_leave_town(&s);
		id = s.escaped;
	}
	CHECK(id != 0);
	for (k = 0, held = 0; k < SIM_PRISONERS; k++)
		held += s.prisoners[k] == 0;
	CHECK(held == 1);
	/* Prison frees them all. */
	s.player.expedition = -1;
	s.start_year = s.year;
	sim_imprison(&s, JAIL_SHIP, -1);
	for (k = 0; k < SIM_PRISONERS; k++)
		CHECK(s.prisoners[k] == 0);
}

/* The treasure fleet and the silver train. */
static void
test_convoys(void)
{
	struct era_towns era;
	struct scenario scn;
	struct sim s;
	struct sack sack;
	int k, wealth, gold, seen = 0;
	long before;

	make_era(&era, &scn);
	for (k = 0; k < CONVOY_STOPS; k++)
		era.fleet_route[k] = era.train_route[k] = -1;
	/* Alpha and Beta are Spanish, Gamma English: passed by. */
	era.fleet_route[0] = 0;
	era.fleet_route[1] = 2;
	era.fleet_route[2] = 1;
	era.train_route[0] = 1;
	sim_new_game(&s, &era, &scn, 3);
	s.world = &test_world;
	/* The start, by the date: stop doy / 15 - 18 (+24), odd eras +2. */
	k = s.day_of_year / 15 - 18 + ((s.era_year == 1600 ||
	    s.era_year == 1640 || s.era_year == 1680) ? 2 : 0);
	if (k < 0)
		k += 24;
	CHECK(s.fleet_step == k);
	CHECK(s.fleet_town == (k < CONVOY_STOPS ? era.fleet_route[k] : -1));
	/* The fleet's month: back to the start, 1 or 2 stops on, never in
	 * the English town; where it stops wealth +10, gold / 4. */
	s.towns[0].wealth = s.towns[1].wealth = 100;
	s.towns[0].gold = s.towns[1].gold = 400;
	for (k = 0; k < 40; k++) {
		s.fleet_step = -1 + 0;	/* as at the restart */
		s.fleet_town = -1;
		s.day_of_year = 8 * 30 - 1;	/* the next update is month 9 */
		s.month = 8;
		wealth = s.towns[0].wealth + s.towns[1].wealth;
		gold = s.towns[0].gold + s.towns[1].gold;
		sim_advance(&s, 1);
		CHECK(s.fleet_town != 2);
		if (s.fleet_town == 0 || s.fleet_town == 1) {
			seen |= 1 << s.fleet_town;
			CHECK(s.towns[0].wealth + s.towns[1].wealth >= wealth);
			CHECK(s.towns[s.fleet_town].gold <= 100 ||
			    s.towns[0].gold + s.towns[1].gold < gold);
		}
		s.towns[0].wealth = s.towns[1].wealth = 100;
		s.towns[0].gold = s.towns[1].gold = 400;
	}
	CHECK(seen == 3);
	/* An attack on the fleet's town: 10 soldiers more. */
	s.fleet_town = k = 1;
	CHECK(sim_convoys_at(&s, k) == CONVOY_FLEET);
	s.train_town = -1;
	s.towns[k].soldiers = 20;
	sim_attack_begin(&s, k);
	CHECK(s.towns[k].soldiers == 30);
	/* Sacked: the fleet taken, the galleon's cargo x 20 in tens. */
	before = s.fleet.gold;
	s.heat = 0;
	s.towns[k].gold = 0;
	test_world.treasure_ship[5] = 100;
	sim_sack(&s, k, &sack);
	CHECK((sack.convoys & CONVOY_FLEET) && sack.gold == 100 * 20 * 10 &&
	    s.fleet.gold == before + sack.gold && s.fleet_town == -1 &&
	    s.fleet_step == 16);
	CHECK(news_abs(&s, s.nnews - 1)->code == NEWS_FLEET_TAKEN ||
	    news_abs(&s, s.nnews - 2)->code == NEWS_FLEET_TAKEN);
	test_world.treasure_ship[5] = 0;
	/* Past its last stop a convoy is away. */
	s.fleet_step = 13;
	s.day_of_year = 30 - 1;
	s.month = 0;
	for (k = 0; k < 3; k++)
		sim_advance(&s, 30);
	CHECK(s.fleet_town == -1);
}

/* Removes every ',"key":<number>' from a JSON text. */
static void
strip_key(char *text, const char *key)
{
	char pat[32], *p, *e;

	snprintf(pat, sizeof(pat), ",\"%s\":", key);
	while ((p = strstr(text, pat)) != NULL) {
		e = p + strlen(pat);
		while (*e == '-' || (*e >= '0' && *e <= '9'))
			e++;
		memmove(p, e, strlen(e) + 1);
	}
}

/* A version 14 state still loads; the town table fills what it lacks. */
static void
test_old_state(void)
{
	static const char *later[] = { "barmaid", "garrison", "land_x",
	    "land_y", "party_x", "party_y", "date" };
	struct era_towns era;
	struct scenario scn;
	struct sim s, t;
	struct jbuf st = { 0 };
	struct json *j;
	char err[128], *v;
	size_t i;

	make_era(&era, &scn);
	era.towns[0].garrison = 7;
	era.towns[0].party_x = 9;		/* a landing cell */
	sim_new_game(&s, &era, &scn, 3);
	CHECK(s.towns[0].garrison == 7 && s.towns[0].party_x == 9);
	sim_to_json(&s, &st);
	for (i = 0; i < sizeof(later) / sizeof(later[0]); i++)
		strip_key(st.s, later[i]);
	/* The current version has two digits, as 14 and 99. */
	CHECK((v = strstr(st.s, "\"version\":")) != NULL &&
	    v[10] >= '1' && v[10] <= '9' && v[11] >= '0' && v[11] <= '9' &&
	    v[12] == ',');
	if (v != NULL)
		memcpy(v, "\"version\":14", 12);
	j = json_parse(st.s, err, sizeof(err));
	CHECK(j != NULL && sim_from_json(&t, j, err, sizeof(err)) == 0);
	CHECK(t.towns[0].garrison == TOWN_UNSET);
	sim_restore_towns(&t, &era);
	CHECK(t.towns[0].garrison == 7 && t.towns[0].party_x == 9);
	/* Undated news is dated back from today. */
	CHECK(t.nnews == s.nnews && sim_news_at(&t, 0)->date ==
	    sim_news_at(&s, 0)->date);
	/* A newer version than the engine knows is refused. */
	if (v != NULL)
		memcpy(v, "\"version\":99", 12);
	json_free(j);
	j = json_parse(st.s, err, sizeof(err));
	CHECK(j != NULL && sim_from_json(&t, j, err, sizeof(err)) != 0);
	json_free(j);
	jb_free(&st);
}

int
main(void)
{
	test_rng();
	test_json();
	test_rules();
	test_protocol();
	test_snapshot();
	test_hof();
	test_sailing();
	test_sail_snapshot();
	test_encounters();
	test_battles();
	test_battle_protocol();
	test_sword();
	test_sword_protocol();
	test_towns();
	test_shipwright();
	test_merchant();
	test_tavern();
	test_governor();
	test_bank();
	test_cannon();
	test_arrival();
	test_command();
	test_coast();
	test_screens();
	test_fort_battle();
	test_leave_heading();
	test_assault();
	test_tavern_people();
	test_landing();
	test_land_battle();
	test_old_state();
	test_treasure();
	test_villains();
	test_missions();
	test_crew_mood();
	test_prison();
	test_prisoners();
	test_convoys();
	if (failures > 0) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return (1);
	}
	printf("all simulation tests passed\n");
	return (0);
}
