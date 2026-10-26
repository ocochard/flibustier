/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * The start of the game: intro, welcome menu, and the choices that make a
 * new career (era, nation, name, difficulty, special ability), or a
 * famous expedition.  The texts are the game's own (CSTR and CSTN).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/img.h"
#include "assets/scenario.h"
#include "assets/pcx.h"
#include "sim/hof.h"
#include "sim/save.h"
#include "screen.h"

/* String table entries used here (CSTR) and word lists (CSTN groups). */
#define S_SELECT_EXPEDITION	62
#define S_SELECT_ERA		64
#define S_NAME_PROMPT		65
#define S_DIFFICULTY_PROMPT	66
#define S_ABILITY_PROMPT	67
#define S_NATION_PROMPT		70
#define S_HOF_QUESTION		73	/* with its own ~Yes ~No options */
#define S_SAVE_GAME		290
#define S_LOAD_GAME		320
#define S_ERA_TEXT		261	/* six era descriptions */
#define S_EXPEDITION_TEXT	267	/* six expedition descriptions */
#define W_MONTH			7	/* January.. */
#define W_LOG			20	/* log sentences, by news code */
#define W_DIFFICULTY		35
#define W_NATION		71	/* Spanish, English, French, Dutch */
#define W_ABILITY		83
#define W_ERA			98
#define W_EXPEDITION		99
#define W_WELCOME		100	/* 6 heading lines, then 4 options */
#define W_NATION_NAME		81	/* Spain, England, France, Holland */
#define W_TRAVEL		86	/* in-game menu */
#define W_COMPASS		13	/* S, SSW... clockwise */
#define W_SAILING		30	/* sailing phrases */
#define SAILING_FROM		9	/* "from the %s" */
#define SAILING_KNOTS		12	/* "Knots: " */
#define S_SAIL_HO		118	/* "Sail ho!" ... %s */
#define S_WATERS		119	/* "We're in %s waters" */
#define S_LOOKS_LIKE		120	/* "She looks like %s" */
#define S_COLOURS		122	/* "She's flying %s colors" */
#define S_OVERTAKEN		126
#define S_CHASING		311	/* "He's chasing us" */
#define S_UNANSWERED		315
#define W_A_SHIP		11	/* "a pinnace"... by ship type */
#define W_COLOURS		106	/* "Spanish"... "pirate" */
#define W_SHIP_TYPE		8	/* "Pinnace".. "Fort" */
#define W_DAMAGE		14	/* damage levels */
#define W_NEWS			18	/* 2 "Ship's News", 3 "(no entries)" */
#define NEWS_TITLE		2
#define NEWS_NONE		3
#define NEWS_SHOWN		6	/* the latest items a hail shows */
#define S_PRIZE			164	/* "we've captured %sa %s ton %s..." */
#define S_YOU_SUNK		167
#define S_ASHORE		161	/* the last ship lost: washed ashore */
#define S_PICKED_UP		162	/* "...back as %s captain" */
#define W_CAPTAIN		73	/* "an apprentice"..., by difficulty */
#define S_FOE_SUNK		168
#define S_SUNSET		169
#define S_STRUCK		170
#define S_YOU_ESCAPED		197
#define S_COMMAND		286
#define W_PLUNDERED		10	/* 6: " gold pieces plundered!" */
#define PLUNDERED_WORD		6
#define S_FORT_SUNSET		317
#define S_FORT_DRIVES_OFF	196
#define S_BEACHED		173
#define BTL_COMMAND		4	/* battle stage: choosing the ship */
#define S_FOE_ESCAPED		198
#define S_BOARDING		171	/* "...you spot %s approaching" */
#define SAILING_RELOADING	0
#define SAILING_LOADED		1
#define SAILING_BATTLE_SAILS	2
#define SAILING_FULL_SAILS	3
#define SAILING_RAISING		4
#define SAILING_LOWERING	5
#define SAILING_CAPTAIN		7	/* "the enemy captain" */
#define SAILING_GUNS		10	/* "Guns: " */
#define SAILING_MEN		11	/* "Men: " */
#define TRAVEL_CONTINUE		0
#define TRAVEL_SAVE		8
#define TRAVEL_EXIT		9

#define WELCOME_HEADING		6

/* Text fonts by size: PIRATE1 is the largest (25 px). */
#define FONT_TITLE		0
#define FONT_BODY		1
#define FONT_SMALL		2	/* PIRATE3, 14 px */
#define FONT_PIRATE4		3

/* Era of each famous expedition, from its year. */
static const int expedition_era[6] = { 0, 0, 2, 4, 4, 5 };

/* Portraits for the abilities, in the order of the ability list. */
static const char *ability_portrait[5] = {
	"spr/bigfen", "spr/bigcha", "spr/bignav", "spr/biggun", "spr/bigmed",
};

/* Flags in the oval, in town nation order. */
static const char *nation_flag[4] = {
	"spr/bigsflg", "spr/bigeflg", "spr/bigfflg", "spr/bigdflg",
};

/*
 * Identity screen (pic/idselect).  The game stores the rectangle
 * (237, 66)-(395, 273) next to its portrait table: the oval's opening.
 * Portraits are drawn clipped to it; centring them is a guess, since the
 * drawing code is not traced yet.  The scroll's flat parchment spans
 * x 51-582, y 315-391.
 */
#define OVAL_X0		237
#define OVAL_Y0		66
#define OVAL_X1		395
#define OVAL_Y1		273
#define SCROLL_X	80
#define SCROLL_Y	318
#define SCROLL_W	480
#define SCROLL_COL	230	/* option column width */

static void
plot(void *arg, int x, int y, uint32_t ink)
{
	struct fb *fb = arg;

	if (x >= 0 && y >= 0 && x < FB_W && y < FB_H)
		fb->px[(size_t)y * FB_W + x] = (uint8_t)ink;
}

static void
style(const struct game *g, int font, int caps, struct text_style *st)
{
	st->font = &g->font[font];
	st->caps = caps ? &g->caps : NULL;
	st->ink = fb_nearest(&g->fb, g->ink[0], g->ink[1], g->ink[2]);
	st->highlight = fb_nearest(&g->fb, g->highlight[0], g->highlight[1],
	    g->highlight[2]);
	st->line_gap = 2;
}

/* Draw text into the framebuffer; options are remembered for input. */
static int
draw(struct game *g, int font, int x, int y, int w, const char *text,
    int menu)
{
	struct text_target t = { plot, &g->fb };
	struct text_style st;
	struct text_layout lay;

	style(g, font, 1, &st);
	text_draw(&t, &st, x, y, w, text, menu ? g->selected : -1, &lay);
	if (menu)
		g->options = lay;
	return (lay.height);
}

/*
 * "Click to continue" (VA 0x1a3c4): after about seven seconds without
 * input (420 vertical retraces), the plaque of ui/misc frame 7 appears
 * at (268, 449); a key or a click goes on at any time.
 */
#define CONTINUE_IDLE		420
#define CONTINUE_FRAME		7

static void
continue_prompt(struct game *g)
{
	if (g->idle >= CONTINUE_IDLE && g->misc.count > CONTINUE_FRAME)
		fb_frame(&g->fb, &g->misc.frames[CONTINUE_FRAME], 268, 449);
}

static void
draw_centered(struct game *g, int font, int y, const char *text)
{
	int w = text_width(&g->font[font], text);

	draw(g, font, (FB_W - w) / 2, y, FB_W, text, 0);
}

/* Build "~a\r~b..." from a word list, for a menu. */
static void
options(const struct game *g, size_t group, int count, char *buf,
    size_t len)
{
	int i;

	buf[0] = '\0';
	for (i = 0; i < count; i++) {
		if (i > 0)
			strncat(buf, "\r", len - strlen(buf) - 1);
		strncat(buf, "~", len - strlen(buf) - 1);
		strncat(buf, game_word(g, group, (size_t)i),
		    len - strlen(buf) - 1);
	}
}

/*
 * Menu input shared by every choice screen: arrows move, Enter or a click
 * chooses, Escape goes back.  "enabled" may veto options.
 */
static void
menu_event(struct game *g, const struct ui_event *ev, int count,
    int (*enabled)(const struct game *, int),
    void (*choose)(struct game *, int), const struct screen *back)
{
	int i, step;

	switch (ev->type) {
	case EV_KEY:
		if (ev->key == UI_KEY_UP || ev->key == UI_KEY_DOWN) {
			step = ev->key == UI_KEY_UP ? count - 1 : 1;
			for (i = 0; i < count; i++) {
				g->selected = (g->selected + step) % count;
				if (enabled == NULL || enabled(g, g->selected))
					break;
			}
		} else if (ev->key == UI_KEY_ENTER) {
			if (enabled == NULL || enabled(g, g->selected))
				choose(g, g->selected);
		} else if (ev->key == UI_KEY_ESCAPE && back != NULL)
			game_goto(g, back);
		break;
	case EV_MOVE:
	case EV_CLICK:
		for (i = 0; i < g->options.noptions && i < count; i++) {
			const struct text_option *o = &g->options.option[i];

			if (ev->x < o->x || ev->x >= o->x + o->w ||
			    ev->y < o->y || ev->y >= o->y + o->h)
				continue;
			if (enabled != NULL && !enabled(g, i))
				break;
			g->selected = i;
			if (ev->type == EV_CLICK)
				choose(g, i);
			break;
		}
		break;
	default:
		break;
	}
}


/*
 * Intro (VA 0x3325): the MicroProse logo; "and", "MPS Labs" and
 * "Present" on black, each faded in and out; then the moonlit ship
 * (moon03) with its palette cycling, where the "Pirates! Gold" title
 * dissolves in on the right.  A key or a click skips to the menu.
 */
static void	load_bank(struct game *, const char *, int,
		    struct sprite_bank *);
static void	fill_args(char *, size_t, const char *, const char *const *,
		    int);
static void	sword_load(struct game *);
static void	fill(char *, size_t, const char *, const char *, const char *);

#define INTRO_STEPS		5
#define TITLE_X			272	/* the title's left edge */
#define TITLE_W			368
#define TITLE_H			274	/* two sprites: 135 + 134 rows */
#define CELLS_W			184	/* the dissolve's 2 x 2 cells */
#define CELLS_H			137
#define CELL_SPACE		32768	/* power of two above 0x61c0 */
#define TITLE_PASSES		45000	/* loop passes before the menu */
#define TITLE_LEAD		2000	/* palette cycling before the title */
#define PASSES_PER_TICK		167	/* engine choice: 0.1 ms a pass */

static const struct {
	const char	*pic;		/* NULL: black, the logo's palette */
	const char	*spr;
	int		 x, y;
	int		 hold;		/* ticks at full brightness */
} intro_steps[INTRO_STEPS] = {
	{ "pic/micro", NULL, 0, 0, 120 },	/* 20 x 100 ms */
	{ NULL, "spr/and", 229, 227, 60 },	/* 20 x 50 ms */
	{ NULL, "spr/mpslabs", 195, 100, 120 },
	{ NULL, "spr/present", 134, 231, 60 },
	{ "pic/moon03", NULL, 0, 0, 0 },	/* ends by its loop */
};

/* Rotate palette entries [start, start + count) up by one. */
static void
cycle(struct game *g, int start, int count)
{
	uint8_t last[3];

	memcpy(last, &g->fb.pal[(start + count - 1) * 3], 3);
	memmove(&g->fb.pal[(start + 1) * 3], &g->fb.pal[start * 3],
	    (size_t)(count - 1) * 3);
	memcpy(&g->fb.pal[start * 3], last, 3);
}

/* Compose both title sprites into one layer. */
static void
intro_title_layer(struct game *g)
{
	int i, x, y, oy = 0;

	free(g->intro_title);
	free(g->intro_shown);
	g->intro_title = calloc(TITLE_W * TITLE_H, 1);
	g->intro_shown = calloc(CELLS_W * CELLS_H, 1);
	if (g->intro_title == NULL || g->intro_shown == NULL)
		return;
	for (i = 0; i < 2; i++) {
		const struct frame *f;

		if (g->intro_spr[i].count == 0)
			continue;
		f = &g->intro_spr[i].frames[0];
		for (y = 0; y < f->h && oy + y < TITLE_H; y++)
			for (x = 0; x < f->w && x < TITLE_W; x++)
				if (f->mask == NULL || f->mask[y * f->w + x])
					g->intro_title[(oy + y) * TITLE_W + x] =
					    f->pixels[y * f->w + x];
		oy += f->h;
	}
}

static void
intro_enter(struct game *g)
{
	int step;

	if (g->intro_next) {	/* changed only once the fade is over */
		g->intro_step++;
		g->intro_next = 0;
	}
	step = g->intro_step;
	game_portrait(g, NULL);
	g->intro_hold = 0;
	bank_free(&g->intro_spr[0]);
	bank_free(&g->intro_spr[1]);
	if (intro_steps[step].pic != NULL)
		game_background(g, intro_steps[step].pic);
	else
		image_free(&g->bg);	/* black, keeping the palette */
	if (intro_steps[step].spr != NULL)
		load_bank(g, intro_steps[step].spr, 0, &g->intro_spr[0]);
	if (step == INTRO_STEPS - 1) {
		load_bank(g, "spr/title", 0, &g->intro_spr[0]);
		load_bank(g, "spr/title1", 0, &g->intro_spr[1]);
		intro_title_layer(g);
		g->intro_iter = 0;
		g->intro_cell = 0;	/* the original starts at random */
		g->intro_left = CELL_SPACE;
		g->intro_timer[0] = g->intro_timer[3] = 300;
		g->intro_timer[1] = 50;
		g->intro_timer[2] = 100;
		g->intro_cycles = 26;
		g->intro_pause = 0;
	}
}

static void
intro_leave(struct game *g)
{
	free(g->intro_title);
	free(g->intro_shown);
	g->intro_title = g->intro_shown = NULL;
	game_goto(g, &screen_credits);
}

static void
intro_event(struct game *g, const struct ui_event *ev)
{
	if (ev->type == EV_KEY || ev->type == EV_CLICK)
		intro_leave(g);
}

/* One pass of the title screen's loop (VA 0x36e4, then 0x379f).
 * Returns 1 when the screen is over. */
static int
title_pass(struct game *g)
{
	uint32_t row, col;

	if (g->intro_iter < TITLE_LEAD) {
		if (--g->intro_timer[0] < 0) {
			cycle(g, 0xee, 0x11);
			g->intro_timer[0] = 300;
		}
		if (--g->intro_timer[1] < 0) {
			cycle(g, 100, 0x40);
			g->intro_timer[1] = 50;
		}
		if (--g->intro_timer[2] < 0) {
			cycle(g, 0xc2, 0xd);
			g->intro_timer[2] = 100;
		}
		g->intro_iter++;
		return (0);
	}
	if (g->intro_left != 0) {
		/* Visit the cells in the order of x = 5x + 1, skipping
		 * those below the title. */
		for (;;) {
			g->intro_cell = (g->intro_cell * 5 + 1) %
			    CELL_SPACE;
			row = g->intro_cell / CELLS_W;
			if (row < CELLS_H)
				break;
			if (--g->intro_left == 0)
				break;
		}
		if (g->intro_left != 0) {
			col = g->intro_cell % CELLS_W;
			if (g->intro_shown != NULL)
				g->intro_shown[row * CELLS_W + col] = 1;
			g->intro_left--;
		}
	} else if (g->intro_pause == 0) {
		if (--g->intro_timer[3] < 0) {
			cycle(g, 0xd3, 0x1a);
			g->intro_timer[3] = 300;
			if (--g->intro_cycles == 0) {
				g->intro_cycles = 26;
				g->intro_pause = 10000;
			}
		}
	} else
		g->intro_pause--;
	if (--g->intro_timer[0] < 0) {
		cycle(g, 0xee, 0x11);
		g->intro_timer[0] = 300;
	}
	if (--g->intro_timer[1] < 0) {
		cycle(g, 100, 0x40);
		g->intro_timer[1] = 50;
	}
	if (--g->intro_timer[2] < 0) {
		cycle(g, 0xc2, 0xd);
		g->intro_timer[2] = 100;
	}
	g->intro_iter++;
	return (g->intro_iter - TITLE_LEAD >= TITLE_PASSES &&
	    g->intro_cycles == 26);
}

static void
intro_tick(struct game *g)
{
	int i;

	if (g->fading != 0)
		return;
	if (g->intro_step == INTRO_STEPS - 1) {
		for (i = 0; i < PASSES_PER_TICK; i++)
			if (title_pass(g)) {
				intro_leave(g);
				return;
			}
		return;
	}
	if (++g->intro_hold >= intro_steps[g->intro_step].hold) {
		g->intro_next = 1;
		game_goto(g, &screen_intro);
	}
}

static void
intro_draw(struct game *g)
{
	int step = g->intro_step, x, y;
	uint8_t c;

	if (g->intro_spr[0].count > 0 && step < INTRO_STEPS - 1)
		fb_frame(&g->fb, &g->intro_spr[0].frames[0],
		    intro_steps[step].x, intro_steps[step].y);
	if (step != INTRO_STEPS - 1 || g->intro_title == NULL ||
	    g->intro_shown == NULL)
		return;
	for (y = 0; y < TITLE_H; y++)
		for (x = 0; x < TITLE_W; x++)
			if (g->intro_shown[(y / 2) * CELLS_W + x / 2] &&
			    (c = g->intro_title[y * TITLE_W + x]) != 0)
				g->fb.px[y * FB_W + TITLE_X + x] = c;
}

const struct screen screen_intro = {
	"intro", intro_enter, intro_event, intro_tick, intro_draw,
};

/*
 * Credits (VA 0x1fe91): on the ship picture, six blocks of the credits
 * picture appear one at a time on the right.  Each fades in as its ink
 * (palette entry 0x20) steps from the parchment colour to black, stays
 * 1.6 s and fades out.  A key skips to the last block; another ends it.
 */
#define CRED_BLOCKS		6
#define CRED_INK		0x20
#define CRED_STEPS		20
#define CRED_STEP_MS		40
#define CRED_HOLD_MS		1600
#define CRED_CX			508	/* the blocks' centre */
#define CRED_CY			240

static const char *
credits_picture(const struct game *g)
{
	if (strcmp(g->lang, "fre") == 0)
		return ("pic/credtxtf");
	if (strcmp(g->lang, "ger") == 0)
		return ("pic/credtxtg");
	return ("pic/credtext");
}

/* Block "i": its rectangle in the credits picture. */
static int
credit_rect(const struct game *g, int i, int r[4])
{
	const uint8_t *p = exe_at(g->layout, g->exe, g->exe_len,
	    g->layout->credits_va + (uint32_t)i * 8, 8);
	int k;

	if (p == NULL)
		return (-1);
	for (k = 0; k < 4; k++)
		r[k] = (int16_t)(p[2 * k] | p[2 * k + 1] << 8);
	return (0);
}

/* Ink step 0 (parchment) .. 19 (black). */
static void
credit_ink(struct game *g, int step)
{
	const uint8_t *p = exe_at(g->layout, g->exe, g->exe_len,
	    g->layout->credits_va + CRED_BLOCKS * 8 +
	    (uint32_t)step * 3, 3);

	if (p != NULL)
		memcpy(&g->fb.pal[CRED_INK * 3], p, 3);
}

static void
credits_enter(struct game *g)
{
	struct blob b;

	game_background(g, "pic/credpic");
	image_free(&g->credits);
	if (packs_load(g->packs, credits_picture(g), &b) == 0) {
		pcx_decode(b.data, b.len, &g->credits);
		blob_free(&b);
	}
	g->cred_block = 0;
	g->cred_ms = 0;
	credit_ink(g, 0);
}

static void
credits_done(struct game *g)
{
	image_free(&g->credits);
	game_goto(g, &screen_welcome);
}

static void
credits_event(struct game *g, const struct ui_event *ev)
{
	if (ev->type != EV_KEY && ev->type != EV_CLICK)
		return;
	if (g->cred_block < CRED_BLOCKS - 1) {
		g->cred_block = CRED_BLOCKS - 1;
		g->cred_ms = 0;
		credit_ink(g, 0);
	} else
		credits_done(g);
}

static void
credits_tick(struct game *g)
{
	int t, fade = CRED_STEPS * CRED_STEP_MS;

	if (g->fading != 0)
		return;
	g->cred_ms += 1000 / 60;
	t = g->cred_ms;
	if (t < fade)
		credit_ink(g, t / CRED_STEP_MS);
	else if (t < fade + CRED_HOLD_MS)
		credit_ink(g, CRED_STEPS - 1);
	else if (t < 2 * fade + CRED_HOLD_MS)
		credit_ink(g, CRED_STEPS - 1 - (t - fade - CRED_HOLD_MS) /
		    CRED_STEP_MS);
	else if (++g->cred_block < CRED_BLOCKS) {
		g->cred_ms = 0;
		credit_ink(g, 0);
	} else
		credits_done(g);
}

static void
credits_draw(struct game *g)
{
	int r[4], w, h, x, y;

	if (g->credits.pixels == NULL || g->cred_block >= CRED_BLOCKS ||
	    credit_rect(g, g->cred_block, r) != 0)
		return;
	w = r[2] - r[0] + 1;
	h = r[3] - r[1];
	x = (CRED_CX - w / 2) & ~7;
	y = CRED_CY - h / 2;
	fb_image_part(&g->fb, &g->credits, r[0], r[1], w, h + 1, x, y, -1);
}

const struct screen screen_credits = {
	"credits", credits_enter, credits_event, credits_tick, credits_draw,
};

/* Welcome menu. */

enum { WELCOME_NEW, WELCOME_LOAD, WELCOME_FAMOUS, WELCOME_QUIT };

static void
welcome_enter(struct game *g)
{
	game_background(g, "pic/textback");
	game_portrait(g, NULL);
}

static int
welcome_enabled(const struct game *g, int i)
{
	return (i != WELCOME_LOAD || g->save_dir[0] != '\0');
}

static void
welcome_choose(struct game *g, int i)
{
	switch (i) {
	case WELCOME_NEW:
		g->career.expedition = -1;
		game_goto(g, &screen_era);
		break;
	case WELCOME_LOAD:
		g->saving = 0;
		game_goto(g, &screen_slots);
		break;
	case WELCOME_FAMOUS:
		game_goto(g, &screen_expedition);
		break;
	case WELCOME_QUIT:
		game_goto(g, NULL);
		break;
	}
}

static void
welcome_event(struct game *g, const struct ui_event *ev)
{
	menu_event(g, ev, 4, welcome_enabled, welcome_choose, NULL);
}

static void
welcome_draw(struct game *g)
{
	/* VA 0x1443e: every line centred; the heading in PIRATE4, the
	 * options in PIRATE1 with full-width rows for the mouse. */
	static const int line_y[WELCOME_HEADING + 4] = { 70, 120, 145, 170,
	    195, 250, 300, 330, 360, 390 };
	struct text_target t = { plot, &g->fb };
	struct text_style st;
	char opt[256];
	const char *text;
	int i, w;

	for (i = 0; i < WELCOME_HEADING + 4; i++) {
		text = game_word(g, W_WELCOME, (size_t)i);
		if (i < WELCOME_HEADING) {
			w = text_width(&g->font[FONT_PIRATE4], text);
			draw(g, FONT_PIRATE4, FB_W / 2 - w / 2, line_y[i],
			    FB_W, text, 0);
			continue;
		}
		snprintf(opt, sizeof(opt), "~%s", text);
		w = text_width(&g->font[FONT_TITLE], text);
		style(g, FONT_TITLE, 1, &st);
		text_draw(&t, &st, FB_W / 2 - w / 2, line_y[i], FB_W, opt,
		    g->selected == i - WELCOME_HEADING ? 0 : -1, NULL);
		g->options.option[i - WELCOME_HEADING].x = 0;
		g->options.option[i - WELCOME_HEADING].y = line_y[i];
		g->options.option[i - WELCOME_HEADING].w = FB_W;
		g->options.option[i - WELCOME_HEADING].h = 30;
	}
	g->options.noptions = 4;
}

