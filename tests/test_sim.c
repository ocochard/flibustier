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
	memset(w->colour_class, TERRAIN_SEA, sizeof(w->colour_class));
	w->colour_class[99] = TERRAIN_LAND;
	memset(w->tiles + 1 * WORLD_TILE * WORLD_TILE, 99,
	    WORLD_TILE * WORLD_TILE);	/* tile 1: land */
	for (y = 40; y < 50; y++)
		for (x = 110; x < 130; x++)
			w->map[y * WORLD_CELLS_W + x] = 1;
	/* Battle tiles: 1 land as on the map, 2 shallows, on the row under
	 * the land (tile 2 looks like sea when sailing). */
	w->colour_class[98] = TERRAIN_SHALLOWS;
	memset(w->battle_tiles + 1 * BATTLE_TILE * BATTLE_TILE, 99,
	    BATTLE_TILE * BATTLE_TILE);
	memset(w->battle_tiles + 2 * BATTLE_TILE * BATTLE_TILE, 98,
	    BATTLE_TILE * BATTLE_TILE);
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

	/* Firing: one shot, then a reload of |144 - 8 x 4| = 112 ticks. */
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
	/* Lost: the flagship goes, as if sunk; the last one leaves the
	 * player marooned with a pinnace. */
	battle_game(&s);
	bt->outcome = BAT_BOARDING;
	sim_board(&s, 0);
	s.sword.result = SWORD_LOST;
	CHECK(sim_battle_finish(&s, 1) == BAT_DEFEATED);
	CHECK(s.fleet.nships == 1 && s.fleet.ships[0] == 0);

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
	/* The last ship: the castaway's pinnace. */
	sim_drop_ship(&s, 0, 0);
	CHECK(s.fleet.nships == 1 && s.fleet.ships[0] == 0 &&
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
	if (failures > 0) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return (1);
	}
	printf("all simulation tests passed\n");
	return (0);
}
