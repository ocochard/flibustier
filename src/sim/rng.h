/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_RNG_H
#define STMALO_RNG_H

#include <stdint.h>

/*
 * PCG32 (O'Neill, pcg-random.org): small, fast and fully determined by
 * its two words, so a game state saved with them replays exactly.  It is
 * not the original game's generator; only the distributions matter.
 */
struct rng {
	uint64_t	state;
	uint64_t	inc;	/* stream selector, always odd */
};

void		rng_seed(struct rng *r, uint64_t seed, uint64_t stream);
uint32_t	rng_next(struct rng *r);

/* Uniform integer in 0..n-1; 0 when n <= 0, like an empty range. */
int		rng_below(struct rng *r, int n);

#endif
