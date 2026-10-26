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
#include "ui/screen.h"
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
	static char full[2048];

	/* A leading "enter" means "skip to the menu": one key ends the
	 * intro, one jumps to the last credits block, one ends them. */
	if (strncmp(script, "enter", 5) == 0 && strlen(script) + 16 <
	    sizeof(full)) {
		snprintf(full, sizeof(full), "enter enter %s", script);
		script = full;
	}
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

	/* Intro, welcome, era 1660 (5th), the English flag, ability 3
	 * (gunnery), difficulty 2, name, summary. */
	g = play("eng", "enter enter down down down down enter down enter "
	    "right right right right enter right enter type:Blackbeard enter enter");
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
	g = play("eng", "enter down enter enter wait:40 right enter enter "
	    "type:Hawkins enter enter");
	if (g != NULL) {
		CHECK(g->started && g->career.expedition == 0);
		CHECK(strcmp(g->career.title, "John Hawkins") == 0);
		CHECK(g->sim.era_year == 1560);
		game_free(g);
	}
	/* No Dutch career in 1560: down from the French flag skips past
	 * the Dutch one to the Spanish. */
	g = play("eng", "enter enter enter down down down");
	if (g != NULL) {
		CHECK(g->id_key == 3);
		game_free(g);
	}
	/* With the mouse: a flag, an ability icon, then a level (pointing
	 * at it changes the portrait). */
	g = play("eng", "enter enter down down down down enter wait:40 "
	    "click:60,60");
	if (g != NULL) {
		CHECK(g->screen == &screen_ability &&
		    g->career.nation == NATION_ENGLAND);
		CHECK(game_script(g, "click:310,430") == 0 &&
		    g->screen == &screen_difficulty &&
		    g->career.ability == ABILITY_NAVIGATION);
		CHECK(game_script(g, "move:350,355") == 0 && g->id_key == 2);
		CHECK(game_script(g, "click:350,355") == 0 &&
		    g->screen == &screen_name && g->career.difficulty == 2);
		game_free(g);
	}
	/* "Are ye satisfied?": Nay starts over from the flags. */
	g = play("eng", "enter enter down down down down enter down enter "
	    "right enter enter enter right");
	if (g != NULL) {
		CHECK(g->screen == &screen_confirm && g->id_key == 1);
		CHECK(game_script(g, "enter") == 0 && g->screen == &screen_nation);
		CHECK(game_script(g, "down down enter right enter enter enter "
		    "click:210,380 wait:40") == 0);
		CHECK(g->started && g->career.nation == NATION_FRANCE);
		game_free(g);
	}
	/* Escape goes back, the name needs a letter, Backspace erases. */
	g = play("eng", "enter enter esc");
	if (g != NULL) {
		CHECK(!g->started && g->career.era == 0);
		game_free(g);
	}
	g = play("eng", "enter enter enter left enter right enter enter "
	    "type:Ab bs enter");
	if (g != NULL) {
		CHECK(strcmp(g->career.name, "A") == 0);
		game_free(g);
	}
	/* The name starts as "Incognito": Enter keeps it, Backspace does
	 * nothing until a letter replaces it; 11 letters at most. */
	g = play("eng", "enter enter enter left enter right enter enter bs");
	if (g != NULL) {
		CHECK(strcmp(g->career.name, "Incognito") == 0);
		CHECK(game_script(g, "type:ABCDEFGHIJKLM") == 0 &&
		    strcmp(g->career.name, "ABCDEFGHIJK") == 0);
		game_free(g);
	}
	g = play("eng", "enter enter enter left enter right enter enter enter "
	    "enter");
	if (g != NULL) {
		CHECK(g->started && strcmp(g->sim.player.name, "Incognito") == 0);
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

	g = play("fre", "enter enter down down down down enter left enter "
	    "right enter enter type:Surcouf enter enter");
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
	    "down enter right enter enter type:Kidd enter enter enter enter wait:100 esc "
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
	    "down enter right enter enter type:Anne enter enter enter ";

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
	    "right enter enter type:Low enter enter enter");	/* at Port Royale */
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
	    "right enter enter type:Lo enter enter enter enter hold:right:16");
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
		    g->sim.battle.active && g->screen == &screen_battle);
		game_free(g);
		return;
	}
	CHECK(game_script(g, "enter wait:10") == 0);	/* back to sea */
	game_free(g);
}

