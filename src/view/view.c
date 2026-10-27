/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data/exe.h"
#include "assets/font.h"
#include "assets/img.h"
#include "assets/fonts.h"
#include "assets/pic.h"
#include "assets/sheet.h"
#include "assets/sprite.h"
#include "assets/text.h"
#include "assets/tiles.h"
#include "assets/tileset.h"
#include "assets/towns.h"
#include "assets/world.h"
#include "gfx/sea.h"
#include "gfx/text.h"
#include "view.h"

#define WORLD_W		640	/* cells */
#define WORLD_H		480
#define TILE		16
#define VIEW_CELLS_X	(VIEW_W / TILE)
#define VIEW_CELLS_Y	(VIEW_H / TILE)
#define WORLD_STEP	10	/* cells between scroll positions */
#define WORLD_STEPS_X	((WORLD_W - VIEW_CELLS_X) / WORLD_STEP + 1)
#define WORLD_STEPS_Y	((WORLD_H - VIEW_CELLS_Y) / WORLD_STEP + 1)
#define SHEET_COLS	16
#define TEXT_FONT	"font/pirate2"
#define SMALL_FONT	"font/pirate3"

enum kind {
	K_PICTURE,
	K_BANK,
	K_GROUP,
	K_TILES,
	K_FONT,
	K_CSTR,
	K_CSTN,
	K_WORLD,
	K_TOWNS,
	K_RAW
};

struct view {
	enum kind		 kind;
	struct packs		*packs;
	char			 id[64];
	char			 info[256];
	size_t			 nframes;

	struct image		 img;
	struct sprite_bank	 bank;
	struct sprite_group	 group;
	struct tileset		 tiles;
	struct font		 font;
	struct font		 textfont;
	struct strtab		 tab;
	struct strgroups	 groups;
	size_t			 rawlen;

	uint8_t			 pal[256 * 3];
	char			 pal_id[64];

	struct world		*world;		/* world and towns views */
	uint8_t			 classes[256];
	struct sea		 sea;		/* sea colour animation */
	struct era_towns	 era;

	struct rgb		 out;
};

static const char *virtual_ids[] = {
	"world/1560", "world/1600", "world/1620", "world/1640",
	"world/1660", "world/1680",
	"towns/1560", "towns/1600", "towns/1620", "towns/1640",
	"towns/1660", "towns/1680",
};

/*
 * Default palette for paletteless sprite banks, from the pairings in the
 * original conversion scripts (SPRS.DEF, CONV.*) where known.  The first
 * matching prefix wins; a pic/ of the same name is tried before these.
 */
static const struct {
	const char	*prefix;
	const char	*pic;
} palette_guess[] = {
	{ "grp/",		"pic/alley" },
	{ "img/wpnsel",		"pic/alley" },
	{ "img/cloud",		"pic/sbat" },
	{ "cur/ship",		"pic/sbat" },
	{ "cur/sbtlmisc",	"pic/sbat" },
	{ "cur/lbtlmisc",	"pic/sbat" },
	{ "cur/cloud",		"pic/sbat" },
	{ "cur/fortbtl",	"pic/sbat" },
	{ "cur/",		"pic/lbat" },
	{ "tiles15/sbtl",	"pic/sbat" },
	{ "tiles15/lbtl",	"pic/lbat" },
	{ "spr/big",		"pic/idselect" },
	{ "spr/tcave",		"pic/trescave" },
	{ "spr/gov",		"pic/governor" },
	{ "spr/",		"pic/town" },
	{ "ui/",		"pic/town" },
};

/* Terrain class colours for the towns view (class 1..4 from the game). */
static const uint8_t class_rgb[5][3] = {
	{ 0, 0, 0 }, { 70, 130, 190 }, { 60, 120, 50 }, { 20, 50, 110 },
	{ 200, 180, 110 },
};

static const uint8_t nation_rgb[4][3] = {
	{ 230, 200, 30 }, { 220, 40, 40 }, { 240, 240, 240 }, { 240, 130, 20 },
};

static int
starts(const char *s, const char *prefix)
{
	return (strncmp(s, prefix, strlen(prefix)) == 0);
}

size_t
view_virtual_count(void)
{
	return (sizeof(virtual_ids) / sizeof(virtual_ids[0]));
}

const char *
view_virtual_id(size_t i)
{
	return (i < view_virtual_count() ? virtual_ids[i] : NULL);
}

