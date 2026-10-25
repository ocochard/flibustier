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
#include <string.h>

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
#define S_CONTINUE		71
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
#define S_PRIZE			164	/* "we've captured %sa %s ton %s..." */
#define S_YOU_SUNK		167
#define S_FOE_SUNK		168
#define S_SUNSET		169
#define S_STRUCK		170
#define S_BOARDING		171	/* "...you spot %s approaching" */
#define SAILING_RELOADING	0
#define SAILING_LOADED		1
#define SAILING_BATTLE_SAILS	2
#define SAILING_FULL_SAILS	3
#define SAILING_CAPTAIN		7	/* "the enemy captain" */
#define SAILING_GUNS		10	/* "Guns: " */
#define SAILING_MEN		11	/* "Men: " */
#define TRAVEL_CONTINUE		0
#define TRAVEL_SAVE		8
#define TRAVEL_EXIT		9

#define WELCOME_HEADING		6
#define INTRO_TICKS		150

/* Text fonts by size: PIRATE1 is the largest (25 px). */
#define FONT_TITLE		0
#define FONT_BODY		1
#define FONT_SMALL		2	/* PIRATE3, 14 px */

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
 * Draw a word list as menu options on a grid of "cols" columns, and record
 * their hit boxes for input.
 */
static void
draw_grid(struct game *g, size_t group, int count, int x, int y, int cols,
    int colw)
{
	struct text_target t = { plot, &g->fb };
	struct text_style st;
	struct text_layout lay;
	char buf[128];
	int i, line;

	style(g, FONT_BODY, 0, &st);
	st.line_gap = 0;	/* the scroll is short */
	line = st.font->height + st.line_gap;
	memset(&g->options, 0, sizeof(g->options));
	for (i = 0; i < count && i < TEXT_MAX_OPTIONS; i++) {
		snprintf(buf, sizeof(buf), "~%s",
		    game_word(g, group, (size_t)i));
		text_draw(&t, &st, x + (i % cols) * colw, y + (i / cols) * line,
		    colw, buf, i == g->selected ? 0 : -1, &lay);
		g->options.option[i] = lay.option[0];
		g->options.noptions = i + 1;
	}
}

static void
draw_portrait(struct game *g)
{
	const struct frame *f;

	if (g->portrait.count == 0)
		return;
	f = &g->portrait.frames[0];
	fb_frame_clip(&g->fb, f, (OVAL_X0 + OVAL_X1 - f->w) / 2,
	    (OVAL_Y0 + OVAL_Y1 - f->h) / 2, OVAL_X0, OVAL_Y0, OVAL_X1, OVAL_Y1);
}

/* Intro: the publisher's logo, until a key, a click or a few seconds. */

static void
intro_enter(struct game *g)
{
	game_background(g, "pic/micro");
	game_portrait(g, NULL);
}

static void
intro_event(struct game *g, const struct ui_event *ev)
{
	if (ev->type == EV_KEY || ev->type == EV_CLICK)
		game_goto(g, &screen_welcome);
}

static void
intro_tick(struct game *g)
{
	if (g->ticks == INTRO_TICKS)
		game_goto(g, &screen_welcome);
}

static void
intro_draw(struct game *g)
{
	(void)g;
}

const struct screen screen_intro = {
	"intro", intro_enter, intro_event, intro_tick, intro_draw,
};

/* Welcome menu. */

enum { WELCOME_NEW, WELCOME_LOAD, WELCOME_FAMOUS, WELCOME_QUIT };

