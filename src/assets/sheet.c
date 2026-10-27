/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "png.h"
#include "sheet.h"

#define SCREEN_H	480
#define MAX_FRAMES	1024
#define MAX_BANKS	64

struct rect {
	int	bank;		/* 0 for banks */
	int	frame;
	int	x, y, w, h;
	int	ox, oy;		/* drawing offset, art pixels */
};

struct sheet {
	char		 image[256];
	struct rect	*rects;
	size_t		 count;
	int		 banks;	/* highest bank + 1 */
};

static char *
trim(char *s)
{
	char *end;

	while (isspace((unsigned char)*s))
		s++;
	end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		*--end = '\0';
	return (s);
}

/* A whole non-negative number, followed by "stop"; *p moves past it. */
static int
number(char **p, int stop, int *out)
{
	char *end;
	long v;

	if (!isdigit((unsigned char)**p))
		return (-1);
	v = strtol(*p, &end, 10);
	if (v > 1 << 20 || (stop == ' ' ? !isspace((unsigned char)*end) &&
	    *end != '\0' : *end != stop))
		return (-1);
	*out = (int)v;
	*p = end;
	while (isspace((unsigned char)**p))
		(*p)++;
	if (stop == '.')
		(*p)++;
	return (0);
}

/* A whole number, maybe negative, then a space or the end. */
static int
offset(char **p, int *out)
{
	int neg = **p == '-';

	if (neg)
		(*p)++;
	if (number(p, ' ', out) != 0)
		return (-1);
	if (neg)
		*out = -*out;
	return (0);
}

/* "N" or, in groups, "B.N". */
static int
frame_key(char *key, int grouped, struct rect *r)
{
	r->bank = 0;
	if (grouped && number(&key, '.', &r->bank) != 0)
		return (-1);
	return (number(&key, '\0', &r->frame));
}

static int
parse(const uint8_t *data, size_t len, int grouped, struct sheet *sh)
{
	char *text, *line, *next, *eq, *key, *val;
	struct rect r, *rs;
	enum { NONE, SHEET, FRAMES } section = NONE;

	memset(sh, 0, sizeof(*sh));
	if ((text = malloc(len + 1)) == NULL)
		return (-1);
	memcpy(text, data, len);
	text[len] = '\0';
	for (line = text; line != NULL; line = next) {
		if ((next = strchr(line, '\n')) != NULL)
			*next++ = '\0';
		line = trim(line);
		if (*line == '\0' || *line == '#' || *line == ';')
			continue;
		if (strcmp(line, "[sheet]") == 0 ||
		    strcmp(line, "[frames]") == 0) {
			section = line[1] == 's' ? SHEET : FRAMES;
			continue;
		}
		if ((eq = strchr(line, '=')) == NULL || section == NONE)
			goto bad;
		*eq = '\0';
		key = trim(line);
		val = trim(eq + 1);
		if (section == SHEET) {
			if (strcmp(key, "image") == 0)
				snprintf(sh->image, sizeof(sh->image), "%s",
				    val);
			continue;	/* unknown keys: forward compatibility */
		}
		if (frame_key(key, grouped, &r) != 0 ||
		    number(&val, ' ', &r.x) != 0 ||
		    number(&val, ' ', &r.y) != 0 ||
		    number(&val, ' ', &r.w) != 0 ||
		    number(&val, ' ', &r.h) != 0 ||
		    (r.ox = r.oy = 0, *val != '\0' &&
		    (offset(&val, &r.ox) != 0 || offset(&val, &r.oy) != 0)) ||
		    *val != '\0' ||
		    r.w == 0 || r.h == 0 || r.bank >= MAX_BANKS ||
		    r.frame >= MAX_FRAMES || sh->count >= MAX_FRAMES)
			goto bad;
		if ((rs = realloc(sh->rects, (sh->count + 1) *
		    sizeof(*rs))) == NULL)
			goto bad;
		sh->rects = rs;
		sh->rects[sh->count++] = r;
		if (r.bank + 1 > sh->banks)
			sh->banks = r.bank + 1;
	}
	free(text);
	return (sh->image[0] != '\0' && sh->count > 0 ? 0 : -1);
bad:
	free(text);
	free(sh->rects);
	sh->rects = NULL;
	return (-1);
}