const struct screen screen_welcome = {
	"welcome", welcome_enter, welcome_event, NULL, welcome_draw,
};

/* Era choice, with the era's description. */

static void
parchment_enter(struct game *g)
{
	game_background(g, "pic/textback");
	game_portrait(g, NULL);
}

static void
era_choose(struct game *g, int i)
{
	g->career.era = i;
	game_goto(g, &screen_nation);
}

static void
era_event(struct game *g, const struct ui_event *ev)
{
	menu_event(g, ev, 6, NULL, era_choose, &screen_welcome);
}

/*
 * The era screen (VA 0x15a3c): the title, then each period's name
 * centred (PIRATE1) with its description under it in the small PIRATE3,
 * every 70 pixels; each period is a full-width row for the mouse.
 */
#define ERA_NAME_Y		60
#define ERA_DESC_Y		83
#define ERA_STEP		70

static void
era_draw(struct game *g)
{
	struct text_target t = { plot, &g->fb };
	struct text_style st;
	char opt[160];
	const char *name;
	int i, w;

	w = text_width(&g->font[FONT_BODY], game_str(g, S_SELECT_ERA));
	draw(g, FONT_BODY, FB_W / 2 - w / 2, 20, FB_W,
	    game_str(g, S_SELECT_ERA), 0);
	for (i = 0; i < 6; i++) {
		name = game_word(g, W_ERA, (size_t)i);
		snprintf(opt, sizeof(opt), "~%s", name);
		w = text_width(&g->font[FONT_TITLE], name);
		style(g, FONT_TITLE, 1, &st);
		text_draw(&t, &st, FB_W / 2 - w / 2, ERA_NAME_Y + i * ERA_STEP,
		    FB_W, opt, g->selected == i ? 0 : -1, NULL);
		draw(g, FONT_SMALL, 40, ERA_DESC_Y + i * ERA_STEP, 580,
		    game_str(g, S_ERA_TEXT + (size_t)i), 0);
		g->options.option[i].x = 0;
		g->options.option[i].y = ERA_NAME_Y + i * ERA_STEP;
		g->options.option[i].w = FB_W;
		g->options.option[i].h = ERA_STEP;
	}
	g->options.noptions = 6;
}

const struct screen screen_era = {
	"era", parchment_enter, era_event, NULL, era_draw,
};

/* Famous expedition choice. */

static void
expedition_choose(struct game *g, int i)
{
	struct scenario scn;

	g->career.expedition = i;
	g->career.era = expedition_era[i];
	/* The expedition's own colours, for the flag. */
	if (scenario_load(g->layout, g->exe, g->exe_len,
	    SCENARIO_CAREERS + i, &scn) == 0)
		g->career.nation = scn.nation;
	game_goto(g, &screen_ability);
}

static void
expedition_event(struct game *g, const struct ui_event *ev)
{
	menu_event(g, ev, 6, NULL, expedition_choose, &screen_welcome);
}

static void
expedition_draw(struct game *g)
{
	char buf[512];

	draw_centered(g, FONT_TITLE, 40, game_str(g, S_SELECT_EXPEDITION));
	options(g, W_EXPEDITION, 6, buf, sizeof(buf));
	draw(g, FONT_BODY, 150, 100, 420, buf, 1);
	draw(g, FONT_BODY, 80, 300, 480,
	    game_str(g, S_EXPEDITION_TEXT + (size_t)g->selected), 0);
}

const struct screen screen_expedition = {
	"expedition", parchment_enter, expedition_event, NULL,
	expedition_draw,
};

/*
 * Identity (VA 0x1482e): one picture that fills in as the player
 * answers.  The captain's portrait sits in the oval; the nationality is
 * chosen by clicking one of four flags, two on each side (VA 0x14aa4),
 * and its big flag then stays at the top left; the special ability is
 * one of five icons along the bottom (VA 0x14e6e), its picture then at
 * the top right; the difficulty is a row of four levels on the scroll,
 * the portrait changing with each (VA 0x151c6); the name comes last.
 * Pointing at a choice names it on the scroll.  The rectangles are the
 * game's (VA 0x33488, 0x334b0, 0x33448).
 */
#define W_COLORS		82	/* "English colors"... */
#define W_DIFF_TEXT		101	/* "An easy level"... */
#define ID_LABEL_Y		368
#define FLAG_SLOTS		4	/* English, French, Dutch, Spanish */
#define SKILL_SLOTS		5

/* Flag slots, in the game's order, as town nations. */
static const int flag_nation[FLAG_SLOTS] = {
	NATION_ENGLAND, NATION_FRANCE, NATION_HOLLAND, NATION_SPAIN,
};
static const struct { int x0, y0, x1, y1; } flag_rect[FLAG_SLOTS] = {
	{ 0, 0, 129, 133 }, { 51, 155, 199, 279 }, { 481, 1, 629, 139 },
	{ 441, 151, 599, 279 },
};
/* Ability slots: the icon frame shown in each. */
static const int skill_frame[SKILL_SLOTS] = { 0, 3, 1, 2, 4 };
static const struct { int x0, y0, x1, y1; } skill_rect[SKILL_SLOTS] = {
	{ 42, 342, 85, 477 }, { 147, 391, 178, 474 }, { 276, 396, 353, 470 },
	{ 421, 396, 527, 456 }, { 540, 345, 620, 460 },
};
static const int level_x[4] = { 80, 190, 300, 410 };
#define LEVEL_Y			345
#define LEVEL_H			20

/* Captain portraits, one per difficulty level. */
static const char *captain_portrait[4] = {
	"spr/bigman0", "spr/bigman1", "spr/bigman2", "spr/bigman3",
};

static void
identity_enter(struct game *g)
{
	game_background(g, "pic/idselect");
	game_portrait(g, NULL);
	g->id_key = -1;
}

static void
id_frame(struct game *g, int bank, int frame, int x, int y)
{
	if ((size_t)frame < g->id_spr[bank].count)
		fb_frame(&g->fb, &g->id_spr[bank].frames[frame], x, y);
}

static void
id_label(struct game *g, int cx, int y, const char *text)
{
	int w = text_width(&g->font[FONT_BODY], text);

	draw(g, FONT_BODY, cx - w / 2, y, FB_W, text, 0);
}

/* The labels under the big pictures, in the game's second, light ink. */
static void
id_caption(struct game *g, int cx, int y, const char *text)
{
	static const uint8_t light[3] = { 240, 220, 170 };
	uint8_t keep[3];

	memcpy(keep, g->ink, 3);
	memcpy(g->ink, light, 3);
	id_label(g, cx, y, text);
	memcpy(g->ink, keep, 3);
}

/* What is decided so far: portrait, big flag and ability picture. */
static void
identity_draw_base(struct game *g, int flag, int skill)
{
	id_frame(g, 2, 0, OVAL_X0, OVAL_Y0);
	if (flag) {
		id_frame(g, 3, 0, 0, 0);
		id_caption(g, 160, 250, game_word(g, W_NATION,
		    (size_t)g->career.nation));
	}
	if (skill) {
		id_frame(g, 4, 0, 440, 0);
		id_caption(g, 520, 250, game_word(g, W_ABILITY,
		    (size_t)g->career.ability));
	}
}

static int
flag_open(const struct game *g, int slot)
{
	/* No Dutch careers in 1560: the scenario slot is empty. */
	return (!(flag_nation[slot] == NATION_HOLLAND && g->career.era == 0));
}

static int
id_hit_flag(const struct game *g, int x, int y)
{
	int i;

	for (i = 0; i < FLAG_SLOTS; i++)
		if (flag_open(g, i) && x >= flag_rect[i].x0 &&
		    x <= flag_rect[i].x1 && y >= flag_rect[i].y0 &&
		    y <= flag_rect[i].y1)
			return (i);
	return (-1);
}

static void
nation_enter(struct game *g)
{
	identity_enter(g);
	load_bank(g, "spr/lilflgs", 0, &g->id_spr[0]);
	load_bank(g, captain_portrait[0], 0, &g->id_spr[2]);
}

static void
nation_choose(struct game *g, int slot)
{
	g->career.nation = flag_nation[slot];
	load_bank(g, nation_flag[g->career.nation & 3], 0, &g->id_spr[3]);
	game_switch(g, &screen_ability);
}

/* The keyboard steps through the open slots (codes 2 and 4). */
static int
id_step(int key, int n, int d, int (*open)(const struct game *, int),
    const struct game *g)
{
	int i;

	for (i = 0; i < n; i++) {
		key = key < 0 ? (d > 0 ? 0 : n - 1) : (key + d + n) % n;
		if (open == NULL || open(g, key))
			break;
	}
	return (key);
}

static void
nation_event(struct game *g, const struct ui_event *ev)
{
	int i;

	switch (ev->type) {
	case EV_MOVE:
		g->id_key = id_hit_flag(g, ev->x, ev->y);
		break;
	case EV_CLICK:
		if ((i = id_hit_flag(g, ev->x, ev->y)) >= 0)
			nation_choose(g, i);
		break;
	case EV_KEY:
		if (ev->key == UI_KEY_RIGHT || ev->key == UI_KEY_DOWN)
			g->id_key = id_step(g->id_key, FLAG_SLOTS, 1, flag_open,
			    g);
		else if (ev->key == UI_KEY_LEFT || ev->key == UI_KEY_UP)
			g->id_key = id_step(g->id_key, FLAG_SLOTS, -1,
			    flag_open, g);
		else if (ev->key == UI_KEY_ENTER && g->id_key >= 0)
			nation_choose(g, g->id_key);
		else if (ev->key == UI_KEY_ESCAPE)
			game_goto(g, &screen_era);
		break;
	default:
		break;
	}
}

static void
nation_draw(struct game *g)
{
	int i;

	identity_draw_base(g, 0, 0);
	for (i = 0; i < FLAG_SLOTS; i++)
		if (flag_open(g, i))
			id_frame(g, 0, i, flag_rect[i].x0, flag_rect[i].y0);
	draw(g, FONT_BODY, 180, 316, 280, game_str(g, S_NATION_PROMPT), 0);
	if (g->id_key >= 0)
		id_label(g, 320, ID_LABEL_Y, game_word(g, W_COLORS,
		    (size_t)g->id_key));
}

const struct screen screen_nation = {
	"nation", nation_enter, nation_event, NULL, nation_draw,
};

/* Special ability: five icons along the bottom. */

static int
id_hit_skill(int x, int y)
{
	int i;

	for (i = 0; i < SKILL_SLOTS; i++)
		if (x >= skill_rect[i].x0 && x <= skill_rect[i].x1 &&
		    y >= skill_rect[i].y0 && y <= skill_rect[i].y1)
			return (i);
	return (-1);
}

static void
ability_enter(struct game *g)
{
	g->id_key = -1;
	load_bank(g, "spr/lilskls", 0, &g->id_spr[1]);
	if (g->id_spr[2].count == 0)
		load_bank(g, captain_portrait[0], 0, &g->id_spr[2]);
	if (g->career.expedition >= 0 || g->id_spr[3].count == 0) {
		identity_enter(g);
		load_bank(g, nation_flag[g->career.nation & 3], 0,
		    &g->id_spr[3]);
	}
}

static void
ability_choose(struct game *g, int i)
{
	g->career.ability = i;
	load_bank(g, ability_portrait[i], 0, &g->id_spr[4]);
	game_switch(g, &screen_difficulty);
}

static void
ability_event(struct game *g, const struct ui_event *ev)
{
	int i;

	switch (ev->type) {
	case EV_MOVE:
		g->id_key = id_hit_skill(ev->x, ev->y);
		break;
	case EV_CLICK:
		if ((i = id_hit_skill(ev->x, ev->y)) >= 0)
			ability_choose(g, i);
		break;
	case EV_KEY:
		if (ev->key == UI_KEY_RIGHT || ev->key == UI_KEY_DOWN)
			g->id_key = id_step(g->id_key, SKILL_SLOTS, 1, NULL, g);
		else if (ev->key == UI_KEY_LEFT || ev->key == UI_KEY_UP)
			g->id_key = id_step(g->id_key, SKILL_SLOTS, -1, NULL,
			    g);
		else if (ev->key == UI_KEY_ENTER && g->id_key >= 0)
			ability_choose(g, g->id_key);
		else if (ev->key == UI_KEY_ESCAPE)
			game_goto(g, g->career.expedition >= 0 ?
			    &screen_expedition : &screen_nation);
		break;
	default:
		break;
	}
}

static void
ability_draw(struct game *g)
{
	int i;

	identity_draw_base(g, 1, 0);
	draw(g, FONT_BODY, 180, 316, 280, game_str(g, S_ABILITY_PROMPT), 0);
	for (i = 0; i < SKILL_SLOTS; i++)
		id_frame(g, 1, skill_frame[i], skill_rect[i].x0,
		    skill_rect[i].y0);
	if (g->id_key >= 0)
		id_label(g, 320, ID_LABEL_Y, game_word(g, W_ABILITY,
		    (size_t)g->id_key));
}

const struct screen screen_ability = {
	"ability", ability_enter, ability_event, NULL, ability_draw,
};

/* Difficulty: four levels in a row; the portrait follows. */

static void
difficulty_show(struct game *g, int i)
{
	g->id_key = i;
	load_bank(g, captain_portrait[i & 3], 0, &g->id_spr[2]);
}

static int
id_hit_level(int x, int y)
{
	static const int x1[4] = { 199, 295, 423, 479 };
	int i;

	for (i = 0; i < 4; i++)
		if (x >= level_x[i] && x <= x1[i] && y >= LEVEL_Y &&
		    y <= LEVEL_Y + LEVEL_H)
			return (i);
	return (-1);
}

static void
difficulty_enter(struct game *g)
{
	difficulty_show(g, 0);
}

static void
difficulty_choose(struct game *g, int i)
{
	g->career.difficulty = i;
	game_switch(g, &screen_name);
}

static void
difficulty_event(struct game *g, const struct ui_event *ev)
{
	int i;

	switch (ev->type) {
	case EV_MOVE:
		if ((i = id_hit_level(ev->x, ev->y)) >= 0 && i != g->id_key)
			difficulty_show(g, i);
		break;
	case EV_CLICK:
		if ((i = id_hit_level(ev->x, ev->y)) >= 0)
			difficulty_choose(g, i);
		break;
	case EV_KEY:
		if (ev->key == UI_KEY_RIGHT || ev->key == UI_KEY_DOWN)
			difficulty_show(g, (g->id_key + 1) & 3);
		else if (ev->key == UI_KEY_LEFT || ev->key == UI_KEY_UP)
			difficulty_show(g, (g->id_key + 3) & 3);
		else if (ev->key == UI_KEY_ENTER)
			difficulty_choose(g, g->id_key);
		else if (ev->key == UI_KEY_ESCAPE)
			game_switch(g, &screen_ability);
		break;
	default:
		break;
	}
}

static void
difficulty_draw(struct game *g)
{
	uint8_t keep[3];
	int i;

	identity_draw_base(g, 1, 1);
	id_label(g, 320, 316, game_str(g, S_DIFFICULTY_PROMPT));
	for (i = 0; i < 4; i++) {
		memcpy(keep, g->ink, 3);
		if (i == g->id_key)
			memcpy(g->ink, g->highlight, 3);
		draw(g, FONT_BODY, level_x[i], LEVEL_Y, 140,
		    game_word(g, W_DIFFICULTY, (size_t)i), 0);
		memcpy(g->ink, keep, 3);
	}
	id_label(g, 320, 380, game_word(g, W_DIFF_TEXT, (size_t)g->id_key));
}

const struct screen screen_difficulty = {
	"difficulty", difficulty_enter, difficulty_event, NULL,
	difficulty_draw,
};

/*
 * Name entry, last, under the chosen portrait (VA 0x155fa, 0x157f4): the
 * field starts as "Incognito" (CSTN 92.4), which Enter or a click
 * accepts; the first letter typed replaces it.  At most 11 characters.
 */
#define W_NAMES			92
#define NAME_DEFAULT		4
#define NAME_CHARS		11

static void
name_enter(struct game *g)
{
	snprintf(g->career.name, sizeof(g->career.name), "%s",
	    game_word(g, W_NAMES, NAME_DEFAULT));
	g->name_default = 1;
}

static void
name_event(struct game *g, const struct ui_event *ev)
{
	size_t len = strlen(g->career.name);

	if (ev->type == EV_CHAR && ev->ch >= 0x20 && ev->ch < 0x7F) {
		if (g->name_default) {
			g->career.name[0] = '\0';
			g->name_default = 0;
			len = 0;
		}
		if (len < NAME_CHARS && len < NAME_MAX_LEN) {
			g->career.name[len] = (char)ev->ch;
			g->career.name[len + 1] = '\0';
		}
	} else if (ev->type == EV_KEY && ev->key == UI_KEY_BACKSPACE &&
	    !g->name_default && len > 0)
		g->career.name[len - 1] = '\0';
	else if ((ev->type == EV_CLICK || (ev->type == EV_KEY &&
	    ev->key == UI_KEY_ENTER)) && len > 0)
		game_switch(g, &screen_confirm);
	else if (ev->type == EV_KEY && ev->key == UI_KEY_ESCAPE)
		game_switch(g, &screen_difficulty);
}

static void
name_draw(struct game *g)
{
	char buf[NAME_MAX_LEN * 2 + 4];

	identity_draw_base(g, 1, 1);
	id_label(g, 320, 322, game_str(g, S_NAME_PROMPT));
	/* A blinking cursor after the name typed so far. */
	snprintf(buf, sizeof(buf), "%s%s", g->career.name,
	    !g->name_default && (g->ticks / 30) % 2 ? "_" : "");
	id_label(g, 320, 352, buf);
}

const struct screen screen_name = {
	"name", name_enter, name_event, NULL, name_draw,
};

/*
 * Confirmation (VA 0x15498): "Are ye satisfied with your choices,
 * Captain?" (CSTR 273), "Yes" or "Nay" on the scroll.  Nay starts the
 * identity over from the nationality (an expedition: the ability).
 */
#define S_SATISFIED		273
#define S_YES			274
#define S_NAY			275

static const struct { int x0, y0, x1, y1; } confirm_rect[2] = {
	{ 200, 368, 240, 399 }, { 380, 368, 440, 399 },
};

static void
confirm_enter(struct game *g)
{
	g->id_key = 0;
}

static void
confirm_choose(struct game *g, int i)
{
	if (i == 0) {
		/* The career starts now; its story comes first. */
		if (game_start_career(g) != 0) {
			snprintf(g->err, sizeof(g->err),
			    "cannot start the career");
			game_goto(g, &screen_summary);
			return;
		}
		g->story_pos = 0;
		game_goto(g, &screen_story);
		return;
	}
	bank_free(&g->id_spr[4]);
	if (g->career.expedition >= 0)
		game_switch(g, &screen_ability);
	else {
		bank_free(&g->id_spr[3]);
		load_bank(g, captain_portrait[0], 0, &g->id_spr[2]);
		game_switch(g, &screen_nation);
	}
}

static void
confirm_event(struct game *g, const struct ui_event *ev)
{
	int i;

	switch (ev->type) {
	case EV_MOVE:
	case EV_CLICK:
		for (i = 0; i < 2; i++)
			if (ev->x >= confirm_rect[i].x0 &&
			    ev->x <= confirm_rect[i].x1 &&
			    ev->y >= confirm_rect[i].y0 &&
			    ev->y <= confirm_rect[i].y1) {
				g->id_key = i;
				if (ev->type == EV_CLICK)
					confirm_choose(g, i);
				break;
			}
		break;
	case EV_KEY:
		if (ev->key == UI_KEY_LEFT || ev->key == UI_KEY_RIGHT ||
		    ev->key == UI_KEY_UP || ev->key == UI_KEY_DOWN)
			g->id_key ^= 1;
		else if (ev->key == UI_KEY_ENTER)
			confirm_choose(g, g->id_key);
		else if (ev->key == UI_KEY_ESCAPE)
			game_switch(g, &screen_name);
		break;
	default:
		break;
	}
}

static void
confirm_draw(struct game *g)
{
	uint8_t keep[3];

	identity_draw_base(g, 1, 1);
	draw(g, FONT_BODY, 180, 316, 280, game_str(g, S_SATISFIED), 0);
	memcpy(keep, g->ink, 3);
	if (g->id_key == 0)
		memcpy(g->ink, g->highlight, 3);
	draw(g, FONT_BODY, 208, 368, 100, game_str(g, S_YES), 0);
	memcpy(g->ink, keep, 3);
	if (g->id_key == 1)
		memcpy(g->ink, g->highlight, 3);
	draw(g, FONT_BODY, 390, 368, 100, game_str(g, S_NAY), 0);
	memcpy(g->ink, keep, 3);
}

const struct screen screen_confirm = {
	"confirm", confirm_enter, confirm_event, NULL, confirm_draw,
};

/*
 * The career's story (VA 0x139e2): on the frame picture with the
 * captain's portrait, pages of text from a byte script in the
 * executable, one script per career (era x 4 + flag slot):
 *   a CSTR number shows that text (with the treasure, a town, the year
 *   and the player's name); -2 waits for a key and turns the page;
 *   ')' waits for a key; 'a' is a sword fight; 0 ends the story.
 * '*' and ')' are the original's copy-protection question, which this
 * build always passes; codes '2'-'7' show nothing.
 */
#define STORY_TEXT_Y		340
#define STORY_INK		2
#define STORY_END		0
#define STORY_PAGE		(-2)
#define STORY_WAIT		')'
#define STORY_CHECK		'*'
#define STORY_FIGHT		'a'
#define STORY_ASK		6
#define S_TREASURE_FLEET	46
#define STORY_MAX		4096

/* The script for this career: the "index"th, after -1 separators. */
static const uint8_t *
story_script(const struct game *g, size_t *len)
{
	const uint8_t *p = exe_at(g->layout, g->exe, g->exe_len,
	    g->layout->story_va, STORY_MAX);
	int index, n = 0;
	size_t i = 0, start;

	if (p == NULL)
		return (NULL);
	index = g->career.expedition >= 0 ? SCENARIO_CAREERS +
	    g->career.expedition : scenario_career(g->career.era,
	    (enum scenario_nation)(g->career.nation == NATION_SPAIN ? 3 :
	    g->career.nation - 1));
	for (; i < STORY_MAX && n < index; i++)
		if (p[i] == 0xFF)
			n++;
	start = i;
	while (i < STORY_MAX && p[i] != 0xFF)
		i++;
	*len = i - start;
	return (p + start);
}

/* Read on to the next stop: a page to wait on, a fight, or the end. */
static void
story_advance(struct game *g)
{
	const uint8_t *p;
	size_t len;
	int c;

	if ((p = story_script(g, &len)) == NULL) {
		game_goto(g, &screen_master);
		return;
	}
	g->story_waiting = 0;
	while (g->story_pos < len) {
		c = (int8_t)p[g->story_pos++];
		if (c == STORY_END || c == STORY_ASK)
			break;
		if (c == STORY_PAGE || c == STORY_WAIT) {
			g->story_waiting = c == STORY_PAGE ? 1 : 2;
			return;
		}
		if (c == STORY_FIGHT) {
			g->sw_story = 1;
			game_goto(g, &screen_sword);
			return;
		}
		if (c == STORY_CHECK || (c >= '2' && c <= '7'))
			continue;
		g->story_text = c < 0 ? -c : c;
	}
	game_goto(g, &screen_master);
}

static void
story_enter(struct game *g)
{
	game_background(g, "pic/frame");
	game_portrait(g, NULL);
	load_bank(g, captain_portrait[g->career.difficulty & 3], 0,
	    &g->id_spr[2]);
	/* The game's ink here is palette entry 2 (VA 0x159df, table at VA
	 * 0x3421c), an orange in this picture. */
	if (g->bg.pixels != NULL)
		memcpy(g->ink, &g->bg.palette[STORY_INK * 3], 3);
	if (g->story_pos == 0)
		g->story_text = -1;
	story_advance(g);
}

