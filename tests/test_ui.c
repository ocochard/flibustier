/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Game shell tests: drive the start screens with scripted input, without
 * a window.  Needs the game data (STMALO_DATA); skipped otherwise.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include "assets/towns.h"
#include "data/pack.h"
#include "sim/hof.h"
#include "sim/json.h"
#include "sim/proto.h"
#include "sim/save.h"
#include "ui/game.h"
#include "ui/script.h"

#define SKIP	77

static int failures;
static struct packs *packs;
static char saves_tmp[] = "/tmp/stmalo-ui-XXXXXX";

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,	\
		    __LINE__, #cond);					\
		failures++;						\
	}								\
} while (0)

/*
 * Run a script in a fresh game; the caller frees it.  Saved games are
 * disabled unless "saves" is set, which removes "Continue a saved game"
 * from the welcome menu.
 */
static struct game *
play_in(const char *saves, const char *lang, const char *script)
{
	static struct game g;

	memset(&g, 0, sizeof(g));
	if (game_init(&g, packs, lang, saves) != 0) {
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

static struct game *
play(const char *lang, const char *script)
{
	return (play_in(NULL, lang, script));
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

/* Save in slot 3 after two months, then load it in a new game. */
static void
test_saves(void)
{
	struct game *g;
	struct jbuf before = { 0 }, after = { 0 };
	char path[600], desc[128], err[256];

	/* Sail for a while, back to the menu, then save in slot 4. */
	g = play_in(saves_tmp, "eng", "enter enter down down down down enter "
	    "down enter type:Kidd enter enter enter enter enter wait:100 esc "
	    "down enter down down down enter");
	if (g == NULL)
		return;
	CHECK(g->started && g->sim.day >= 1 && g->sim.fleet.ticks >= 100);
	CHECK(strncmp(g->msg, "Kidd, English Buccaneer, ", 25) == 0);
	sim_to_json(&g->sim, &before);
	game_free(g);
	save_slot_path(saves_tmp, 3, path, sizeof(path));
	CHECK(save_read(path, NULL, desc, sizeof(desc), err, sizeof(err)) == 0);
	CHECK(strstr(desc, "Kidd") != NULL);

	/* Welcome: "Continue a saved game", then slot 4 (index 3); empty
	 * slots are skipped, so "down" from slot 1 lands on it. */
	g = play_in(saves_tmp, "eng", "enter down enter down enter");
	if (g == NULL)
		return;
	CHECK(g->started);
	sim_to_json(&g->sim, &after);
	CHECK(before.s != NULL && after.s != NULL &&
	    strcmp(before.s, after.s) == 0);
	CHECK(strcmp(g->sim.player.name, "Kidd") == 0);
	game_free(g);
	jb_free(&before);
	jb_free(&after);

	/* A file that is not a save is refused. */
	save_slot_path(saves_tmp, 5, path, sizeof(path));
	{
		FILE *fp = fopen(path, "w");

		if (fp != NULL) {
			fputs("{\"format\":\"other\"}", fp);
			fclose(fp);
		}
	}
	CHECK(save_read(path, NULL, desc, sizeof(desc), err, sizeof(err)) == -1);
	remove(path);
	save_slot_path(saves_tmp, 3, path, sizeof(path));
	remove(path);
}

/* Exit game: "Yes" records the career in the Hall of Fame, "No" not. */
static void
test_hall_of_fame(void)
{
	struct game *g;
	struct hof h;
	char path[600], err[256];
	static const char *career = "enter enter down down down down enter "
	    "down enter type:Anne enter enter enter enter ";

	hof_path(saves_tmp, path, sizeof(path));
	remove(path);
	g = play_in(saves_tmp, "eng", career);	/* to the voyage screen */
	if (g == NULL)
		return;
	/* Three months at sea, then Exit game, then "No". */
	g->sim.day = 90;
	CHECK(game_script(g, "down down enter down enter") == 0);
	CHECK(!g->started);
	game_free(g);
	CHECK(hof_load(path, &h, err, sizeof(err)) == 0 && h.count == 0);

	g = play_in(saves_tmp, "eng", career);
	if (g == NULL)
		return;
	g->sim.day = 90;
	CHECK(game_script(g, "down down enter enter") == 0);
	CHECK(g->hof_new == 0);
	game_free(g);
	CHECK(hof_load(path, &h, err, sizeof(err)) == 0 && h.count == 1);
	CHECK(strcmp(h.entry[0].name, "Anne") == 0 && h.entry[0].score == 3);
	CHECK(h.entry[0].rating == hof_rating(3));
	remove(path);
}

/* Sailing: the ship leaves port, time passes, steering turns it. */
static void
test_sailing(void)
{
	struct game *g;
	int y0, h0;

	g = play("eng", "enter enter down down down down enter down enter "
	    "type:Low enter enter enter enter");	/* at Port Royale */
	if (g == NULL)
		return;
	CHECK(g->started && g->world != NULL && g->sim.world == g->world);
	/* The fleet starts outside the harbour, bows north toward the
	 * island: half a turn to starboard, then out to sea, southward. */
	CHECK(world_terrain(g->world, g->sim.fleet.x, g->sim.fleet.y) !=
	    TERRAIN_LAND);
	g->sim.encounters = 0;	/* encounters have their own test */
	CHECK(game_script(g, "enter") == 0);	/* set sail */
	h0 = g->sim.fleet.heading;
	CHECK(game_script(g, "hold:right:16") == 0);
	CHECK(g->sim.fleet.heading == ((h0 + 0x80) & 0xFF));
	y0 = g->sim.fleet.y;
	CHECK(game_script(g, "wait:64") == 0);
	CHECK(g->sim.fleet.ticks >= 64 && g->sim.day >= 1);
	CHECK(g->sim.fleet.y > y0);
	CHECK(game_script(g, "esc") == 0);	/* back to the menu */
	game_free(g);
}

/* An encounter on the real map: sighted, identified, hailed. */
static void
test_encounter_screens(void)
{
	struct game *g;
	int i;

	g = play("eng", "enter enter down down down down enter down enter "
	    "type:Lo enter enter enter enter enter hold:right:16");
	if (g == NULL)
		return;
	/* Sail until a sail is sighted. */
	for (i = 0; i < 200 && g->sim.encounter.stage == ENC_NONE; i++)
		CHECK(game_script(g, "wait:32") == 0);
	CHECK(g->sim.encounter.stage == ENC_SIGHTED);
	CHECK(game_script(g, "enter") == 0);	/* investigate */
	CHECK(g->sim.encounter.stage == ENC_IDENTIFIED);
	if (!g->sim.encounter.hostile) {
		/* Peaceful: investigate again, then hail for news. */
		CHECK(game_script(g, "enter down enter") == 0);
		CHECK(g->sim.encounter.stage == ENC_NONE &&
		    g->enc_result == RES_NEWS);
	} else {
		/* A hunter: close for battle. */
		CHECK(game_script(g, "enter enter") == 0);
		CHECK(g->sim.encounter.stage == ENC_NONE &&
		    g->enc_result == RES_BATTLE);
	}
	CHECK(game_script(g, "enter wait:10") == 0);	/* back to sea */
	game_free(g);
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
	if (mkdtemp(saves_tmp) == NULL)
		return (1);
	test_career();
	test_menus();
	test_languages();
	test_saves();
	test_hall_of_fame();
	test_sailing();
	test_encounter_screens();
	rmdir(saves_tmp);
	packs_free(packs);
	if (failures > 0) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return (1);
	}
	printf("all shell tests passed\n");
	return (0);
}
