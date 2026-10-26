/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_WORLDFILES_H
#define STMALO_WORLDFILES_H

#include <stddef.h>

#include "data/pack.h"

/*
 * Write the world grids the packs give into directory "dir" (created if
 * needed) as an open pack: the six era maps and the sea and land battle
 * screens as Tiled maps, Tiled tilesets for the map and battle tiles
 * (their images, map.png and battle.png, are for the author to supply),
 * and a pack.ini.  For checking the open formats against the original
 * and as a start for authors: exported from the original data, the
 * files must not be distributed.  Returns 0, or -1 with a message in
 * "err".
 */
int	world_export(struct packs *packs, const char *dir, char *err,
	    size_t errlen);

#endif
