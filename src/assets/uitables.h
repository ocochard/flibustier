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

#define UI_CABIN_SPOTS		11
#define UI_TAVERN_SPOTS		10
#define UI_TOWN_SPOTS		8
#define UI_WEAPON_SPOTS		3
#define UI_BOARD_PIECES		29
#define UI_CAVE_PILES		8
#define UI_CREDIT_BLOCKS	6
#define UI_CREDIT_STEPS		20
#define UI_PANEL_WORDS		94	/* the fleet panel's layout */
#define UI_STORY_MAX		4096
#define UI_CURSOR_MAX		64

/*
 * The tables the screens read, decoded once at game start (see
 * aidd_docs/plans/open-game-data.md).  Hot spots are {x0, y0, x1, y1},
 * plus a fifth word, "on", where the original keeps one.
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
	/* Little-endian words, addressed by byte offset as in screens.c. */
	int16_t		panel[UI_PANEL_WORDS];
	/* A named captain's Jolly Roger (1-20), by captain bits. */
	uint16_t	jolly[4][16];
	uint8_t		shade[256];	/* a colour's shadow */
	/* The career stories: byte scripts separated by 0xFF. */
	uint8_t		story[UI_STORY_MAX];
	/* The original's mouse pointer; w is 0 when there is none. */
	int		cursor_w, cursor_h;
	uint8_t		cursor[UI_CURSOR_MAX * UI_CURSOR_MAX];
};

int	ui_tables_load(struct ui_tables *t, const struct exe_layout *layout,
	    const uint8_t *exe, size_t len, char *err, size_t errlen);

#endif
