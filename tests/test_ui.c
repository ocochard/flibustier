/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Game shell tests: drive the start screens with scripted input, without
 * a window.  Needs the game data (STMALO_DATA); skipped otherwise.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/towns.h"
#include "data/pack.h"
#include "ui/game.h"
#include "ui/script.h"

#define SKIP	77

static int failures;
static struct packs *packs;

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,	\
		    __LINE__, #cond);					\
		failures++;						\
	}								\
} while (0)

/* Run a script in a fresh game; the caller frees it. */
static struct game *
play(const char *lang, const char *script)
{
	static struct game g;

	memset(&g, 0, sizeof(g));
	if (game_init(&g, packs, lang) != 0) {
		fprintf(stderr, "game_init: %s\n", g.err);
		failures++;
		return (NULL);
	}
	if (game_script(&g, script) != 0) {
		fprintf(stderr, "script: %s\n", g.err);
		failures++;
	}
	return (&g);
}

static void
test_career(void)
{
	struct game *g;

	/* Intro, welcome, era 1660 (5th), English, name, difficulty 2,
	 * ability 3 (gunnery), summary. */
	g = play("eng", "enter enter down down down down enter down enter "
	    "type:Blackbeard enter down enter down down down enter");
	if (g == NULL)
		return;
	CHECK(g->started);
	CHECK(g->career.era == 4 && g->career.nation == NATION_ENGLAND);
	CHECK(strcmp(g->career.name, "Blackbeard") == 0);
	CHECK(g->career.difficulty == 1 && g->career.ability == 3);
	CHECK(strcmp(g->career.title, "English Buccaneer") == 0);
	CHECK(g->sim.era_year == 1660 && g->sim.ntowns == 43);
	/* England and Spain start at war in this scenario. */
	CHECK(g->sim.relations[NATION_SPAIN][NATION_ENGLAND] == -1);
	game_free(g);
}

static void
test_menus(void)
{
	struct game *g;

	/* "Continue a saved game" is disabled: one down skips to the famous
	 * expeditions; the first expedition is in 1560. */
	g = play("eng", "enter down enter enter type:Hawkins enter enter "
	    "enter");
	if (g != NULL) {
		CHECK(g->started && g->career.expedition == 0);
		CHECK(strcmp(g->career.title, "John Hawkins") == 0);
		CHECK(g->sim.era_year == 1560);
		game_free(g);
	}
	/* No Dutch career in 1560: down from French skips past Dutch. */
	g = play("eng", "enter enter enter down down down");
	if (g != NULL) {
		CHECK(g->selected == NATION_SPAIN);	/* wrapped around */
		game_free(g);
	}
	/* Escape goes back, the name needs a letter, Backspace erases. */
	g = play("eng", "enter enter esc");
	if (g != NULL) {
		CHECK(!g->started && g->career.era == 0);
		game_free(g);
	}
	g = play("eng", "enter enter enter enter enter type:Ab bs enter");
	if (g != NULL) {
		CHECK(strcmp(g->career.name, "A") == 0);
		game_free(g);
	}
	/* Return to DOS. */
	g = play("eng", "enter down down enter");
	if (g != NULL) {
		CHECK(g->quit);
		game_free(g);
	}
	/* Pointer: clicking the second era picks 1600. */
	g = play("eng", "enter enter");
	if (g != NULL && g->options.noptions == 6) {
		const struct text_option *o = &g->options.option[1];
		char step[64];

		snprintf(step, sizeof(step), "click:%d,%d", o->x + 2,
		    o->y + o->h / 2);
		CHECK(game_script(g, step) == 0);
		CHECK(g->career.era == 1);
		game_free(g);
	} else if (g != NULL) {
		CHECK(!"era screen shows six options");
		game_free(g);
	}
}

static void
test_languages(void)
{
	struct game *g;

	g = play("fre", "enter enter down down down down enter enter "
	    "type:Surcouf enter enter enter");
	if (g != NULL) {
		CHECK(g->started && g->sim.era_year == 1660);
		game_free(g);
	}
	g = play("ger", "enter");
	if (g != NULL) {
		/* A bad script step is reported, not ignored. */
		CHECK(game_script(g, "enter dance") == -1);
		CHECK(strstr(g->err, "dance") != NULL);
		game_free(g);
	}
}

int
main(void)
{
	const char *path;

	if ((path = getenv("STMALO_DATA")) == NULL || *path == '\0') {
		printf("STMALO_DATA not set, skipping\n");
		return (SKIP);
	}
	if ((packs = packs_new()) == NULL ||
	    packs_add_original(packs, path) != 0)
		return (1);
	test_career();
	test_menus();
	test_languages();
	packs_free(packs);
	if (failures > 0) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return (1);
	}
	printf("all shell tests passed\n");
	return (0);
}