static int
alloc_out(struct view *v, int w, int h)
{
	uint8_t *px;

	if (v->out.w == w && v->out.h == h)
		return (0);
	if ((px = realloc(v->out.px, (size_t)w * h * 3)) == NULL)
		return (-1);
	v->out.px = px;
	v->out.w = w;
	v->out.h = h;
	return (0);
}

static void
fill(struct rgb *o, uint8_t r, uint8_t g, uint8_t b)
{
	size_t i, n = (size_t)o->w * o->h;

	for (i = 0; i < n; i++) {
		o->px[3 * i] = r;
		o->px[3 * i + 1] = g;
		o->px[3 * i + 2] = b;
	}
}

static void
put(struct rgb *o, int x, int y, const uint8_t c[3])
{
	uint8_t *p;

	if (x < 0 || y < 0 || x >= o->w || y >= o->h)
		return;
	p = o->px + ((size_t)y * o->w + x) * 3;
	p[0] = c[0];
	p[1] = c[1];
	p[2] = c[2];
}

/* Checkerboard showing transparency. */
static void
checker(struct rgb *o)
{
	static const uint8_t a[3] = { 90, 90, 90 }, b[3] = { 60, 60, 60 };
	int x, y;

	for (y = 0; y < o->h; y++)
		for (x = 0; x < o->w; x++)
			put(o, x, y, ((x / 8 + y / 8) & 1) ? a : b);
}

/* Blit indexed pixels (optional mask) at (dx, dy) through "pal". */
static void
blit(struct rgb *o, int dx, int dy, int w, int h, const uint8_t *px,
    const uint8_t *mask, const uint8_t *pal)
{
	int x, y;
	size_t i;

	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			i = (size_t)y * w + x;
			if (mask == NULL || mask[i])
				put(o, dx + x, dy + y, pal + 3 * px[i]);
		}
}

static int
load_font(struct packs *packs, const char *id, struct font *f)
{
	return (font_load(packs, id, VIEW_H, f));
}

/* Text through the shared renderer, plotting packed 0xRRGGBB colours. */
static void
plot_rgb(void *arg, int x, int y, uint32_t ink)
{
	const uint8_t c[3] = { (uint8_t)(ink >> 16), (uint8_t)(ink >> 8),
	    (uint8_t)ink };

	put(arg, x, y, c);
}

static uint32_t
packed(const uint8_t c[3])
{
	return ((uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2]);
}

/* Returns the y below the text. */
static int
draw_text(struct rgb *o, const struct font *f, int x, int y, int maxw,
    const char *s, const uint8_t col[3])
{
	struct text_target t = { plot_rgb, o, NULL };
	struct text_style st;
	struct text_layout lay;

	memset(&st, 0, sizeof(st));
	st.font = f;
	st.ink = st.highlight = packed(col);
	st.line_gap = 2;
	text_draw(&t, &st, x, y, maxw, s, -1, &lay);
	return (y + lay.height);
}

/* True-colour art at its full resolution. */
static int
render_rgba(struct view *v)
{
	int w = v->img.rgba_w, h = v->img.rgba_h;
	size_t i;

	if (alloc_out(v, w, h) != 0)
		return (-1);
	for (i = 0; i < (size_t)w * h; i++)
		memcpy(v->out.px + 3 * i, v->img.rgba + 4 * i, 3);
	return (0);
}

static int
load_palette(struct view *v, const char *pic)
{
	struct image img;

	if (pic_load(v->packs, pic, &img) != 0)
		return (-1);
	memcpy(v->pal, img.palette, sizeof(v->pal));
	image_free(&img);
	snprintf(v->pal_id, sizeof(v->pal_id), "%s", pic);
	return (0);
}

/* Grey ramp, used when no palette can be found. */
static void
grey_palette(struct view *v)
{
	int i;

	for (i = 0; i < 256; i++)
		v->pal[3 * i] = v->pal[3 * i + 1] = v->pal[3 * i + 2] =
		    (uint8_t)i;
	snprintf(v->pal_id, sizeof(v->pal_id), "grey");
}

