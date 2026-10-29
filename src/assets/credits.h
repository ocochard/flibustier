/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_CREDITS_H
#define FLIBUSTIER_CREDITS_H

#include <stddef.h>
#include <stdint.h>

/*
 * A pack's credits as text (text/<lang>/credits, JSON): blocks shown one
 * at a time after the intro, each a heading and its lines.  See "Open
 * credits" in aidd_docs/memory/internal/asset-packs.md.
 */
#define CREDITS_MAX_BLOCKS	16
#define CREDITS_MAX_LINES	12

struct credit_block {
	char	 *heading;	/* UTF-8, "" when none */
	size_t	  nlines;
	char	**lines;	/* UTF-8 */
};

struct credits {
	size_t			 nblocks;
	struct credit_block	*blocks;
	uint8_t			 ink[3];	/* default black */
};

/* Returns 0, or -1 with a message in "err" and "out" empty. */
int	credits_decode(const uint8_t *data, size_t len, struct credits *out,
	    char *err, size_t errlen);
void	credits_free(struct credits *c);

#endif
