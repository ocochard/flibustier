/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "datafields.h"
#include "uitables.h"

#define FIELD(name, member, type, d0, d1)				\
	{ name, offsetof(struct ui_tables, member), type, { d0, d1, 0 } }

static const struct data_field cabin_fields[] = {
	FIELD("spots", cabin, DATA_I16, UI_CABIN_SPOTS, 5),
};
static const struct data_field tavern_fields[] = {
	FIELD("spots", tavern, DATA_I16, UI_TAVERN_SPOTS, 4),
};
static const struct data_field town_fields[] = {
	FIELD("spots", town, DATA_I16, UI_TOWN_SPOTS, 4),
};
static const struct data_field weapon_fields[] = {
	FIELD("spots", weapon, DATA_I16, UI_WEAPON_SPOTS, 4),
};
static const struct data_field board_fields[] = {
	FIELD("pieces", board, DATA_I16, UI_BOARD_PIECES, 4),
};
static const struct data_field cave_fields[] = {
	FIELD("piles", cave, DATA_I32, UI_CAVE_PILES, 3),
};
static const struct data_field panel_fields[] = {
	FIELD("ship_x", panel.ship_x, DATA_I16, UI_PANEL_SHIPS, 0),
	FIELD("ship_y", panel.ship_y, DATA_I16, UI_PANEL_SHIPS, 0),
	FIELD("spots", panel.spot, DATA_I16, UI_PANEL_SHIPS + 1, 5),
	FIELD("type_x", panel.type_x, DATA_I16, UI_PANEL_SHIPS, 0),
	FIELD("type_y", panel.type_y, DATA_I16, UI_PANEL_SHIPS, 0),
	FIELD("damage_x", panel.damage_x, DATA_I16, UI_PANEL_SHIPS, 0),
	FIELD("damage_y", panel.damage_y, DATA_I16, UI_PANEL_SHIPS, 0),
};
static const struct data_field flag_fields[] = {
	FIELD("flags", jolly, DATA_U16, 4, 8),
};

#define TABLE(id, name, f)	{ id, name, f, sizeof(f) / sizeof(f[0]) }

/* The tables a pack may give, and the export's file for each. */
static const struct {
	const char		*id;
	const char		*name;
	const struct data_field	*f;
	size_t			 n;
} tables[] = {
	TABLE("data/layout/cabin", "layout-cabin.json", cabin_fields),
	TABLE("data/layout/tavern", "layout-tavern.json", tavern_fields),
	TABLE("data/layout/town", "layout-town.json", town_fields),
	TABLE("data/layout/weapons", "layout-weapons.json", weapon_fields),
	TABLE("data/layout/board", "layout-board.json", board_fields),
	TABLE("data/layout/cave", "layout-cave.json", cave_fields),
	TABLE("data/layout/fleet-panel", "layout-fleet-panel.json",
	    panel_fields),
	TABLE("data/captain-flags", "captain-flags.json", flag_fields),
};
#define NTABLES	(sizeof(tables) / sizeof(tables[0]))

/* "n" little-endian words of records "stride" bytes apart, "per" each. */
static int
read_records(const struct exe_layout *layout, const uint8_t *exe,
    size_t len, uint32_t va, int16_t *out, int n, int per, int stride)
{
	const uint8_t *p;
	int i, k;

	for (i = 0; i < n; i++) {
		if ((p = exe_at(layout, exe, len, va + (uint32_t)(i * stride),
		    (size_t)(2 * per))) == NULL)
			return (-1);
		for (k = 0; k < per; k++)
			out[i * per + k] = (int16_t)(p[2 * k] | p[2 * k + 1] << 8);
	}
	return (0);
}

/* The mouse pointer: u16 w, h, then w * h pixels.  Optional. */
static void
load_cursor(struct ui_tables *t, const struct exe_layout *layout,
    const uint8_t *exe, size_t len)
{
	const uint8_t *p;
	int w, h;

	if ((p = exe_at(layout, exe, len, layout->cursor_va, 4)) == NULL)
		return;
	w = p[0] | p[1] << 8;
	h = p[2] | p[3] << 8;
	if (w <= 0 || h <= 0 || w > UI_CURSOR_MAX || h > UI_CURSOR_MAX ||
	    (p = exe_at(layout, exe, len, layout->cursor_va + 4,
	    (size_t)(w * h))) == NULL)
		return;
	memcpy(t->cursor, p, (size_t)(w * h));
	t->cursor_w = w;
	t->cursor_h = h;
}

/*
 * The original's story scripts (VA 0x35968): one per scenario, separated
 * by 0xFF.  A CSTR number (or its negation) shows that text, -2 turns
 * the page, ')' waits (the copy-protection question's, a plain wait
 * here), 'a' fights, and 0 or 6 ends; '*' and '2'-'7' show nothing.
 * What follows the first end, the question's failing branch, is not
 * kept.
 */
#define STORY_BYTES	4096

