/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_SCRIPT_H
#define FLIBUSTIER_SCRIPT_H

#include "game.h"

/*
 * Drive the shell without a window: space-separated steps
 *   up down left right enter esc bs	key presses
 *   type:TEXT				typed characters
 *   click:X,Y move:X,Y			pointer, 640x480 coordinates
 *   wait:N				N ticks
 * Every step is followed by enough ticks to finish a screen fade.
 * Returns 0, or -1 on a bad step (message in g->err).
 */
int	game_script(struct game *g, const char *script);

#endif
