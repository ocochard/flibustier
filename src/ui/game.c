/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/pic.h"
#include "assets/fonts.h"
#include "assets/scenario.h"
#include "assets/sheet.h"
#include "assets/towns.h"
#include "assets/world.h"
#include "game.h"
#include "screen.h"

#define FADE_STEP	32	/* brightness per tick: 8 ticks per fade */

const char *
game_str(const struct game *g, size_t i)
{
	return (i < g->cstr.count ? g->cstr.str[i] : "");
}

const char *
game_word(const struct game *g, size_t group, size_t i)
{
	if (group >= g->cstn.count || i >= g->cstn.groups[group].count)
		return ("");
	return (g->cstn.groups[group].str[i]);
}

static int
load_text(struct game *g)
{
	struct blob b;
	char id[32];
	int rc;

	snprintf(id, sizeof(id), "text/%s/cstr", g->lang);
	if (packs_load(g->packs, id, &b) != 0)
		return (-1);
	rc = cstr_decode(b.data, b.len, &g->cstr);
	blob_free(&b);
	if (rc != 0)
		return (-1);
	snprintf(id, sizeof(id), "text/%s/cstn", g->lang);
	if (packs_load(g->packs, id, &b) != 0)
		return (-1);
	rc = cstn_decode(b.data, b.len, &g->cstn);
	blob_free(&b);
	return (rc);
}

static int
load_fonts(struct game *g)
{
	char id[32];
	int i;

	for (i = 0; i < GAME_FONTS; i++) {
		snprintf(id, sizeof(id), "font/pirate%d", i + 1);
		if (font_load(g->packs, id, g->fb.out_h, &g->font[i]) != 0)
			return (-1);
	}
	return (font_load(g->packs, "font/caps", g->fb.out_h, &g->caps));
}

/*
 * The mouse pointer, a sword, is a raw picture in the executable: u16
 * width and height, then the pixels, colour 0 transparent.  Its palette
 * entries (0x1a-0x1f) are the same greys in the game's pictures.
 */
static void
load_cursor(struct game *g)
{
	const uint8_t *p;
	int w, h, i;

	if ((p = exe_at(g->layout, g->exe, g->exe_len, g->layout->cursor_va,
	    4)) == NULL)
		return;
	w = p[0] | p[1] << 8;
	h = p[2] | p[3] << 8;
	if (w <= 0 || h <= 0 || w > 64 || h > 64 ||
	    (p = exe_at(g->layout, g->exe, g->exe_len,
	    g->layout->cursor_va + 4, (size_t)(w * h))) == NULL)
		return;
	if ((g->cursor.pixels = malloc((size_t)(w * h))) == NULL ||
	    (g->cursor.mask = malloc((size_t)(w * h))) == NULL) {
		free(g->cursor.pixels);
		g->cursor.pixels = NULL;
		return;
	}
	memcpy(g->cursor.pixels, p, (size_t)(w * h));
	for (i = 0; i < w * h; i++)
		g->cursor.mask[i] = p[i] != 0;
	g->cursor.w = w;
	g->cursor.h = h;
}

/* The language's permanent sprites (VA 0x1dd6: misc.crs, miscf.crs,
 * miscg.crs), which hold the "Click to continue" plaque. */
static void
load_misc(struct game *g)
{
	const char *id = strcmp(g->lang, "fre") == 0 ? "ui/miscf" :
	    strcmp(g->lang, "ger") == 0 ? "ui/miscg" : "ui/misc";

	bank_load(g->packs, id, BANK_RUNS, &g->misc);
}