/* The rectangle of frame "frame" of bank "bank", or NULL. */
static const struct rect *
find(const struct sheet *sh, int bank, int frame)
{
	size_t i;

	for (i = 0; i < sh->count; i++)
		if (sh->rects[i].bank == bank && sh->rects[i].frame == frame)
			return (&sh->rects[i]);
	return (NULL);
}

/* Frames in bank "bank": all of 0..n-1 must be there, once. */
static int
bank_frames(const struct sheet *sh, int bank)
{
	size_t i;
	int n = 0;

	for (i = 0; i < sh->count; i++)
		if (sh->rects[i].bank == bank)
			n++;
	for (i = 0; i < (size_t)n; i++)
		if (find(sh, bank, (int)i) == NULL)
			return (-1);
	return (n);
}

static int
screen_size(int art, int art_height)
{
	int v = (int)(((long)art * SCREEN_H + art_height / 2) / art_height);

	return (v < 1 ? 1 : v);
}

/* An offset in art pixels, in 640x480 pixels, rounded either way. */
static int
screen_offset(int art, int art_height)
{
	long v = (long)(art < 0 ? -art : art) * SCREEN_H;

	v = (v + art_height / 2) / art_height;
	return ((int)(art < 0 ? -v : v));
}

/*
 * Cut frame "r" out of the sheet image; take its indices from "orig"
 * when that has the same size, else reduce its colours.
 */
static int
make_frame(const struct image *art, const struct rect *r, int art_height,
    const struct frame *orig, const uint8_t *pal, struct frame *f)
{
	size_t n;
	int y;

	if (r->x + r->w > art->rgba_w || r->y + r->h > art->rgba_h)
		return (-1);
	f->w = screen_size(r->w, art_height);
	f->h = screen_size(r->h, art_height);
	f->ox = screen_offset(r->ox, art_height);
	f->oy = screen_offset(r->oy, art_height);
	f->rgba_w = r->w;
	f->rgba_h = r->h;
	n = (size_t)f->w * f->h;
	if ((f->rgba = malloc((size_t)r->w * r->h * 4)) == NULL ||
	    (f->pixels = malloc(n)) == NULL)
		return (-1);
	for (y = 0; y < r->h; y++)
		memcpy(f->rgba + (size_t)y * r->w * 4, art->rgba +
		    ((size_t)(r->y + y) * art->rgba_w + r->x) * 4,
		    (size_t)r->w * 4);
	if (orig != NULL && orig->w == f->w && orig->h == f->h &&
	    f->ox == 0 && f->oy == 0) {
		memcpy(f->pixels, orig->pixels, n);
		f->key = orig->key;
		if (orig->mask != NULL) {
			if ((f->mask = malloc(n)) == NULL)
				return (-1);
			memcpy(f->mask, orig->mask, n);
		}
		return (0);
	}
	if ((f->mask = malloc(n)) == NULL)
		return (-1);
	return (rgba_reduce(f->rgba, r->w, r->h, f->w, f->h, pal, f->pixels,
	    f->mask));
}

/* Build bank "bank" of the sheet, with "orig" for the indices. */
static int
make_bank(const struct sheet *sh, int bank, const struct image *art,
    int art_height, const struct sprite_bank *orig, struct sprite_bank *out)
{
	uint8_t pal[256 * 3];
	int n, i;

	memset(out, 0, sizeof(*out));
	if ((n = bank_frames(sh, bank)) <= 0)
		return (-1);
	if ((out->frames = calloc((size_t)n, sizeof(*out->frames))) == NULL)
		return (-1);
	out->count = (size_t)n;
	palette_fixed(pal);
	for (i = 0; i < n; i++)
		if (make_frame(art, find(sh, bank, i), art_height,
		    orig != NULL && (size_t)i < orig->count ?
		    &orig->frames[i] : NULL, pal, &out->frames[i]) != 0) {
			bank_free(out);
			return (-1);
		}
	return (0);
}

