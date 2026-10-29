/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_UITABLES_H
#define FLIBUSTIER_UITABLES_H

#include <stddef.h>
#include <stdint.h>

#include "data/exe.h"
#include "data/pack.h"
#include "sim/json.h"

#define UI_CABIN_SPOTS		11
#define UI_TAVERN_SPOTS		10
#define UI_TOWN_SPOTS		8
#define UI_WEAPON_SPOTS		3
#define UI_BOARD_PIECES		29
#define UI_CAVE_PILES		8
#define UI_CREDIT_BLOCKS	6
#define UI_CREDIT_STEPS		20
#define UI_PANEL_SHIPS		8
#define UI_STORIES		30	/* one per start scenario */
#define UI_STORY_STEPS		64
#define UI_CURSOR_MAX		64

/* The fleet panel (VA 0xffae): per ship, its picture's corner, its type
 * and damage labels' centre; the ships' and the button's spots. */
struct ui_fleet_panel {
	int16_t		ship_x[UI_PANEL_SHIPS], ship_y[UI_PANEL_SHIPS];
	int16_t		spot[UI_PANEL_SHIPS + 1][5];
	int16_t		type_x[UI_PANEL_SHIPS], type_y[UI_PANEL_SHIPS];
	int16_t		damage_x[UI_PANEL_SHIPS], damage_y[UI_PANEL_SHIPS];
};

/* A step of a career's story (game-rules.md, "New career: the story"). */
enum ui_story_op {
	UI_STORY_TEXT,		/* show CSTR "text" alone on the page */
	UI_STORY_PAGE,		/* wait for a key, then a new page */
	UI_STORY_WAIT,		/* wait for a key, the text kept */
	UI_STORY_FIGHT		/* the story's sword fight */
};

struct ui_story_step {
	uint8_t		op;		/* enum ui_story_op */
	int16_t		text;		/* UI_STORY_TEXT: the CSTR index */
};

/*
 * The tables the screens read, decoded once at game start, each from a
 * pack's data/layout/<screen> or data/captain-flags when it gives one,
 * otherwise from the executable (asset-packs.md, "Open game data").
 * Hot spots are {x0, y0, x1, y1}, plus a fifth word, "on", where the
 * original keeps one.
 */
struct ui_tables {
	int16_t		cabin[UI_CABIN_SPOTS][5];
	int16_t		tavern[UI_TAVERN_SPOTS][4];
	int16_t		town[UI_TOWN_SPOTS][4];
	int16_t		weapon[UI_WEAPON_SPOTS][4];
	/* The trading board: frame, highlighted frame, x, y. */
	int16_t		board[UI_BOARD_PIECES][4];
	/* The treasure cave's piles: x, y, the fortune that shows it. */
	int32_t		cave[UI_CAVE_PILES][3];
	/* The credits picture's blocks, and its ink steps (RGB). */
	int16_t		credit_rect[UI_CREDIT_BLOCKS][4];
	uint8_t		credit_ink[UI_CREDIT_STEPS][3];
	struct ui_fleet_panel panel;
	/* A named captain's Jolly Roger (1-20), by the captain id's bits
	 * 6-7, then 3-5. */
	uint16_t	jolly[4][8];
	uint8_t		shade[256];	/* a colour's shadow */
	/* The career stories, by scenario (data/stories). */
	struct ui_story_step story[UI_STORIES][UI_STORY_STEPS];
	uint8_t		nstory[UI_STORIES];
	/* The original's mouse pointer; w is 0 when there is none. */
	int		cursor_w, cursor_h;
	uint8_t		cursor[UI_CURSOR_MAX * UI_CURSOR_MAX];
};

int	ui_tables_load(struct ui_tables *t, struct packs *packs,
	    const struct exe_layout *layout, const uint8_t *exe, size_t len,
	    char *err, size_t errlen);

/* Write each table a pack can give, data/stories included: file
 * "name", for ID "id". */
int	ui_tables_export(const struct ui_tables *t,
	    int (*out)(void *arg, const char *id, const char *name,
	    struct jbuf *b), void *arg);

#endif
