/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include "rng.h"

#define PCG_MULT	6364136223846793005ULL

void
rng_seed(struct rng *r, uint64_t seed, uint64_t stream)
{
	r->state = 0;
	r->inc = stream << 1 | 1;
	rng_next(r);
	r->state += seed;
	rng_next(r);
}

uint32_t
rng_next(struct rng *r)
{
	uint64_t old = r->state;
	uint32_t xorshifted, rot;

	r->state = old * PCG_MULT + r->inc;
	xorshifted = (uint32_t)(((old >> 18) ^ old) >> 27);
	rot = (uint32_t)(old >> 59);
	return (xorshifted >> rot | xorshifted << ((32 - rot) & 31));
}

int
rng_below(struct rng *r, int n)
{
	if (n <= 0)
		return (0);
	/* Multiply-shift: unbiased enough for game use, and branch-free. */
	return ((int)(((uint64_t)rng_next(r) * (uint32_t)n) >> 32));
}