/* A sea battle: fight, see the outcome, take the prize, sail on. */
static void
test_battle_screen(void)
{
	struct game *g;
	int i;

	g = play("eng", "enter enter down down down down enter down enter "
	    "right enter enter type:Lo enter enter enter enter hold:right:16");
	if (g == NULL)
		return;
	for (i = 0; i < 200 && g->sim.encounter.stage == ENC_NONE; i++)
		CHECK(game_script(g, "wait:32") == 0);
	CHECK(game_script(g, "enter enter") == 0);	/* investigate twice */
	if (g->sim.encounter.stage == ENC_COLOURS)
		CHECK(game_script(g, "enter") == 0);	/* attack */
	CHECK(g->screen == &screen_battle && g->sim.battle.active);
	CHECK(game_script(g, "up wait:200") == 0);	/* full sails */
	CHECK(g->sim.battle.you.full_sails || g->sim.battle.outcome != 0);
	/* Steer toward her and fire until it is decided. */
	for (i = 0; i < 300 && g->btl_stage == 0; i++)
		CHECK(game_script(g, "enter wait:16") == 0);
	CHECK(g->btl_stage == 1 && g->sim.battle.outcome != BAT_NONE);
	CHECK(game_script(g, "enter") == 0);
	if (g->btl_stage == 2)			/* a prize: keep her */
		CHECK(game_script(g, "enter") == 0);
	CHECK(g->screen == &screen_sail && !g->sim.battle.active);
	CHECK(g->sim.fleet.nships >= 1);
	game_free(g);
}

/* Press up to three keys in order, release them; the pending move. */
static int
sword_keys(struct game *g, int k1, int k2, int k3)
{
	const int keys[3] = { k1, k2, k3 };
	struct ui_event ev;
	int i, got;

	memset(&ev, 0, sizeof(ev));
	g->sim.sword.input = -1;
	for (i = 0; i < 3 && keys[i] != 0; i++) {
		ev.type = EV_KEY;
		ev.key = keys[i];
		game_event(g, &ev);
	}
	got = g->sim.sword.input;
	for (i = 0; i < 3 && keys[i] != 0; i++) {
		ev.type = EV_KEYUP;
		ev.key = keys[i];
		game_event(g, &ev);
	}
	CHECK(g->sw_held == 0);
	return (got);
}