static void
welcome_enter(struct game *g)
{
	game_background(g, "pic/credpic");
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
	char buf[512], opt[512];
	int i, y = 40;

	for (i = 0; i < WELCOME_HEADING; i++)
		y += draw(g, i < 5 ? FONT_TITLE : FONT_BODY, 400, y, 230,
		    game_word(g, W_WELCOME, (size_t)i), 0) + (i == 4 ? 16 : 0);
	buf[0] = '\0';
	for (i = 0; i < 4; i++) {
		snprintf(opt, sizeof(opt), "%s~%s", i > 0 ? "\r" : "",
		    game_word(g, W_WELCOME, (size_t)(WELCOME_HEADING + i)));
		strncat(buf, opt, sizeof(buf) - strlen(buf) - 1);
	}
	draw(g, FONT_BODY, 410, y + 10, 220, buf, 1);
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

static void
era_draw(struct game *g)
{
	char buf[512];

	draw_centered(g, FONT_TITLE, 40, game_str(g, S_SELECT_ERA));
	options(g, W_ERA, 6, buf, sizeof(buf));
	draw(g, FONT_BODY, 170, 100, 400, buf, 1);
	draw(g, FONT_BODY, 80, 300, 480,
	    game_str(g, S_ERA_TEXT + (size_t)g->selected), 0);
}

const struct screen screen_era = {
	"era", parchment_enter, era_event, NULL, era_draw,
};

/* Famous expedition choice. */

static void
expedition_choose(struct game *g, int i)
{
	g->career.expedition = i;
	g->career.era = expedition_era[i];
	game_goto(g, &screen_name);
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

/* Nationality, with the nation's flag in the oval. */

static void
identity_enter(struct game *g)
{
	game_background(g, "pic/idselect");
	game_portrait(g, NULL);
}

static int
nation_enabled(const struct game *g, int i)
{
	/* No Dutch careers in 1560: the scenario slot is empty. */
	return (!(i == NATION_HOLLAND && g->career.era == 0));
}

static void
nation_choose(struct game *g, int i)
{
	g->career.nation = i;
	game_goto(g, &screen_name);
}

static void
nation_event(struct game *g, const struct ui_event *ev)
{
	menu_event(g, ev, 4, nation_enabled, nation_choose, &screen_era);
}

static void
nation_draw(struct game *g)
{
	int y;

	if (g->portrait.count == 0 || g->ticks == 0 ||
	    g->career.nation != g->selected) {
		game_portrait(g, nation_flag[g->selected & 3]);
		g->career.nation = g->selected;
	}
	draw_portrait(g);
	y = draw(g, FONT_BODY, SCROLL_X, SCROLL_Y, SCROLL_W,
	    game_str(g, S_NATION_PROMPT), 0);
	draw_grid(g, W_NATION, 4, SCROLL_X + 40, SCROLL_Y + y + 4, 2,
	    SCROLL_COL);
}

const struct screen screen_nation = {
	"nation", identity_enter, nation_event, NULL, nation_draw,
};

/* Name entry. */

/* Captain portraits; assumed to follow the town nation order. */
static const char *captain_portrait[4] = {
	"spr/bigman0", "spr/bigman1", "spr/bigman2", "spr/bigman3",
};

static void
name_enter(struct game *g)
{
	identity_enter(g);
	game_portrait(g, captain_portrait[g->career.nation & 3]);
	g->career.name[0] = '\0';
}

static void
name_event(struct game *g, const struct ui_event *ev)
{
	size_t len = strlen(g->career.name);

	if (ev->type == EV_CHAR && ev->ch >= 0x20 && ev->ch < 0x7F &&
	    len < NAME_MAX_LEN) {
		g->career.name[len] = (char)ev->ch;
		g->career.name[len + 1] = '\0';
	} else if (ev->type == EV_KEY && ev->key == UI_KEY_BACKSPACE &&
	    len > 0)
		g->career.name[len - 1] = '\0';
	else if (ev->type == EV_KEY && ev->key == UI_KEY_ENTER && len > 0)
		game_goto(g, &screen_difficulty);
	else if (ev->type == EV_KEY && ev->key == UI_KEY_ESCAPE)
		game_goto(g, g->career.expedition >= 0 ? &screen_expedition :
		    &screen_nation);
}

static void
name_draw(struct game *g)
{
	char buf[NAME_MAX_LEN * 2 + 4];
	int y;

	draw_portrait(g);
	y = draw(g, FONT_BODY, SCROLL_X, SCROLL_Y, SCROLL_W,
	    game_str(g, S_NAME_PROMPT), 0);
	/* A blinking cursor after the name typed so far. */
	snprintf(buf, sizeof(buf), "%s%s", g->career.name,
	    (g->ticks / 30) % 2 ? "_" : "");
	draw(g, FONT_TITLE, SCROLL_X + 40, SCROLL_Y + y + 6, SCROLL_W, buf, 0);
}

const struct screen screen_name = {
	"name", name_enter, name_event, NULL, name_draw,
};

/* Difficulty level. */

static void
difficulty_choose(struct game *g, int i)
{
	g->career.difficulty = i;
	game_goto(g, &screen_ability);
}

static void
difficulty_event(struct game *g, const struct ui_event *ev)
{
	menu_event(g, ev, 4, NULL, difficulty_choose, &screen_name);
}

static void
difficulty_draw(struct game *g)
{
	int y;

	y = draw(g, FONT_BODY, SCROLL_X, SCROLL_Y, SCROLL_W,
	    game_str(g, S_DIFFICULTY_PROMPT), 0);
	draw_grid(g, W_DIFFICULTY, 4, SCROLL_X + 40, SCROLL_Y + y + 4, 2,
	    SCROLL_COL);
}

const struct screen screen_difficulty = {
	"difficulty", identity_enter, difficulty_event, NULL, difficulty_draw,
};

/* Special ability, with its portrait in the oval. */

static void
ability_choose(struct game *g, int i)
{
	g->career.ability = i;
	game_goto(g, &screen_summary);
}

static void
ability_event(struct game *g, const struct ui_event *ev)
{
	menu_event(g, ev, 5, NULL, ability_choose, &screen_difficulty);
}

static void
ability_draw(struct game *g)
{
	int y;

	if (g->portrait.count == 0 || g->career.ability != g->selected) {
		game_portrait(g, ability_portrait[g->selected % 5]);
		g->career.ability = g->selected;
	}
	draw_portrait(g);
	y = draw(g, FONT_BODY, SCROLL_X, SCROLL_Y, SCROLL_W,
	    game_str(g, S_ABILITY_PROMPT), 0);
	draw_grid(g, W_ABILITY, 5, SCROLL_X + 40, SCROLL_Y + y + 4, 2,
	    SCROLL_COL);
}

const struct screen screen_ability = {
	"ability", identity_enter, ability_event, NULL, ability_draw,
};

/* Summary: the career is created; the voyage itself is the next phase. */

static void
summary_enter(struct game *g)
{
	parchment_enter(g);
	if (game_start_career(g) != 0)
		snprintf(g->err, sizeof(g->err), "cannot start the career");
}

static void
summary_event(struct game *g, const struct ui_event *ev)
{
	if (g->started && (ev->type == EV_CLICK ||
	    (ev->type == EV_KEY && ev->key == UI_KEY_ENTER)))
		game_goto(g, &screen_voyage);
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
	draw_centered(g, FONT_BODY, 420, game_str(g, S_CONTINUE));
}

const struct screen screen_summary = {
	"summary", summary_enter, summary_event, NULL, summary_draw,
};

/*
 * Replace each %s of a log sentence with the next argument, as the
 * game's printf-style strings expect.
 */
static void
fill(char *out, size_t len, const char *fmt, const char *a, const char *b)
{
	const char *arg[2] = { a, b };
	size_t n = 0;
	int k = 0;

	while (*fmt != '\0' && n + 1 < len) {
		if (fmt[0] == '%' && fmt[1] == 's') {
			const char *v = k < 2 && arg[k] != NULL ? arg[k] : "";

			k++;
			while (*v != '\0' && n + 1 < len)
				out[n++] = *v++;
			fmt += 2;
		} else
			out[n++] = *fmt++;
	}
	out[n] = '\0';
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
 * Voyage: a stand-in for the sailing screens of the next phase, with the
 * game's own in-game menu words: continue travels (a month passes),
 * save, or exit.
 */

static const int travel_items[3] = {
	TRAVEL_CONTINUE, TRAVEL_SAVE, TRAVEL_EXIT,
};

static int
voyage_enabled(const struct game *g, int i)
{
	return (travel_items[i] != TRAVEL_SAVE || g->save_dir[0] != '\0');
}

static void
voyage_choose(struct game *g, int i)
{
	switch (travel_items[i]) {
	case TRAVEL_CONTINUE:
		g->msg[0] = '\0';
		game_goto(g, g->world != NULL ? &screen_sail : &screen_voyage);
		break;
	case TRAVEL_SAVE:
		g->saving = 1;
		game_goto(g, &screen_slots);
		break;
	case TRAVEL_EXIT:
		game_goto(g, &screen_retire);
		break;
	}
}

static void
voyage_event(struct game *g, const struct ui_event *ev)
{
	menu_event(g, ev, 3, voyage_enabled, voyage_choose, NULL);
}

static void
voyage_draw(struct game *g)
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
	/* The latest news, oldest first. */
	kept = sim_news_kept(&g->sim);
	first = kept > 12 ? kept - 12 : 0;
	for (i = first; i < kept; i++) {
		n = sim_news_at(&g->sim, i);
		news_text(g, n, line, sizeof(line));
		y += draw(g, FONT_BODY, 80, y, 480, line, 0);
	}
	buf[0] = '\0';
	for (i = 0; i < 3; i++) {
		snprintf(line, sizeof(line), "%s~%s", i > 0 ? "\r" : "",
		    game_word(g, W_TRAVEL, (size_t)travel_items[i]));
		strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
	}
	draw(g, FONT_BODY, 240, 370, 300, buf, 1);
	if (g->msg[0] != '\0')
		draw_centered(g, FONT_BODY, 440, g->msg);
}

const struct screen screen_voyage = {
	"voyage", parchment_enter, voyage_event, NULL, voyage_draw,
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
		game_goto(g, &screen_voyage);
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
	game_goto(g, &screen_voyage);
}

static void
slots_event(struct game *g, const struct ui_event *ev)
{
	menu_event(g, ev, SAVE_SLOTS, slots_enabled, slots_choose,
	    g->saving ? &screen_voyage : &screen_welcome);
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
	menu_event(g, ev, 2, NULL, retire_choose, &screen_voyage);
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
		game_goto(g, &screen_voyage);
}

static void
sail_tick(struct game *g)
{
	int town;

	if (g->fading != 0)
		return;		/* the ship waits for the screen */
	switch (sim_sail(&g->sim, g->turn, 1, &town)) {
	case SAIL_TOWN:
		game_goto(g, &screen_voyage);
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

	sea_animate(&g->sea, g->ticks / 6, g->fb.pal);
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
	int y;

	if (g->enc_result < 0)
		encounter_picture(g);

	if (g->enc_result >= 0) {
		text_band(g, 300, 150);
		y = 310;
		switch (g->enc_result) {
		case RES_OVERTAKEN:
			y += draw(g, FONT_BODY, 80, y, 480,
			    game_str(g, S_OVERTAKEN), 0);
			break;
		case RES_UNANSWERED:
			y += draw(g, FONT_BODY, 80, y, 480,
			    game_str(g, S_UNANSWERED), 0);
			break;
		case RES_NEWS:
			kept = sim_news_kept(&g->sim);
			for (i = kept > 4 ? kept - 4 : 0; i < kept; i++) {
				n = sim_news_at(&g->sim, i);
				news_text(g, n, text, sizeof(text));
				y += draw(g, FONT_BODY, 80, y, 480, text, 0);
			}
			break;
		}

		draw_centered(g, FONT_BODY, 430, game_str(g, S_CONTINUE));
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
	text_band(g, 300, 150);
	draw(g, FONT_BODY, 80, 310, 480, text, 1);
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
	load_bank(g, damage_picture[type % 9], 1, &g->btl_damage[side]);
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
	g->turn = 0;
	g->ink[0] = g->ink[1] = g->ink[2] = 240;
}

static void
battle_finish(struct game *g, int keep)
{
	g->btl_final = (int)sim_battle_finish(&g->sim, keep);
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
	if (g->btl_stage == 1) {
		if (!((ev->type == EV_KEY && ev->key == UI_KEY_ENTER) ||
		    ev->type == EV_CLICK))
			return;
		/* Boarding is decided by crews until sword fights exist. */
		if (outcome == BAT_SURRENDER || (outcome == BAT_BOARDING &&
		    g->sim.battle.you.crew >= g->sim.battle.foe.crew)) {
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

static void
battle_tick(struct game *g)
{
	if (g->fading != 0 || g->btl_stage != 0)
		return;
	sim_battle_tick(&g->sim, g->turn, g->btl_fire, g->btl_sails);
	g->btl_fire = 0;
	g->btl_sails = 0;
	if (g->sim.battle.outcome != BAT_NONE) {
		g->btl_stage = 1;
		g->turn = 0;
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
	const struct sim_fleet *f = &g->sim.fleet;
	char text[600], from[64], tons[16], guns[16];
	int cx0, cy0, cx, cy, lvl;

	/* The sea around the fleet, at twice the sailing scale. */
	cx0 = f->x / WORLD_TILE - AREA_CELLS / 2;
	cy0 = f->y / WORLD_TILE - AREA_CELLS / 2;
	if (g->btl_tiles.tile_w == 32)
		for (cy = 0; cy < AREA_CELLS; cy++)
			for (cx = 0; cx < AREA_CELLS; cx++) {
				struct image t;
				int mx = clampi(cx0 + cx, 0, WORLD_CELLS_W - 1);
				int my = clampi(cy0 + cy, 0, WORLD_CELLS_H - 1);

				t.w = t.h = 32;
				t.pixels = g->btl_tiles.pixels + 32 * 32 *
				    g->world->map[my * WORLD_CELLS_W + mx];
				t.has_palette = 0;
				fb_image(&g->fb, &t, cx * 32, cy * 32, 0);
			}
	battle_ship_draw(g, 0, &bt->you);
	battle_ship_draw(g, 1, &bt->foe);
	if (bt->you.shot.ticks > 0)
		fb_rect(&g->fb, bt->you.shot.x / 256 - 1, bt->you.shot.y / 256 -
		    1, 3, 3, fb_nearest(&g->fb, 20, 20, 20));
	if (bt->foe.shot.ticks > 0)
		fb_rect(&g->fb, bt->foe.shot.x / 256 - 1, bt->foe.shot.y / 256 -
		    1, 3, 3, fb_nearest(&g->fb, 20, 20, 20));

	/* The panel: the enemy above, the player below, the wind between. */
	battle_side_text(g, 14, &bt->foe, -1);
	battle_side_text(g, 358, &bt->you, bt->you.reload > 0 ?
	    SAILING_RELOADING : -1);
	lvl = bt->foe.damage / 16 > 4 ? 4 : bt->foe.damage / 16;
	if (g->btl_damage[1].count == 5)
		fb_frame(&g->fb, &g->btl_damage[1].frames[lvl], 489, 137);
	lvl = bt->you.damage / 16 > 4 ? 4 : bt->you.damage / 16;
	if (g->btl_damage[0].count == 5)
		fb_frame(&g->fb, &g->btl_damage[0].frames[lvl], 489, 258);
	fill(from, sizeof(from), game_word(g, W_SAILING, SAILING_FROM),
	    game_word(g, W_COMPASS, (size_t)(bt->wind >> 4)), NULL);
	draw(g, FONT_SMALL, PANEL_X + 40, 233, 100, from, 0);

	if (g->btl_stage == 0)
		return;
	text_band(g, 300, 150);
	if (g->btl_stage == 2) {
		snprintf(tons, sizeof(tons), "%d", g->sim.encounter.cargo);
		snprintf(guns, sizeof(guns), "%d", bt->foe.guns);
		/* "%sa %s ton %s with %s guns... space for %s tons" */
		{
			const char *fmt = game_str(g, S_PRIZE);
			char a[600];

			fill(a, sizeof(a), fmt, "", tons);
			fill(text, sizeof(text), a, game_word(g, W_SHIP_TYPE,
			    (size_t)(bt->foe.type % 10)), guns);
			fill(a, sizeof(a), text, "?", NULL);
			snprintf(text, sizeof(text), "%s", a);
		}
		draw(g, FONT_BODY, 80, 310, 400, text, 1);
		return;
	}
	switch (bt->outcome) {
	case BAT_PLAYER_SUNK:
		snprintf(text, sizeof(text), "%s", game_str(g, S_YOU_SUNK));
		break;
	case BAT_ENEMY_SUNK:
		snprintf(text, sizeof(text), "%s", game_str(g, S_FOE_SUNK));
		break;
	case BAT_SUNSET:
		snprintf(text, sizeof(text), "%s", game_str(g, S_SUNSET));
		break;
	case BAT_SURRENDER:
		snprintf(text, sizeof(text), "%s", game_str(g, S_STRUCK));
		break;
	case BAT_BOARDING:
		fill(text, sizeof(text), game_str(g, S_BOARDING),
		    game_word(g, W_SAILING, SAILING_CAPTAIN), NULL);
		strncat(text, "\r(Sword fights are not implemented yet: the "
		    "larger crew wins.)", sizeof(text) - strlen(text) - 1);
		break;
	default:	/* out of range: the battle simply ends */
		text[0] = '\0';
		break;
	}
	draw(g, FONT_BODY, 80, 310, 400, text, 0);
	draw_centered(g, FONT_BODY, 430, game_str(g, S_CONTINUE));
}

const struct screen screen_battle = {
	"battle", battle_enter, battle_event, battle_tick, battle_draw,
};