static void
guess_palette(struct view *v)
{
	char pic[80];
	const char *slash;
	size_t i;

	slash = strchr(v->id, '/');
	snprintf(pic, sizeof(pic), "pic/%s", slash != NULL ? slash + 1 : "");
	if (load_palette(v, pic) == 0)
		return;
	for (i = 0; i < sizeof(palette_guess) / sizeof(palette_guess[0]); i++)
		if (starts(v->id, palette_guess[i].prefix) &&
		    load_palette(v, palette_guess[i].pic) == 0)
			return;
	grey_palette(v);
}

int
view_set_palette(struct view *v, const char *pic_id)
{
	if (pic_id == NULL) {
		guess_palette(v);
		return (0);
	}
	return (load_palette(v, pic_id));
}

/* Load the era's world (map, tiles), towns and tile classes. */
static int
load_world(struct view *v, int year, char *err, size_t errlen)
{
	struct blob b;
	const struct exe_layout *layout;
	const uint8_t *classes;
	int era, rc;

	for (era = 0; era < EXE_ERAS; era++)
		if (town_era_years[era] == year)
			break;
	if (era == EXE_ERAS) {
		snprintf(err, errlen, "no era %d", year);
		return (-1);
	}
	if (packs_load(v->packs, "program/main", &b) != 0 ||
	    (layout = exe_identify(b.data, b.len)) == NULL ||
	    towns_for_era(v->packs, layout, b.data, b.len, era, &v->era, err,
	    errlen) != 0 ||
	    (classes = exe_at(layout, b.data, b.len, layout->tile_class_va,
	    256)) == NULL) {
		blob_free(&b);
		snprintf(err, errlen, "unknown or missing program/main");
		return (-1);
	}
	memcpy(v->classes, classes, 256);
	if ((v->world = malloc(sizeof(*v->world))) == NULL) {
		blob_free(&b);
		snprintf(err, errlen, "out of memory");
		return (-1);
	}
	rc = world_load(v->world, v->packs, layout, b.data, b.len, era, err,
	    errlen);
	blob_free(&b);
	if (rc != 0)
		return (-1);
	memcpy(v->pal, v->world->palette, sizeof(v->pal));
	snprintf(v->pal_id, sizeof(v->pal_id), "tiles/map");
	return (0);
}

static int
render_world(struct view *v, size_t frame, unsigned tick)
{
	int cx0, cy0, cx, cy;
	const uint8_t *t;

	if (alloc_out(v, VIEW_W, VIEW_H) != 0)
		return (-1);
	sea_animate(&v->sea, tick, v->pal);
	cx0 = (int)(frame % WORLD_STEPS_X) * WORLD_STEP;
	cy0 = (int)(frame / WORLD_STEPS_X) * WORLD_STEP;
	for (cy = 0; cy < VIEW_CELLS_Y; cy++)
		for (cx = 0; cx < VIEW_CELLS_X; cx++) {
			t = v->world->tiles + (size_t)TILE * TILE *
			    v->world->map[(size_t)(cy0 + cy) * WORLD_W + cx0 +
			    cx];
			blit(&v->out, cx * TILE, cy * TILE, TILE, TILE, t, NULL,
			    v->pal);
		}
	return (0);
}

static int
render_towns(struct view *v)
{
	const struct town *t;
	size_t i;
	int x, y;

	if (alloc_out(v, VIEW_W, VIEW_H) != 0)
		return (-1);
	for (y = 0; y < WORLD_H; y++)
		for (x = 0; x < WORLD_W; x++)
			put(&v->out, x, y, class_rgb[v->classes[
			    v->world->map[(size_t)y * WORLD_W + x]] % 5]);
	for (i = 0; i < v->era.count; i++) {
		t = &v->era.towns[i];
		for (y = -2; y <= 2; y++)
			for (x = -2; x <= 2; x++)
				put(&v->out, t->x + x, t->y + y,
				    nation_rgb[t->nation & 3]);
		if (v->textfont.count > 0)
			draw_text(&v->out, &v->textfont, t->x + 4,
			    t->y - v->textfont.height / 2, VIEW_W, t->name,
			    nation_rgb[t->nation & 3]);
	}
	return (0);
}

static const struct frame *
group_frame(const struct view *v, size_t frame)
{
	size_t b;

	for (b = 0; b < v->group.count; b++) {
		if (frame < v->group.banks[b].count)
			return (&v->group.banks[b].frames[frame]);
		frame -= v->group.banks[b].count;
	}
	return (NULL);
}