/* Parse sheet "b" and load its image from the same pack. */
static int
open_sheet(struct packs *packs, const struct blob *b, int grouped,
    struct sheet *sh, struct image *art)
{
	struct blob ib;
	int rc;

	if (parse(b->data, b->len, grouped, sh) != 0)
		return (-1);
	if (packs_load_beside(packs, b, sh->image, &ib) != 0) {
		free(sh->rects);
		return (-1);
	}
	/* At the original height, the art keeps its pixel size. */
	rc = png_decode(ib.data, ib.len, SCREEN_H, art);
	blob_free(&ib);
	if (rc != 0)
		free(sh->rects);
	return (rc);
}

/* The original bank behind a sheet, if any. */
static int
original_bank(struct packs *packs, const char *id, enum bank_kind kind,
    struct sprite_bank *out)
{
	static const char *const exts[] = { "spr", "crs", "cur", "bmp" };
	struct blob b;
	size_t i;
	int rc;

	memset(out, 0, sizeof(*out));
	for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
		if (packs_load_ext(packs, id, exts[i], &b) != 0)
			continue;
		rc = kind == BANK_RAW ? bank_decode_raw(b.data, b.len, out) :
		    bank_decode_runs(b.data, b.len, out);
		blob_free(&b);
		return (rc);
	}
	return (-1);
}

int
bank_from_blob(struct packs *packs, const char *id, const struct blob *b,
    enum bank_kind kind, struct sprite_bank *out)
{
	struct sprite_bank orig;
	struct sheet sh;
	struct image art;
	int rc;

	memset(out, 0, sizeof(*out));
	if (strcmp(b->ext, "sheet") != 0)
		return (kind == BANK_RAW ? bank_decode_raw(b->data, b->len, out) :
		    bank_decode_runs(b->data, b->len, out));
	if (open_sheet(packs, b, 0, &sh, &art) != 0)
		return (-1);
	original_bank(packs, id, kind, &orig);
	rc = make_bank(&sh, 0, &art, b->art_height, &orig, out);
	bank_free(&orig);
	image_free(&art);
	free(sh.rects);
	return (rc);
}

int
bank_load(struct packs *packs, const char *id, enum bank_kind kind,
    struct sprite_bank *out)
{
	struct blob b;
	int rc;

	memset(out, 0, sizeof(*out));
	if (packs_load(packs, id, &b) != 0)
		return (-1);
	rc = bank_from_blob(packs, id, &b, kind, out);
	blob_free(&b);
	return (rc);
}

int
group_from_blob(struct packs *packs, const char *id, const struct blob *b,
    struct sprite_group *out)
{
	struct sprite_group orig = { 0 };
	struct sheet sh;
	struct image art;
	struct blob ob;
	int i, rc = 0;

	memset(out, 0, sizeof(*out));
	if (strcmp(b->ext, "sheet") != 0)
		return (group_decode(b->data, b->len, out));
	if (open_sheet(packs, b, 1, &sh, &art) != 0)
		return (-1);
	if (packs_load_ext(packs, id, "grp", &ob) == 0) {
		if (group_decode(ob.data, ob.len, &orig) != 0)
			memset(&orig, 0, sizeof(orig));
		blob_free(&ob);
	}
	if ((out->banks = calloc((size_t)sh.banks, sizeof(*out->banks))) ==
	    NULL)
		rc = -1;
	else
		out->count = (size_t)sh.banks;
	for (i = 0; rc == 0 && i < sh.banks; i++)
		rc = make_bank(&sh, i, &art, b->art_height,
		    (size_t)i < orig.count ? &orig.banks[i] : NULL,
		    &out->banks[i]);
	if (rc != 0)
		group_free(out);
	group_free(&orig);
	image_free(&art);
	free(sh.rects);
	return (rc);
}

int
group_load(struct packs *packs, const char *id, struct sprite_group *out)
{
	struct blob b;
	int rc;

	memset(out, 0, sizeof(*out));
	if (packs_load(packs, id, &b) != 0)
		return (-1);
	rc = group_from_blob(packs, id, &b, out);
	blob_free(&b);
	return (rc);
}
