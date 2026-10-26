/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <string.h>

#include "assets/text.h"
#include "text.h"

#define CAP_MARGIN	4	/* space between a drop capital and text */

/*
 * The pen moves in 1/FONT_SUB pixels, so replacement fonts keep their
 * spacing; the original fonts advance by whole pixels.
 */
#define SUB		FONT_SUB

/* Nearest pixel of pen position "v". */
static int
pixel(int v)
{
	v += SUB / 2;
	return (v >= 0 ? v / SUB : -((-v + SUB - 1) / SUB));
}

static int
mapped(int c)
{
	return (c == '^' ? '\'' : c);
}

static int
advance(const struct font *f, int c)
{
	c = mapped(c);
	if (c == ' ')
		return (f->space_sub > 0 ? f->space_sub : f->height / 3 * SUB);
	if (c < (int)f->first || (size_t)(c - (int)f->first) >= f->count)
		return (0);
	return (f->glyphs[c - (int)f->first].advance_sub);
}

static void
glyph(const struct text_target *t, const struct font *f, size_t g,
    int x_sub, int y, uint32_t ink)
{
	int gx, gy, x;

	if (t == NULL || g >= f->count)
		return;
	if (t->glyph != NULL) {
		t->glyph(t->arg, f, g, x_sub, y, ink);
		return;
	}
	x = pixel(x_sub);
	for (gy = 0; gy < f->height; gy++)
		for (gx = 0; gx < f->cell_w; gx++)
			if (font_pixel(f, g, gx, gy))
				t->plot(t->arg, x - f->origin_x + gx, y + gy, ink);
}

static void
draw_char(const struct text_target *t, const struct font *f, int c, int x,
    int y, uint32_t ink)
{
	c = mapped(c);
	if (c > ' ' && c >= (int)f->first &&
	    (size_t)(c - (int)f->first) < f->count)
		glyph(t, f, (size_t)(c - (int)f->first), x, y, ink);
}

int
text_width(const struct font *font, const char *s)
{
	int w = 0;

	while (*s != '\0')
		w += advance(font, utf8_next_macroman(&s));
	return (pixel(w));
}

/* Width of the word at s, in pen steps: up to a space or line break. */
static int
word_width(const struct font *f, const char *s)
{
	int w = 0;

	while (*s != '\0' && *s != ' ' && *s != '\r' && *s != '\n')
		w += advance(f, utf8_next_macroman(&s));
	return (w);
}

void
text_draw(const struct text_target *t, const struct text_style *st,
    int x, int y, int w, const char *s, int selected,
    struct text_layout *layout)
{
	const struct font *f = st->font;
	int line_h = f->height + st->line_gap;
	int pen, top = y, indent = 0, indent_until = y, option = -1, c;
	int at_line_start = 1, at_para_start = 1, ci;
	int left = x * SUB, right = (x + w) * SUB;	/* pen steps */
	uint32_t ink = st->ink;
	struct text_option *opt = NULL;
	struct text_layout tmp;

	if (layout == NULL)
		layout = &tmp;
	memset(layout, 0, sizeof(*layout));
	pen = left;
	while (*s != '\0') {
		if (*s == '\r' || *s == '\n') {
			s++;
			y += line_h;
			pen = left + (y < indent_until ? indent : 0);
			at_line_start = at_para_start = 1;
			option = -1;
			ink = st->ink;
			continue;
		}
		if (at_line_start && *s == '~') {
			s++;
			if (layout->noptions < TEXT_MAX_OPTIONS) {
				option = layout->noptions++;
				opt = &layout->option[option];
				opt->x = pixel(pen);
				opt->y = y;
				opt->w = 0;
				opt->h = line_h;
			}
			ink = option == selected ? st->highlight : st->ink;
			at_line_start = 0;
			continue;
		}
		if (at_para_start && *s == '`') {
			s++;
			at_para_start = 0;
			c = utf8_next_macroman(&s);
			if (st->caps != NULL && (ci = caps_index(c)) >= 0 &&
			    (size_t)ci < st->caps->count) {
				/* Capitals have no pen origin offset. */
				glyph(t, st->caps, (size_t)ci, pen +
				    st->caps->origin_x * SUB, y, ink);
				indent = (st->caps->glyphs[ci].ink +
				    CAP_MARGIN) * SUB;
				indent_until = y + st->caps->height;
				pen += indent;
			} else {
				draw_char(t, f, c, pen, y, ink);
				pen += advance(f, c);
			}
			at_line_start = 0;
			continue;
		}
		at_line_start = at_para_start = 0;
		if (*s == '`') {
			s++;	/* a lead-in not at a paragraph start: no cap */
			continue;
		}
		/* Wrap before a word that does not fit. */
		if (*s != ' ' && pen > left + (y < indent_until ? indent : 0) &&
		    pen + word_width(f, s) > right) {
			y += line_h;
			pen = left + (y < indent_until ? indent : 0);
			if (*s == ' ')
				continue;
		}
		c = utf8_next_macroman(&s);
		if (c < 0x20)
			continue;
		if (c == ' ' && pen == left + (y < indent_until ? indent : 0))
			continue;	/* no leading space after a wrap */
		draw_char(t, f, c, pen, y, ink);
		pen += advance(f, c);
		if (option >= 0 && opt != NULL && pixel(pen) - opt->x > opt->w)
			opt->w = pixel(pen) - opt->x;
	}
	y += line_h;
	if (y < indent_until)
		y = indent_until;
	layout->height = y - top;
}