static void
story_event(struct game *g, const struct ui_event *ev)
{
	/* Escape skips the story: an engine convenience. */
	if (ev->type == EV_KEY && ev->key == UI_KEY_ESCAPE) {
		game_goto(g, &screen_master);
		return;
	}
	if (!g->story_waiting || !(ev->type == EV_CLICK ||
	    (ev->type == EV_KEY && ev->key == UI_KEY_ENTER)))
		return;
	if (g->story_waiting == 1)
		g->story_text = -1;		/* a new page */
	story_advance(g);
}

static void
story_draw(struct game *g)
{
	const char *arg[4];
	char text[1024], year[16];
	const struct sim_town *t;

	id_frame(g, 2, 0, 236, 58);
	if (g->story_text < 0)
		return;
	t = g->sim.ntowns > 0 ? &g->sim.towns[g->sim.fleet.leaving >= 0 ?
	    g->sim.fleet.leaving : 0] : NULL;
	snprintf(year, sizeof(year), "%d", g->sim.era_year);
	arg[0] = game_str(g, S_TREASURE_FLEET);
	arg[1] = t != NULL ? t->name : "";
	arg[2] = year;
	arg[3] = g->sim.player.name;
	fill_args(text, sizeof(text), game_str(g, g->story_text), arg, 4);
	draw(g, FONT_BODY, 40, STORY_TEXT_Y, 580, text, 0);
	if (g->story_waiting)
		continue_prompt(g);
}

const struct screen screen_story = {
	"story", story_enter, story_event, NULL, story_draw,
};

/*
 * The sailing master (VA 0x13e3f), when the story is over: on his
 * picture, "Here in <town>, you'll probably be wantin' to see the
 * <nation> governor..." (CSTR 68), then, for every other nation, its
 * nearest town (CSTR 69) by distance on the map, "a few" (CSTR 48)
 * days away under 31 or "many" (49), and in which direction (CSTN 75).
 * Then the career begins in the start town.
 */
#define S_MASTER		68
#define S_CLOSEST		69
#define S_A_FEW			48
#define S_MANY			49
#define W_DIRECTION		75
#define FEW_DAYS		31

static void
master_enter(struct game *g)
{
	game_background(g, "pic/smaster");
	game_portrait(g, NULL);
	g->ink[0] = 240;
	g->ink[1] = 220;
	g->ink[2] = 170;
	g->master_page = 0;
}

/* Replace a text's last character (a line break) with a quote. */
static void
close_quote(char *text)
{
	size_t n = strlen(text);

	if (n > 0)
		text[n - 1] = '"';
}

static void
master_text(struct game *g, int page, char *out, size_t len)
{
	const struct sim *sm = &g->sim;
	const struct sim_town *here_t, *t;
	int best[4], dist[4], n, i, d, dx, dy, dir;
	const char *arg[4];
	char line[400], days[32];

	out[0] = '\0';
	if (sm->fleet.leaving < 0 || (size_t)sm->fleet.leaving >= sm->ntowns)
		return;
	here_t = &sm->towns[sm->fleet.leaving];
	if (page == 0) {
		fill(out, len, game_str(g, S_MASTER), here_t->name,
		    game_word(g, W_NATION, here_t->nation & 3));
		close_quote(out);
		return;
	}
	for (n = 0; n < 4; n++) {
		best[n] = -1;
		dist[n] = 0x7FFF;
	}
	for (i = 0; i < (int)sm->ntowns; i++) {
		t = &sm->towns[i];
		if (i == sm->fleet.leaving || t->nation > 3)
			continue;
		d = abs(t->x - here_t->x) + abs(t->y - here_t->y);
		if (d < dist[t->nation]) {
			dist[t->nation] = d;
			best[t->nation] = i;
		}
	}
	for (n = 0; n < 4; n++) {
		if (n == here_t->nation || best[n] < 0)
			continue;
		t = &sm->towns[best[n]];
		dx = t->x - here_t->x;
		dy = t->y - here_t->y;
		if (abs(dy) * 2 < abs(dx))
			dy = 0;
		if (abs(dx) * 2 < abs(dy))
			dx = 0;
		dir = dy < 0 ? (dx < 0 ? 4 : dx == 0 ? 0 : 5) :
		    dy == 0 ? (dx < 0 ? 3 : dx == 0 ? 8 : 2) :
		    (dx < 0 ? 6 : dx == 0 ? 1 : 7);
		snprintf(days, sizeof(days), "%s", game_str(g,
		    abs(dx) + abs(dy) < FEW_DAYS ? S_A_FEW : S_MANY));
		arg[0] = game_word(g, W_NATION, (size_t)n);
		arg[1] = t->name;
		arg[2] = days;
		arg[3] = game_word(g, W_DIRECTION, (size_t)dir);
		fill_args(line, sizeof(line), game_str(g, S_CLOSEST), arg, 4);
		strncat(out, line, len - strlen(out) - 1);
	}
	close_quote(out);
}

static void
master_event(struct game *g, const struct ui_event *ev)
{
	if (!(ev->type == EV_CLICK || (ev->type == EV_KEY &&
	    (ev->key == UI_KEY_ENTER || ev->key == UI_KEY_ESCAPE))))
		return;
	if (g->master_page == 0) {
		g->master_page = 1;
		g->idle = 0;
		return;
	}
	/* The career begins in the start town. */
	sim_enter_town(&g->sim, g->sim.fleet.leaving);
	game_goto(g, &screen_town);
}

static void
master_draw(struct game *g)
{
	char text[2048];

	master_text(g, g->master_page, text, sizeof(text));
	draw(g, FONT_BODY, 340, 100, 280, text, 0);
	continue_prompt(g);
}

const struct screen screen_master = {
	"master", master_enter, master_event, NULL, master_draw,
};

/* Summary: the career is created; the voyage itself is the next phase. */

static void
summary_enter(struct game *g)
{
	parchment_enter(g);
	if (!g->started && g->err[0] == '\0' && game_start_career(g) != 0)
		snprintf(g->err, sizeof(g->err), "cannot start the career");
}

static void
summary_event(struct game *g, const struct ui_event *ev)
{
	if (g->started && (ev->type == EV_CLICK ||
	    (ev->type == EV_KEY && ev->key == UI_KEY_ENTER)))
		game_goto(g, &screen_cabin);
	else if (!g->started && ev->type == EV_KEY)
		game_goto(g, &screen_welcome);
}

static void
summary_draw(struct game *g)
{
	char buf[256];
	int y = 60;

	if (!g->started) {
		draw_centered(g, FONT_TITLE, 200, g->err);
		return;
	}
	draw_centered(g, FONT_TITLE, y, g->career.name);
	y += 40;
	draw_centered(g, FONT_TITLE, y, g->career.title);
	y += 50;
	snprintf(buf, sizeof(buf), "%s, %s",
	    game_word(g, W_DIFFICULTY, (size_t)g->career.difficulty),
	    game_word(g, W_ERA, (size_t)g->career.era));
	draw_centered(g, FONT_BODY, y, buf);
	y += 30;
	draw_centered(g, FONT_BODY, y,
	    game_word(g, W_ABILITY, (size_t)g->career.ability));
	continue_prompt(g);
}

const struct screen screen_summary = {
	"summary", summary_enter, summary_event, NULL, summary_draw,
};

/*
 * Replace each %s of a log sentence with the next argument, as the
 * game's printf-style strings expect.
 */
static void
fill_args(char *out, size_t len, const char *fmt, const char *const *arg,
    int nargs)
{
	size_t n = 0;
	int k = 0;

	while (*fmt != '\0' && n + 1 < len) {
		if (fmt[0] == '%' && fmt[1] == 's') {
			const char *v = k < nargs && arg[k] != NULL ? arg[k] :
			    "";

			k++;
			while (*v != '\0' && n + 1 < len)
				out[n++] = *v++;
			fmt += 2;
		} else
			out[n++] = *fmt++;
	}
	out[n] = '\0';
}

static void
fill(char *out, size_t len, const char *fmt, const char *a, const char *b)
{
	const char *arg[2] = { a, b };

	fill_args(out, len, fmt, arg, 2);
}

static const char *
town_name(const struct game *g, int i)
{
	return (i >= 0 && (size_t)i < g->sim.ntowns ? g->sim.towns[i].name :
	    "");
}

/* A news item as the game's log words it. */
static void
news_text(const struct game *g, const struct sim_news *n, char *out,
    size_t len)
{
	const char *fmt = game_word(g, W_LOG, (size_t)n->code);

	switch (n->code) {
	case NEWS_WAR:
	case NEWS_PEACE:
	case NEWS_ALLIANCE:
		fill(out, len, fmt, game_word(g, W_NATION_NAME, (size_t)n->a & 3),
		    game_word(g, W_NATION_NAME, (size_t)n->b & 3));
		break;
	case NEWS_AMNESTY:
		fill(out, len, fmt, game_word(g, W_NATION_NAME,
		    (size_t)n->a & 3), NULL);
		break;
	case NEWS_CAPTURE:
		fill(out, len, fmt, game_word(g, W_NATION_NAME,
		    (size_t)n->a & 3), town_name(g, n->b));
		break;
	default:
		fill(out, len, fmt, town_name(g, n->a), NULL);
	}
}

static void
date_text(const struct game *g, char *out, size_t len)
{
	snprintf(out, len, "%s %d", game_word(g, W_MONTH,
	    (size_t)(g->sim.day_of_year / 30)), g->sim.year);
}

/*
 * Logbook (cabin spot 4): the latest news, oldest first, on shiplog.pcx.
 * The original's log layout is not traced; an engine page for now.
 */
static void
logbook_enter(struct game *g)
{
	game_background(g, "pic/shiplog");
	game_portrait(g, NULL);
	if (g->bg.pixels != NULL)
		memcpy(g->ink, g->bg.palette, 3);
}

static void
logbook_event(struct game *g, const struct ui_event *ev)
{
	if (ev->type == EV_CLICK || (ev->type == EV_KEY &&
	    (ev->key == UI_KEY_ENTER || ev->key == UI_KEY_ESCAPE)))
		game_goto(g, &screen_cabin);
}

static void
logbook_draw(struct game *g)
{
	const struct sim_news *n;
	char buf[256], line[160];
	size_t i, kept, first;
	int y = 30;

	draw_centered(g, FONT_TITLE, y, g->sim.fleet.leaving >= 0 ?
	    town_name(g, g->sim.fleet.leaving) : g->sim.player.name);
	y += 32;
	date_text(g, line, sizeof(line));
	snprintf(buf, sizeof(buf), "%s, %s", g->sim.player.title, line);
	draw_centered(g, FONT_BODY, y, buf);
	y += 40;
	kept = sim_news_kept(&g->sim);
	first = kept > 14 ? kept - 14 : 0;
	for (i = first; i < kept; i++) {
		n = sim_news_at(&g->sim, i);
		news_text(g, n, line, sizeof(line));
		y += draw(g, FONT_BODY, 80, y, 480, line, 0);
	}
	continue_prompt(g);
}

const struct screen screen_logbook = {
	"logbook", logbook_enter, logbook_event, NULL, logbook_draw,
};

/*
 * The captain's cabin (VA 0x9ffc): cabin.pcx, with cabin.spr frame 0 at
 * (325, 229) while no treasure map is held, and eleven spots (table at
 * VA 0x327e0: x0, y0, x1, y1, on).  The title, "Captain's Cabin" (CSTN
 * 92.7), is centred at (320, 38) in ink 0; pointing at a spot shows its
 * label there instead (CSTN 86).  Search, Divide party and Join party
 * are off unless a party is ashore.
 */
#define CABIN_SPOTS		11
#define W_CABIN			86
#define W_CABIN_TITLE		92
#define CABIN_TITLE		7
enum { CABIN_CONTINUE, CABIN_MAPS, CABIN_CAVE, CABIN_LOG, CABIN_MAP,
    CABIN_SEARCH, CABIN_DIVIDE, CABIN_JOIN, CABIN_SAVE, CABIN_EXIT,
    CABIN_PRACTICE };

static int
cabin_rect(const struct game *g, int i, int r[5])
{
	const uint8_t *p = exe_at(g->layout, g->exe, g->exe_len,
	    g->layout->cabin_va + (uint32_t)i * 10, 10);
	int k;

	if (p == NULL)
		return (-1);
	for (k = 0; k < 5; k++)
		r[k] = (int16_t)(p[2 * k] | p[2 * k + 1] << 8);
	return (0);
}

static int
cabin_open(const struct game *g, int i)
{
	int r[5];

	if (cabin_rect(g, i, r) != 0 || !r[4])
		return (0);
	return (i != CABIN_SAVE || g->save_dir[0] != '\0');
}

static int
cabin_hit(const struct game *g, int x, int y)
{
	int i, r[5];

	for (i = 0; i < CABIN_SPOTS; i++)
		if (cabin_open(g, i) && cabin_rect(g, i, r) == 0 &&
		    x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3])
			return (i);
	return (-1);
}

static void
cabin_enter(struct game *g)
{
	game_background(g, "pic/cabin");
	game_portrait(g, NULL);
	load_bank(g, "spr/cabin", 0, &g->cabin_spr);
	if (g->bg.pixels != NULL)
		memcpy(g->ink, g->bg.palette, 3);
	g->cabin_spot = -1;
	g->cabin_key = CABIN_CONTINUE;
}

static void
cabin_choose(struct game *g, int i)
{
	g->msg[0] = '\0';
	switch (i) {
	case CABIN_CONTINUE:
		game_goto(g, g->sim.player_town >= 0 ? &screen_town :
		    g->world != NULL ? &screen_sail : &screen_town);
		break;
	case CABIN_CAVE:
		g->cave_back = &screen_cabin;
		game_goto(g, &screen_cave);
		break;
	case CABIN_LOG:
		game_goto(g, &screen_logbook);
		break;
	case CABIN_SAVE:
		g->saving = 1;
		game_goto(g, &screen_slots);
		break;
	case CABIN_EXIT:
		game_goto(g, &screen_retire);
		break;
	case CABIN_PRACTICE:
		g->sw_practice = 1;
		game_goto(g, &screen_sword);
		break;
	default:
		snprintf(g->msg, sizeof(g->msg), "(%s: not implemented yet.)",
		    game_word(g, W_CABIN, (size_t)i));
		break;
	}
}

static void
cabin_step(struct game *g, int d)
{
	int n;

	for (n = 0; n < CABIN_SPOTS; n++) {
		g->cabin_key = (g->cabin_key + d + CABIN_SPOTS) % CABIN_SPOTS;
		if (cabin_open(g, g->cabin_key))
			break;
	}
	g->cabin_spot = g->cabin_key;
}

static void
cabin_event(struct game *g, const struct ui_event *ev)
{
	int i;

	switch (ev->type) {
	case EV_MOVE:
		g->cabin_spot = cabin_hit(g, ev->x, ev->y);
		if (g->cabin_spot >= 0)
			g->cabin_key = g->cabin_spot;
		break;
	case EV_CLICK:
		if ((i = cabin_hit(g, ev->x, ev->y)) >= 0)
			cabin_choose(g, i);
		break;
	case EV_KEY:
		if (ev->key == UI_KEY_LEFT || ev->key == UI_KEY_UP)
			cabin_step(g, -1);
		else if (ev->key == UI_KEY_RIGHT || ev->key == UI_KEY_DOWN)
			cabin_step(g, 1);
		else if (ev->key == UI_KEY_ENTER)
			cabin_choose(g, g->cabin_key);
		else if (ev->key == UI_KEY_ESCAPE)
			cabin_choose(g, CABIN_CONTINUE);
		break;
	default:
		break;
	}
}

static void
cabin_draw(struct game *g)
{
	const char *label;
	int w;

	if (g->cabin_spr.count > 0)
		fb_frame(&g->fb, &g->cabin_spr.frames[0], 325, 229);
	label = g->cabin_spot >= 0 ?
	    game_word(g, W_CABIN, (size_t)g->cabin_spot) :
	    game_word(g, W_CABIN_TITLE, CABIN_TITLE);
	w = text_width(&g->font[FONT_TITLE], label);
	draw(g, FONT_TITLE, 320 - w / 2, 38, FB_W, label, 0);
	if (g->msg[0] != '\0')
		draw_centered(g, FONT_BODY, 444, g->msg);
}

const struct screen screen_cabin = {
	"cabin", cabin_enter, cabin_event, NULL, cabin_draw,
};

/* Save slots, for both saving and loading. */

static void
slots_enter(struct game *g)
{
	char path[600], err[256];
	int i;

	game_background(g, "pic/savegame");
	game_portrait(g, NULL);
	/* The slate behind the slots needs a light highlight. */
	g->highlight[0] = g->highlight[1] = g->highlight[2] = 240;
	for (i = 0; i < SAVE_SLOTS; i++) {
		save_slot_path(g->save_dir, i, path, sizeof(path));
		if (save_read(path, NULL, g->slot_desc[i],
		    sizeof(g->slot_desc[i]), err, sizeof(err)) != 0)
			g->slot_desc[i][0] = '\0';
	}
}

static int
slots_enabled(const struct game *g, int i)
{
	return (g->saving || g->slot_desc[i][0] != '\0');
}

static void
slots_choose(struct game *g, int i)
{
	char path[600], desc[SAVE_DESC_LEN + 1], date[64];

	save_slot_path(g->save_dir, i, path, sizeof(path));
	if (g->saving) {
		date_text(g, date, sizeof(date));
		snprintf(desc, sizeof(desc), "%s, %s, %s", g->sim.player.name,
		    g->sim.player.title, date);
		if (save_write(path, &g->sim, desc, g->err,
		    sizeof(g->err)) != 0)
			snprintf(g->msg, sizeof(g->msg), "%s", g->err);
		else
			snprintf(g->msg, sizeof(g->msg), "%s", desc);
		game_goto(g, &screen_cabin);
		return;
	}
	if (save_read(path, &g->sim, NULL, 0, g->err, sizeof(g->err)) != 0 ||
	    game_attach_world(g) != 0) {
		snprintf(g->msg, sizeof(g->msg), "%s", g->err);
		return;
	}
	g->started = 1;
	g->news_seen = g->sim.nnews;
	g->msg[0] = '\0';
	game_goto(g, &screen_cabin);
}

static void
slots_event(struct game *g, const struct ui_event *ev)
{
	menu_event(g, ev, SAVE_SLOTS, slots_enabled, slots_choose,
	    g->saving ? &screen_cabin : &screen_welcome);
}

static void
slots_draw(struct game *g)
{
	char buf[SAVE_SLOTS * 100], line[120];
	int i;

	draw_centered(g, FONT_TITLE, 40,
	    game_str(g, g->saving ? S_SAVE_GAME : S_LOAD_GAME));
	buf[0] = '\0';
	for (i = 0; i < SAVE_SLOTS; i++) {
		snprintf(line, sizeof(line), "%s~%d. %s", i > 0 ? "\r" : "",
		    i + 1, g->slot_desc[i][0] != '\0' ? g->slot_desc[i] :
		    "-");
		strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
	}
	draw(g, FONT_BODY, 100, 100, 460, buf, 1);
	if (g->msg[0] != '\0')
		draw_centered(g, FONT_BODY, 400, g->msg);
}

const struct screen screen_slots = {
	"slots", slots_enter, slots_event, NULL, slots_draw,
};

/* Retirement: offer the Hall of Fame, as the game does. */

/*
 * Stand-in career score: months at sea.  The original's score weighs
 * wealth, land, titles and family (FUN_00005039), which the simulation
 * does not model yet.
 */
static long
career_score(const struct game *g)
{
	return (g->sim.day / 30);
}

static void
retire_choose(struct game *g, int i)
{
	struct hof h;
	struct hof_entry e;
	char path[600];

	g->started = 0;
	g->hof_new = -1;
	if (i != 0 || g->save_dir[0] == '\0') {
		game_goto(g, &screen_welcome);
		return;
	}
	hof_path(g->save_dir, path, sizeof(path));
	if (hof_load(path, &h, g->err, sizeof(g->err)) != 0) {
		snprintf(g->msg, sizeof(g->msg), "%s", g->err);
		memset(&h, 0, sizeof(h));
	}
	memset(&e, 0, sizeof(e));
	e.score = career_score(g);
	e.rating = hof_rating(e.score);
	snprintf(e.name, sizeof(e.name), "%s", g->sim.player.name);
	snprintf(e.title, sizeof(e.title), "%s", g->sim.player.title);
	g->hof_new = hof_add(&h, &e);
	if (g->hof_new >= 0 &&
	    hof_save(path, &h, g->err, sizeof(g->err)) != 0)
		snprintf(g->msg, sizeof(g->msg), "%s", g->err);
	game_goto(g, &screen_hof);
}

static void
retire_event(struct game *g, const struct ui_event *ev)
{
	menu_event(g, ev, 2, NULL, retire_choose, &screen_cabin);
}

static void
retire_draw(struct game *g)
{
	draw(g, FONT_BODY, 80, 180, 480, game_str(g, S_HOF_QUESTION), 1);
}

const struct screen screen_retire = {
	"retire", parchment_enter, retire_event, NULL, retire_draw,
};

/*
 * Hall of Fame (pic/fame), with the original's layout: rows 25 pixels
 * apart from y 200; rank, name, then score columns.
 */
#define HOF_TOP		200
#define HOF_ROW		25

static void
hof_enter(struct game *g)
{
	game_background(g, "pic/fame");
	game_portrait(g, NULL);
	/* Light text on the red cloth, gold for the new entry. */
	g->ink[0] = g->ink[1] = g->ink[2] = 235;
	g->highlight[0] = 250;
	g->highlight[1] = 210;
	g->highlight[2] = 60;
}

static void
hof_event(struct game *g, const struct ui_event *ev)
{
	if (ev->type == EV_CLICK || ev->type == EV_KEY)
		game_goto(g, &screen_welcome);
}

static void
hof_draw(struct game *g)
{
	struct hof h;
	char path[600], buf[96], err[256];
	size_t i;
	int y;

	if (g->save_dir[0] == '\0')
		return;
	hof_path(g->save_dir, path, sizeof(path));
	if (hof_load(path, &h, err, sizeof(err)) != 0)
		return;
	g->selected = 0;	/* the new entry is option 0 of its own line */
	for (i = 0; i < h.count; i++) {
		y = HOF_TOP + (int)i * HOF_ROW;
		/* "~" makes the new entry the highlighted option. */
		snprintf(buf, sizeof(buf), "%s%zu) %s", (int)i == g->hof_new ?
		    "~" : "", i + 1, h.entry[i].name);
		draw(g, FONT_BODY, 120, y, 200, buf, (int)i == g->hof_new);
		draw(g, FONT_BODY, 300, y, 180, h.entry[i].title, 0);
		snprintf(buf, sizeof(buf), "%ld", h.entry[i].score);
		draw(g, FONT_BODY, 480, y, 80, buf, 0);
	}
	if (g->msg[0] != '\0')
		draw_centered(g, FONT_BODY, 450, g->msg);
}

const struct screen screen_hof = {
	"hof", hof_enter, hof_event, NULL, hof_draw,
};

/*
 * Sailing: the map around the ship, as the original shows it.  The view
 * is tile aligned and jumps when the ship nears an edge, looking ahead in
 * the direction of travel (VA 0x231b8).  One simulation tick per frame.
 */
#define VIEW_EDGE	50	/* pixels from the left or top edge */
#define VIEW_RIGHT	570
#define VIEW_BOTTOM	432
#define VIEW_AHEAD_X	192
#define VIEW_AHEAD_Y	128
#define VIEW_MAX_X	(600 * WORLD_TILE)	/* last origin, pixels */
#define VIEW_MAX_Y	(450 * WORLD_TILE)

/* Lookahead toward the side the heading points at, or 0 when it points
 * mostly along the other axis (within 0x20 of it). */
