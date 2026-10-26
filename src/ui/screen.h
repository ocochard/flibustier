/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_SCREEN_H
#define FLIBUSTIER_SCREEN_H

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
extern const struct screen screen_tmap;
extern const struct screen screen_villain;
extern const struct screen screen_crew;
extern const struct screen screen_lastship;
extern const struct screen screen_battle_end;
extern const struct screen screen_volunteers;
extern const struct screen screen_ending;
extern const struct screen screen_vnews;
extern const struct screen screen_merdaugh;
extern const struct screen screen_hostage;
extern const struct screen screen_citymap;
extern const struct screen screen_deserted;
extern const struct screen screen_contact;
extern const struct screen screen_dig;
extern const struct screen screen_credits;
extern const struct screen screen_welcome;
extern const struct screen screen_era;
extern const struct screen screen_expedition;
extern const struct screen screen_nation;
extern const struct screen screen_name;
extern const struct screen screen_difficulty;
extern const struct screen screen_ability;
extern const struct screen screen_summary;
extern const struct screen screen_cabin;
extern const struct screen screen_logbook;
extern const struct screen screen_slots;
extern const struct screen screen_retire;
extern const struct screen screen_hof;
extern const struct screen screen_sail;
extern const struct screen screen_encounter;
extern const struct screen screen_battle;
extern const struct screen screen_sword;
extern const struct screen screen_town;
extern const struct screen screen_cityinfo;
extern const struct screen screen_shipwright;
extern const struct screen screen_merchant;
extern const struct screen screen_tavern;
extern const struct screen screen_recruit;
extern const struct screen screen_rumours;
extern const struct screen screen_governor;
extern const struct screen screen_confirm;
extern const struct screen screen_story;
extern const struct screen screen_master;
extern const struct screen screen_bank;
extern const struct screen screen_cave;
extern const struct screen screen_cannon;
extern const struct screen screen_arrive;
extern const struct screen screen_disgrace;
extern const struct screen screen_assault;
extern const struct screen screen_sack;
extern const struct screen screen_prison;
extern const struct screen screen_mapman;
extern const struct screen screen_traveller;
extern const struct screen screen_barmaid;
extern const struct screen screen_fleet_panel;
extern const struct screen screen_land;

/* Fade to another screen; NULL quits. */
void	game_goto(struct game *g, const struct screen *s);
/* Load a pic/ ID as the background and take its palette. */
int	game_background(struct game *g, const char *pic);
/* Load a spr/ ID as the portrait, or clear it with NULL. */
int	game_portrait(struct game *g, const char *spr);
/* Create the simulation from g->career. */
int	game_start_career(struct game *g);
/* Load the world of the simulation's era and attach it; 0 or -1. */
int	game_attach_world(struct game *g);

#endif
