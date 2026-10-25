/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_TEXT_RENDER_H
#define STMALO_TEXT_RENDER_H

#include <stdint.h>

#include "assets/font.h"

/*
 * Text layout and drawing with the original 1-bit fonts and the markup of
 * the game's strings:
 *   \r or \n	line break
 *   ~		at the start of a line: a selectable menu option
 *   `		at the start of a paragraph: its first letter is a drop
 *		capital (when a capitals font is given)
 *   ^		apostrophe
 * Other control bytes are skipped.  Text is UTF-8 (the decoded tables).
 */

#define TEXT_MAX_OPTIONS	16

/* Where pixels go: a palette index for the framebuffer, or a colour. */
struct text_target {
	void	(*plot)(void *arg, int x, int y, uint32_t ink);
	void	 *arg;
};

struct text_style {
	const struct font	*font;
	const struct font	*caps;		/* drop capitals, or NULL */
	uint32_t		 ink;
	uint32_t		 highlight;	/* ink of the selected option */
	int			 line_gap;	/* extra pixels between lines */
};

struct text_option {
	int	x, y, w, h;	/* hit box, for mouse selection */
};

struct text_layout {
	int			 height;	/* total height drawn */
	int			 noptions;
	struct text_option	 option[TEXT_MAX_OPTIONS];
};

/*
 * Draw "text" in a box of width "w" at (x, y), option number "selected"
 * (0-based, -1 for none) in the highlight ink.  Fills "layout" when not
 * NULL.  A NULL target only measures.
 */
void	text_draw(const struct text_target *t, const struct text_style *st,
	    int x, int y, int w, const char *text, int selected,
	    struct text_layout *layout);

/* Width of a single line of text, without wrapping or markup. */
int	text_width(const struct font *font, const char *text);

#endif