static int
lookahead(int angle, int amount)
{
	int a = (int)(signed char)(uint8_t)angle;

	if (a > 0x60 || a < -0x60 || (a > -0x20 && a < 0x20))
		return (0);
	return (a > 0 ? amount : -amount);
}

static int
clampi(int v, int lo, int hi)
{
	return (v < lo ? lo : v > hi ? hi : v);
}

static void
follow_ship(struct game *g, int force)
{
	const struct sim_fleet *f = &g->sim.fleet;
	int sx = f->x - g->view_x, sy = f->y - g->view_y;

	if (force || sx > VIEW_RIGHT || sx < VIEW_EDGE) {
		g->view_x = (f->x - FB_W / 2 +
		    lookahead(f->heading, VIEW_AHEAD_X)) / WORLD_TILE;
		g->view_x = clampi(g->view_x * WORLD_TILE, 0, VIEW_MAX_X);
	}
	if (force || sy > VIEW_BOTTOM || sy < VIEW_EDGE) {
		g->view_y = (f->y - FB_H / 2 +
		    lookahead(f->heading - 0x40, VIEW_AHEAD_Y)) / WORLD_TILE;
		g->view_y = clampi(g->view_y * WORLD_TILE, 0, VIEW_MAX_Y);
	}
}

/*
 * Clouds over the sea (VA 0x24523, 0x231b8, 0x297c4): (|month - 5| + 1)
 * / 2 of them, none in June, three in winter, each a random cloud.cur
 * frame (0, 1, 2 or 0 again).  Every sailing pass cloud i drifts with
 * the wind by (4i + 6) x 2, in eighths; once out of -100..640 across or
 * -40..480 down from the view, it comes back at the right edge at a
 * random height.  Each is drawn with its shadow 30 pixels left and down,
 * which darkens what it covers through the table at VA 0x3400c.
 */
#define CLOUD_SHADOW		30

static int
cloud_random(struct game *g, int n)
{
	g->cloud_rand = g->cloud_rand * 1103515245u + 12345u;
	return ((int)((g->cloud_rand >> 16) % (uint32_t)n));
}

static void
clouds_start(struct game *g)
{
	static const int frames[4] = { 0, 1, 2, 0 };
	int i;

	load_bank(g, "cur/cloud", 1, &g->cloud_spr);
	if (g->cloud_rand == 0)
		g->cloud_rand = (uint32_t)g->sim.day * 2654435761u + 1;
	for (i = 0; i < 3; i++) {
		g->clouds[i].frame = frames[cloud_random(g, 4)];
		g->clouds[i].x = g->clouds[i].y = 0;	/* off the view */
	}
}

static int
clouds_count(const struct game *g)
{
	int m = g->sim.day_of_year / 30 - 5;

	return (((m < 0 ? -m : m) + 1) / 2);
}

static int
cloud_sine(const struct game *g, int h, int v)
{
	int s = g->world->sine[h & 0x7F] * v >> 8;

	return ((h & 0x80) ? -s : s);
}

static void
clouds_move(struct game *g)
{
	int i, n = clouds_count(g), sx, sy, w = g->sim.wind;

	for (i = 0; i < n && i < 3; i++) {
		g->clouds[i].x += cloud_sine(g, w, (4 * i + 6) * 2) >> 3;
		g->clouds[i].y -= cloud_sine(g, w + 0x40, (4 * i + 6) * 2) >> 3;
		sx = g->clouds[i].x - g->view_x;
		sy = g->clouds[i].y - g->view_y;
		if (sx < -100 || sx > FB_W || sy < -40 || sy > FB_H) {
			g->clouds[i].x = g->view_x + FB_W;
			g->clouds[i].y = g->view_y + cloud_random(g, 440);
		}
	}
}

static void
clouds_draw(struct game *g)
{
	const uint8_t *shade = exe_at(g->layout, g->exe, g->exe_len,
	    g->layout->shade_va, 256);
	const struct frame *f;
	int i, n = clouds_count(g), x, y, sx, sy, px, py;

	for (i = 0; i < n && i < 3; i++) {
		if ((size_t)g->clouds[i].frame >= g->cloud_spr.count)
			continue;
		f = &g->cloud_spr.frames[g->clouds[i].frame];
		sx = g->clouds[i].x - g->view_x;
		sy = g->clouds[i].y - g->view_y;
		if (shade != NULL)
			for (y = 0; y < f->h; y++)
				for (x = 0; x < f->w; x++) {
					px = sx - CLOUD_SHADOW + x;
					py = sy + CLOUD_SHADOW + y;
					if (f->pixels[y * f->w + x] != 0 &&
					    px >= 0 && px < FB_W && py >= 0 &&
					    py < FB_H)
						g->fb.px[py * FB_W + px] =
						    shade[g->fb.px[py * FB_W + px]];
				}
		for (y = 0; y < f->h; y++)
			for (x = 0; x < f->w; x++) {
				px = sx + x;
				py = sy + y;
				if (f->pixels[y * f->w + x] != 0 && px >= 0 &&
				    px < FB_W && py >= 0 && py < FB_H)
					g->fb.px[py * FB_W + px] =
					    f->pixels[y * f->w + x];
			}
	}
}

/* Sailing sprites by ship size, as the game picks them. */
static const char *
ship_sprite(const struct sim_fleet *f)
{
	int i, c = 0;

	for (i = 0; i < f->nships; i++)
		if ((f->ships[i] & 7) > c)
			c = f->ships[i] & 7;
	return (c < 3 ? "spr/ship0" : c < 6 ? "spr/ship1" : "spr/ship2");
}

static void
sail_enter(struct game *g)
{
	struct blob b;

	image_free(&g->bg);
	game_portrait(g, NULL);
	memcpy(g->fb.pal, g->world->palette, sizeof(g->fb.pal));
	sea_init(&g->sea, g->fb.pal);
	bank_free(&g->ship);
	if (packs_load(g->packs, ship_sprite(&g->sim.fleet), &b) == 0) {
		bank_decode_runs(b.data, b.len, &g->ship);
		blob_free(&b);
	}
	g->turn = 0;
	g->ink[0] = g->ink[1] = g->ink[2] = 240;
	follow_ship(g, 1);
	clouds_start(g);
}

static void
sail_event(struct game *g, const struct ui_event *ev)
{
	if (ev->type == EV_KEY && ev->key == UI_KEY_LEFT)
		g->turn = -1;
	else if (ev->type == EV_KEY && ev->key == UI_KEY_RIGHT)
		g->turn = 1;
	else if (ev->type == EV_KEYUP &&
	    (ev->key == UI_KEY_LEFT || ev->key == UI_KEY_RIGHT))
		g->turn = 0;
	else if (ev->type == EV_KEY && ev->key == UI_KEY_ESCAPE)
		game_goto(g, &screen_cabin);
	else if (ev->type == EV_CHAR && (ev->ch == 't' || ev->ch == 'T'))
		g->sail_fast = !g->sail_fast;	/* VA 0x1b6ef */
}

/*
 * The sailing loop's pace (VA 0x231b8): each pass waits for the sound
 * driver's clock, 60.9 Hz (PIT divisor 0x4C90), to advance by 5, or by 1
 * once T has been pressed: 12.2 or 60.9 passes a second.  At 60 frames a
 * second, a pass every 5 frames or every frame.
 */
#define SAIL_FRAMES		5

static void
sail_tick(struct game *g)
{
	int town;

	if (g->fading != 0)
		return;		/* the ship waits for the screen */
	if (!g->sail_fast && ++g->sail_wait < SAIL_FRAMES)
		return;
	g->sail_wait = 0;
	g->sail_passes++;
	clouds_move(g);
	switch (sim_sail(&g->sim, g->turn, 1, &town)) {
	case SAIL_TOWN:
		g->sail_fast = 0;		/* VA 0x284b6 */
		g->arrive_stage = 0;
		game_goto(g, &screen_arrive);
		break;
	case SAIL_ENCOUNTER:
		g->turn = 0;
		game_goto(g, &screen_encounter);
		break;
	case SAIL_LAND:
	case SAIL_TICKS:
		break;
	}
	follow_ship(g, 0);
}

static void
sail_draw(struct game *g)
{
	const struct sim_fleet *f = &g->sim.fleet;
	const uint8_t *t;
	const struct frame *fr;
	char buf[128], from[64];
	int cx, cy, x0 = g->view_x / WORLD_TILE, y0 = g->view_y / WORLD_TILE;

	sea_animate(&g->sea, g->sail_passes, g->fb.pal);
	for (cy = 0; cy < FB_H / WORLD_TILE; cy++)
		for (cx = 0; cx < FB_W / WORLD_TILE; cx++) {
			struct image tile;

			t = g->world->tiles + WORLD_TILE * WORLD_TILE *
			    g->world->map[(y0 + cy) * WORLD_CELLS_W + x0 + cx];
			tile.w = tile.h = WORLD_TILE;
			tile.pixels = (uint8_t *)t;
			tile.has_palette = 0;
			fb_image(&g->fb, &tile, cx * WORLD_TILE, cy * WORLD_TILE,
			    0);
		}
	if (g->ship.count == 16) {
		fr = &g->ship.frames[f->heading >> 4];
		fb_frame(&g->fb, fr, f->x - g->view_x - fr->w / 2,
		    f->y - g->view_y - fr->h / 2);
	}
	clouds_draw(g);
	date_text(g, buf, sizeof(buf));
	fill(from, sizeof(from), game_word(g, W_SAILING, SAILING_FROM),
	    game_word(g, W_COMPASS, (size_t)(g->sim.wind >> 4)), NULL);
	snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
	    "   Wind %s   %s%d", from, game_word(g, W_SAILING, SAILING_KNOTS),
	    f->speed);
	draw(g, FONT_BODY, 10, 6, 620, buf, 0);
}

const struct screen screen_sail = {
	"sail", sail_enter, sail_event, sail_tick, sail_draw,
};

/*
 * Encounter: the game's dialogs over its pictures, "Sail ho!", then the
 * ship's portrait once she is identified, then her flag.  Sea battles are
 * a later phase; for now a battle ends the encounter with a notice.
 */
static const char *ship_portrait[9] = {
	"pic/s_pinn", "pic/s_sloop", "pic/s_barque", "pic/s_fluyt",
	"pic/s_merch", "pic/s_frig", "pic/s_war", "pic/s_gal", "pic/s_fast",
};

static const char *nation_colours[5] = {
	"pic/f-span", "pic/f-engl", "pic/f-french", "pic/f-dutch",
	"pic/fjolly01",
};

static void
encounter_background(struct game *g)
{
	const struct sim_encounter *e = &g->sim.encounter;

	switch (e->stage) {
	case ENC_IDENTIFIED:
		game_background(g, ship_portrait[e->type % 9]);
		break;
	case ENC_COLOURS:
		game_background(g, nation_colours[e->nation % 5]);
		break;
	default:
		game_background(g, "pic/sail-ho");
		break;
	}
	g->ink[0] = g->ink[1] = g->ink[2] = 235;
	g->highlight[0] = 250;
	g->highlight[1] = 210;
	g->highlight[2] = 60;
	if (e->stage == ENC_COLOURS) {
		/* Flag palettes have no yellow; their red reads well. */
		g->highlight[0] = 230;
		g->highlight[1] = 40;
		g->highlight[2] = 30;
	}
}

static void
encounter_enter(struct game *g)
{
	game_portrait(g, NULL);
	g->enc_result = -1;
	encounter_background(g);
}

static void
encounter_choose(struct game *g, int i)
{
	static const int sighted[2] = { ACT_INVESTIGATE, ACT_IGNORE };
	static const int colours[3] = { ACT_BATTLE, ACT_HAIL, ACT_IGNORE };
	enum encounter_result r;
	int action;

	action = g->sim.encounter.stage == ENC_COLOURS ? colours[i % 3] :
	    sighted[i % 2];
	r = sim_encounter_act(&g->sim, (enum encounter_action)action);
	if (r == RES_CONTINUE) {
		g->selected = 0;
		encounter_background(g);
		return;
	}
	if (r == RES_ESCAPED) {
		game_goto(g, &screen_sail);
		return;
	}
	if (r == RES_BATTLE) {
		game_goto(g, &screen_battle);
		return;
	}
	if (r == RES_NEWS) {
		game_background(g, "pic/shipnews");
		g->ink[0] = 240;	/* light, on the dark blue page */
		g->ink[1] = 220;
		g->ink[2] = 170;
	}
	g->enc_result = (int)r;	/* show the outcome until a key */
}

static void
encounter_event(struct game *g, const struct ui_event *ev)
{
	int count;

	if (g->enc_result >= 0) {
		if (ev->type == EV_CLICK || (ev->type == EV_KEY &&
		    ev->key == UI_KEY_ENTER))
			game_goto(g, g->sim.battle.active ? &screen_battle :
			    &screen_sail);
		return;
	}
	count = g->sim.encounter.stage == ENC_COLOURS ? 3 : 2;
	menu_event(g, ev, count, NULL, encounter_choose, NULL);
}

/* A dark band behind the text, so it reads over any picture. */
static void
text_band(struct game *g, int y, int h)
{
	fb_rect(&g->fb, 60, y, FB_W - 120, h, fb_nearest(&g->fb, 20, 15, 10));
}

/*
 * Ship portraits are sheets of 5 round views of about 200 pixels, and
 * flags sheets of 8 waving frames of 200x200 on a green key.  Show the
 * first view, and wave the flag.
 */
#define PORTRAIT_VIEW	200
#define FLAG_FRAME	200
#define FLAG_FRAMES	8

static void
encounter_picture(struct game *g)
{
	const struct sim_encounter *e = &g->sim.encounter;
	int f, key;

	if (e->stage != ENC_IDENTIFIED && e->stage != ENC_COLOURS)
		return;		/* "Sail ho!" is a full picture */
	fb_clear(&g->fb, fb_nearest(&g->fb, 0, 0, 0));
	if (e->stage == ENC_IDENTIFIED) {
		fb_image_part(&g->fb, &g->bg, 0, 0, PORTRAIT_VIEW,
		    PORTRAIT_VIEW, (FB_W - PORTRAIT_VIEW) / 2, 50, -1);
		return;
	}
	f = (int)(g->ticks / 6) % FLAG_FRAMES;
	key = fb_nearest(&g->fb, 0, 255, 0);
	fb_image_part(&g->fb, &g->bg, (f % 4) * FLAG_FRAME,
	    (f / 4) * FLAG_FRAME, FLAG_FRAME, FLAG_FRAME,
	    (FB_W - FLAG_FRAME) / 2, 50, key);
}

static void
encounter_draw(struct game *g)
{
	const struct sim_encounter *e = &g->sim.encounter;
	char text[512], waters[160];
	const struct sim_news *n;
	size_t i, kept;
	int y, w;

	if (g->enc_result < 0)
		encounter_picture(g);

	if (g->enc_result == RES_NEWS) {
		/* The ship's news (VA 0x1e78d): the title and the latest
		 * items left of the picture, or "(no entries)". */
		w = text_width(&g->font[FONT_TITLE],
		    game_word(g, W_NEWS, NEWS_TITLE));
		draw(g, FONT_TITLE, 160 - w / 2, 40, 300,
		    game_word(g, W_NEWS, NEWS_TITLE), 0);
		kept = sim_news_kept(&g->sim);
		y = 100;
		if (kept == 0)
			draw(g, FONT_BODY, 20, y, 290,
			    game_word(g, W_NEWS, NEWS_NONE), 0);
		for (i = kept; i > 0 && i + NEWS_SHOWN > kept && y < 420;
		    i--) {
			n = sim_news_at(&g->sim, i - 1);
			news_text(g, n, text, sizeof(text));
			y += draw(g, FONT_BODY, 20, y, 290, text, 0) + 8;
		}
		continue_prompt(g);
		return;
	}
	if (g->enc_result >= 0) {
		y = 256;
		switch (g->enc_result) {
		case RES_OVERTAKEN:
			y += draw(g, FONT_BODY, 200, y, 340,
			    game_str(g, S_OVERTAKEN), 0);
			break;
		case RES_UNANSWERED:
			y += draw(g, FONT_BODY, 200, y, 340,
			    game_str(g, S_UNANSWERED), 0);
			break;
		}

		continue_prompt(g);
		return;
	}
	switch (e->stage) {
	case ENC_SIGHTED:
		waters[0] = '\0';
		if (g->sim.player.difficulty < 2 && e->town >= 0)
			fill(waters, sizeof(waters), game_str(g, S_WATERS),
			    game_word(g, W_NATION,
			    (size_t)g->sim.towns[e->town].nation & 3), NULL);
		fill(text, sizeof(text), game_str(g, S_SAIL_HO), waters, NULL);
		break;
	case ENC_IDENTIFIED:
		fill(text, sizeof(text), game_str(g, S_LOOKS_LIKE),
		    game_word(g, W_A_SHIP, (size_t)e->type), NULL);
		break;
	default:
		if (e->hostile)
			snprintf(text, sizeof(text), "%s", game_str(g, S_CHASING));
		else
			fill(text, sizeof(text), game_str(g, S_COLOURS),
			    game_word(g, W_COLOURS, (size_t)e->nation), NULL);
		break;
	}
	/* No box: the text is written on the screen (VA 0x10560): "Sail
	 * ho!" at (100, 300), the later questions at (200, 256), all up
	 * to x 540. */
	if (e->stage == ENC_SIGHTED)
		draw(g, FONT_BODY, 100, 300, 440, text, 1);
	else
		draw(g, FONT_BODY, 200, 256, 340, text, 1);
}

const struct screen screen_encounter = {
	"encounter", encounter_enter, encounter_event, NULL, encounter_draw,
};

/*
 * Sea battle (pic/sbat): a 480x480 battle area, the encounter's corner of
 * the map drawn with the 32x32 battle tiles, and a panel on the right
 * with each side's men, guns and damage picture, and the wind.  Left and
 * Right steer, Enter or Space fires, Up and Down set the sails.
 */
#define AREA		480
#define AREA_CELLS	(AREA / 32)
#define PANEL_X		492

static const char *damage_picture[9] = {
	"cur/pinnabtl", "cur/sloopbtl", "cur/barqubtl", "cur/cargobtl",
	"cur/merchbtl", "cur/frigabtl", "cur/wargabtl", "cur/gallebtl",
	"cur/fastgbtl",
};

static void
load_bank(struct game *g, const char *id, int raw, struct sprite_bank *b)
{
	struct blob bl;

	bank_free(b);
	if (packs_load(g->packs, id, &bl) != 0)
		return;
	if (raw)
		bank_decode_raw(bl.data, bl.len, b);
	else
		bank_decode_runs(bl.data, bl.len, b);
	blob_free(&bl);
}

static void
battle_sprites(struct game *g, int side, int type)
{
	static const char *a[3] = { "cur/ship0a", "cur/ship1a", "cur/ship2a" };
	static const char *b[3] = { "cur/ship0b", "cur/ship1b", "cur/ship2b" };
	int size = (type & 7) < 3 ? 0 : (type & 7) < 6 ? 1 : 2;

	load_bank(g, a[size], 1, &g->btl_ship[side][0]);
	load_bank(g, b[size], 1, &g->btl_ship[side][1]);
	load_bank(g, side == 1 && g->sim.battle.fort ? "cur/fortbtl" :
	    damage_picture[type % 9], 1, &g->btl_damage[side]);
}

static void
battle_enter(struct game *g)
{
	struct blob b;

	game_background(g, "pic/sbat");
	game_portrait(g, NULL);
	battle_sprites(g, 0, g->sim.battle.you.type);
	battle_sprites(g, 1, g->sim.battle.foe.type);
	tileset_free(&g->btl_tiles);
	if (packs_load(g->packs, "tiles/battle", &b) == 0) {
		tileset_decode(b.data, b.len, &g->btl_tiles);
		blob_free(&b);
	}
	g->btl_fire = g->btl_sails = 0;
	g->btl_stage = g->sim.battle.outcome == BAT_NONE ? 0 : 1;
	if (g->sim.battle.outcome == BAT_BOARDING &&
	    g->sim.sword.result == SWORD_WON) {
		g->btl_stage = 2;	/* back from the fight: the prize */
		g->selected = 0;
	}
	/* Several ships: which one to command, before the first tick. */
	if (g->btl_stage == 0 && g->sim.battle.tick == 0 &&
	    g->sim.fleet.nships > 1) {
		g->btl_stage = BTL_COMMAND;
		g->selected = 0;
	}
	g->turn = 0;
	g->ink[0] = g->ink[1] = g->ink[2] = 240;
}

static void
command_choose(struct game *g, int i)
{
	sim_battle_command(&g->sim, i);
	battle_sprites(g, 0, g->sim.battle.you.type);
	g->btl_stage = 0;
}

static void
battle_finish(struct game *g, int keep)
{
	int landed = g->sim.battle.fort &&
	    g->sim.battle.outcome == BAT_LANDED;
	int aboard = g->sim.battle.crew_ashore;

	g->btl_final = (int)sim_battle_finish(&g->sim, keep);
	if (landed) {
		/* The assault follows (VA 0x18c2c). */
		g->assault_town = g->sim.encounter.town;
		g->assault_aboard = aboard;
		sim_fort_battle_garrison(&g->sim, g->assault_town);
		game_goto(g, &screen_assault);
		return;
	}
	game_goto(g, &screen_sail);
}

/* "Shall we keep her?": yes, send a prize crew; no, plunder and sink. */
static void
prize_choose(struct game *g, int i)
{
	battle_finish(g, i == 0);
}

static void
battle_event(struct game *g, const struct ui_event *ev)
{
	int outcome = g->sim.battle.outcome;

	if (g->btl_stage == 2) {
		menu_event(g, ev, 2, NULL, prize_choose, NULL);
		return;
	}
	if (g->btl_stage == BTL_COMMAND) {
		menu_event(g, ev, g->sim.fleet.nships, NULL, command_choose,
		    NULL);
		return;
	}
	if (g->btl_stage == 1) {
		if (!((ev->type == EV_KEY && ev->key == UI_KEY_ENTER) ||
		    ev->type == EV_CLICK))
			return;
		if (outcome == BAT_SURRENDER) {
			g->btl_stage = 2;
			g->selected = 0;
		} else
			battle_finish(g, 0);
		return;
	}
	if (ev->type == EV_KEY && ev->key == UI_KEY_LEFT)
		g->turn = -1;
	else if (ev->type == EV_KEY && ev->key == UI_KEY_RIGHT)
		g->turn = 1;
	else if (ev->type == EV_KEYUP &&
	    (ev->key == UI_KEY_LEFT || ev->key == UI_KEY_RIGHT))
		g->turn = 0;
	else if ((ev->type == EV_KEY && ev->key == UI_KEY_ENTER) ||
	    (ev->type == EV_CHAR && ev->ch == ' '))
		g->btl_fire = 1;
	else if (ev->type == EV_KEY && ev->key == UI_KEY_UP)
		g->btl_sails = 1;
	else if (ev->type == EV_KEY && ev->key == UI_KEY_DOWN)
		g->btl_sails = -1;
}

/* The text of a battle's end (CSTR 167-170, 197, 198). */
static void
battle_end_text(struct game *g, char *text, size_t len)
{
	const struct sim_battle *bt = &g->sim.battle;

	switch (bt->outcome) {
	case BAT_PLAYER_SUNK:
		snprintf(text, len, "%s", game_str(g, S_YOU_SUNK));
		if (g->sim.fleet.nships == 1) {
			/* The last ship (VA 0x1f2a1): washed ashore, then
			 * picked up by former crew members. */
			char more[400];

			fill(more, sizeof(more), game_str(g, S_PICKED_UP),
			    game_word(g, W_CAPTAIN,
			    (size_t)g->sim.player.difficulty), NULL);
			snprintf(text, len, "%s\r%s\r%s",
			    game_str(g, S_YOU_SUNK), game_str(g, S_ASHORE),
			    more);
		}
		break;
	case BAT_ENEMY_SUNK:
		snprintf(text, len, "%s", game_str(g, S_FOE_SUNK));
		break;
	case BAT_SUNSET:
		snprintf(text, len, "%s", game_str(g, bt->fort ?
		    S_FORT_SUNSET : S_SUNSET));
		break;
	case BAT_SURRENDER:
		snprintf(text, len, "%s", game_str(g, S_STRUCK));
		break;
	case BAT_BEACHED:
		snprintf(text, len, "%s", game_str(g, S_BEACHED));
		break;
	case BAT_ESCAPED:
		/* Out of range (VA 0xe82a): the enemy pulled away when it
		 * was fleeing (decision 2), otherwise the player did. */
		snprintf(text, len, "%s", game_str(g, bt->fort ?
		    S_FORT_DRIVES_OFF : bt->decision == 2 ? S_FOE_ESCAPED :
		    S_YOU_ESCAPED));
		break;
	default:
		text[0] = '\0';
		break;
	}
}