/* Boarding: the picture, the weapon, the keys, the fight to its end. */
static void
test_sword_screen(void)
{
	struct game *g;
	int i;

	g = play("eng", "enter enter down down down down enter down enter "
	    "right enter enter type:Lo enter enter enter enter hold:right:16");
	if (g == NULL)
		return;
	for (i = 0; i < 200 && g->sim.encounter.stage == ENC_NONE; i++)
		CHECK(game_script(g, "wait:32") == 0);
	CHECK(game_script(g, "enter enter") == 0);
	if (g->sim.encounter.stage == ENC_COLOURS)
		CHECK(game_script(g, "enter") == 0);
	CHECK(g->screen == &screen_battle);
	/* Bring the ships together: boarding. */
	g->sim.battle.foe.x = g->sim.battle.you.x;
	g->sim.battle.foe.y = g->sim.battle.you.y;
	CHECK(game_script(g, "wait:30") == 0);
	CHECK(g->screen == &screen_sword && g->sw_stage == 0);
	CHECK(game_script(g, "enter") == 0);
	CHECK(g->sw_stage == 1 && g->selected == 2);	/* cutlass first */
	CHECK(game_script(g, "up up enter") == 0);	/* the rapier */
	CHECK(g->sw_stage == 2 && g->sim.sword.active &&
	    g->sim.sword.you.weapon == 0);
	CHECK(g->sw_parts[0][0].count == 16 && g->sw_parts[1][1].count == 16);
	/* Keys: toward the opponent lunges, with Shift slashes; up and
	 * down pick the line; away steps back. */
	CHECK(sword_keys(g, UI_KEY_LEFT, 0, 0) == SWM_LUNGE);
	CHECK(sword_keys(g, UI_KEY_UP, UI_KEY_LEFT, 0) == SWM_LUNGE + 2);
	CHECK(sword_keys(g, UI_KEY_SHIFT, UI_KEY_LEFT, 0) == SWM_SLASH);
	CHECK(sword_keys(g, UI_KEY_SHIFT, UI_KEY_DOWN, UI_KEY_LEFT) ==
	    SWM_SLASH + 1);
	CHECK(sword_keys(g, UI_KEY_RIGHT, 0, 0) == SWM_BACK);
	CHECK(sword_keys(g, UI_KEY_UP, 0, 0) == SWM_GUARD + 2);
	/* Fight until it is decided. */
	for (i = 0; i < 600 && g->sim.sword.result == SWORD_NONE; i++)
		CHECK(game_script(g, i % 2 ? "up left wait:10" :
		    "down left wait:10") == 0);
	CHECK(g->sim.sword.result != SWORD_NONE);
	CHECK(game_script(g, "wait:30") == 0);
	if (g->sim.sword.result == SWORD_WON) {
		CHECK(g->screen == &screen_battle && g->btl_stage == 2);
		CHECK(game_script(g, "enter wait:30") == 0);	/* keep her */
		CHECK(g->sim.fleet.nships == 2);
	} else {
		CHECK(g->screen == &screen_sword && g->sw_stage == 3);
		CHECK(game_script(g, "enter wait:30") == 0);
	}
	CHECK(g->screen == &screen_sail && !g->sim.battle.active &&
	    !g->sim.sword.active);
	game_free(g);
}

/* The sword pointer: drawn where the mouse is, once it moved. */
static void
test_pointer(void)
{
	struct game *g;
	int x, y, drawn = 0;

	g = play("eng", "enter wait:60");
	if (g == NULL)
		return;
	CHECK(g->cursor.w == 24 && g->cursor.h == 27);
	CHECK(!g->pointer_seen);
	CHECK(game_script(g, "move:300,250 wait:1") == 0);
	CHECK(g->pointer_seen && g->pointer_x == 300 && g->pointer_y == 250);
	game_draw(g);
	for (y = 0; y < g->cursor.h; y++)
		for (x = 0; x < g->cursor.w; x++)
			if (g->cursor.mask[y * g->cursor.w + x])
				drawn += g->fb.px[(250 + y) * FB_W + 300 + x] ==
				    g->cursor.pixels[y * g->cursor.w + x];
	CHECK(drawn > 50);
	/* The menu's rows span the screen, as in the original. */
	CHECK(g->screen == &screen_welcome);
	CHECK(game_script(g, "move:20,365 wait:1") == 0 && g->selected == 2);
	CHECK(game_script(g, "move:620,395 wait:1") == 0 && g->selected == 3);
	game_free(g);
}