static int
stories_from_exe(struct ui_tables *t, const uint8_t *p)
{
	struct ui_story_step *st;
	size_t i = 0;
	int n, c, done;

	for (n = 0; n < UI_STORIES && i < STORY_BYTES; n++, i++) {
		for (done = 0; i < STORY_BYTES && p[i] != 0xFF; i++) {
			c = (int8_t)p[i];
			if (done || c == '*' || (c >= '2' && c <= '7'))
				continue;
			if (c == 0 || c == 6) {
				done = 1;
				continue;
			}
			if (t->nstory[n] == UI_STORY_STEPS)
				return (-1);
			st = &t->story[n][t->nstory[n]++];
			st->op = c == -2 ? UI_STORY_PAGE : c == ')' ?
			    UI_STORY_WAIT : c == 'a' ? UI_STORY_FIGHT :
			    UI_STORY_TEXT;
			if (st->op == UI_STORY_TEXT)
				st->text = (int16_t)(c < 0 ? -c : c);
		}
	}
	return (0);
}

static const char *const story_ops[] = { "text", "page", "wait", "fight" };

/*
 * data/stories: {"stories": [30 lists of steps]}, a step "page", "wait",
 * "fight", or {"text": <CSTR index>}.
 */
static int
stories_decode(struct ui_tables *t, const struct json *j, char *err,
    size_t errlen)
{
	const struct json *list, *steps, *st, *v;
	size_t n, i;
	int op;

	list = json_get(j, "stories");
	if (list == NULL || list->type != JSON_ARRAY ||
	    list->count != UI_STORIES) {
		snprintf(err, errlen, "data/stories: \"stories\" must list "
		    "%d stories", UI_STORIES);
		return (-1);
	}
	memset(t->story, 0, sizeof(t->story));
	memset(t->nstory, 0, sizeof(t->nstory));
	for (n = 0; n < UI_STORIES; n++) {
		steps = list->items[n];
		if (steps->type != JSON_ARRAY ||
		    steps->count > UI_STORY_STEPS)
			goto bad;
		for (i = 0; i < steps->count; i++) {
			st = steps->items[i];
			if (st->type == JSON_STRING) {
				for (op = UI_STORY_PAGE; op <= UI_STORY_FIGHT &&
				    strcmp(st->str, story_ops[op]) != 0; op++)
					continue;
				if (op > UI_STORY_FIGHT)
					goto bad;
				t->story[n][i].op = (uint8_t)op;
			} else if (st->type == JSON_OBJECT && st->count == 1 &&
			    (v = json_get(st, "text")) != NULL &&
			    v->type == JSON_INT && v->num >= 0 &&
			    v->num <= 1023) {
				t->story[n][i].op = UI_STORY_TEXT;
				t->story[n][i].text = (int16_t)v->num;
			} else
				goto bad;
		}
		t->nstory[n] = (uint8_t)steps->count;
	}
	return (0);
bad:
	snprintf(err, errlen, "data/stories: story %zu: at most %d steps, "
	    "each \"page\", \"wait\", \"fight\" or {\"text\": 0-1023}", n,
	    UI_STORY_STEPS);
	return (-1);
}

static void
stories_encode(const struct ui_tables *t, struct jbuf *b)
{
	const struct ui_story_step *st;
	size_t n, i;

	jb_raw(b, "{\"stories\": [");
	for (n = 0; n < UI_STORIES; n++) {
		jb_raw(b, n > 0 ? ",\n  [" : "\n  [");
		for (i = 0; i < t->nstory[n]; i++) {
			st = &t->story[n][i];
			if (i > 0)
				jb_raw(b, ", ");
			if (st->op == UI_STORY_TEXT) {
				jb_raw(b, "{\"text\": ");
				jb_int(b, st->text);
				jb_raw(b, "}");
			} else
				jb_str(b, story_ops[st->op]);
		}
		jb_raw(b, "]");
	}
	jb_raw(b, "\n]}\n");
}

static int	exe_tables(struct ui_tables *, const struct exe_layout *,
		    const uint8_t *, size_t);

/* A table no pack gives, with no original data to fall back on. */
static int
missing(const char *id, char *err, size_t errlen)
{
	snprintf(err, errlen, "no %s: give the original data or a pack that "
	    "has it", id);
	return (-1);
}

/* The fleet panel's layout: words at these byte offsets (VA 0x32e68). */
static int
load_panel(struct ui_fleet_panel *f, const struct exe_layout *layout,
    const uint8_t *exe, size_t len)
{
	const uint32_t va = layout->fleet_panel_va;

	return (read_records(layout, exe, len, va, f->ship_x, 1,
	    UI_PANEL_SHIPS, 0) != 0 || read_records(layout, exe, len,
	    va + 0x10, f->ship_y, 1, UI_PANEL_SHIPS, 0) != 0 ||
	    read_records(layout, exe, len, va + 0x20, &f->spot[0][0], 1,
	    (UI_PANEL_SHIPS + 1) * 5, 0) != 0 || read_records(layout, exe,
	    len, va + 0x7c, f->type_x, 1, UI_PANEL_SHIPS, 0) != 0 ||
	    read_records(layout, exe, len, va + 0x8c, f->type_y, 1,
	    UI_PANEL_SHIPS, 0) != 0 || read_records(layout, exe, len,
	    va + 0x9c, f->damage_x, 1, UI_PANEL_SHIPS, 0) != 0 ||
	    read_records(layout, exe, len, va + 0xac, f->damage_y, 1,
	    UI_PANEL_SHIPS, 0) != 0 ? -1 : 0);
}