/* A battle's end on its own picture: the sunset's text at (40, 150) up to
 * x 280, the others at (100, 300) up to x 560 (VA 0xe67b-0xe883). */
static void
battle_end_draw(struct game *g)
{
	char text[600];

	if (g->sim.battle.outcome == BAT_BEACHED) {
		fb_clear(&g->fb, fb_nearest(&g->fb, 0, 0, 0));
		fb_image_part(&g->fb, &g->bg, 0, 0, PORTRAIT_VIEW,
		    PORTRAIT_VIEW, (FB_W - PORTRAIT_VIEW) / 2, 50, -1);
	}
	battle_end_text(g, text, sizeof(text));
	if (g->sim.battle.outcome == BAT_SUNSET)
		draw(g, FONT_BODY, 40, 150, 240, text, 0);
	else
		draw(g, FONT_BODY, 100, 300, 460, text, 0);
	continue_prompt(g);
}

/* The picture of a battle's end (VA 0xe67b-0xe918), or NULL to stay on
 * the battle. */
static const char *
battle_end_picture(int outcome)
{
	switch (outcome) {
	case BAT_PLAYER_SUNK:
	case BAT_ENEMY_SUNK:
		return ("pic/sinking");
	case BAT_ESCAPED:
		return ("pic/outrun");
	case BAT_SUNSET:
		return ("pic/night");
	case BAT_BEACHED:		/* the enemy's portrait (VA 0xe9e6) */
		return ("beached");
	default:
		return (NULL);
	}
}

static void
battle_tick(struct game *g)
{
	if (g->fading != 0 || g->btl_stage != 0)
		return;
	sim_battle_tick(&g->sim, g->turn, g->btl_fire, g->btl_sails);
	g->btl_fire = 0;
	g->btl_sails = 0;
	if (g->sim.battle.outcome == BAT_BOARDING) {
		g->turn = 0;
		g->btl_stage = 1;
		game_goto(g, &screen_sword);
	} else if (g->sim.battle.outcome == BAT_LANDED) {
		g->turn = 0;
		g->btl_stage = 1;
		battle_finish(g, 0);
	} else if (g->sim.battle.outcome != BAT_NONE) {
		g->btl_stage = 1;
		g->turn = 0;
		if (g->sim.battle.outcome == BAT_BEACHED) {
			game_background(g, ship_portrait[g->sim.battle.foe.type %
			    9]);
			g->ink[0] = g->ink[1] = g->ink[2] = 240;
		} else if (battle_end_picture(g->sim.battle.outcome) != NULL) {
			game_background(g,
			    battle_end_picture(g->sim.battle.outcome));
			/* The ink is palette entry 2 (VA 0xe8a9). */
			if (g->bg.pixels != NULL)
				memcpy(g->ink, &g->bg.palette[STORY_INK * 3],
				    3);
		}
	}
}

static void
battle_ship_draw(struct game *g, int side, const struct battle_ship *b)
{
	int idx = ((b->heading + 8) >> 4) & 15;
	const struct sprite_bank *bank = &g->btl_ship[side][idx / 8];
	const struct frame *f;

	if (bank->count != 8)
		return;
	f = &bank->frames[idx % 8];
	/* Raw frames: skip the background colour 0, clip to the area. */
	{
		int x, y, sx = b->x / 256 - f->w / 2, sy = b->y / 256 - f->h / 2;

		for (y = 0; y < f->h; y++)
			for (x = 0; x < f->w; x++) {
				uint8_t c = f->pixels[y * f->w + x];

				if (c != 0 && sx + x >= 0 && sx + x < AREA &&
				    sy + y >= 0 && sy + y < AREA)
					g->fb.px[(sy + y) * FB_W + sx + x] = c;
			}
	}
}

static void
battle_side_text(struct game *g, int y, const struct battle_ship *b,
    int status)
{
	char buf[96];

	draw(g, FONT_SMALL, PANEL_X, y, 140,
	    game_word(g, W_SHIP_TYPE, (size_t)(b->type % 10)), 0);
	snprintf(buf, sizeof(buf), "%s%d  %s%d",
	    game_word(g, W_SAILING, SAILING_MEN), b->crew,
	    game_word(g, W_SAILING, SAILING_GUNS), b->guns);
	draw(g, FONT_SMALL, PANEL_X, y + 72, 140, buf, 0);
	snprintf(buf, sizeof(buf), "%s", game_word(g, W_SAILING, (size_t)(
	    status >= 0 ? status : b->full_sails ? SAILING_FULL_SAILS :
	    SAILING_BATTLE_SAILS)));
	draw(g, FONT_SMALL, PANEL_X, y + 102, 140, buf, 0);
}

static void
battle_draw(struct game *g)
{
	const struct sim_battle *bt = &g->sim.battle;
	char text[600], from[64], tons[16], guns[16];
	int cx, cy, lvl;

	if (g->btl_stage == 1 && battle_end_picture(bt->outcome) != NULL) {
		battle_end_draw(g);
		return;
	}
	/* The battle's screen: 15 x 15 battle tiles (VA 0x2a9e8). */
	if (g->btl_tiles.tile_w == 32)
		for (cy = 0; cy < BATTLE_SCREEN; cy++)
			for (cx = 0; cx < BATTLE_SCREEN; cx++) {
				struct image t;

				t.w = t.h = 32;
				t.pixels = g->btl_tiles.pixels + 32 * 32 *
				    g->world->battle_screens[bt->screen][cy *
				    BATTLE_SCREEN + cx];
				t.has_palette = 0;
				fb_image(&g->fb, &t, cx * 32, cy * 32, 0);
			}
	battle_ship_draw(g, 0, &bt->you);
	if (!bt->fort)		/* a fort is drawn in its screen (VA 0x8db6) */
		battle_ship_draw(g, 1, &bt->foe);
	if (bt->you.shot.ticks > 0)
		fb_rect(&g->fb, bt->you.shot.x / 256 - 1, bt->you.shot.y / 256 -
		    1, 3, 3, fb_nearest(&g->fb, 20, 20, 20));
	if (bt->foe.shot.ticks > 0)
		fb_rect(&g->fb, bt->foe.shot.x / 256 - 1, bt->foe.shot.y / 256 -
		    1, 3, 3, fb_nearest(&g->fb, 20, 20, 20));

	/* The panel: the enemy above, the player below, the wind between. */
	battle_side_text(g, 14, &bt->foe, -1);
	/* While the sails change, "Raising sail" or "Lowering sail" (VA
	 * 0xef06, event 10); reloading waits for them. */
	battle_side_text(g, 358, &bt->you, bt->you.sail_timer > 0 ?
	    (bt->you.full_sails ? SAILING_LOWERING : SAILING_RAISING) :
	    bt->you.reload > 0 ? SAILING_RELOADING : -1);
	/* A fort's damage shows in four stages of 20 (VA 0xebd7). */
	lvl = bt->fort ? (bt->fort_damage / 20 > 3 ? 3 : bt->fort_damage /
	    20) : bt->foe.damage / 16 > 4 ? 4 : bt->foe.damage / 16;
	if ((size_t)lvl < g->btl_damage[1].count)
		fb_frame(&g->fb, &g->btl_damage[1].frames[lvl], 489, 137);
	lvl = bt->you.damage / 16 > 4 ? 4 : bt->you.damage / 16;
	if (g->btl_damage[0].count == 5)
		fb_frame(&g->fb, &g->btl_damage[0].frames[lvl], 489, 258);
	fill(from, sizeof(from), game_word(g, W_SAILING, SAILING_FROM),
	    game_word(g, W_COMPASS, (size_t)(bt->wind >> 4)), NULL);
	draw(g, FONT_SMALL, PANEL_X + 40, 233, 100, from, 0);

	if (g->btl_stage == 0)
		return;
	if (g->btl_stage == BTL_COMMAND) {
		/* "Which ship will you command?" (CSTR 286) and the fleet;
		 * the original picks on its fleet panel (VA 0xffae). */
		int i;

		text_band(g, 160, 290);
		snprintf(text, sizeof(text), "%s", game_str(g, S_COMMAND));
		for (i = 0; i < g->sim.fleet.nships; i++) {
			strncat(text, "\r~", sizeof(text) - strlen(text) - 1);
			strncat(text, game_word(g, W_SHIP_TYPE,
			    (size_t)(g->sim.fleet.ships[i] % 9)),
			    sizeof(text) - strlen(text) - 1);
		}
		draw(g, FONT_BODY, 80, 170, 400, text, 1);
		return;
	}
	if (g->btl_stage == 2) {
		text_band(g, 160, 290);
		/* "%sa %s ton %s with %s guns... space for %s tons" (VA
		 * 0xdb7b): 20 tons per class step, guns rounded up to even,
		 * and the fleet's room (no cargo is carried yet). */
		{
			const char *arg[5];
			char space[16];
			int i, room = 0;

			for (i = 0; i < g->sim.fleet.nships; i++)
				room += ((g->sim.fleet.ships[i] & 0xF) * 2 + 2) *
				    10;
			snprintf(tons, sizeof(tons), "%d",
			    (bt->foe.type & 0xF) * 20 + 20);
			snprintf(guns, sizeof(guns), "%d",
			    (bt->foe.guns + 1) & ~1);
			snprintf(space, sizeof(space), "%d", room);
			arg[0] = "";
			arg[1] = tons;
			arg[2] = game_word(g, W_SHIP_TYPE,
			    (size_t)(bt->foe.type % 10));
			arg[3] = guns;
			arg[4] = space;
			fill_args(text, sizeof(text), game_str(g, S_PRIZE), arg,
			    5);
		}
		draw(g, FONT_BODY, 80, 170, 400, text, 1);
		return;
	}
	/* The colours struck: written over the battle, no box. */
	battle_end_text(g, text, sizeof(text));
	draw(g, FONT_BODY, 100, 300, 460, text, 0);
	continue_prompt(g);
}

const struct screen screen_battle = {
	"battle", battle_enter, battle_event, battle_tick, battle_draw,
};

/*
 * Sword fight (boarding): the boarding picture, the weapon choice, then
 * the fight on the ship's deck.  The arrows move the fighter on a 3 x 3
 * pad, the opponent to the left: toward him attacks (a lunge, or a slash
 * with Shift), away steps back, up and down pick the line.  Clicking the
 * pad on the deck does the same.
 */
#define S_CHOOSE_WEAPON		85
#define W_WEAPON		70	/* Rapier, Longsword, Cutlass */
#define W_TITLE			74	/* Guard... Captain, First Mate */
#define SW_Y			140	/* fighters' line on the screen */
#define SW_FALL_DROP		24
#define SW_PAD_X		283	/* the arrow pad on pic/shipdeck */
#define SW_PAD_Y		291
#define SW_PAD_CELL		24
#define SW_YOU_FLAG_X		584
#define SW_FOE_FLAG_X		10
#define H_LEFT			1
#define H_RIGHT			2
#define H_UP			4
#define H_DOWN			8
#define H_SHIFT			16

static const char *sw_layers[2][6] = {
	{ "grp/mybody", NULL, "grp/myhead", "grp/mychest", "grp/mylegs",
	    NULL },
	{ "grp/hebody", NULL, "grp/hehead", "grp/hechest", "grp/helegs",
	    "grp/hehead1" },
};
static const char *sw_weapon_layers[2][SWORD_WEAPONS] = {
	{ "grp/myrapier", "grp/mysword", "grp/mycutlas" },
	{ "grp/herapier", "grp/hesword", "grp/hecutlas" },
};

static void
load_group(struct game *g, const char *id, struct sprite_group *out)
{
	struct blob b;

	group_free(out);
	if (id == NULL || packs_load(g->packs, id, &b) != 0)
		return;
	group_decode(b.data, b.len, out);
	blob_free(&b);
}

/* The story's fight (VA 0x14630): 200 men of factor 32 against 20 of
 * 16, no weapon to choose and nothing at stake. */
static void
sword_story_start(struct game *g)
{
	struct sword_setup in;

	memset(&in, 0, sizeof(in));
	in.kind = SWORD_STORY;
	in.you_men = 200;
	in.you_factor = 32;
	in.foe_men = 20;
	in.foe_factor = 16;
	in.title = 3;
	in.nation = 5;
	sim_sword_start(&g->sim, &in);
	game_background(g, "pic/shipdeck");
	g->ink[0] = g->ink[1] = g->ink[2] = 240;
	g->sw_stage = 2;
	g->sw_held = 0;
}

static void
sword_enter(struct game *g)
{
	struct blob b;

	g->sw_stage = 0;
	g->sw_held = 0;
	g->selected = SWORD_WEAPONS - 1;	/* the cutlass, named */
	g->sw_named = 1;
	game_background(g, "pic/boarding");
	g->ink[0] = g->ink[1] = g->ink[2] = 240;
	image_free(&g->sw_weapons);
	if (packs_load(g->packs, "img/wpnsel", &b) == 0) {
		img_decode(b.data, b.len, &g->sw_weapons);
		blob_free(&b);
	}
	load_bank(g, "spr/miscswrd", 0, &g->sw_misc);
	if (g->sw_guard) {
		/* The weapon is chosen on the alley itself (VA 0x16205). */
		game_background(g, "pic/alley");
		g->sw_stage = 1;
	}
	if (g->sw_practice) {		/* on the deck (scene 3) */
		game_background(g, "pic/shipdeck");
		g->sw_stage = 1;
	}
	if (g->sw_assault) {		/* the fort, or the alley */
		game_background(g, g->sim.towns[g->assault_town].forts > 0 ?
		    "pic/fort" : "pic/alley");
		g->sw_stage = 1;
	}
	if (g->sw_story) {
		sword_story_start(g);
		sword_load(g);
	}
}

/* The fight's assets, once both weapons are known. */
static void
sword_load(struct game *g)
{
	int side, i;

	for (side = 0; side < 2; side++)
		for (i = 0; i < 6; i++)
			load_group(g, i == 1 ? sw_weapon_layers[side][side == 0 ?
			    g->sim.sword.you.weapon : g->sim.sword.foe.weapon] :
			    side == 1 && i == 5 && g->sim.sword.scene == 0 ?
			    "grp/hehead2" : side == 1 && i == 5 &&
			    (g->sim.sword.scene == 1 || g->sim.sword.scene == 2) ?
			    "grp/hehead3" :
			    sw_layers[side][i], &g->sw_parts[side][i]);
	g->sw_flag_y[0] = g->sw_flag_y[1] = 0;
	g->sw_wait = 1;
	g->ink[0] = g->ink[1] = g->ink[2] = 240;	/* the fight's panel */
}

/* The blade under (x, y) on the weapon choice (VA 0x33628), or -1. */
static int
weapon_hit(const struct game *g, int x, int y)
{
	const uint8_t *p;
	int i, k, r[4];

	for (i = 0; i < SWORD_WEAPONS; i++) {
		p = exe_at(g->layout, g->exe, g->exe_len,
		    g->layout->weapon_spots_va + (uint32_t)i * 10, 8);
		if (p == NULL)
			continue;
		for (k = 0; k < 4; k++)
			r[k] = (int16_t)(p[2 * k] | p[2 * k + 1] << 8);
		if (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3])
			return (i);
	}
	return (-1);
}

/* The move the held keys ask for. */
static int
sword_code(int held)
{
	int line = held & H_UP ? 2 : held & H_DOWN ? 1 : 0;

	if (held & H_LEFT)
		return ((held & H_SHIFT ? SWM_SLASH : SWM_LUNGE) + line);
	if (held & H_RIGHT)
		return (SWM_BACK + line);
	return (SWM_GUARD + line);
}

static int
key_bit(int key)
{
	switch (key) {
	case UI_KEY_LEFT:
		return (H_LEFT);
	case UI_KEY_RIGHT:
		return (H_RIGHT);
	case UI_KEY_UP:
		return (H_UP);
	case UI_KEY_DOWN:
		return (H_DOWN);
	case UI_KEY_SHIFT:
		return (H_SHIFT);
	case UI_KEY_UP_LEFT:
		return (H_UP | H_LEFT);
	case UI_KEY_UP_RIGHT:
		return (H_UP | H_RIGHT);
	case UI_KEY_DOWN_LEFT:
		return (H_DOWN | H_LEFT);
	case UI_KEY_DOWN_RIGHT:
		return (H_DOWN | H_RIGHT);
	default:
		return (0);
	}
}

static void
sword_outcome_done(struct game *g)
{
	if (g->sw_story) {
		g->sw_story = 0;
		g->sim.sword.active = 0;
		game_goto(g, &screen_story);
		return;
	}
	if (g->sw_practice) {
		g->sw_practice = 0;
		sim_practice_end(&g->sim);
		game_goto(g, &screen_cabin);
		return;
	}
	if (g->sw_assault) {
		int r = sim_assault_end(&g->sim, g->assault_town);

		g->sw_assault = 0;
		if (r == 1) {
			sim_sack(&g->sim, g->assault_town, &g->sack);
			g->sack_stage = 0;
			game_goto(g, &screen_sack);
		} else
			game_goto(g, r < 0 ? &screen_prison : &screen_sail);
		return;
	}
	if (g->sw_guard) {
		g->sw_guard = 0;
		/* Back to sea either way (VA 0x195f5); a loss or a retreat
		 * is a disgrace (VA 0x16384). */
		game_goto(g, sim_duel_end(&g->sim) ? &screen_disgrace :
		    &screen_sail);
		return;
	}
	if (g->sim.sword.result == SWORD_WON)
		game_goto(g, &screen_battle);	/* the prize */
	else
		battle_finish(g, 0);
}

static void
sword_event(struct game *g, const struct ui_event *ev)
{
	int enter = (ev->type == EV_KEY && ev->key == UI_KEY_ENTER) ||
	    ev->type == EV_CLICK;
	int bit, cx, cy;

	switch (g->sw_stage) {
	case 0:
		if (enter) {
			game_background(g, "pic/shipdeck");
			g->ink[0] = g->ink[1] = g->ink[2] = 240;
			g->sw_stage = 1;
		}
		return;
	case 1:
		/* The blades left to right are the rapier, the cutlass and
		 * the longsword: the keys step along them without wrapping;
		 * pointing at a blade names it, clicking it takes it; Enter
		 * or Escape take the one named last (VA 0x16f03). */
		bit = ev->type == EV_MOVE || ev->type == EV_CLICK ?
		    weapon_hit(g, ev->x, ev->y) : -1;
		if (ev->type == EV_MOVE) {
			if (bit >= 0)
				g->selected = bit;
			g->sw_named = bit >= 0;
			return;
		}
		if (ev->type == EV_KEY && (ev->key == UI_KEY_LEFT ||
		    ev->key == UI_KEY_UP)) {
			g->selected = g->selected == 1 ? 2 : g->selected == 2 ?
			    0 : g->selected;
			g->sw_named = 1;
			return;
		}
		if (ev->type == EV_KEY && (ev->key == UI_KEY_RIGHT ||
		    ev->key == UI_KEY_DOWN)) {
			g->selected = g->selected == 0 ? 2 : g->selected == 2 ?
			    1 : g->selected;
			g->sw_named = 1;
			return;
		}
		if (ev->type == EV_CLICK) {
			if (bit < 0)
				return;
			g->selected = bit;
		} else if (!(ev->type == EV_KEY && (ev->key == UI_KEY_ENTER ||
		    ev->key == UI_KEY_ESCAPE)))
			return;
		if (g->sw_assault) {
			sim_assault_start(&g->sim, g->assault_town,
			    g->assault_aboard, g->selected);
			sword_load(g);
			g->sw_stage = 2;
		} else if (g->sw_practice) {
			sim_practice_start(&g->sim, g->selected);
			sword_load(g);
			g->sw_stage = 2;
		} else if (g->sw_guard) {
			sim_guard_duel(&g->sim, g->sim.fleet.leaving,
			    g->selected);
			sword_load(g);
			g->sw_stage = 2;
		} else {
			sim_board(&g->sim, g->selected);
			sword_load(g);
			g->sw_stage = 2;
		}
		return;
	case 2:
		if (ev->type == EV_KEY && (bit = key_bit(ev->key)) != 0) {
			g->sw_held |= bit;
			if (bit != H_SHIFT)
				sim_sword_input(&g->sim, sword_code(g->sw_held));
		} else if (ev->type == EV_KEYUP)
			g->sw_held &= ~key_bit(ev->key);
		else if (ev->type == EV_CLICK) {
			cx = (ev->x - SW_PAD_X) / SW_PAD_CELL;
			cy = (ev->y - SW_PAD_Y) / SW_PAD_CELL;
			if (ev->x >= SW_PAD_X && ev->y >= SW_PAD_Y && cx < 3 &&
			    cy < 3)
				sim_sword_input(&g->sim, sword_code(
				    (cx == 0 ? H_LEFT : cx == 2 ? H_RIGHT : 0) |
				    (cy == 0 ? H_UP : cy == 2 ? H_DOWN : 0)));
		}
		return;
	default:
		if (enter)
			sword_outcome_done(g);
		return;
	}
}

static void
sword_tick(struct game *g)
{
	const struct sword_tables *t;

	if (g->fading != 0 || g->sw_stage != 2 || g->world == NULL)
		return;
	if (--g->sw_wait > 0)
		return;
	/* A fight tick lasts 9-12 units of the sound driver's 60.9 Hz
	 * clock: taken as frames. */
	t = &g->world->sword;
	g->sw_wait = t->delay[g->sim.sword.level & 3];
	if (g->sw_wait < 1)
		g->sw_wait = 1;
	sim_sword_tick(&g->sim);
	if (g->sim.sword.result != SWORD_NONE) {
		if (g->sim.sword.result == SWORD_WON || g->sw_story ||
		    g->sw_practice || g->sw_assault)
			sword_outcome_done(g);
		else
			g->sw_stage = 3;
	}
}

static void
sword_fighter(struct game *g, int side, const struct sword_side *f)
{
	static const int order[2][6] = {
		{ 0, 1, 2, 3, 4, -1 },	/* body, weapon, wounds */
		{ 0, 5, 1, 2, 3, 4 },	/* body, head, weapon, wounds */
	};
	const struct sword_tables *t = &g->world->sword;
	int move = f->move < 0 ? 0 : f->move & (SWORD_MOVES - 1);
	int frame = f->frame, y = SW_Y, i, layer;
	const struct sprite_group *grp;

	if (frame >= t->frames[move])
		frame = t->frames[move] - 1;
	if (move == SWM_FALL && frame > 2)
		y -= SW_FALL_DROP;
	for (i = 0; i < 6; i++) {
		layer = order[side][i];
		/* The opponent's head is its own layer, always drawn: the
		 * deck's head here (hehead1; alleys and forts have theirs,
		 * VA 0x15f84). */
		if (layer < 0 ||
		    (layer >= 2 && layer <= 4 && f->wounds[layer - 2] == 0))
			continue;
		grp = &g->sw_parts[side][layer];
		if ((size_t)move < grp->count &&
		    (size_t)frame < grp->banks[move].count)
			fb_frame(&g->fb, &grp->banks[move].frames[frame], f->x,
			    y);
	}
}