/* The intro's five steps, then the menu; a key skips it. */
static void
test_intro(void)
{
	struct game *g;
	int i, seen = 0, last = -1, dissolved = 0;

	g = play("eng", "");
	if (g == NULL)
		return;
	for (i = 0; i < 2000 && g->screen == &screen_intro; i++) {
		if (g->intro_step != last) {
			CHECK(g->intro_step == last + 1);
			last = g->intro_step;
			seen++;
		}
		if (g->intro_shown != NULL && g->intro_left == 0)
			dissolved = 1;
		CHECK(game_script(g, "wait:1") == 0);
	}
	CHECK(seen == 5 && dissolved);
	CHECK(game_script(g, "wait:40") == 0);
	CHECK(g->screen == &screen_credits && g->intro_title == NULL);
	/* The credits: six blocks, each about 3.2 s, then the menu. */
	for (i = 0, last = -1, seen = 0; i < 2000 &&
	    g->screen == &screen_credits; i++) {
		if (g->cred_block != last && g->cred_block < 6) {
			CHECK(g->cred_block == last + 1);
			last = g->cred_block;
			seen++;
		}
		CHECK(game_script(g, "wait:1") == 0);
	}
	CHECK(seen == 6 && i > 6 * 180);
	CHECK(game_script(g, "wait:40") == 0);
	CHECK(g->screen == &screen_welcome && g->credits.pixels == NULL);
	game_free(g);

	/* Keys: the intro ends; the credits jump to the last block, then
	 * end. */
	g = play("eng", "wait:200 enter wait:40");
	if (g == NULL)
		return;
	CHECK(g->screen == &screen_credits && g->cred_block == 0);
	CHECK(game_script(g, "wait:20 enter wait:5") == 0);
	CHECK(g->cred_block == 5);
	CHECK(game_script(g, "enter wait:40") == 0);
	CHECK(g->screen == &screen_welcome);
	game_free(g);
}

/* A town: the buildings, their labels, City Information, leaving. */
static void
test_town(void)
{
	struct game *g;
	const struct sim_town *t;
	int i;

	g = play("eng", "enter enter down down down down enter down enter "
	    "right enter enter type:Lo enter enter enter enter wait:30");
	if (g == NULL)
		return;
	CHECK(g->screen == &screen_sail);
	g->sim.encounters = 0;
	t = &g->sim.towns[g->sim.fleet.leaving];
	g->sim.fleet.x = t->sea_x * 16 + 8;	/* at its harbour mouth */
	g->sim.fleet.y = t->sea_y * 16 + 8;
	g->sim.fleet.leaving = -1;	/* the home port counts again */
	CHECK(game_script(g, "wait:40") == 0);
	CHECK(g->screen == &screen_town && g->sim.player_town >= 0);
	t = &g->sim.towns[g->sim.player_town];
	CHECK((t->flags & 0x80) && !(t->flags & 0x20));
	/* Pointing at the bank labels it; off the buildings, the name. */
	CHECK(game_script(g, "move:540,320 wait:1") == 0 &&
	    g->town_spot == 2);
	CHECK(game_script(g, "move:300,150 wait:1") == 0 &&
	    g->town_spot == -1);
	/* The keyboard skips a closed governor. */
	g->sim.visit_flags |= VISIT_GOVERNOR;
	g->town_key = 6;
	CHECK(game_script(g, "right") == 0 && g->town_key == 1);
	CHECK(game_script(g, "left") == 0 && g->town_key == 6);
	g->sim.visit_flags = 0;
	/* City Information, then back. */
	CHECK(game_script(g, "click:300,40 wait:40") == 0);
	CHECK(g->screen == &screen_cityinfo);
	CHECK(game_script(g, "enter wait:40") == 0 &&
	    g->screen == &screen_town);
	/* A building not done yet says so. */
	CHECK(game_script(g, "click:540,320 wait:2") == 0 &&
	    strstr(g->msg, "Bank") != NULL);
	/* Leave: back at sea, out of the town. */
	CHECK(game_script(g, "click:60,300 wait:40") == 0);
	CHECK(g->screen == &screen_sail && g->sim.player_town == -1);
	for (i = 0; i < 20; i++)
		CHECK(game_script(g, "wait:1") == 0);
	CHECK(g->screen == &screen_sail);	/* not straight back in */
	game_free(g);
}

