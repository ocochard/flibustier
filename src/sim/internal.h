/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_SIM_INTERNAL_H
#define STMALO_SIM_INTERNAL_H

#include "sim.h"

/* Helpers shared by the simulation's rule modules; not public API. */

/* floor(sine(heading) x v / 256), from the game's half-circle table. */
int	sim_sine_mul(const struct world *w, int heading, int v);

/* a / b, or 0 when a < b or b == 0, as the game's div16 helper. */
int	sim_div16(int a, int b);

/*
 * Compass bearing (0 north, 64 east, 256 per turn) of the vector
 * (dx, dy), y growing southward, with the game's integer approximation
 * (VA 0x1ae99).
 */
int	sim_bearing(int dx, int dy);

#endif
