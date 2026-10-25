/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_SCREEN_H
#define STMALO_SCREEN_H

#include "game.h"

/* One screen of the shell; internal to src/ui. */
struct screen {
	const char	*name;
	void		(*enter)(struct game *g);
	void		(*event)(struct game *g, const struct ui_event *ev);
	void		(*tick)(struct game *g);	/* may be NULL */
	void		(*draw)(struct game *g);
};

extern const struct screen screen_intro;
extern const struct screen screen_welcome;
extern const struct screen screen_era;
extern const struct screen screen_expedition;
extern const struct screen screen_nation;
extern const struct screen screen_name;
extern const struct screen screen_difficulty;
extern const struct screen screen_ability;
extern const struct screen screen_summary;
extern const struct screen screen_voyage;
extern const struct screen screen_slots;

/* Fade to another screen; NULL quits. */
void	game_goto(struct game *g, const struct screen *s);
/* Load a pic/ ID as the background and take its palette. */
int	game_background(struct game *g, const char *pic);
/* Load a spr/ ID as the portrait, or clear it with NULL. */
int	game_portrait(struct game *g, const char *spr);
/* Create the simulation from g->career. */
int	game_start_career(struct game *g);

#endif