static int
render_frame(struct view *v, const struct frame *f)
{
	const uint8_t *a;
	uint8_t *d;
	size_t i;
	int k;

	if (f->rgba == NULL) {
		if (alloc_out(v, f->w, f->h) != 0)
			return (-1);
		checker(&v->out);
		blit(&v->out, 0, 0, f->w, f->h, f->pixels, f->mask, v->pal);
		return (0);
	}
	/* Sheet frames at their full resolution, over the checkerboard. */
	if (alloc_out(v, f->rgba_w, f->rgba_h) != 0)
		return (-1);
	checker(&v->out);
	for (i = 0; i < (size_t)f->rgba_w * f->rgba_h; i++) {
		a = f->rgba + 4 * i;
		d = v->out.px + 3 * i;
		for (k = 0; k < 3; k++)
			d[k] = (uint8_t)((a[k] * a[3] + d[k] * (255 - a[3]) +
			    127) / 255);
	}
	return (0);
}

static int
render_tiles(struct view *v)
{
	int tw = v->tiles.tile_w, th = v->tiles.tile_h, rows;
	size_t i;

	rows = (int)((v->tiles.count + SHEET_COLS - 1) / SHEET_COLS);
	if (alloc_out(v, SHEET_COLS * (tw + 1), rows * (th + 1)) != 0)
		return (-1);
	fill(&v->out, 40, 40, 40);
	for (i = 0; i < v->tiles.count; i++)
		blit(&v->out, (int)(i % SHEET_COLS) * (tw + 1),
		    (int)(i / SHEET_COLS) * (th + 1), tw, th,
		    v->tiles.pixels + i * (size_t)tw * th, NULL, v->pal);
	return (0);
}

static int
render_font(struct view *v)
{
	static const uint8_t white[3] = { 255, 255, 255 };
	int cw = v->font.cell_w, ch = v->font.height, rows, gx, gy;
	size_t i;

	rows = (int)((v->font.count + SHEET_COLS - 1) / SHEET_COLS);
	if (alloc_out(v, SHEET_COLS * (cw + 2), rows * (ch + 2)) != 0)
		return (-1);
	fill(&v->out, 30, 30, 60);
	for (i = 0; i < v->font.count; i++)
		for (gy = 0; gy < ch; gy++)
			for (gx = 0; gx < cw; gx++)
				if (font_pixel(&v->font, i, gx, gy))
					put(&v->out,
					    (int)(i % SHEET_COLS) * (cw + 2) + gx,
					    (int)(i / SHEET_COLS) * (ch + 2) + gy,
					    white);
	return (0);
}

static int
render_strings(struct view *v, const struct strtab *tab, int one_per_line)
{
	static const uint8_t ink[3] = { 60, 35, 10 };
	size_t i;
	int y = 10;

	if (alloc_out(v, VIEW_W, VIEW_H) != 0)
		return (-1);
	fill(&v->out, 232, 214, 170);	/* parchment */
	if (tab == NULL || v->textfont.count == 0)
		return (0);
	for (i = 0; i < tab->count && y < VIEW_H; i++) {
		y = draw_text(&v->out, &v->textfont, 10, y, VIEW_W - 20,
		    tab->str[i], ink);
		if (!one_per_line)
			y += 4;
	}
	return (0);
}

