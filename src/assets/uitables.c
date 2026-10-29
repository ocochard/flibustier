/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdio.h>
#include <string.h>

#include "uitables.h"

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

int
ui_tables_load(struct ui_tables *t, const struct exe_layout *layout,
    const uint8_t *exe, size_t len, char *err, size_t errlen)
{
	const uint8_t *p;
	int i, k;

	memset(t, 0, sizeof(*t));
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
	    read_records(layout, exe, len, layout->fleet_panel_va, t->panel,
	    1, UI_PANEL_WORDS, 0) != 0 ||
	    read_records(layout, exe, len, layout->jolly_va,
	    (int16_t *)&t->jolly[0][0], 1, 4 * 16, 0) != 0)
		goto bad;
	for (i = 0; i < UI_CAVE_PILES; i++) {
		if ((p = exe_at(layout, exe, len, layout->cave_va +
		    (uint32_t)i * 12, 12)) == NULL)
			goto bad;
		for (k = 0; k < 3; k++)
			t->cave[i][k] = (int32_t)(p[4 * k] | p[4 * k + 1] << 8 |
			    p[4 * k + 2] << 16 | (uint32_t)p[4 * k + 3] << 24);
	}
	if ((p = exe_at(layout, exe, len, layout->credits_va +
	    UI_CREDIT_BLOCKS * 8, sizeof(t->credit_ink))) == NULL)
		goto bad;
	memcpy(t->credit_ink, p, sizeof(t->credit_ink));
	if ((p = exe_at(layout, exe, len, layout->shade_va, 256)) == NULL)
		goto bad;
	memcpy(t->shade, p, 256);
	if ((p = exe_at(layout, exe, len, layout->story_va,
	    UI_STORY_MAX)) == NULL)
		goto bad;
	memcpy(t->story, p, UI_STORY_MAX);
	load_cursor(t, layout, exe, len);
	return (0);
bad:
	snprintf(err, errlen, "screen tables missing from the executable");
	return (-1);
}
