/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "fonts.h"
#include "text.h"

#define SCREEN_H	480
#define TEXT_FIRST	0x21
#define TEXT_LAST	0xFF
#define MAX_HEIGHT	200	/* 640x480 pixels */

struct desc {
	char	file[256];
	int	height;
	double	size;		/* 0: height */
	double	baseline;	/* < 0: the font's ascender */
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

static int
real(const char *s, double lo, double hi, double *out)
{
	char *end;

	*out = strtod(s, &end);
	return (*s != '\0' && *end == '\0' && *out >= lo && *out <= hi ? 0 :
	    -1);
}

static int
parse(const uint8_t *data, size_t len, struct desc *d)
{
	char *text, *line, *next, *eq, *key, *val;
	double v;
	int in_font = 0, rc = -1;

	memset(d, 0, sizeof(*d));
	d->baseline = -1;
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
		if (strcmp(line, "[font]") == 0) {
			in_font = 1;
			continue;
		}
		if ((eq = strchr(line, '=')) == NULL || !in_font)
			goto out;
		*eq = '\0';
		key = trim(line);
		val = trim(eq + 1);
		if (strcmp(key, "file") == 0)
			snprintf(d->file, sizeof(d->file), "%s", val);
		else if (strcmp(key, "height") == 0) {
			if (real(val, 1, MAX_HEIGHT, &v) != 0 || v != floor(v))
				goto out;
			d->height = (int)v;
		} else if (strcmp(key, "size") == 0) {
			if (real(val, 1, MAX_HEIGHT, &d->size) != 0)
				goto out;
		} else if (strcmp(key, "baseline") == 0) {
			if (real(val, 0, MAX_HEIGHT, &d->baseline) != 0)
				goto out;
		}
		/* Unknown keys are ignored, for forward compatibility. */
	}
	if (d->file[0] != '\0' && d->height > 0)
		rc = 0;
out:
	free(text);
	if (d->size == 0)
		d->size = d->height;
	return (rc);
}

/* Character (Mac Roman) of glyph i. */
static int
glyph_char(const struct font *f, int caps, size_t i)
{
	return (caps ? caps_char(i) : (int)(f->first + i));
}

/* Load glyph i at the face's current size; 0 if the font lacks it. */
static int
load(FT_Face face, const struct font *f, int caps, size_t i)
{
	FT_UInt gi;
	int c = glyph_char(f, caps, i);

	if (c < 0 || (gi = FT_Get_Char_Index(face,
	    (FT_ULong)macroman_unicode(c))) == 0)
		return (0);
	return (FT_Load_Glyph(face, gi, FT_LOAD_RENDER | FT_LOAD_NO_HINTING) ==
	    0 && face->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY);
}

/* Row r of the rendered glyph. */
static const uint8_t *
row(const FT_Bitmap *bm, unsigned r)
{
	return (bm->pitch >= 0 ? bm->buffer + (size_t)r * bm->pitch :
	    bm->buffer + (size_t)(bm->rows - 1 - r) * -bm->pitch);
}

static int
set_size(FT_Face face, double px)
{
	return (FT_Set_Char_Size(face, 0, (FT_F26Dot6)lround(px * 64), 72,
	    72) == 0 ? 0 : -1);
}

/* The 640x480 glyphs: metrics, and a 1-bit cell each. */
static int
screen_glyphs(FT_Face face, const struct desc *d, int caps, struct font *f)
{
	FT_GlyphSlot s = face->glyph;
	double baseline;
	int left = 0, right = 1, top, x, y;
	unsigned r, c;
	size_t i;

	if (set_size(face, d->size) != 0)
		return (-1);
	baseline = d->baseline >= 0 ? d->baseline :
	    face->size->metrics.ascender / 64.0;
	/* First the cell: the widest extents of all glyphs. */
	for (i = 0; i < f->count; i++) {
		if (!load(face, f, caps, i))
			continue;
		if (s->bitmap_left < left)
			left = s->bitmap_left;
		if (s->bitmap_left + (int)s->bitmap.width > right)
			right = s->bitmap_left + (int)s->bitmap.width;
		f->glyphs[i].advance_sub = (int)lround(s->linearHoriAdvance *
		    (double)FONT_SUB / 65536);
		f->glyphs[i].advance = (f->glyphs[i].advance_sub +
		    FONT_SUB / 2) / FONT_SUB;
		f->glyphs[i].ink = s->bitmap_left + (int)s->bitmap.width;
	}
	f->origin_x = -left;
	f->cell_w = right - left;
	f->height = d->height;
	if ((f->bitmap = calloc(f->count * (size_t)f->height,
	    (size_t)f->cell_w)) == NULL)
		return (-1);
	for (i = 0; i < f->count; i++) {
		if (!load(face, f, caps, i))
			continue;
		top = (int)lround(baseline) - s->bitmap_top;
		for (r = 0; r < s->bitmap.rows; r++) {
			y = top + (int)r;
			if (y < 0 || y >= f->height)
				continue;
			for (c = 0; c < s->bitmap.width; c++) {
				x = f->origin_x + s->bitmap_left + (int)c;
				f->bitmap[(i * f->height + (size_t)y) *
				    f->cell_w + x] = row(&s->bitmap, r)[c] >= 128;
			}
		}
	}
	if (!caps && FT_Load_Char(face, ' ', FT_LOAD_NO_HINTING) == 0)
		f->space_sub = (int)lround(s->linearHoriAdvance *
		    (double)FONT_SUB / 65536);
	return (0);
}