int
ui_tables_load(struct ui_tables *t, struct packs *packs,
    const struct exe_layout *layout, const uint8_t *exe, size_t len,
    char *err, size_t errlen)
{
	struct json *j;
	size_t n;
	int rc;

	memset(t, 0, sizeof(*t));
	if (layout != NULL && exe_tables(t, layout, exe, len) != 0) {
		snprintf(err, errlen, "screen tables missing from the "
		    "executable");
		return (-1);
	}
	/* A pack's tables over the original's; without it, all of them. */
	for (n = 0; n < NTABLES; n++) {
		if ((rc = data_load_json(packs, tables[n].id, &j, err,
		    errlen)) < 0)
			return (-1);
		if (rc == 1 && layout == NULL)
			return (missing(tables[n].id, err, errlen));
		if (rc == 1)
			continue;
		rc = data_decode(j, tables[n].f, tables[n].n, t, tables[n].id,
		    err, errlen);
		json_free(j);
		if (rc != 0)
			return (-1);
	}
	if ((rc = data_load_json(packs, "data/stories", &j, err,
	    errlen)) < 0)
		return (-1);
	if (rc == 1 && layout == NULL)
		return (missing("data/stories", err, errlen));
	if (rc == 0) {
		rc = stories_decode(t, j, err, errlen);
		json_free(j);
		if (rc != 0)
			return (-1);
	}
	return (0);
}

/* Every table from the original executable. */
static int
exe_tables(struct ui_tables *t, const struct exe_layout *layout,
    const uint8_t *exe, size_t len)
{
	const uint8_t *p;
	int i, k;

	if (read_records(layout, exe, len, layout->cabin_va, &t->cabin[0][0],
	    UI_CABIN_SPOTS, 5, 10) != 0 ||
	    read_records(layout, exe, len, layout->tavern_spots_va,
	    &t->tavern[0][0], UI_TAVERN_SPOTS, 4, 10) != 0 ||
	    read_records(layout, exe, len, layout->town_spots_va,
	    &t->town[0][0], UI_TOWN_SPOTS, 4, 10) != 0 ||
	    read_records(layout, exe, len, layout->weapon_spots_va,
	    &t->weapon[0][0], UI_WEAPON_SPOTS, 4, 10) != 0 ||
	    read_records(layout, exe, len, layout->board_va, &t->board[0][0],
	    UI_BOARD_PIECES, 4, 10) != 0 ||
	    read_records(layout, exe, len, layout->credits_va,
	    &t->credit_rect[0][0], UI_CREDIT_BLOCKS, 4, 8) != 0 ||
	    load_panel(&t->panel, layout, exe, len) != 0 ||
	    read_records(layout, exe, len, layout->jolly_va,
	    (int16_t *)&t->jolly[0][0], 4, 8, 32) != 0)
		return (-1);
	for (i = 0; i < UI_CAVE_PILES; i++) {
		if ((p = exe_at(layout, exe, len, layout->cave_va +
		    (uint32_t)i * 12, 12)) == NULL)
			return (-1);
		for (k = 0; k < 3; k++)
			t->cave[i][k] = (int32_t)(p[4 * k] | p[4 * k + 1] << 8 |
			    p[4 * k + 2] << 16 | (uint32_t)p[4 * k + 3] << 24);
	}
	if ((p = exe_at(layout, exe, len, layout->credits_va +
	    UI_CREDIT_BLOCKS * 8, sizeof(t->credit_ink))) == NULL)
		return (-1);
	memcpy(t->credit_ink, p, sizeof(t->credit_ink));
	if ((p = exe_at(layout, exe, len, layout->shade_va, 256)) == NULL)
		return (-1);
	memcpy(t->shade, p, 256);
	if ((p = exe_at(layout, exe, len, layout->story_va,
	    STORY_BYTES)) == NULL || stories_from_exe(t, p) != 0)
		return (-1);
	t->has_shade = 1;
	t->has_credits = 1;
	load_cursor(t, layout, exe, len);
	return (0);
}

int
ui_tables_export(const struct ui_tables *t,
    int (*out)(void *arg, const char *id, const char *name, struct jbuf *b),
    void *arg)
{
	struct jbuf b = { 0 };
	size_t n;
	int rc;

	for (n = 0; n < NTABLES; n++) {
		data_encode(&b, tables[n].f, tables[n].n, t, 0);
		jb_raw(&b, "\n");
		if (out(arg, tables[n].id, tables[n].name, &b) != 0) {
			jb_free(&b);
			return (-1);
		}
		jb_free(&b);
	}
	stories_encode(t, &b);
	rc = out(arg, "data/stories", "stories.json", &b);
	jb_free(&b);
	return (rc != 0 ? -1 : 0);
}
