/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_SEA_H
#define FLIBUSTIER_SEA_H

#include <stdint.h>

/*
 * The sailing sea's shimmer, as the game does it: four 8-colour ranges of
 * the map palette (208-215, 64-71, 72-79, 216-223) are snapshotted, then
 * rewritten from a rotating offset.  In the sailing loop (VA 0x231b8)
 * 208-215 turn every 4 passes, 64-79 and 216-223 (written in reverse)
 * every 2 passes.
 */
struct sea {
	uint8_t	snap[4][8][3];
};

/* Snapshot the ranges from a palette (256 x RGB). */
void	sea_init(struct sea *sea, const uint8_t *palette);
/* Write the ranges for sailing loop pass "pass" into "palette". */
void	sea_animate(const struct sea *sea, unsigned pass, uint8_t *palette);

#endif