static void
sword_flag(struct game *g, int side, int x, int frame, int bar)
{
	const struct sword_tables *t = &g->world->sword;
	int target = t->flag_y[bar / 8 < 0 ? 0 : bar / 8 > 5 ? 5 : bar / 8];
	int *y = &g->sw_flag_y[side];

	if (*y == 0)
		*y = target;
	else if (*y < target)
		*y += 2;
	else if (*y > target)
		*y -= 2;
	if ((size_t)frame < g->sw_misc.count)
		fb_frame(&g->fb, &g->sw_misc.frames[frame], x, *y);
}

static void
sword_draw(struct game *g)
{
	const struct sim_sword *sw = &g->sim.sword;
	char text[600], label[96];
	int w, nation;

	switch (g->sw_stage) {
	case 0:
		/* On the picture, no box (VA 0xf10a): at (274, 306) up to
		 * x 560, in palette entry 2. */
		if (g->bg.pixels != NULL)
			memcpy(g->ink, &g->bg.palette[STORY_INK * 3], 3);
		fill(text, sizeof(text), game_str(g, S_BOARDING),
		    game_word(g, W_SAILING, SAILING_CAPTAIN), NULL);
		draw(g, FONT_BODY, 274, 306, 286, text, 0);
		continue_prompt(g);
		return;
	case 1:
		/* The picture, the prompt in its lower panel and the chosen
		 * weapon's name by its blade (VA 0x16e56, 0x16dda). */
		fb_image(&g->fb, &g->sw_weapons, 369, 56, 0);
		draw(g, FONT_BODY, 392, 354, 160,
		    game_str(g, S_CHOOSE_WEAPON), 0);
		if (g->bg.pixels != NULL)	/* ink entry 1 */
			memcpy(g->ink, &g->bg.palette[STORY_INK * 3], 3);
		if (g->sw_named) {	/* PIRATE1, centred on its blade */
			static const int name_x[SWORD_WEAPONS] = { 426, 506,
			    464 };
			const char *name = game_word(g, W_WEAPON,
			    (size_t)g->selected);

			w = text_width(&g->font[FONT_TITLE], name);
			draw(g, FONT_TITLE, name_x[g->selected] - w / 2, 178,
			    FB_W, name, 0);
		}
		return;
	default:
		break;
	}
	if (g->world == NULL)
		return;
	sword_fighter(g, 0, &sw->you);
	sword_fighter(g, 1, &sw->foe);
	nation = sw->nation < 0 || sw->nation > 4 ? 4 : sw->nation;
	sword_flag(g, 0, SW_YOU_FLAG_X, 6 + (int)(g->ticks & 7), sw->you.bar);
	sword_flag(g, 1, SW_FOE_FLAG_X, 14 + 8 * nation + (int)(g->ticks & 7),
	    sw->foe.bar);

	/* The nation leads the title only in crew fights against a known
	 * nation (VA 0x16205). */
	if (sw->kind == SWORD_CREW && sw->nation >= 0 && sw->nation != 5)
		snprintf(label, sizeof(label), "%s %s",
		    game_word(g, W_NATION, (size_t)sw->nation),
		    game_word(g, W_TITLE, (size_t)sw->title));
	else
		snprintf(label, sizeof(label), "%s",
		    game_word(g, W_TITLE, (size_t)sw->title));
	w = text_width(&g->font[FONT_BODY], label);
	draw(g, FONT_BODY, 160 - w / 2, 336, 300, label, 0);
	w = text_width(&g->font[FONT_BODY], g->sim.player.name);
	draw(g, FONT_BODY, 480 - w / 2, 336, 300, g->sim.player.name, 0);
	/* The crews are shown only for kinds 0 and up (VA 0x169b7): not
	 * in the story's fight nor in practice. */
	/* Crews are counted in crew fights only: a duel is one man each,
	 * and the story's fight hides them. */
	if (sw->kind == SWORD_CREW) {
		snprintf(text, sizeof(text), "%s%d",
		    game_word(g, W_SAILING, SAILING_MEN), sw->foe.men);
		draw(g, FONT_BODY, 100, 380, 140, text, 0);
		snprintf(text, sizeof(text), "%s%d",
		    game_word(g, W_SAILING, SAILING_MEN), sw->you.men);
		draw(g, FONT_BODY, 420, 380, 140, text, 0);
	}

	if (g->sw_stage != 3)
		return;
	text_band(g, 160, 290);
	if (sw->result == SWORD_LOST) {
		snprintf(text, sizeof(text), "%s", "(Your men are beaten. "
		    "Prison is not implemented yet: the enemy takes your "
		    "ship.)");
		if (g->sim.fleet.nships == 1) {
			char more[400], all[600];

			fill(more, sizeof(more), game_str(g, S_PICKED_UP),
			    game_word(g, W_CAPTAIN,
			    (size_t)g->sim.player.difficulty), NULL);
			snprintf(all, sizeof(all), "%s\r%s\r%s", text,
			    game_str(g, S_ASHORE), more);
			snprintf(text, sizeof(text), "%s", all);
		}
	} else
		snprintf(text, sizeof(text), "%s", "(Your boarding party is "
		    "driven back to your ship.)");
	draw(g, FONT_BODY, 80, 170, 400, text, 0);
	continue_prompt(g);
}

const struct screen screen_sword = {
	"sword", sword_enter, sword_event, sword_tick, sword_draw,
};

/*
 * Town (VA 0x1869b): the town picture with its buildings.  Pointing at
 * one shows its label on the scroll in place of the town's name; the
 * arrows step through them; Enter (or Escape) or a click goes in, Space
 * opens the captain's cabin.  The rectangles come from the executable.
 */
#define W_TOWN_SPOT		84	/* "Visit the Governor"... */
#define TOWN_SPOTS		7	/* plus the cabin, from the keyboard */
#define SPOT_GOVERNOR		0
#define SPOT_MERCHANT		1
#define SPOT_BANK		2
#define SPOT_TAVERN		3
#define SPOT_SHIPWRIGHT		4
#define SPOT_INFO		5
#define SPOT_LEAVE		6
#define SPOT_CABIN		7
#define TOWN_NAME_X		325
#define TOWN_NAME_Y		38
#define GOVERNOR_CITIZENS	6	/* fewer: no governor */

static int
town_rect(const struct game *g, int i, int r[4])
{
	const uint8_t *p = exe_at(g->layout, g->exe, g->exe_len,
	    g->layout->town_spots_va + (uint32_t)i * 10, 8);
	int k;

	if (p == NULL)
		return (-1);
	for (k = 0; k < 4; k++)
		r[k] = (int16_t)(p[2 * k] | p[2 * k + 1] << 8);
	return (0);
}

static const struct sim_town *
here(const struct game *g)
{
	return (g->sim.player_town >= 0 ? &g->sim.towns[g->sim.player_town] :
	    NULL);
}

/* Which buildings are open (VA 0x1869b, 0x18547). */
static int
town_open(const struct game *g, int i)
{
	const struct sim_town *t = here(g);

	switch (i) {
	case SPOT_GOVERNOR:
		return (t != NULL && t->citizens >= GOVERNOR_CITIZENS &&
		    !(g->sim.visit_flags & VISIT_GOVERNOR));
	case SPOT_BANK:
		return (!(g->sim.visit_flags & VISIT_SNEAKED));
	default:
		return (1);
	}
}

static int
town_hit(const struct game *g, int x, int y)
{
	int i, r[4];

	for (i = 0; i < TOWN_SPOTS; i++)
		if (town_open(g, i) && town_rect(g, i, r) == 0 &&
		    x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3])
			return (i);
	return (-1);
}

static void
town_enter(struct game *g)
{
	game_background(g, "pic/town");
	game_portrait(g, NULL);
	load_bank(g, "spr/town", 0, &g->town_spr);
	g->town_spot = -1;
	g->ink[0] = g->ink[1] = g->ink[2] = 0;
	g->highlight[0] = g->highlight[1] = g->highlight[2] = 0;
}

static void
town_choose(struct game *g, int i)
{
	g->msg[0] = '\0';
	switch (i) {
	case SPOT_INFO:
		game_goto(g, &screen_cityinfo);
		break;
	case SPOT_SHIPWRIGHT:
		game_goto(g, &screen_shipwright);
		break;
	case SPOT_MERCHANT:
		game_goto(g, &screen_merchant);
		break;
	case SPOT_GOVERNOR:
		game_goto(g, &screen_governor);
		break;
	case SPOT_BANK:
		g->bank_stage = 0;
		game_goto(g, &screen_bank);
		break;
	case SPOT_TAVERN:
		sim_tavern_open(&g->sim);
		g->tavern_key = 6;
		game_goto(g, &screen_tavern);
		break;
	case SPOT_LEAVE:
		sim_leave_town(&g->sim);
		bank_free(&g->town_spr);
		game_goto(g, g->world != NULL ? &screen_sail : &screen_cabin);
		break;
	case SPOT_CABIN:
		game_goto(g, &screen_cabin);
		break;
	default:
		snprintf(g->msg, sizeof(g->msg), "(%s: not implemented yet.)",
		    game_word(g, W_TOWN_SPOT, (size_t)i));
		break;
	}
}

/* Step the keyboard selection, skipping closed buildings (VA 0x19cdd). */
static void
town_step(struct game *g, int d)
{
	int n;

	for (n = 0; n < TOWN_SPOTS; n++) {
		g->town_key += d;
		if (g->town_key < 0)
			g->town_key = TOWN_SPOTS - 1;
		else if (g->town_key >= TOWN_SPOTS)
			g->town_key = 0;
		if (town_open(g, g->town_key))
			break;
	}
	g->town_spot = g->town_key;
}

static void
town_event(struct game *g, const struct ui_event *ev)
{
	int i;

	switch (ev->type) {
	case EV_MOVE:
		g->town_spot = town_hit(g, ev->x, ev->y);
		if (g->town_spot >= 0)
			g->town_key = g->town_spot;
		break;
	case EV_CLICK:
		if ((i = town_hit(g, ev->x, ev->y)) >= 0)
			town_choose(g, i);
		break;
	case EV_KEY:
		if (ev->key == UI_KEY_LEFT || ev->key == UI_KEY_UP)
			town_step(g, -1);
		else if (ev->key == UI_KEY_RIGHT || ev->key == UI_KEY_DOWN)
			town_step(g, 1);
		else if ((ev->key == UI_KEY_ENTER ||
		    ev->key == UI_KEY_ESCAPE) && g->town_key >= 0 &&
		    town_open(g, g->town_key))
			town_choose(g, g->town_key);
		break;
	case EV_CHAR:
		if (ev->ch == ' ')
			town_choose(g, SPOT_CABIN);
		break;
	default:
		break;
	}
}

static void
town_draw(struct game *g)
{
	const struct sim_town *t = here(g);
	const char *label;
	int w;

	/* The harbour and the shipwright's shed when the ship is in port,
	 * the closed gate when the governor will not receive. */
	if (g->town_spr.count > 0)
		fb_frame(&g->fb, &g->town_spr.frames[0], 0, 258);
	if (g->town_spr.count > 1 && !town_open(g, SPOT_GOVERNOR))
		fb_frame(&g->fb, &g->town_spr.frames[1], 582, 216);
	label = g->town_spot >= 0 ?
	    game_word(g, W_TOWN_SPOT, (size_t)g->town_spot) :
	    t != NULL ? t->name : "";
	w = text_width(&g->font[FONT_TITLE], label);
	draw(g, FONT_TITLE, TOWN_NAME_X - w / 2, TOWN_NAME_Y, FB_W, label, 0);
	if (g->msg[0] != '\0')		/* an engine note, in the ink */
		draw_centered(g, FONT_BODY, 444, g->msg);
}

const struct screen screen_town = {
	"town", town_enter, town_event, NULL, town_draw,
};

/*
 * City information (VA 0x19c62, 0x196d0): the arrival picture, by sea
 * or by land, and what the player knows of the town.
 */
#define W_CITY_INFO		23
#define W_NUMBER		79	/* 0 "s", then "One".."Five" */
#define W_WEALTH		9

static void
cityinfo_enter(struct game *g)
{
	const struct sim_town *t = here(g);

	game_background(g, t != NULL && (t->flags & TOWN_INLAND) ?
	    "pic/arr_land" : "pic/arr_sea");
	game_portrait(g, NULL);
	g->ink[0] = 240;	/* light, on black */
	g->ink[1] = 220;
	g->ink[2] = 170;
}

static void
cityinfo_event(struct game *g, const struct ui_event *ev)
{
	if (ev->type == EV_CLICK || (ev->type == EV_KEY &&
	    (ev->key == UI_KEY_ENTER || ev->key == UI_KEY_ESCAPE)))
		game_goto(g, &screen_town);
}

/* The town's description as the game words it. */
static void
city_text(const struct game *g, const struct sim_town *t, char *out,
    size_t len)
{
	const char *arg[7];
	char forts[96], soldiers[16], citizens[16], gold[64], num[16];

	if (!(t->flags & 0x80)) {
		fill(out, len, game_word(g, W_CITY_INFO,
		    t->flags & 0x20 ? 4 : 1), t->name, NULL);
		return;
	}
	forts[0] = '\0';
	if (t->forts > 0)
		fill(forts, sizeof(forts), game_word(g, W_CITY_INFO, 2),
		    game_word(g, W_NUMBER, (size_t)(t->forts > 5 ? 5 :
		    t->forts)), t->forts == 1 ? "" : game_word(g, W_NUMBER, 0));
	gold[0] = '\0';
	if (t->gold > 0) {
		snprintf(num, sizeof(num), "%d", t->gold);
		fill(gold, sizeof(gold), game_word(g, W_CITY_INFO, 3), num,
		    NULL);
	}
	snprintf(soldiers, sizeof(soldiers), "%d", t->soldiers * 10);
	snprintf(citizens, sizeof(citizens), "%d", t->citizens * 100);
	arg[0] = t->name;
	arg[1] = game_word(g, W_NATION, (size_t)t->nation);
	arg[2] = game_word(g, W_WEALTH, (size_t)((t->wealth / 64) & 3));
	arg[3] = forts;
	arg[4] = soldiers;
	arg[5] = citizens;
	arg[6] = gold;
	fill_args(out, len, game_word(g, W_CITY_INFO, 0), arg, 7);
}

static void
cityinfo_draw(struct game *g)
{
	const struct sim_town *t = here(g);
	char text[512];

	if (t == NULL)
		return;
	city_text(g, t, text, sizeof(text));
	draw(g, FONT_BODY, 400, 150, 220, text, 0);
	continue_prompt(g);
}

const struct screen screen_cityinfo = {
	"cityinfo", cityinfo_enter, cityinfo_event, NULL, cityinfo_draw,
};

/*
 * Shipwright (VA 0x218c2): repair ships, sell ships, sell cannon.  The
 * original picks several ships at once on a fleet panel; here each
 * choice is one ship, and the list comes back until "None, thanks".
 */
#define S_NO_PIRATES		256
#define S_WRIGHT_MENU		277
#define S_ONLY_SHIP		279
#define S_NOTHING_WRONG		282
#define S_CANT_AFFORD		283
#define S_ONLY_CANNON		284
#define S_SHORT_FUNDS		319
#define W_WRIGHT		28	/* lists: titles, lines, "None" */
#define WRIGHT_TEXT_X		40
#define WRIGHT_TEXT_Y		300
#define WRIGHT_TEXT_W		540

static void
wright_note(struct game *g, int cstr)
{
	g->wright_note = cstr;
	g->wright_stage = 3;
}

static void
shipwright_enter(struct game *g)
{
	game_background(g, "pic/swright");
	game_portrait(g, NULL);
	g->ink[0] = 240;	/* light, on the dark blue */
	g->ink[1] = 220;
	g->ink[2] = 170;
	g->highlight[0] = 255;
	g->highlight[1] = 200;
	g->highlight[2] = 60;
	g->wright_stage = 0;
	if (sim_shipwright_refuses(&g->sim))
		wright_note(g, S_NO_PIRATES);
}

static int
any_damage(const struct game *g)
{
	int i;

	for (i = 0; i < g->sim.fleet.nships; i++)
		if (sim_repair_cost(&g->sim, i) > 0)
			return (1);
	return (0);
}

static void
wright_menu_choose(struct game *g, int i)
{
	switch (i) {
	case 0:		/* "Parting company" */
		game_goto(g, &screen_town);
		break;
	case 1:
		if (!any_damage(g))
			wright_note(g, S_NOTHING_WRONG);
		else
			g->wright_stage = 1;
		break;
	case 2:
		if (g->sim.fleet.nships < 2)
			wright_note(g, S_ONLY_SHIP);
		else
			g->wright_stage = 2;
		break;
	case 3:
		if (g->sim.fleet.guns < 2)
			wright_note(g, S_ONLY_CANNON);
		else
			game_goto(g, &screen_cannon);
		break;
	}
	g->selected = 0;
}

static void
wright_list_choose(struct game *g, int i)
{
	int r;

	if (i >= g->sim.fleet.nships) {		/* "None, thanks" */
		g->wright_stage = 0;
		g->selected = 0;
		return;
	}
	if (g->wright_stage == 1) {
		if (sim_repair_cost(&g->sim, i) == 0)
			return;
		r = sim_repair(&g->sim, 1u << i);
		if (r == REPAIR_TOO_DEAR)
			wright_note(g, S_CANT_AFFORD);
		else if (!any_damage(g))
			g->wright_stage = 0;
	} else {
		r = sim_sell_ships(&g->sim, 1u << i);
		if (r == SELL_SHORT)
			wright_note(g, S_SHORT_FUNDS);
		else if (g->sim.fleet.nships < 2)
			g->wright_stage = 0;
	}
	g->selected = 0;
}

static void
shipwright_event(struct game *g, const struct ui_event *ev)
{
	g->msg[0] = ev->type == EV_KEY || ev->type == EV_CLICK ? '\0' :
	    g->msg[0];
	switch (g->wright_stage) {
	case 0:
		menu_event(g, ev, 4, NULL, wright_menu_choose, NULL);
		break;
	case 1:
	case 2:
		menu_event(g, ev, g->sim.fleet.nships + 1, NULL,
		    wright_list_choose, NULL);
		break;
	default:
		if (ev->type == EV_CLICK || (ev->type == EV_KEY &&
		    (ev->key == UI_KEY_ENTER || ev->key == UI_KEY_ESCAPE))) {
			if (g->wright_note == S_NO_PIRATES)
				game_goto(g, &screen_town);
			g->wright_stage = 0;
			g->selected = 0;
		}
		break;
	}
}

/* The ship list: "~%s%s: %s gold pieces", or "(no damage)". */
static void
wright_list(struct game *g, char *out, size_t len)
{
	const char *arg[3];
	char line[160], price[16];
	int i, cost;

	snprintf(out, len, "%s", game_word(g, W_WRIGHT,
	    g->wright_stage == 1 ? 1 : 0));
	for (i = 0; i < g->sim.fleet.nships; i++) {
		cost = g->wright_stage == 1 ? sim_repair_cost(&g->sim, i) :
		    sim_ship_price(&g->sim, i);
		arg[0] = game_word(g, W_SHIP_TYPE,
		    (size_t)(g->sim.fleet.ships[i] % 9));
		if (g->wright_stage == 1 && cost == 0) {
			fill(line, sizeof(line), game_word(g, W_WRIGHT, 4),
			    arg[0], NULL);
		} else {
			snprintf(price, sizeof(price), "%d", cost);
			arg[1] = g->sim.fleet.damage[i] >= 16 ?
			    game_word(g, W_SHIP_TYPE, 10) : "";
			arg[2] = price;
			fill_args(line, sizeof(line),
			    game_word(g, W_WRIGHT, 3), arg, 3);
		}
		strncat(out, "\r", len - strlen(out) - 1);
		strncat(out, line, len - strlen(out) - 1);
	}
	strncat(out, "\r", len - strlen(out) - 1);
	strncat(out, game_word(g, W_WRIGHT, 2), len - strlen(out) - 1);
}

static void
shipwright_draw(struct game *g)
{
	char text[1200];

	switch (g->wright_stage) {
	case 0:
		draw(g, FONT_BODY, WRIGHT_TEXT_X, WRIGHT_TEXT_Y, WRIGHT_TEXT_W,
		    game_str(g, S_WRIGHT_MENU), 1);
		break;
	case 1:
	case 2:
		wright_list(g, text, sizeof(text));
		draw(g, FONT_BODY, WRIGHT_TEXT_X, WRIGHT_TEXT_Y, WRIGHT_TEXT_W,
		    text, 1);
		break;
	default:
		draw(g, FONT_BODY, WRIGHT_TEXT_X, WRIGHT_TEXT_Y, WRIGHT_TEXT_W,
		    game_str(g, g->wright_note), 0);
		continue_prompt(g);
		return;
	}
	if (g->msg[0] != '\0')
		draw_centered(g, FONT_BODY, 450, g->msg);
}

const struct screen screen_shipwright = {
	"shipwright", shipwright_enter, shipwright_event, NULL,
	shipwright_draw,
};

/*
 * Merchant (VA 0x21e56): the greeting, then the trading board (VA
 * 0x20229): the merchant's side on the left, the player's on the
 * right, a row per good with its price, the merchant's stock and the
 * player's cargo, both purses and the space in the hold.  Up and Down
 * pick a good, Right buys one, Left sells one; a click on the left of a
 * row buys, on the right sells; Enter, Escape or the exit sign leaves.
 */
#define BOARD_ROWS		3	/* food, goods, sugar (cannon: row 3) */
#define BOARD_PIECES		23	/* layout records for those rows */
#define BOARD_ROW_Y		116
#define BOARD_ROW_H		50
#define BOARD_SIGN		82	/* "Sell", then "Buy" */
#define BOARD_GAUGE		77
#define GLYPH_BIG		0	/* 0-9, then highlighted 10-19 */
#define GLYPH_SMALL		20	/* '/', 0-9; 31 and 42: other inks */

static const char *board_files[3][3] = {
	{ "spr/buysell", "spr/buyboard", "spr/buysell1" },
	{ "spr/buysellf", "spr/buybrdf", "spr/buysll1f" },
	{ "spr/buysellg", "spr/buybrdg", "spr/buysll1g" },
};

/* Layout record "i" (VA 0x348c4): frame, highlighted frame, x, y. */
static int
board_piece(const struct game *g, int i, int r[4])
{
	const uint8_t *p = exe_at(g->layout, g->exe, g->exe_len,
	    g->layout->board_va + (uint32_t)i * 10, 8);
	int k;

	if (p == NULL)
		return (-1);
	for (k = 0; k < 4; k++)
		r[k] = (int16_t)(p[2 * k] | p[2 * k + 1] << 8);
	return (0);
}

static void
board_frame(struct game *g, int bank, int frame, int x, int y)
{
	const struct sprite_bank *b = &g->shop_spr[bank];

	if (frame >= 0 && (size_t)frame < b->count)
		fb_frame(&g->fb, &b->frames[frame], x, y);
}

/* A number in the board's glyphs; "right" aligns it to "digits". */
static void
board_number(struct game *g, long v, int x, int y, int first, int w,
    int digits, int right)
{
	char buf[16];
	int i, n = snprintf(buf, sizeof(buf), "%ld", v < 0 ? 0 : v);

	if (right)
		x += (digits - n) * w;
	for (i = 0; i < n; i++, x += w)
		board_frame(g, 0, first + buf[i] - '0', x, y);
}

static void
merchant_enter(struct game *g)
{
	int lang = strcmp(g->lang, "fre") == 0 ? 1 :
	    strcmp(g->lang, "ger") == 0 ? 2 : 0, i;

	game_background(g, g->looting ? (g->sim.towns[g->assault_town].flags &
	    TOWN_INLAND ? "pic/arr_land" : "pic/arr_sea") : "pic/merchant");
	game_portrait(g, NULL);
	g->ink[0] = 240;
	g->ink[1] = 220;
	g->ink[2] = 170;
	for (i = 0; i < 3; i++)
		load_bank(g, board_files[lang][i], 0, &g->shop_spr[i]);
	g->selected = 0;
	g->shop_buying = 1;
	if (g->looting) {	/* the loot, for nothing (VA 0x2264f) */
		sim_plunder_open(&g->sim, g->sack.goods);
		g->shop_stage = 1;
	} else if (sim_merchant_refuses(&g->sim))
		g->shop_stage = 2;
	else {
		sim_merchant_open(&g->sim);
		g->shop_stage = 0;
	}
}

