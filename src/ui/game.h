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
#include "assets/tileset.h"
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
	UI_KEY_BACKSPACE,
	UI_KEY_SHIFT,		/* either shift key */
	UI_KEY_UP_LEFT,		/* numeric keypad diagonals: 7, 9, 1, 3 */
	UI_KEY_UP_RIGHT,
	UI_KEY_DOWN_LEFT,
	UI_KEY_DOWN_RIGHT
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
	struct frame		 cursor;	/* the sword pointer */
	int			 pointer_x, pointer_y;
	int			 pointer_seen;	/* a pointer event arrived */
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
	int			 enc_result;	/* -1 while the dialogs go on */
	struct sprite_bank	 btl_ship[2][2];	/* [you, foe][A, B] */
	struct sprite_bank	 btl_damage[2];	/* damage pictures */
	struct tileset		 btl_tiles;	/* 32 x 32 battle tiles */
	int			 btl_fire, btl_sails;	/* pending inputs */
	int			 btl_stage;	/* 0 fight, 1 outcome, 2 prize */
	int			 btl_final;	/* outcome after sim_battle_finish */
	struct sprite_group	 sw_parts[2][6];	/* [you, foe] fighter layers */
	struct sprite_bank	 sw_misc;	/* flags */
	struct image		 sw_weapons;	/* weapon choice picture */
	int			 sw_stage;	/* 0 intro, 1 weapon, 2 fight,
						 * 3 outcome */
	int			 sw_held;	/* arrow and shift keys held */
	int			 sw_flag_y[2];	/* drawn flag heights */
	int			 sw_wait;	/* game ticks to the next fight tick */
	int			 intro_step;	/* 0 logo .. 4 title */
	int			 intro_hold;	/* ticks shown at full brightness */
	int			 intro_next;	/* advance on the next enter */
	struct sprite_bank	 intro_spr[2];
	uint8_t			*intro_title;	/* title layer, w x h, 0 clear */
	uint8_t			*intro_shown;	/* dissolve cells revealed */
	uint32_t		 intro_iter;	/* title screen loop passes */
	uint32_t		 intro_cell;	/* dissolve generator state */
	uint32_t		 intro_left;	/* dissolve cells to visit */
	int			 intro_timer[4];	/* palette cycle counters */
	int			 intro_cycles;	/* steps of the 0xd3 cycle */
	int			 intro_pause;
	struct image		 credits;	/* the credits' text picture */
	int			 cred_block;	/* 0..5 */
	int			 cred_ms;	/* time into the block */
	struct sprite_bank	 town_spr;	/* harbour and closed gate */
	int			 town_spot;	/* building under the cursor, -1 */
	int			 town_key;	/* keyboard selection, -1 */
	int			 wright_stage;	/* 0 menu, 1 repair, 2 sell, 3 note */
	int			 wright_note;	/* CSTR shown at stage 3 */
	int			 shop_stage;	/* 0 greeting, 1 trading, 2 refused */
	struct sprite_bank	 shop_spr[3];	/* board pieces, board, exit */
	int			 shop_buying;	/* the Sell/Buy sign */
	struct sprite_bank	 tavern_spr;	/* the people who may be there */
	int			 tavern_key;	/* keyboard selection */
	struct sprite_bank	 id_spr[5];	/* small flags, small skills,
						 * portrait, big flag, big skill */
	int			 id_key;	/* keyboard selection, or -1 */
	int			 name_default;	/* the name is still the default */
	size_t			 story_pos;	/* next byte of the story script */
	int			 story_text;	/* CSTR on the page, or -1 */
	int			 story_waiting;	/* a key turns the page */
	int			 sw_story;	/* the sword fight is the story's */
	struct sprite_bank	 misc;		/* ui/misc: the "continue" plaque */
	unsigned		 idle;		/* ticks since the last input */
	int			 master_page;	/* the sailing master: 0 or 1 */
	int			 bank_stage;
	int			 arrive_stage;	/* the arrival menu, then its result */
	int			 arrive_result;	/* enum arrival */
	int			 arrive_last;	/* the fort sank the only ship */
	int			 sw_guard;	/* the sword fight is the guard's duel */
	struct plunder		 plunder;
	struct sprite_bank	 cave[8];	/* the treasure piles */
	struct sprite_bank	 gov_spr;	/* the proclamation */
	int			 gov_stage;	/* greeting, promotion, land, offer,
						 * short of gold */
	int			 gov_rank, gov_acres;
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
/* Go to a screen at once, without a fade (steps of one picture). */
void	game_switch(struct game *g, const struct screen *s);

void	game_event(struct game *g, const struct ui_event *ev);
void	game_tick(struct game *g);	/* 60 per second */
void	game_draw(struct game *g);	/* into g->fb */

/* Strings, "" when missing. */
const char	*game_str(const struct game *g, size_t i);
const char	*game_word(const struct game *g, size_t group, size_t i);

#endif
