/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_SPRITE_H
#define FLIBUSTIER_SPRITE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Sprite banks.  All original formats share one container: a u16 offset
 * table whose first entry is the offset of frame 0 and equals twice the
 * frame count.  Frames come in two encodings:
 * - run-coded (SPR, CRS): transparent skips and opaque runs, positioned
 *   with the 640-pixel screen stride the original blitter used;
 * - raw (CUR, BMP tiles): w * h pixels, all opaque.  Some files use the
 *   frame "key" as a colour key, which is left to the renderer.
 * Frames carry no palette; each file pairs with a picture's palette.
 */

struct frame {
	int		 w;
	int		 h;
	unsigned	 key;		/* per-frame header word */
	uint8_t		*pixels;	/* w * h indices */
	uint8_t		*mask;		/* w * h, 1 = opaque; NULL = opaque */
	/* Replacement art (sprite sheets): true colours with alpha at
	 * their own resolution, covering the w x h box; or NULL. */
	uint8_t		*rgba;		/* rgba_w * rgba_h * 4 */
	int		 rgba_w;
	int		 rgba_h;
};

struct sprite_bank {
	size_t		 count;
	struct frame	*frames;
};

/* GRP: a u32 offset table of run-coded banks (fencing animations). */
struct sprite_group {
	size_t			 count;
	struct sprite_bank	*banks;
};

/* All return 0 on success, -1 on malformed input. */
int	bank_decode_runs(const uint8_t *data, size_t len,
	    struct sprite_bank *out);
int	bank_decode_raw(const uint8_t *data, size_t len,
	    struct sprite_bank *out);
int	group_decode(const uint8_t *data, size_t len,
	    struct sprite_group *out);

void	bank_free(struct sprite_bank *bank);
void	group_free(struct sprite_group *group);

#endif