static void
merchant_leave(struct game *g)
{
	if (g->looting) {
		g->looting = 0;
		sim_plunder_close(&g->sim);
		game_goto(g, &screen_sail);
		return;
	}
	sim_merchant_close(&g->sim);
	game_goto(g, &screen_town);
}

static void
merchant_trade(struct game *g, int row, int buy)
{
	g->selected = row;
	g->shop_buying = buy;
	sim_trade(&g->sim, row, buy ? 1 : -1);
}

static void
merchant_event(struct game *g, const struct ui_event *ev)
{
	int go = ev->type == EV_CLICK || (ev->type == EV_KEY &&
	    (ev->key == UI_KEY_ENTER || ev->key == UI_KEY_ESCAPE));
	int row;

	if (g->shop_stage != 1) {
		if (go) {
			if (g->shop_stage == 2)
				game_goto(g, &screen_town);
			else
				g->shop_stage = 1;
		}
		return;
	}
	if (ev->type == EV_CLICK) {
		row = (ev->y - BOARD_ROW_Y) / BOARD_ROW_H;
		if (ev->y >= 420)
			merchant_leave(g);		/* the exit sign */
		else if (ev->y >= BOARD_ROW_Y && row < BOARD_ROWS)
			merchant_trade(g, row, ev->x < FB_W / 2);
		return;
	}
	if (ev->type != EV_KEY)
		return;
	switch (ev->key) {
	case UI_KEY_UP:
		g->selected = (g->selected + BOARD_ROWS - 1) % BOARD_ROWS;
		break;
	case UI_KEY_DOWN:
		g->selected = (g->selected + 1) % BOARD_ROWS;
		break;
	case UI_KEY_RIGHT:
		merchant_trade(g, g->selected, 1);
		break;
	case UI_KEY_LEFT:
		merchant_trade(g, g->selected, 0);
		break;
	case UI_KEY_ENTER:
	case UI_KEY_ESCAPE:
		merchant_leave(g);
		break;
	default:
		break;
	}
}

static void
merchant_draw(struct game *g)
{
	const struct sim_market *m = &g->sim.market;
	const struct sim_fleet *f = &g->sim.fleet;
	int i, r[4], row, sel, held, used, room;

	if (g->shop_stage != 1) {
		/* On the picture, no box, in ink 0 (VA 0x21e56): at (160,
		 * 40) up to x 480. */
		uint8_t ink[3];

		memcpy(ink, g->ink, 3);
		if (g->bg.pixels != NULL)
			memcpy(g->ink, g->bg.palette, 3);
		draw(g, FONT_BODY, 160, 40, 320, game_str(g,
		    g->shop_stage == 2 ? S_NO_PIRATES :
		    sim_merchant_greeting(&g->sim)), 0);
		memcpy(g->ink, ink, 3);
		continue_prompt(g);
		return;
	}
	board_frame(g, 1, 0, 203, 17);
	if (g->looting) {	/* "... gold pieces plundered!" (CSTN 10.6) */
		char line[96];

		snprintf(line, sizeof(line), "%ld%s", g->sack.gold,
		    game_word(g, W_PLUNDERED, PLUNDERED_WORD));
		draw_centered(g, FONT_BODY, 400, line);
	}
	for (i = 0; i < BOARD_PIECES; i++) {
		if (board_piece(g, i, r) != 0)
			continue;
		row = i < 5 ? -1 : (i - 5) / 6;
		board_frame(g, 0, row == g->selected ? r[1] : r[0], r[2], r[3]);
	}
	board_frame(g, 0, BOARD_SIGN + g->shop_buying, 235, 101);
	board_frame(g, 2, 0, 252, 430);
	for (i = 0; i < BOARD_ROWS; i++) {
		sel = i == g->selected;
		held = i == 0 ? f->food : i == 1 ? f->goods : f->sugar;
		/* The merchant's stock, the player's cargo, the price on
		 * both sides (VA 0x207f1, 0x20940). */
		board_number(g, m->stock[i], 106, 117 + i * BOARD_ROW_H,
		    GLYPH_BIG + (sel ? 10 : 0), 15, 4, 1);
		board_number(g, held, 475, 117 + i * BOARD_ROW_H,
		    GLYPH_BIG + (sel ? 10 : 0), 15, 4, 0);
		board_number(g, m->price[i], 71, 119 + i * BOARD_ROW_H,
		    GLYPH_SMALL + 1 + (sel ? 11 : 0), 7, 3, 1);
		board_number(g, m->price[i], 534, 119 + i * BOARD_ROW_H,
		    GLYPH_SMALL + 1 + (sel ? 11 : 0), 7, 3, 1);
	}
	/* The purses (VA 0x213c4) and the space in the hold (0x21612). */
	board_number(g, g->sim.merchant_gold > 999999 ? 999999 :
	    g->sim.merchant_gold, 20, 84, GLYPH_SMALL + 1, 8, 6, 1);
	board_number(g, f->gold > 999999 ? 999999 : f->gold, 576, 84,
	    GLYPH_SMALL + 1, 8, 6, 1);
	room = sim_hold_space(&g->sim);
	board_number(g, room, 336, 138, GLYPH_SMALL + 23, 8, 4, 1);
	/* The hold gauge: an engine guess at the original's formula. */
	used = f->capacity * 10 - room;
	board_frame(g, 0, BOARD_GAUGE, 249 + (f->capacity > 0 ?
	    used * 113 / (f->capacity * 10) : 0), 72);
}

const struct screen screen_merchant = {
	"merchant", merchant_enter, merchant_event, NULL, merchant_draw,
};

/*
 * Tavern (VA 0x11867): the tavern's name on the scroll (CSTN 96, by
 * town) and ten spots: the map seller, the information seller, three
 * groups of seamen to recruit, the bartender's rumours, the barmaid, a
 * stranger, and the door.  People leave once spoken to and are back on
 * the next arrival.  The arrows step through them, Enter goes, Space
 * opens the cabin.
 */
#define W_TAVERN_NAME		96
#define W_REPUTATION		34	/* cowardly .. infamous */
#define W_RANK			78	/* Mr., Ensign, Captain... */
#define W_GOSSIP		33	/* "your promising plans"... */
#define S_RECRUITS		151
#define S_NO_RECRUITS		309
#define S_GOSSIP		125
#define TAVERN_MAPMAN		0
#define TAVERN_INFOMAN		1
#define TAVERN_BARTENDER	3
#define TAVERN_BARMAID		4
#define TAVERN_STRANGER		5
#define TAVERN_DOOR		6
#define RUMOURS_SHOWN		12

static int
tavern_rect(const struct game *g, int i, int r[4])
{
	const uint8_t *p = exe_at(g->layout, g->exe, g->exe_len,
	    g->layout->tavern_spots_va + (uint32_t)i * 10, 8);
	int k;

	if (p == NULL)
		return (-1);
	for (k = 0; k < 4; k++)
		r[k] = (int16_t)(p[2 * k] | p[2 * k + 1] << 8);
	return (0);
}

static int
tavern_open_spot(const struct game *g, int i)
{
	return (i == TAVERN_DOOR || (g->sim.tavern_spots & (1u << i)));
}

static int
tavern_hit(const struct game *g, int x, int y)
{
	int i, r[4];

	for (i = 0; i < TAVERN_SPOTS; i++)
		if (tavern_open_spot(g, i) && tavern_rect(g, i, r) == 0 &&
		    x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3])
			return (i);
	return (-1);
}

static void
tavern_enter(struct game *g)
{
	game_background(g, "pic/tavern");
	game_portrait(g, NULL);
	load_bank(g, "spr/tavern", 0, &g->tavern_spr);
	g->ink[0] = g->ink[1] = g->ink[2] = 0;
}

static void
tavern_choose(struct game *g, int i)
{
	g->msg[0] = '\0';
	if (i == TAVERN_DOOR) {
		bank_free(&g->tavern_spr);
		game_goto(g, &screen_town);
	} else if (TAVERN_CREW & (1u << i))
		game_goto(g, &screen_recruit);
	else if (i == TAVERN_BARTENDER)
		game_goto(g, &screen_rumours);
	else if (i == SPOT_CABIN)
		game_goto(g, &screen_cabin);
	else
		snprintf(g->msg, sizeof(g->msg), "(Not implemented yet.)");
}

static void
tavern_step(struct game *g, int d)
{
	int n;

	for (n = 0; n < TAVERN_SPOTS; n++) {
		g->tavern_key = (g->tavern_key + d + TAVERN_SPOTS) %
		    TAVERN_SPOTS;
		if (tavern_open_spot(g, g->tavern_key))
			break;
	}
}

static void
tavern_event(struct game *g, const struct ui_event *ev)
{
	int i;

	switch (ev->type) {
	case EV_CLICK:
		if ((i = tavern_hit(g, ev->x, ev->y)) >= 0)
			tavern_choose(g, i);
		break;
	case EV_MOVE:
		if ((i = tavern_hit(g, ev->x, ev->y)) >= 0)
			g->tavern_key = i;
		break;
	case EV_KEY:
		if (ev->key == UI_KEY_LEFT || ev->key == UI_KEY_UP)
			tavern_step(g, -1);
		else if (ev->key == UI_KEY_RIGHT || ev->key == UI_KEY_DOWN)
			tavern_step(g, 1);
		else if (ev->key == UI_KEY_ENTER || ev->key == UI_KEY_ESCAPE)
			tavern_choose(g, g->tavern_key);
		break;
	case EV_CHAR:
		if (ev->ch == ' ')
			tavern_choose(g, SPOT_CABIN);
		break;
	default:
		break;
	}
}

static void
tavern_draw(struct game *g)
{
	static const int who[3] = { TAVERN_MAPMAN, TAVERN_INFOMAN,
	    TAVERN_STRANGER };
	static const int at[3][2] = { { 0, 255 }, { 142, 259 },
	    { 349, 199 } };
	const char *name;
	int i, w;

	for (i = 0; i < 3; i++)
		if ((size_t)i < g->tavern_spr.count &&
		    (g->sim.tavern_spots & (1u << who[i])))
			fb_frame(&g->fb, &g->tavern_spr.frames[i], at[i][0],
			    at[i][1]);
	name = game_word(g, W_TAVERN_NAME, (size_t)g->sim.player_town);
	w = text_width(&g->font[FONT_TITLE], name);
	draw(g, FONT_TITLE, 320 - w / 2, TOWN_NAME_Y, FB_W, name, 0);
	if (g->msg[0] != '\0')		/* an engine note, in the ink */
		draw_centered(g, FONT_BODY, 444, g->msg);
}

const struct screen screen_tavern = {
	"tavern", tavern_enter, tavern_event, NULL, tavern_draw,
};

/* Recruiting (VA 0x10f04): "Look! It's that famous sea-dog..." */
static void
recruit_enter(struct game *g)
{
	game_background(g, "pic/crew");
	game_portrait(g, NULL);
	/* The tavern's inks (VA 0x11867): palette entries 2 and 6. */
	if (g->bg.pixels != NULL) {
		memcpy(g->ink, &g->bg.palette[2 * 3], 3);
		memcpy(g->highlight, &g->bg.palette[6 * 3], 3);
	}
}

static void
recruit_choose(struct game *g, int i)
{
	sim_recruit(&g->sim, i == 0);
	game_goto(g, &screen_tavern);
}

static void
recruit_event(struct game *g, const struct ui_event *ev)
{
	if (g->sim.recruits > 0) {
		menu_event(g, ev, 2, NULL, recruit_choose, NULL);
		return;
	}
	if (ev->type == EV_CLICK || (ev->type == EV_KEY &&
	    (ev->key == UI_KEY_ENTER || ev->key == UI_KEY_ESCAPE)))
		recruit_choose(g, 1);
}

static void
recruit_draw(struct game *g)
{
	const char *arg[4];
	char text[600], men[16];
	int r = g->sim.reputation / 4 + 1;

	/* On the picture, no box (VA 0x10f04): at (60, 300) up to x 620,
	 * the choice up to x 560. */
	if (g->sim.recruits == 0) {
		draw(g, FONT_BODY, 60, 300, 560, game_str(g, S_NO_RECRUITS), 0);
		continue_prompt(g);
		return;
	}
	snprintf(men, sizeof(men), "%d", g->sim.recruits);
	arg[0] = game_word(g, W_REPUTATION, (size_t)(r < 0 ? 0 : r > 5 ? 5 :
	    r));
	arg[1] = game_word(g, W_RANK, 0);	/* ranks: not modelled yet */
	arg[2] = g->sim.player.name;
	arg[3] = men;
	fill_args(text, sizeof(text), game_str(g, S_RECRUITS), arg, 4);
	draw(g, FONT_BODY, 60, 300, 500, text, 1);
}

const struct screen screen_recruit = {
	"recruit", recruit_enter, recruit_event, NULL, recruit_draw,
};

/*
 * Rumours (VA 0x1183e): the bartender passes on the latest news, or,
 * with none, listens to the player's last exploit (CSTR 125).
 */
static void
rumours_enter(struct game *g)
{
	game_background(g, "pic/bartenda");
	game_portrait(g, NULL);
	/* Ink entry 0 (VA 0x1e78d): palette index 0. */
	if (g->bg.pixels != NULL)
		memcpy(g->ink, g->bg.palette, 3);
}

static void
rumours_event(struct game *g, const struct ui_event *ev)
{
	if (ev->type == EV_CLICK || (ev->type == EV_KEY &&
	    (ev->key == UI_KEY_ENTER || ev->key == UI_KEY_ESCAPE)))
		game_goto(g, &screen_tavern);
}

static void
rumours_draw(struct game *g)
{
	char text[512];
	size_t i, kept = sim_news_kept(&g->sim);
	int y = 40;

	/* No box: the news at (222, 40) up to x 610 (VA 0x1e78d), the
	 * gossip at (320, 150) up to x 560 (VA 0x10b10). */
	if (kept == 0) {
		/* The personal log is not kept yet: "your promising plans". */
		fill(text, sizeof(text), game_str(g, S_GOSSIP),
		    game_word(g, W_GOSSIP, 0), NULL);
		draw(g, FONT_BODY, 320, 150, 240, text, 0);
	} else {
		y += draw(g, FONT_BODY, 222, y, 388, game_word(g, W_NEWS, 1),
		    0) + 6;
		for (i = kept; i > 0 && i + RUMOURS_SHOWN > kept && y < 400;
		    i--) {
			news_text(g, sim_news_at(&g->sim, i - 1), text,
			    sizeof(text));
			y += draw(g, FONT_BODY, 222, y, 388, text, 0) + 4;
		}
	}
	continue_prompt(g);
}

const struct screen screen_rumours = {
	"rumours", rumours_enter, rumours_event, NULL, rumours_draw,
};

/*
 * Governor (VA 0x13269): the political news (VA 0x11d64), a promotion
 * when earned (VA 0x12549), then a Letter of Marque or a pardon for
 * gold (VA 0x12d6d).  He receives the player once a stay.  His
 * missions, prisoners, ransoms, the family quest and his daughter are
 * not done yet.
 */
#define W_GOV			25	/* the greeting's pieces */
#define W_COUNTRY		81	/* Spain, England, France, Holland */
#define W_NATION_A		104	/* "a Spanish", "an English"... */
#define S_GOV_CHAT		149
#define S_PROMOTION		134
#define S_LAND			135
#define S_MARQUE		147
#define S_PARDON		148
#define S_NO_FUNDS		94
#define GOV_TEXT_X		320
#define GOV_TEXT_Y		150
#define GOV_TEXT_W		300
enum { GOV_GREET, GOV_PROMOTED, GOV_LAND, GOV_OFFER, GOV_SHORT, GOV_DONE };

static void
governor_enter(struct game *g)
{
	game_background(g, "pic/governor");
	game_portrait(g, NULL);
	g->ink[0] = 240;
	g->ink[1] = 220;
	g->ink[2] = 170;
	g->highlight[0] = 255;
	g->highlight[1] = 200;
	g->highlight[2] = 60;
	g->sim.visit_flags |= VISIT_GOVERNOR;
	g->gov_rank = sim_governor_promote(&g->sim, &g->gov_acres);
	g->gov_stage = GOV_GREET;
	if (g->gov_rank > 0)
		load_bank(g, "spr/gov", 0, &g->gov_spr);
}

/* The stage after "from", skipping those with nothing to say. */
static void
governor_next(struct game *g)
{
	long price;
	enum governor_offer o = sim_governor_offer(&g->sim, &price);

	switch (g->gov_stage) {
	case GOV_GREET:
		if (g->gov_rank > 0) {
			g->gov_stage = GOV_PROMOTED;
			break;
		}
		/* FALLTHROUGH */
	case GOV_PROMOTED:
		if (g->gov_stage == GOV_PROMOTED && g->gov_acres > 0) {
			g->gov_stage = GOV_LAND;
			break;
		}
		/* FALLTHROUGH */
	case GOV_LAND:
		if (o == OFFER_MARQUE || o == OFFER_PARDON) {
			g->gov_stage = GOV_OFFER;
			g->selected = 0;
			break;
		}
		/* FALLTHROUGH */
	default:
		bank_free(&g->gov_spr);
		g->gov_stage = GOV_DONE;
		game_goto(g, &screen_town);
		break;
	}
}

static void
governor_offer_choose(struct game *g, int i)
{
	if (i == 0 && !sim_governor_pay(&g->sim)) {
		g->gov_stage = GOV_SHORT;
		return;
	}
	g->gov_stage = GOV_SHORT;	/* past the offer */
	governor_next(g);
}

static void
governor_event(struct game *g, const struct ui_event *ev)
{
	if (g->gov_stage == GOV_DONE)
		return;
	if (g->gov_stage == GOV_OFFER) {
		menu_event(g, ev, 2, NULL, governor_offer_choose, NULL);
		return;
	}
	if (ev->type == EV_CLICK || (ev->type == EV_KEY &&
	    (ev->key == UI_KEY_ENTER || ev->key == UI_KEY_ESCAPE)))
		governor_next(g);
}

/* "We are allied with X, Y and Z": CSTN 25.3-25.5 over the names. */
static void
nation_list(struct game *g, const int *n, int count, char *out,
    size_t len)
{
	const char *arg[3];
	int i;

	for (i = 0; i < count && i < 3; i++)
		arg[i] = game_word(g, W_COUNTRY, (size_t)n[i]);
	fill_args(out, len, game_word(g, W_GOV, (size_t)(count + 2)), arg,
	    count);
}

/* The greeting with the political situation (VA 0x11d64). */
static void
governor_greeting(struct game *g, char *out, size_t len)
{
	const struct sim_town *t = here(g);
	int nat = t != NULL ? t->nation & 3 : 0, allies[4], enemies[4];
	int na = 0, ne = 0, k;
	char part[256], list[160];

	for (k = 0; k < 4; k++) {
		if (k == nat)
			continue;
		if (g->sim.relations[nat][k] == 1)
			allies[na++] = k;
		else if (g->sim.relations[nat][k] == -1)
			enemies[ne++] = k;
	}
	if (na + ne == 0) {
		snprintf(out, len, "%s", game_str(g, S_GOV_CHAT));
		return;
	}
	fill(out, len, game_word(g, W_GOV, 0),
	    game_word(g, W_RANK, g->sim.rank[nat]), g->sim.player.name);
	if (na > 0) {
		nation_list(g, allies, na, list, sizeof(list));
		snprintf(part, sizeof(part), "%s%s", game_word(g, W_GOV, 1),
		    list);
		strncat(out, part, len - strlen(out) - 1);
	}
	if (ne > 0) {
		nation_list(g, enemies, ne, list, sizeof(list));
		fill(part, sizeof(part), game_word(g, W_GOV, 2),
		    na > 0 ? game_word(g, W_GOV, 7) : "", NULL);
		strncat(out, part, len - strlen(out) - 1);
		strncat(out, list, len - strlen(out) - 1);
	}
	strncat(out, game_word(g, W_GOV, ne == 0 || g->sim.standing[nat] < 2 ?
	    8 : 6), len - strlen(out) - 1);
}

static void
governor_draw(struct game *g)
{
	const struct sim_town *t = here(g);
	int nat = t != NULL ? t->nation & 3 : 0;
	char text[1024], num[32];
	long price;
	enum governor_offer o;

	switch (g->gov_stage) {
	case GOV_GREET:
		governor_greeting(g, text, sizeof(text));
		break;
	case GOV_PROMOTED:
	case GOV_LAND:
		if (g->gov_spr.count >= 2) {
			fb_frame(&g->fb, &g->gov_spr.frames[0], 140, 35);
			fb_frame(&g->fb, &g->gov_spr.frames[1], 113, 262);
		}
		if (g->gov_stage == GOV_PROMOTED)
			fill(text, sizeof(text), game_str(g, S_PROMOTION),
			    game_word(g, W_NATION, (size_t)nat),
			    game_word(g, W_RANK, (size_t)g->gov_rank));
		else {
			snprintf(num, sizeof(num), "%d", g->gov_acres);
			fill(text, sizeof(text), game_str(g, S_LAND), num,
			    NULL);
		}
		break;
	case GOV_OFFER:
		o = sim_governor_offer(&g->sim, &price);
		snprintf(num, sizeof(num), "%ld", price);
		fill(text, sizeof(text), game_str(g, o == OFFER_PARDON ?
		    S_PARDON : S_MARQUE), game_word(g, o == OFFER_PARDON ?
		    W_NATION : W_NATION_A, (size_t)nat), num);
		draw(g, FONT_BODY, GOV_TEXT_X, GOV_TEXT_Y, GOV_TEXT_W, text, 1);
		return;
	case GOV_SHORT:
		snprintf(text, sizeof(text), "%s", game_str(g, S_NO_FUNDS));
		break;
	default:
		return;
	}
	draw(g, FONT_BODY, GOV_TEXT_X, GOV_TEXT_Y, GOV_TEXT_W, text, 0);
	continue_prompt(g);
}

const struct screen screen_governor = {
	"governor", governor_enter, governor_event, NULL, governor_draw,
};

/*
 * Bank (VA 0x180c8): "Are you sure you want to divide the plunder?",
 * then each man's share and the crew's mood (CSTR 155), the captain's
 * portions (CSTR 156), the treasure the captain has put aside (the cave,
 * VA 0xbdfe), and "Plan another expedition / Retire / Advance to the
 * next level" (CSTR 157 and 97).  The date is on the small sign.
 */
#define S_SURE			86
#define S_DIVIDE		95
#define S_END_GAME		96
#define S_ADVANCE		97
#define S_SHARES		155
#define S_PORTIONS		156
#define S_PLAN			157
#define S_REFIT			158
#define W_MOOD			72
#define BANK_TEXT_X		40
#define BANK_TEXT_Y		280
#define BANK_TEXT_W		580
enum { BANK_ASK, BANK_SHARES, BANK_PORTIONS, BANK_PLAN, BANK_SURE_END,
    BANK_REFIT };

static void
bank_enter(struct game *g)
{
	game_background(g, "pic/bank");
	game_portrait(g, NULL);
	g->ink[0] = 240;
	g->ink[1] = 220;
	g->ink[2] = 170;
	g->highlight[0] = 255;
	g->highlight[1] = 200;
	g->highlight[2] = 60;
	g->selected = 0;
}