int
game_init(struct game *g, struct packs *packs, const char *lang,
    const char *save_dir)
{
	struct blob b;

	memset(g, 0, sizeof(*g));
	g->packs = packs;
	if (fb_init(&g->fb, packs_art_height(packs)) != 0) {
		snprintf(g->err, sizeof(g->err), "cannot set up a %d pixel "
		    "high screen", packs_art_height(packs));
		return (-1);
	}
	if (save_dir != NULL)
		snprintf(g->save_dir, sizeof(g->save_dir), "%s", save_dir);
	snprintf(g->lang, sizeof(g->lang), "%s", lang);
	if (load_text(g) != 0) {
		snprintf(g->err, sizeof(g->err), "cannot load %s text", lang);
		return (-1);
	}
	if (load_fonts(g) != 0) {
		snprintf(g->err, sizeof(g->err), "cannot load the fonts");
		return (-1);
	}
	if (packs_load(packs, "program/main", &b) != 0 ||
	    (g->layout = exe_identify(b.data, b.len)) == NULL) {
		blob_free(&b);
		snprintf(g->err, sizeof(g->err),
		    "missing or unknown PIRATESG.EXE");
		return (-1);
	}
	g->exe = b.data;	/* kept for the town and scenario tables */
	g->exe_len = b.len;
	load_cursor(g);
	load_misc(g);
	g->career.expedition = -1;
	g->hof_new = -1;
	g->brightness = 0;
	g->screen = &screen_intro;
	g->fading = 1;
	g->screen->enter(g);
	return (0);
}

void
game_free(struct game *g)
{
	int i, j;

	strtab_free(&g->cstr);
	strgroups_free(&g->cstn);
	for (i = 0; i < GAME_FONTS; i++)
		font_free(&g->font[i]);
	font_free(&g->caps);
	image_free(&g->bg);
	bank_free(&g->portrait);
	bank_free(&g->ship);
	bank_free(&g->btl_ship[0][0]);
	bank_free(&g->btl_ship[0][1]);
	bank_free(&g->btl_ship[1][0]);
	bank_free(&g->btl_ship[1][1]);
	bank_free(&g->btl_damage[0]);
	bank_free(&g->btl_damage[1]);
	bank_free(&g->btl_misc);
	tileset_free(&g->btl_tiles);
	tileset_free(&g->map_art);
	free(g->cursor.pixels);
	free(g->cursor.mask);
	bank_free(&g->intro_spr[0]);
	bank_free(&g->intro_spr[1]);
	free(g->intro_title);
	free(g->intro_shown);
	image_free(&g->credits);
	bank_free(&g->town_spr);
	bank_free(&g->shop_spr[0]);
	bank_free(&g->shop_spr[1]);
	bank_free(&g->shop_spr[2]);
	bank_free(&g->tavern_spr);
	bank_free(&g->gov_spr);
	bank_free(&g->misc);
	bank_free(&g->cabin_spr);
	bank_free(&g->cloud_spr);
	bank_free(&g->party_spr);
	for (i = 0; i < 5; i++)
		bank_free(&g->land_spr[i]);
	for (i = 0; i < 8; i++)
		bank_free(&g->panel_spr[i]);
	for (i = 0; i < 8; i++)
		bank_free(&g->cave[i]);
	for (i = 0; i < 5; i++)
		bank_free(&g->id_spr[i]);
	for (i = 0; i < 2; i++)
		for (j = 0; j < 6; j++)
			group_free(&g->sw_parts[i][j]);
	bank_free(&g->sw_misc);
	image_free(&g->sw_weapons);
	free(g->world);
	free(g->exe);
	fb_free(&g->fb);
}

void
game_switch(struct game *g, const struct screen *s)
{
	g->screen = s;
	g->ticks = 0;
	g->selected = 0;
	memset(&g->options, 0, sizeof(g->options));
	s->enter(g);
}

void
game_goto(struct game *g, const struct screen *s)
{
	if (g->fading == -1)
		return;		/* already leaving */
	g->next = s;
	g->fading = -1;
}

int
game_background(struct game *g, const char *pic)
{
	int rc;

	image_free(&g->bg);
	/* Dark ink and a red highlight suit parchment; screens may change
	 * them after this. */
	g->ink[0] = 55;
	g->ink[1] = 30;
	g->ink[2] = 10;
	g->highlight[0] = 170;
	g->highlight[1] = 20;
	g->highlight[2] = 10;
	if ((rc = pic_load(g->packs, pic, &g->bg)) == 0)
		memcpy(g->fb.pal, g->bg.palette, sizeof(g->fb.pal));
	return (rc);
}

int
game_portrait(struct game *g, const char *spr)
{
	bank_free(&g->portrait);
	if (spr == NULL)
		return (0);
	return (bank_load(g->packs, spr, BANK_RUNS, &g->portrait));
}

