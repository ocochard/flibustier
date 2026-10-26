/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_VIEWER_H
#define FLIBUSTIER_VIEWER_H

#include "data/pack.h"

/*
 * Run the interactive asset viewer.  "start_id" preselects an asset
 * (may be NULL).  With "shot" set, the screen is saved to that PNG after
 * "shot_frames" frames and the viewer exits.  Returns an exit status.
 */
int	viewer_run(struct packs *packs, const char *start_id,
	    const char *shot, int shot_frames);

#endif