static int
open_decoded(struct view *v, const struct blob *b, char *err, size_t errlen)
{
	const char *id = v->id;
	size_t i;
	int rc = 0;

	if (starts(id, "pic/")) {
		v->kind = K_PICTURE;
		rc = pic_decode(v->packs, id, b, &v->img);
		memcpy(v->pal, v->img.palette, sizeof(v->pal));
		snprintf(v->pal_id, sizeof(v->pal_id), "own");
		v->nframes = 1;
	} else if (starts(id, "img/")) {
		v->kind = K_PICTURE;
		rc = pic_decode(v->packs, id, b, &v->img);
		guess_palette(v);
		v->nframes = 1;
	} else if (starts(id, "spr/") || starts(id, "ui/")) {
		v->kind = K_BANK;
		rc = bank_from_blob(v->packs, id, b, BANK_RUNS, &v->bank);
		guess_palette(v);
		v->nframes = v->bank.count;
	} else if (starts(id, "cur/") || starts(id, "tiles15/")) {
		v->kind = K_BANK;
		rc = bank_from_blob(v->packs, id, b, BANK_RAW, &v->bank);
		guess_palette(v);
		v->nframes = v->bank.count;
	} else if (starts(id, "grp/")) {
		v->kind = K_GROUP;
		rc = group_from_blob(v->packs, id, b, &v->group);
		guess_palette(v);
		for (i = 0; i < v->group.count; i++)
			v->nframes += v->group.banks[i].count;
	} else if (starts(id, "tiles/")) {
		v->kind = K_TILES;
		rc = tileset_from_blob(v->packs, id, b, &v->tiles);
		if (rc == 0 && v->tiles.has_palette) {
			memcpy(v->pal, v->tiles.palette, sizeof(v->pal));
			snprintf(v->pal_id, sizeof(v->pal_id), "own");
		} else
			grey_palette(v);
		v->nframes = 1;
	} else if (starts(id, "font/")) {
		v->kind = K_FONT;
		rc = font_from_blob(v->packs, id, b, VIEW_H, &v->font);
		v->nframes = 1;
	} else if (starts(id, "text/") && strstr(id, "/cstr") != NULL) {
		v->kind = K_CSTR;
		rc = cstr_decode(b->data, b->len, &v->tab);
		v->nframes = v->tab.count;
	} else if (starts(id, "text/")) {
		v->kind = K_CSTN;
		rc = cstn_decode(b->data, b->len, &v->groups);
		v->nframes = v->groups.count;
	} else {
		v->kind = K_RAW;
		v->rawlen = b->len;
		v->nframes = 1;
	}
	if (rc != 0)
		snprintf(err, errlen, "%s: cannot decode", id);
	return (rc);
}

static void
describe(struct view *v, const char *pack)
{
	switch (v->kind) {
	case K_PICTURE:
		if (v->img.rgba != NULL)
			snprintf(v->info, sizeof(v->info), "picture %dx%d, true "
			    "colour %dx%d, from %s", v->img.w, v->img.h,
			    v->img.rgba_w, v->img.rgba_h, pack);
		else
			snprintf(v->info, sizeof(v->info), "picture %dx%d, "
			    "palette %s, from %s", v->img.w, v->img.h,
			    v->pal_id, pack);
		break;
	case K_BANK:
	case K_GROUP:
		snprintf(v->info, sizeof(v->info), "%zu frames, palette %s, "
		    "from %s", v->nframes, v->pal_id, pack);
		break;
	case K_TILES:
		snprintf(v->info, sizeof(v->info), "%zu tiles of %dx%d, "
		    "palette %s, from %s", v->tiles.count, v->tiles.tile_w,
		    v->tiles.tile_h, v->pal_id, pack);
		break;
	case K_FONT:
		snprintf(v->info, sizeof(v->info), "%zu glyphs, %d px high, "
		    "from %s", v->font.count, v->font.height, pack);
		break;
	case K_CSTR:
		snprintf(v->info, sizeof(v->info), "%zu strings, from %s",
		    v->tab.count, pack);
		break;
	case K_CSTN:
		snprintf(v->info, sizeof(v->info), "%zu word lists, from %s",
		    v->groups.count, pack);
		break;
	case K_WORLD:
		snprintf(v->info, sizeof(v->info), "sailing world %d: 640x480 "
		    "tiles, %zu towns; frames scroll", v->era.year,
		    v->era.count);
		break;
	case K_TOWNS:
		snprintf(v->info, sizeof(v->info), "%zu towns in %d, map by "
		    "terrain class", v->era.count, v->era.year);
		break;
	case K_RAW:
		snprintf(v->info, sizeof(v->info), "binary, %zu bytes, from %s",
		    v->rawlen, pack);
		break;
	}
}