/* The shipwright: repair one ship, sell another, the refusals. */
static void
test_shipwright(void)
{
	struct game *g;
	const struct sim_town *t;
	long gold;

	g = play("eng", "enter enter down down down down enter down enter "
	    "right enter enter type:Lo enter enter enter enter wait:30");
	if (g == NULL)
		return;
	g->sim.encounters = 0;
	t = &g->sim.towns[g->sim.fleet.leaving];
	CHECK(t->nation != NATION_SPAIN);
	g->sim.fleet.x = t->sea_x * 16 + 8;
	g->sim.fleet.y = t->sea_y * 16 + 8;
	g->sim.fleet.leaving = -1;
	g->sim.fleet.nships = 2;
	g->sim.fleet.ships[1] = 4;
	g->sim.fleet.damage[1] = 0;
	g->sim.fleet.damage[0] = 40;	/* the sloop: level 2 */
	CHECK(game_script(g, "wait:40 click:60,400 wait:40") == 0);
	CHECK(g->screen == &screen_shipwright && g->wright_stage == 0);
	/* Repair: the list, the sloop, paid. */
	gold = g->sim.fleet.gold;
	CHECK(game_script(g, "down enter") == 0 && g->wright_stage == 1);
	CHECK(game_script(g, "enter") == 0);
	CHECK(g->sim.fleet.damage[0] == 0 && g->sim.fleet.gold == gold - 200);
	CHECK(g->wright_stage == 0);	/* nothing left to repair */
	/* Repairing again: "nothing wrong". */
	CHECK(game_script(g, "down enter") == 0 && g->wright_stage == 3);
	CHECK(game_script(g, "enter") == 0 && g->wright_stage == 0);
	/* Sell the merchantman: 50 x 5 x 10 gold. */
	gold = g->sim.fleet.gold;
	CHECK(game_script(g, "down down enter") == 0 && g->wright_stage == 2);
	CHECK(game_script(g, "down enter") == 0);
	CHECK(g->sim.fleet.nships == 1 && g->sim.fleet.gold == gold + 2500);
	/* The only ship is not for sale. */
	CHECK(game_script(g, "down down enter") == 0 && g->wright_stage == 3);
	CHECK(game_script(g, "enter") == 0);
	/* Parting company: back in town. */
	CHECK(game_script(g, "enter wait:40") == 0 &&
	    g->screen == &screen_town);
	game_free(g);
}

/* The merchant: the greeting, the board, buying and selling, leaving. */
static void
test_merchant(void)
{
	struct game *g;
	const struct sim_town *t;
	long gold;
	int price, food;

	g = play("eng", "enter enter down down down down enter down enter "
	    "right enter enter type:Lo enter enter enter enter wait:30");
	if (g == NULL)
		return;
	g->sim.encounters = 0;
	t = &g->sim.towns[g->sim.fleet.leaving];
	g->sim.fleet.x = t->sea_x * 16 + 8;
	g->sim.fleet.y = t->sea_y * 16 + 8;
	g->sim.fleet.leaving = -1;
	CHECK(game_script(g, "wait:40 click:400,320 wait:40") == 0);
	CHECK(g->screen == &screen_merchant && g->shop_stage == 0);
	CHECK(g->sim.market.open && g->shop_spr[0].count == 93);
	CHECK(game_script(g, "enter") == 0 && g->shop_stage == 1);
	/* Right buys a unit of the selected good; Left sells it back. */
	gold = g->sim.fleet.gold;
	food = g->sim.fleet.food;
	price = g->sim.market.price[0];
	CHECK(game_script(g, "right right") == 0);
	CHECK(g->sim.fleet.food == food + 2 &&
	    g->sim.fleet.gold == gold - 2 * price && g->shop_buying);
	CHECK(game_script(g, "left") == 0 && g->sim.fleet.food == food + 1 &&
	    !g->shop_buying);
	/* Down selects goods; a click on the right of its row sells (none
	 * held), on the left buys. */
	CHECK(game_script(g, "down") == 0 && g->selected == 1);
	CHECK(game_script(g, "click:500,175") == 0 && g->sim.fleet.goods == 0);
	CHECK(game_script(g, "click:100,175") == 0 && g->sim.fleet.goods == 1);
	/* O.K.: back in town, the shop closed. */
	CHECK(game_script(g, "click:320,440 wait:40") == 0);
	CHECK(g->screen == &screen_town && !g->sim.market.open);
	game_free(g);
}

