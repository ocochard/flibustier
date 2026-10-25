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

int
main(void)
{
	test_rng();
	test_json();
	test_rules();
	test_protocol();
	test_snapshot();
	test_hof();
	if (failures > 0) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return (1);
	}
	printf("all simulation tests passed\n");
	return (0);
}
