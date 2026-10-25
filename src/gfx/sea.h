/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_SEA_H
#define STMALO_SEA_H

#include <stdint.h>

/*
 * The sailing sea's shimmer, as the game does it: four 8-colour ranges of
 * the map palette (208-215, 64-71, 72-79, 216-223) are snapshotted, then
 * rewritten every tick from a rotating offset.  208-215 and 216-223 turn
 * every tick (the latter written in reverse), 64-79 every other tick.
 */
struct sea {
	uint8_t	snap[4][8][3];
};

/* Snapshot the ranges from a palette (256 x RGB). */
void	sea_init(struct sea *sea, const uint8_t *palette);
/* Write the ranges for animation tick "tick" into "palette". */
void	sea_animate(const struct sea *sea, unsigned tick, uint8_t *palette);

#endif
