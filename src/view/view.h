/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_VIEW_H
#define STMALO_VIEW_H

#include <stddef.h>
#include <stdint.h>

#include "data/pack.h"

/*
 * Asset views: render any asset, or a derived view of several, into an
 * RGB image.  This is the CPU side of the asset viewer; it has no
 * raylib dependency so it can run headless (export, tests, bots).
 *
 * Besides pack IDs, two virtual IDs exist:
 *   world/<year>	the sailing world of an era, drawn from map tiles
 *			with the animated sea; frames are scroll positions
 *			10 cells apart, view_row_frames() per row
 *   towns/<year>	the era's tile map by terrain class, with towns
 */

#define VIEW_W	640
#define VIEW_H	480

struct view;

struct rgb {
	int		 w;
	int		 h;
	uint8_t		*px;	/* w * h * 3 */
};

/* Returns NULL on error, with a message in "err". */
struct view	*view_open(struct packs *packs, const char *id, char *err,
		    size_t errlen);
void		 view_close(struct view *v);

/* Number of frames (sprite frames, strings, scroll positions...). */
size_t		 view_frames(const struct view *v);

/* Most useful first frame: for world views, centred on Havana. */
size_t		 view_start_frame(const struct view *v);

/* Frames per row for 2D-scrolling views (world), or 0. */
size_t		 view_row_frames(const struct view *v);

/*
 * Render frame "frame" at animation tick "tick" into "out" (allocated
 * by the view, valid until the next call or view_close()).  Returns 0, or
 * -1 on error.
 */
int		 view_render(struct view *v, size_t frame, unsigned tick,
		    const struct rgb **out);

/* One-line description: kind, size, frame count, palette used. */
const char	*view_info(const struct view *v);

/*
 * Palette override for paletteless assets (sprites): a pic/ ID, or NULL
 * to restore the default guess.  Returns 0, or -1 if it cannot be loaded.
 */
int		 view_set_palette(struct view *v, const char *pic_id);

/* Virtual IDs, to list alongside the pack IDs. */
size_t		 view_virtual_count(void);
const char	*view_virtual_id(size_t i);

#endif