void
game_event(struct game *g, const struct ui_event *ev)
{
	if (ev->type != EV_MOVE)
		g->idle = 0;
	if (ev->type == EV_MOVE || ev->type == EV_CLICK) {
		g->pointer_x = ev->x;
		g->pointer_y = ev->y;
		g->pointer_seen = 1;
	}
	if (g->fading == -1 || g->quit)
		return;		/* input is ignored while leaving a screen */
	g->screen->event(g, ev);
}

void
game_tick(struct game *g)
{
	if (g->fading < 0) {
		g->brightness -= FADE_STEP;
		if (g->brightness <= 0) {
			g->brightness = 0;
			g->screen = g->next;
			g->next = NULL;
			g->ticks = 0;
			g->selected = 0;
			memset(&g->options, 0, sizeof(g->options));
			g->fading = 1;
			if (g->screen == NULL) {
				g->quit = 1;
				return;
			}
			g->screen->enter(g);
		}
	} else if (g->fading > 0) {
		g->brightness += FADE_STEP;
		if (g->brightness >= 256) {
			g->brightness = 256;
			g->fading = 0;
		}
	}
	if (g->screen != NULL && g->screen->tick != NULL)
		g->screen->tick(g);
	g->ticks++;
	g->idle++;
}

void
game_draw(struct game *g)
{
	if (g->screen == NULL)
		return;
	if (g->bg.pixels != NULL)
		fb_image(&g->fb, &g->bg, 0, 0, 0);
	else
		fb_clear(&g->fb, 0);
	g->screen->draw(g);
	/* The pointer, its tip at the top left, once the mouse is used;
	 * not over the intro pictures. */
	if (g->pointer_seen && g->cursor.pixels != NULL &&
	    g->screen != &screen_intro)
		fb_frame(&g->fb, &g->cursor, g->pointer_x, g->pointer_y);
}

int
game_attach_world(struct game *g)
{
	int era;

	for (era = 0; era < EXE_ERAS; era++)
		if (town_era_years[era] == g->sim.era_year)
			break;
	if (era == EXE_ERAS)
		return (-1);
	if (g->world == NULL && (g->world = calloc(1, sizeof(*g->world))) == NULL)
		return (-1);
	if (g->world->era != era || g->world->sine[64] == 0) {
		if (world_load(g->world, g->packs, g->layout, g->exe,
		    g->exe_len, era, g->err, sizeof(g->err)) != 0) {
			free(g->world);
			g->world = NULL;
			return (-1);
		}
	}
	g->sim.world = g->world;
	/* An older save lacks some town data: the town table has it. */
	{
		struct era_towns towns;

		if (towns_load(g->layout, g->exe, g->exe_len, era,
		    &towns) != 0)
			return (-1);
		sim_restore_towns(&g->sim, &towns);
	}
	return (0);
}

/* Start the simulation from the player's choices. */
int
game_start_career(struct game *g)
{
	struct era_towns era;
	struct scenario scn;
	int index;

	index = g->career.expedition >= 0 ?
	    SCENARIO_CAREERS + g->career.expedition :
	    scenario_career(g->career.era, g->career.nation == NATION_SPAIN ?
	    SCN_SPAIN : (enum scenario_nation)(g->career.nation - 1));
	if (scenario_load(g->layout, g->exe, g->exe_len, index, &scn) != 0 ||
	    towns_load(g->layout, g->exe, g->exe_len, g->career.era,
	    &era) != 0)
		return (-1);
	snprintf(g->career.title, sizeof(g->career.title), "%s", scn.name);
	/* A fixed seed for now; it will come from the clock or a setting. */
	sim_new_game(&g->sim, &era, &scn, 1);
	snprintf(g->sim.player.name, sizeof(g->sim.player.name), "%s",
	    g->career.name);
	g->sim.player.nation = g->career.nation;
	g->sim.player.difficulty = g->career.difficulty;
	g->sim.player.ability = g->career.ability;
	g->sim.player.expedition = g->career.expedition;
	g->started = 1;
	g->news_seen = g->sim.nnews;
	return (game_attach_world(g));
}
