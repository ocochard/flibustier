/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_GAME_H
#define STMALO_GAME_H

#include <stddef.h>
#include <stdint.h>

#include "assets/font.h"
#include "assets/text.h"
#include "data/exe.h"
#include "data/pack.h"
#include "assets/image.h"
#include "assets/sprite.h"
#include "assets/world.h"
#include "gfx/sea.h"
#include "gfx/fb.h"
#include "gfx/text.h"
#include "sim/sim.h"

/*
 * The game shell: screens, input and drawing, independent of raylib.
 * A front end feeds events and ticks, and shows game.fb; tests and bots do
 * the same without a window.
 */

enum ui_key {
	UI_KEY_NONE,
	UI_KEY_UP,
	UI_KEY_DOWN,
	UI_KEY_LEFT,
	UI_KEY_RIGHT,
	UI_KEY_ENTER,
	UI_KEY_ESCAPE,
	UI_KEY_BACKSPACE
};

enum ui_event_type {
	EV_KEY,		/* key pressed */
	EV_KEYUP,	/* key released */
	EV_CHAR,	/* ch: a typed character, Unicode */
	EV_MOVE,	/* x, y: pointer position on the 640x480 screen */
	EV_CLICK	/* x, y */
};

struct ui_event {
	enum ui_event_type	type;
	int			key;
	int			ch;
	int			x, y;
};

#define GAME_FONTS	4
#define NAME_MAX_LEN	20

struct screen;

/* The player's choices on the way to a new career. */
struct career {
	int	era;		/* 0..5 */
	int	nation;		/* town nation order: Spain, England... */
	int	difficulty;	/* 0..3 */
	int	ability;	/* 0..4 */
	int	expedition;	/* -1, or 0..5 for a famous expedition */
	char	name[NAME_MAX_LEN * 2 + 1];	/* UTF-8 */
	char	title[24];	/* start scenario: "English Buccaneer" */
};

struct game {
	struct packs		*packs;
	char			 lang[4];	/* eng, fre or ger */
	struct strtab		 cstr;
	struct strgroups	 cstn;
	struct font		 font[GAME_FONTS];	/* PIRATE1..4 */
	struct font		 caps;
	uint8_t			*exe;
	size_t			 exe_len;
	const struct exe_layout	*layout;

	struct fb		 fb;
	int			 brightness;	/* 0..256, for fades */
	const struct screen	*screen;
	const struct screen	*next;		/* during a fade out */
	int			 fading;	/* -1 out, +1 in, 0 none */
	int			 selected;	/* option under the cursor */
	struct text_layout	 options;	/* menu options of the last draw */
	unsigned		 ticks;		/* on the current screen */
	struct image		 bg;		/* current background */
	uint8_t			 ink[3];	/* text colour */
	uint8_t			 highlight[3];	/* selected option colour */
	struct sprite_bank	 portrait;	/* current portrait, if any */
	int			 quit;

	struct career		 career;
	struct sim		 sim;
	int			 started;	/* sim holds a game */
	size_t			 news_seen;	/* news shown so far */
	struct world		*world;		/* the career's era, for sailing */
	struct sprite_bank	 ship;		/* sailing ship, 16 headings */
	struct sea		 sea;
	int			 view_x, view_y;	/* sailing view origin, pixels */
	int			 turn;		/* steering input: -1, 0, +1 */
	char			 save_dir[512];	/* empty: saving disabled */
	int			 saving;	/* slot screen: save, not load */
	char			 slot_desc[10][96];	/* "" when empty */
	char			 msg[128];	/* one-line feedback */
	int			 hof_new;	/* new Hall of Fame row, or -1 */
	char			 err[256];
};

/*
 * Returns 0, or -1 with a message in g->err.  "save_dir" may be NULL or
 * empty to disable saved games.
 */
int	game_init(struct game *g, struct packs *packs, const char *lang,
	    const char *save_dir);
void	game_free(struct game *g);

void	game_event(struct game *g, const struct ui_event *ev);
void	game_tick(struct game *g);	/* 60 per second */
void	game_draw(struct game *g);	/* into g->fb */

/* Strings, "" when missing. */
const char	*game_str(const struct game *g, size_t i);
const char	*game_word(const struct game *g, size_t group, size_t i);

#endif