struct view *
view_open(struct packs *packs, const char *id, char *err, size_t errlen)
{
	struct view *v;
	struct blob b;
	int year;

	if ((v = calloc(1, sizeof(*v))) == NULL) {
		snprintf(err, errlen, "out of memory");
		return (NULL);
	}
	v->packs = packs;
	snprintf(v->id, sizeof(v->id), "%s", id);
	/* The text font is optional: views without it just omit text. */
	load_font(packs, starts(id, "towns/") ? SMALL_FONT : TEXT_FONT,
	    &v->textfont);

	if (sscanf(id, "world/%d", &year) == 1 ||
	    sscanf(id, "towns/%d", &year) == 1) {
		if (load_world(v, year, err, errlen) != 0) {
			view_close(v);
			return (NULL);
		}
		if (starts(id, "world/")) {
			v->kind = K_WORLD;
			v->nframes = WORLD_STEPS_X * WORLD_STEPS_Y;
			sea_init(&v->sea, v->pal);
		} else {
			v->kind = K_TOWNS;
			v->nframes = 1;
		}
		describe(v, "");
		return (v);
	}
	if (packs_load(packs, id, &b) != 0) {
		snprintf(err, errlen, "%s", packs_error(packs));
		view_close(v);
		return (NULL);
	}
	if (open_decoded(v, &b, err, errlen) != 0) {
		blob_free(&b);
		view_close(v);
		return (NULL);
	}
	describe(v, b.pack);
	blob_free(&b);
	return (v);
}

void
view_close(struct view *v)
{
	if (v == NULL)
		return;
	image_free(&v->img);
	bank_free(&v->bank);
	group_free(&v->group);
	tileset_free(&v->tiles);
	font_free(&v->font);
	font_free(&v->textfont);
	strtab_free(&v->tab);
	strgroups_free(&v->groups);
	free(v->world);
	free(v->out.px);
	free(v);
}

size_t
view_frames(const struct view *v)
{
	return (v->nframes);
}

static int
clamp(int v, int lo, int hi)
{
	return (v < lo ? lo : v > hi ? hi : v);
}

size_t
view_start_frame(const struct view *v)
{
	size_t i;
	int sx, sy;

	if (v->kind != K_WORLD)
		return (0);
	for (i = 0; i < v->era.count; i++)
		if (strcmp(v->era.towns[i].name, "Havana") == 0) {
			/* Centre the view on the town. */
			sx = (v->era.towns[i].x - VIEW_CELLS_X / 2 +
			    WORLD_STEP / 2) / WORLD_STEP;
			sy = (v->era.towns[i].y - VIEW_CELLS_Y / 2 +
			    WORLD_STEP / 2) / WORLD_STEP;
			return ((size_t)clamp(sy, 0, WORLD_STEPS_Y - 1) *
			    WORLD_STEPS_X +
			    (size_t)clamp(sx, 0, WORLD_STEPS_X - 1));
		}
	return (0);
}

size_t
view_row_frames(const struct view *v)
{
	return (v->kind == K_WORLD ? WORLD_STEPS_X : 0);
}

const char *
view_info(const struct view *v)
{
	return (v->info);
}

int
view_render(struct view *v, size_t frame, unsigned tick,
    const struct rgb **out)
{
	const struct frame *f;
	int rc = -1;

	if (frame >= v->nframes)
		frame = v->nframes > 0 ? v->nframes - 1 : 0;
	switch (v->kind) {
	case K_PICTURE:
		if (v->img.rgba != NULL) {
			rc = render_rgba(v);
			break;
		}
		if ((rc = alloc_out(v, v->img.w, v->img.h)) == 0)
			blit(&v->out, 0, 0, v->img.w, v->img.h, v->img.pixels,
			    NULL, v->pal);
		break;
	case K_BANK:
		rc = v->bank.count > 0 ?
		    render_frame(v, &v->bank.frames[frame]) : -1;
		break;
	case K_GROUP:
		rc = (f = group_frame(v, frame)) != NULL ?
		    render_frame(v, f) : -1;
		break;
	case K_TILES:
		rc = render_tiles(v);
		break;
	case K_FONT:
		rc = render_font(v);
		break;
	case K_CSTR:
		if ((rc = render_strings(v, NULL, 0)) == 0 &&
		    v->textfont.count > 0)
			draw_text(&v->out, &v->textfont, 10, 10, VIEW_W - 20,
			    v->tab.str[frame], (const uint8_t[3]){ 60, 35, 10 });
		break;
	case K_CSTN:
		rc = render_strings(v, &v->groups.groups[frame], 1);
		break;
	case K_WORLD:
		rc = render_world(v, frame, tick);
		break;
	case K_TOWNS:
		rc = render_towns(v);
		break;
	case K_RAW:
		if ((rc = alloc_out(v, VIEW_W, 40)) == 0)
			fill(&v->out, 40, 40, 40);
		break;
	}
	if (rc == 0)
		*out = &v->out;
	return (rc);
}
