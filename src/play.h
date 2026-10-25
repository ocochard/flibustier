/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_PLAY_H
#define STMALO_PLAY_H

#include "data/pack.h"

/*
 * Run the game in a window.  With "shot" set, the screen is saved to that
 * PNG after "shot_frames" frames and the game exits.  Returns an exit
 * status.
 */
int	play_run(struct packs *packs, const char *lang, const char *shot,
	    int shot_frames);

/*
 * Run the game without a window: play "script" (see src/ui/script.h),
 * then save the screen to "out".  Returns an exit status.
 */
int	play_headless(struct packs *packs, const char *lang,
	    const char *script, const char *out);

#endif