static void
bank_choose(struct game *g, int i)
{
	switch (g->bank_stage) {
	case BANK_ASK:
		if (i != 0) {
			game_goto(g, &screen_town);
			return;
		}
		sim_divide_plunder(&g->sim, &g->plunder);
		g->bank_stage = BANK_SHARES;
		break;
	case BANK_PLAN:
		if (i == 1) {
			g->bank_stage = BANK_SURE_END;
			break;
		}
		sim_new_expedition(&g->sim, i == 2);
		g->bank_stage = BANK_REFIT;
		break;
	case BANK_SURE_END:
		if (i == 0) {
			game_goto(g, &screen_retire);
			return;
		}
		sim_new_expedition(&g->sim, 0);
		g->bank_stage = BANK_REFIT;
		break;
	default:
		break;
	}
	g->selected = 0;
}

static void
bank_event(struct game *g, const struct ui_event *ev)
{
	int go = ev->type == EV_CLICK || (ev->type == EV_KEY &&
	    (ev->key == UI_KEY_ENTER || ev->key == UI_KEY_ESCAPE));

	switch (g->bank_stage) {
	case BANK_ASK:
	case BANK_SURE_END:
		menu_event(g, ev, 2, NULL, bank_choose, NULL);
		break;
	case BANK_PLAN:
		menu_event(g, ev, g->sim.player.difficulty < 3 ? 3 : 2, NULL,
		    bank_choose, NULL);
		break;
	case BANK_SHARES:
		if (go)
			g->bank_stage = BANK_PORTIONS;
		break;
	case BANK_PORTIONS:
		if (go) {
			g->bank_stage = BANK_PLAN;
			g->cave_back = &screen_bank;
			game_goto(g, &screen_cave);
		}
		break;
	case BANK_REFIT:
		if (go)
			game_goto(g, &screen_town);
		break;
	}
}

static void
bank_draw(struct game *g)
{
	char text[1200], a[32], b[32], date[64], line[160];
	const char *arg[3];
	int menu = 0;

	date_text(g, date, sizeof(date));
	{
		int w = text_width(&g->font[FONT_BODY], date);

		draw(g, FONT_BODY, 306 - w / 2, 244, 200, date, 0);
	}
	switch (g->bank_stage) {
	case BANK_ASK:
	case BANK_SURE_END:
		fill(text, sizeof(text), game_str(g, S_SURE), game_str(g,
		    g->bank_stage == BANK_ASK ? S_DIVIDE : S_END_GAME), NULL);
		menu = 1;
		break;
	case BANK_SHARES:
		snprintf(a, sizeof(a), "%d", g->plunder.share);
		fill(text, sizeof(text), game_str(g, S_SHARES), a,
		    game_word(g, W_MOOD, (size_t)g->plunder.mood));
		break;
	case BANK_PORTIONS:
		snprintf(a, sizeof(a), "%d", g->plunder.portions);
		snprintf(b, sizeof(b), "%ld", g->plunder.yours);
		arg[0] = game_word(g, W_DIFFICULTY,
		    (size_t)g->sim.player.difficulty);
		arg[1] = a;
		arg[2] = b;
		fill_args(text, sizeof(text), game_str(g, S_PORTIONS), arg, 3);
		break;
	case BANK_PLAN:
		snprintf(text, sizeof(text), "%s", game_str(g, S_PLAN));
		if (g->sim.player.difficulty < 3) {
			fill(line, sizeof(line), game_str(g, S_ADVANCE),
			    game_word(g, W_DIFFICULTY,
			    (size_t)g->sim.player.difficulty + 1), NULL);
			if (text[0] != '\0' && text[strlen(text) - 1] != '\r')
				strncat(text, "\r", sizeof(text) -
				    strlen(text) - 1);
			strncat(text, line, sizeof(text) - strlen(text) - 1);
		}
		menu = 1;
		break;
	case BANK_REFIT:
		snprintf(text, sizeof(text), "%s", game_str(g, S_REFIT));
		break;
	}
	draw(g, FONT_BODY, BANK_TEXT_X, BANK_TEXT_Y, BANK_TEXT_W, text, menu);
	if (!menu)
		continue_prompt(g);
}

const struct screen screen_bank = {
	"bank", bank_enter, bank_event, NULL, bank_draw,
};

/*
 * The treasure cave (VA 0xbdfe): eight piles (tcave0-7), each shown
 * once the captain's fortune passes its mark (table at VA 0x32928: x,
 * y, mark; 500, 1000, then by thousands).
 */
static const char *cave_pile[8] = {
	"spr/tcave0", "spr/tcave1", "spr/tcave2", "spr/tcave3",
	"spr/tcave4", "spr/tcave5", "spr/tcave6", "spr/tcave7",
};

static int
cave_pile_at(const struct game *g, int i, int r[3])
{
	const uint8_t *p = exe_at(g->layout, g->exe, g->exe_len,
	    g->layout->cave_va + (uint32_t)i * 12, 12);
	int k;

	if (p == NULL)
		return (-1);
	for (k = 0; k < 3; k++)
		r[k] = (int32_t)(p[4 * k] | p[4 * k + 1] << 8 |
		    p[4 * k + 2] << 16 | (uint32_t)p[4 * k + 3] << 24);
	return (0);
}

static void
cave_enter(struct game *g)
{
	int i;

	game_background(g, "pic/trescave");
	game_portrait(g, NULL);
	for (i = 0; i < 8; i++)
		load_bank(g, cave_pile[i], 0, &g->cave[i]);
}

static void
cave_event(struct game *g, const struct ui_event *ev)
{
	if (ev->type == EV_CLICK || (ev->type == EV_KEY &&
	    (ev->key == UI_KEY_ENTER || ev->key == UI_KEY_ESCAPE)))
		game_goto(g, g->cave_back != NULL ? g->cave_back :
		    &screen_bank);
}

static void
cave_draw(struct game *g)
{
	int i, r[3];

	for (i = 0; i < 8; i++)
		if (cave_pile_at(g, i, r) == 0 && r[2] < g->sim.fortune &&
		    g->cave[i].count > 0)
			fb_frame(&g->fb, &g->cave[i].frames[0], r[0], r[1]);
	continue_prompt(g);
}

const struct screen screen_cave = {
	"cave", cave_enter, cave_event, NULL, cave_draw,
};

/*
 * Selling cannon (VA 0x22c76): the trading board, lower on the
 * shipwright's picture, with a single row: the shipwright's stock and
 * the player's guns, 50 gold each, paid from the shipwright's gold.
 * Left sells, Right buys back; Enter, Escape or the sign leaves.
 */
#define S_CANNON_PRICE		313

static void
cannon_enter(struct game *g)
{
	int lang = strcmp(g->lang, "fre") == 0 ? 1 :
	    strcmp(g->lang, "ger") == 0 ? 2 : 0, i;

	game_background(g, "pic/swright");
	game_portrait(g, NULL);
	g->ink[0] = 240;	/* light, on the dark blue */
	g->ink[1] = 220;
	g->ink[2] = 170;
	for (i = 0; i < 3; i++)
		load_bank(g, board_files[lang][i], 0, &g->shop_spr[i]);
	g->shop_buying = 0;
	sim_shipwright_gold(&g->sim);
}

static void
cannon_event(struct game *g, const struct ui_event *ev)
{
	if (ev->type == EV_CLICK) {
		if (ev->y >= 420)
			game_goto(g, &screen_shipwright);
		else if (ev->y >= 340) {
			g->shop_buying = ev->x < FB_W / 2;
			sim_trade_cannon(&g->sim, g->shop_buying ? 1 : -1);
		}
		return;
	}
	if (ev->type != EV_KEY)
		return;
	if (ev->key == UI_KEY_LEFT) {
		g->shop_buying = 0;
		sim_trade_cannon(&g->sim, -1);
	} else if (ev->key == UI_KEY_RIGHT) {
		g->shop_buying = 1;
		sim_trade_cannon(&g->sim, 1);
	} else if (ev->key == UI_KEY_ENTER || ev->key == UI_KEY_ESCAPE)
		game_goto(g, &screen_shipwright);
}

static void
cannon_draw(struct game *g)
{
	const struct sim_fleet *f = &g->sim.fleet;
	int room, used;

	board_frame(g, 1, 0, 204, 265);
	board_frame(g, 0, 84, 71, 326);		/* the gold boxes */
	board_frame(g, 0, 84, 491, 326);
	board_frame(g, 0, 87, 487, 248);	/* the player's colours */
	board_frame(g, 0, 88, 67, 250);		/* the shipwright's */
	board_frame(g, 2, 1, 63, 360);		/* the cannon, each side */
	board_frame(g, 2, 2, 483, 360);
	board_frame(g, 2, 0, 252, 430);		/* the sign */
	board_frame(g, 0, BOARD_SIGN + g->shop_buying, 238, 350);
	board_number(g, g->sim.wright_gold > 999999 ? 999999 :
	    g->sim.wright_gold, 87, 336, GLYPH_SMALL + 1, 8, 6, 1);
	board_number(g, g->sim.fleet.gold > 999999 ? 999999 :
	    g->sim.fleet.gold, 507, 336, GLYPH_SMALL + 1, 8, 6, 1);
	board_number(g, g->sim.wright_cannon, 78, 372, GLYPH_BIG + 10, 15, 4,
	    1);
	board_number(g, g->sim.fleet.guns, 520, 372, GLYPH_BIG + 10, 15, 4, 0);
	/* The space in the hold and its gauge, as the merchant's, lower. */
	room = sim_hold_space(&g->sim);
	board_number(g, room, 336, 386, GLYPH_SMALL + 23, 8, 4, 1);
	used = f->capacity * 10 - room;
	board_frame(g, 0, BOARD_GAUGE, 249 + (f->capacity > 0 ?
	    used * 113 / (f->capacity * 10) : 0), 320);
	draw(g, FONT_BODY, 150, 20, 470, game_str(g, S_CANNON_PRICE), 0);
}

const struct screen screen_cannon = {
	"cannon", cannon_enter, cannon_event, NULL, cannon_draw,
};

/*
 * Arrival (VA 0x18fa9): on arr_sea.pcx (arr_land.pcx inland), "You have
 * arrived at the lovely <seaside town> of <name>. <forts> The <nation>
 * flag flies over the town." (CSTR 114, at (330, 40) up to x 620), and
 * the choices (CSTR 115, at (330, 260)): sail into the harbour, attack,
 * sneak in, or leave.  The fort's answer is written in place; a guard's
 * challenge (CSTR 163) is on landpir.pcx, before the duel.
 */
#define S_ARRIVED		114
#define S_ARRIVE_MENU		115
#define S_FORT_FIRES		116
#define S_SHIP_SUNK		117
#define S_RECOGNIZED		163
#define S_IMPRISONED		175
#define S_TRADED_BACK		190
#define S_DISGRACE		174
#define W_ARRIVAL		29
#define ARR_ONE_FORT		0	/* +8 inland */
#define ARR_FORTS		1
#define ARR_SAIL_IN		4	/* 5: march into town */
#define ARR_INLAND		6	/* 7: seaside town */
#define ARR_TEXT_X		330
enum { ARRIVE_MENU, ARRIVE_ANSWER, ARRIVE_GUARD };

static const struct sim_town *
arrive_town(const struct game *g)
{
	int i = g->sim.fleet.leaving;

	return (i >= 0 && (size_t)i < g->sim.ntowns ? &g->sim.towns[i] :
	    NULL);
}

static void
arrive_enter(struct game *g)
{
	const struct sim_town *t = arrive_town(g);
	int inland = t != NULL && (t->flags & TOWN_INLAND);

	game_background(g, g->arrive_stage == ARRIVE_GUARD ? "pic/landpir" :
	    inland ? "pic/arr_land" : "pic/arr_sea");
	game_portrait(g, NULL);
	if (g->bg.pixels != NULL) {
		memcpy(g->ink, &g->bg.palette[STORY_INK * 3], 3);
		memcpy(g->highlight, &g->bg.palette[6 * 3], 3);
	}
	g->selected = 0;
	g->msg[0] = '\0';
	if (g->arrive_stage == ARRIVE_MENU && t != NULL)
		sim_arrive(&g->sim, g->sim.fleet.leaving);
}

static void
arrive_choose(struct game *g, int i)
{
	int town = g->sim.fleet.leaving;

	switch (i) {
	case 0:
		g->arrive_last = g->sim.fleet.nships == 1;
		g->arrive_result = sim_sail_in(&g->sim, town);
		if (g->arrive_result == ARRIVE_ENTER)
			game_goto(g, &screen_town);
		else
			g->arrive_stage = ARRIVE_ANSWER;
		break;
	case 1:
		/* From the sea, a fort first fights the fleet (VA 0x18b3f);
		 * a town without forts goes straight to the assault. */
		if (g->sim.towns[town].forts > 0) {
			sim_fort_battle_start(&g->sim, town);
			game_goto(g, &screen_battle);
		} else {
			g->assault_town = town;
			g->assault_aboard = 0;
			game_goto(g, &screen_assault);
		}
		break;
	case 2:
		if (sim_sneak_in(&g->sim, town) == ARRIVE_ENTER)
			game_goto(g, &screen_town);
		else {
			g->arrive_stage = ARRIVE_GUARD;
			game_switch(g, &screen_arrive);
		}
		break;
	default:
		game_goto(g, &screen_sail);
		break;
	}
}

static void
arrive_event(struct game *g, const struct ui_event *ev)
{
	int go = ev->type == EV_CLICK || (ev->type == EV_KEY &&
	    (ev->key == UI_KEY_ENTER || ev->key == UI_KEY_ESCAPE));

	switch (g->arrive_stage) {
	case ARRIVE_MENU:
		menu_event(g, ev, 4, NULL, arrive_choose, NULL);
		break;
	case ARRIVE_ANSWER:
		if (go)
			game_goto(g, &screen_sail);
		break;
	default:
		if (go) {
			g->sw_guard = 1;
			game_goto(g, &screen_sword);
		}
		break;
	}
}

static void
arrive_draw(struct game *g)
{
	const struct sim_town *t = arrive_town(g);
	char text[1024], forts[160], label[64];
	const char *arg[5];
	int inland, n;

	if (t == NULL)
		return;
	inland = (t->flags & TOWN_INLAND) != 0;
	if (g->arrive_stage == ARRIVE_GUARD) {
		draw(g, FONT_BODY, 40, 150, 200, game_str(g, S_RECOGNIZED), 0);
		continue_prompt(g);
		return;
	}
	if (g->arrive_stage == ARRIVE_ANSWER) {
		snprintf(text, sizeof(text), "%s", game_str(g, S_FORT_FIRES));
		if (g->arrive_result == ARRIVE_SUNK)
			snprintf(text + strlen(text), sizeof(text) -
			    strlen(text), " %s", game_str(g, S_SHIP_SUNK));
		if (g->arrive_result == ARRIVE_SUNK && g->arrive_last) {
			/* The last ship (VA 0x1f2a1, from a fort): prison,
			 * then traded back (CSTR 175, 190). */
			char more[600];

			fill(more, sizeof(more), game_str(g, S_IMPRISONED),
			    t->name, NULL);
			snprintf(text + strlen(text), sizeof(text) -
			    strlen(text), "\r%s", more);
			fill(more, sizeof(more), game_str(g, S_TRADED_BACK),
			    game_word(g, W_CAPTAIN,
			    (size_t)g->sim.player.difficulty), NULL);
			snprintf(text + strlen(text), sizeof(text) -
			    strlen(text), "\r%s", more);
		}
		draw(g, FONT_BODY, ARR_TEXT_X, 40, 290, text, 0);
		continue_prompt(g);
		return;
	}
	n = t->forts;
	forts[0] = '\0';
	if (n == 1)
		snprintf(forts, sizeof(forts), "%s", game_word(g, W_ARRIVAL,
		    ARR_ONE_FORT + (inland ? 8 : 0)));
	else if (n > 1)
		fill(forts, sizeof(forts), game_word(g, W_ARRIVAL, ARR_FORTS +
		    (inland ? 8 : 0)), game_word(g, W_NUMBER, (size_t)n),
		    NULL);
	arg[0] = game_word(g, W_ARRIVAL, ARR_INLAND + (inland ? 0 : 1));
	arg[1] = t->name;
	arg[2] = forts;
	arg[3] = game_word(g, W_NATION, t->nation & 3);
	arg[4] = "";	/* the treasure fleet or silver train: not modelled */
	fill_args(text, sizeof(text), game_str(g, S_ARRIVED), arg, 5);
	draw(g, FONT_BODY, ARR_TEXT_X, 40, 290, text, 0);
	snprintf(label, sizeof(label), "%s", game_word(g, W_ARRIVAL,
	    ARR_SAIL_IN));
	fill(text, sizeof(text), game_str(g, S_ARRIVE_MENU), label, NULL);
	draw(g, FONT_BODY, ARR_TEXT_X, 260, 290, text, 1);
	if (g->msg[0] != '\0')
		draw(g, FONT_SMALL, ARR_TEXT_X, 440, 290, g->msg, 0);
}

const struct screen screen_arrive = {
	"arrive", arrive_enter, arrive_event, NULL, arrive_draw,
};

/*
 * Disgrace (VA 0x16384): the captain's portrait in its frame, as for the
 * story (VA 0x159df), and CSTR 174 at (100, 350) up to x 560.
 */
static void
disgrace_enter(struct game *g)
{
	game_background(g, "pic/frame");
	game_portrait(g, NULL);
	load_bank(g, captain_portrait[g->sim.player.difficulty & 3], 0,
	    &g->id_spr[2]);
	if (g->bg.pixels != NULL)
		memcpy(g->ink, &g->bg.palette[STORY_INK * 3], 3);
}

static void
disgrace_event(struct game *g, const struct ui_event *ev)
{
	if (ev->type == EV_CLICK || (ev->type == EV_KEY &&
	    (ev->key == UI_KEY_ENTER || ev->key == UI_KEY_ESCAPE)))
		game_goto(g, &screen_sail);
}

static void
disgrace_draw(struct game *g)
{
	id_frame(g, 2, 0, 236, 58);
	draw(g, FONT_BODY, 100, 350, 460, game_str(g, S_DISGRACE), 0);
	continue_prompt(g);
}

const struct screen screen_disgrace = {
	"disgrace", disgrace_enter, disgrace_event, NULL, disgrace_draw,
};

/*
 * The assault (VA 0x18b3f): on landpir.pcx, "Your men scale the walls of
 * the fort..." (CSTR 192), or in a town without forts "As you and your
 * men enter the town..." (CSTR 211), at (20, 150) up to x 250.  Then
 * the sword fight with the commander.
 */
#define S_SCALE_WALLS		192
#define S_ENTER_TOWN		211
#define S_TOWN_AT_MERCY		193
#define S_INSTALL		194
#define S_GOLD_HIDDEN		195
#define W_HIDDEN		15

static void
assault_enter(struct game *g)
{
	game_background(g, "pic/landpir");
	game_portrait(g, NULL);
	if (g->bg.pixels != NULL)
		memcpy(g->ink, &g->bg.palette[STORY_INK * 3], 3);
}

static void
assault_event(struct game *g, const struct ui_event *ev)
{
	if (ev->type == EV_CLICK || (ev->type == EV_KEY &&
	    (ev->key == UI_KEY_ENTER || ev->key == UI_KEY_ESCAPE))) {
		g->sw_assault = 1;
		game_goto(g, &screen_sword);
	}
}

static void
assault_draw(struct game *g)
{
	draw(g, FONT_BODY, 20, 150, 230, game_str(g,
	    g->sim.towns[g->assault_town].forts > 0 ? S_SCALE_WALLS :
	    S_ENTER_TOWN), 0);
	continue_prompt(g);
}

const struct screen screen_assault = {
	"assault", assault_enter, assault_event, NULL, assault_draw,
};

/*
 * The sack (VA 0x22764): on the town's arrival picture, "The once-proud
 * town of %s is at your mercy..." (CSTR 193, at (340, 150) up to x 620);
 * when warned, "%s of their gold has been hidden away" (CSTR 195); with
 * a crew big enough, on governor.pcx, the new governor's nation (CSTR
 * 194); then the loot on the trading board.  The plunder's cannon join
 * the fleet at once (an engine choice: the original offers them on the
 * board).
 */
enum { SACK_MERCY, SACK_HIDDEN, SACK_INSTALL };

static void
sack_picture(struct game *g)
{
	const struct sim_town *t = &g->sim.towns[g->assault_town];

	game_background(g, g->sack_stage == SACK_INSTALL ? "pic/governor" :
	    t->flags & TOWN_INLAND ? "pic/arr_land" : "pic/arr_sea");
	game_portrait(g, NULL);
	if (g->bg.pixels != NULL) {
		memcpy(g->ink, &g->bg.palette[STORY_INK * 3], 3);
		memcpy(g->highlight, &g->bg.palette[6 * 3], 3);
	}
	g->selected = 0;
}

static void
sack_enter(struct game *g)
{
	sack_picture(g);
}

static void
sack_loot(struct game *g)
{
	g->sim.fleet.guns += g->sack.cannon;
	g->looting = 1;
	game_goto(g, &screen_merchant);
}

static void
sack_install(struct game *g, int i)
{
	sim_install_governor(&g->sim, g->assault_town, i);
	sack_loot(g);
}

static void
sack_event(struct game *g, const struct ui_event *ev)
{
	int go = ev->type == EV_CLICK || (ev->type == EV_KEY &&
	    (ev->key == UI_KEY_ENTER || ev->key == UI_KEY_ESCAPE));

	if (g->sack_stage == SACK_INSTALL) {
		menu_event(g, ev, 4, NULL, sack_install, NULL);
		return;
	}
	if (!go)
		return;
	if (g->sack_stage == SACK_MERCY && g->sack.hidden >= 0)
		g->sack_stage = SACK_HIDDEN;
	else if (g->sack.conquest) {
		g->sack_stage = SACK_INSTALL;
		sack_picture(g);
	} else
		sack_loot(g);
}

static void
sack_draw(struct game *g)
{
	char text[600];

	switch (g->sack_stage) {
	case SACK_MERCY:
		fill(text, sizeof(text), game_str(g, S_TOWN_AT_MERCY),
		    g->sim.towns[g->assault_town].name, NULL);
		break;
	case SACK_HIDDEN:
		fill(text, sizeof(text), game_str(g, S_GOLD_HIDDEN),
		    game_word(g, W_HIDDEN, (size_t)g->sack.hidden), NULL);
		break;
	default:
		draw(g, FONT_BODY, 340, 150, 280, game_str(g, S_INSTALL), 1);
		return;
	}
	draw(g, FONT_BODY, 340, 150, 280, text, 0);
	continue_prompt(g);
}

const struct screen screen_sack = {
	"sack", sack_enter, sack_event, NULL, sack_draw,
};

/*
 * Prison (VA 0x16647), after a defeat: on prison.pcx, "...You are
 * imprisoned in %s." (CSTR 175), then "Your former comrades take pity
 * on you..." (CSTR 190), at (300, 150) up to x 560.  What happens in
 * prison (VA 0x1f803) is not traced yet.
 */
static void
prison_enter(struct game *g)
{
	game_background(g, "pic/prison");
	game_portrait(g, NULL);
	if (g->bg.pixels != NULL)
		memcpy(g->ink, &g->bg.palette[STORY_INK * 3], 3);
	g->sack_stage = 0;
}

static void
prison_event(struct game *g, const struct ui_event *ev)
{
	if (!(ev->type == EV_CLICK || (ev->type == EV_KEY &&
	    (ev->key == UI_KEY_ENTER || ev->key == UI_KEY_ESCAPE))))
		return;
	if (g->sack_stage++ > 0)
		game_goto(g, &screen_sail);
}

static void
prison_draw(struct game *g)
{
	char text[600];

	if (g->sack_stage == 0)
		fill(text, sizeof(text), game_str(g, S_IMPRISONED),
		    g->sim.towns[g->assault_town].name, NULL);
	else
		fill(text, sizeof(text), game_str(g, S_TRADED_BACK),
		    game_word(g, W_CAPTAIN, (size_t)g->sim.player.difficulty),
		    NULL);
	draw(g, FONT_BODY, 300, 150, 260, text, 0);
	continue_prompt(g);
}

const struct screen screen_prison = {
	"prison", prison_enter, prison_event, NULL, prison_draw,
};