/* The glyphs for a canvas "canvas_h" pixels high, with coverage. */
static int
canvas_glyphs(FT_Face face, const struct desc *d, int caps, int canvas_h,
    struct font *f)
{
	FT_GlyphSlot s = face->glyph;
	double k = (double)canvas_h / SCREEN_H, baseline;
	struct hd_glyph *h;
	unsigned r;
	size_t i;

	if (set_size(face, d->size * k) != 0)
		return (-1);
	baseline = d->baseline >= 0 ? d->baseline * k :
	    face->size->metrics.ascender / 64.0;
	if ((f->hd = calloc(f->count, sizeof(*f->hd))) == NULL)
		return (-1);
	f->hd_height = canvas_h;
	for (i = 0; i < f->count; i++) {
		if (!load(face, f, caps, i) || s->bitmap.width == 0 ||
		    s->bitmap.rows == 0)
			continue;
		h = &f->hd[i];
		h->x = s->bitmap_left;
		h->y = (int)lround(baseline) - s->bitmap_top;
		h->w = (int)s->bitmap.width;
		h->h = (int)s->bitmap.rows;
		if ((h->cov = malloc((size_t)h->w * h->h)) == NULL)
			return (-1);
		for (r = 0; r < s->bitmap.rows; r++)
			memcpy(h->cov + (size_t)r * h->w, row(&s->bitmap, r),
			    (size_t)h->w);
	}
	return (0);
}

int
font_from_blob(struct packs *packs, const char *id, const struct blob *b,
    int canvas_h, struct font *out)
{
	int caps = strcmp(id, "font/caps") == 0, rc = -1;
	struct desc d;
	struct blob fb;
	FT_Library lib;
	FT_Face face;

	memset(out, 0, sizeof(*out));
	if (strcmp(b->ext, "font") != 0)
		return (caps ? caps_decode(b->data, b->len, out) :
		    font_decode(b->data, b->len, out));
	if (parse(b->data, b->len, &d) != 0 ||
	    packs_load_beside(packs, b, d.file, &fb) != 0)
		return (-1);
	if (FT_Init_FreeType(&lib) != 0) {
		blob_free(&fb);
		return (-1);
	}
	if (FT_New_Memory_Face(lib, fb.data, (FT_Long)fb.len, 0, &face) == 0) {
		FT_Select_Charmap(face, FT_ENCODING_UNICODE);
		out->first = caps ? 0 : TEXT_FIRST;
		out->count = caps ? CAPS_COUNT : TEXT_LAST - TEXT_FIRST + 1;
		if ((out->glyphs = calloc(out->count, sizeof(*out->glyphs))) !=
		    NULL && screen_glyphs(face, &d, caps, out) == 0 &&
		    canvas_glyphs(face, &d, caps, canvas_h, out) == 0)
			rc = 0;
		FT_Done_Face(face);
	}
	FT_Done_FreeType(lib);
	blob_free(&fb);
	if (rc != 0)
		font_free(out);
	return (rc);
}

int
font_load(struct packs *packs, const char *id, int canvas_h,
    struct font *out)
{
	struct blob b;
	int rc;

	memset(out, 0, sizeof(*out));
	if (packs_load(packs, id, &b) != 0)
		return (-1);
	rc = font_from_blob(packs, id, &b, canvas_h, out);
	blob_free(&b);
	return (rc);
}
