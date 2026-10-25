/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <string.h>

#include "sea.h"

static const int ranges[4] = { 208, 64, 72, 216 };

void
sea_init(struct sea *sea, const uint8_t *palette)
{
	int r, i;

	for (r = 0; r < 4; r++)
		for (i = 0; i < 8; i++)
			memcpy(sea->snap[r][i], palette + 3 * (ranges[r] + i), 3);
}

void
sea_animate(const struct sea *sea, unsigned tick, uint8_t *palette)
{
	unsigned c[4];
	int r, i, entry;

	c[0] = tick;
	c[1] = c[2] = tick / 2;
	c[3] = tick;
	for (r = 0; r < 4; r++)
		for (i = 0; i < 8; i++) {
			entry = r == 3 ? ranges[r] + 7 - i : ranges[r] + i;
			memcpy(palette + 3 * entry, sea->snap[r][(c[r] + i) & 7],
			    3);
		}
}