/* The tavern: recruiting, the bartender, the door. */
static void
test_tavern(void)
{
	struct game *g;
	const struct sim_town *t;
	int crew, men;

	g = play("eng", "enter enter down down down down enter down enter "
	    "right enter enter type:Lo enter enter enter enter wait:30");
	if (g == NULL)
		return;
	g->sim.encounters = 0;
	t = &g->sim.towns[g->sim.fleet.leaving];
	g->sim.fleet.x = t->sea_x * 16 + 8;
	g->sim.fleet.y = t->sea_y * 16 + 8;
	g->sim.fleet.leaving = -1;
	CHECK(game_script(g, "wait:40 click:250,350 wait:40") == 0);
	CHECK(g->screen == &screen_tavern && g->tavern_spr.count == 3);
	CHECK(g->sim.recruits > 0);
	/* The seamen: sign them up; they are gone afterwards. */
	crew = g->sim.fleet.crew;
	men = g->sim.recruits;
	CHECK(game_script(g, "click:280,400 wait:40") == 0 &&
	    g->screen == &screen_recruit);
	CHECK(game_script(g, "enter wait:40") == 0);
	CHECK(g->screen == &screen_tavern && g->sim.fleet.crew == crew + men);
	CHECK(game_script(g, "click:280,400 wait:5") == 0 &&
	    g->screen == &screen_tavern);	/* nobody there now */
	/* The bartender's rumours, then back. */
	CHECK(game_script(g, "click:110,250 wait:40") == 0 &&
	    g->screen == &screen_rumours);
	CHECK(game_script(g, "enter wait:40") == 0 &&
	    g->screen == &screen_tavern);
	/* The keyboard skips the seamen now; the door leads out. */
	g->tavern_key = 1;
	CHECK(game_script(g, "right") == 0 && g->tavern_key == 3);
	CHECK(game_script(g, "click:610,300 wait:40") == 0 &&
	    g->screen == &screen_town);
	game_free(g);
}

/* The governor: the news, the Letter of Marque, once a stay. */
static void
test_governor(void)
{
	struct game *g;
	const struct sim_town *t;
	long gold;

	g = play("eng", "enter enter down down down down enter down enter "
	    "right enter enter type:Lo enter enter enter enter wait:30");
	if (g == NULL)
		return;
	g->sim.encounters = 0;
	t = &g->sim.towns[g->sim.fleet.leaving];
	g->sim.fleet.x = t->sea_x * 16 + 8;
	g->sim.fleet.y = t->sea_y * 16 + 8;
	g->sim.fleet.leaving = -1;
	CHECK(game_script(g, "wait:40 click:580,220 wait:40") == 0);
	CHECK(g->screen == &screen_governor && g->gov_stage == 0);
	CHECK(g->sim.visit_flags & VISIT_GOVERNOR);
	/* Standing 0 with England: a Letter of Marque for 800. */
	gold = g->sim.fleet.gold;
	CHECK(game_script(g, "enter") == 0 && g->gov_stage == 3);
	CHECK(game_script(g, "enter wait:40") == 0);
	CHECK(g->sim.fleet.gold == gold - 800 &&
	    g->sim.standing[NATION_ENGLAND] == 2);
	CHECK(g->screen == &screen_town);
	/* He will not receive the player again this stay. */
	CHECK(game_script(g, "click:580,220 wait:5") == 0 &&
	    g->screen == &screen_town);
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
	test_battle_screen();
	test_sword_screen();
	test_pointer();
	test_intro();
	test_town();
	test_shipwright();
	test_merchant();
	test_tavern();
	test_governor();
	rmdir(saves_tmp);
	packs_free(packs);
	if (failures > 0) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return (1);
	}
	printf("all shell tests passed\n");
	return (0);
}
