/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_TILES_H
#define FLIBUSTIER_TILES_H

#include "data/pack.h"
#include "image.h"
#include "tileset.h"

/*
 * Tilesets from any pack (tiles/ IDs): an original IMB, or a replacement
 * tile sheet, a text file naming a PNG that holds the tiles in rows, left
 * to right, top to bottom:
 *
 *	[tiles]
 *	image = map.png
 *	# tile side in art pixels: 36 for 16 pixel tiles at 1080p
 *	tile = 36
 *	# optional: tile count (default 256)
 *	count = 256
 *	# optional: animation frames (default 1), each a full set of
 *	# tiles below the previous one, and sailing passes per frame
 *	# (default 4); the sea shimmer of the original palette
 *	frames = 8
 *	period = 4
 *	# optional: terrain, the same grid at 640x480 resolution in
 *	# three colours: sea #0000FF, shallows #00FFFF, land #00FF00
 *	terrain = map-terrain.png
 *
 *	# optional, battle tiles: land battle ground per tile, clear,
 *	# cover, swamp or water; unlisted tiles are clear
 *	[ground]
 *	12 = cover
 *
 * A tile covers its art size times 480 / art_height of its pack in
 * 640x480 pixels, rounded.  Palette indices and a palette still come
 * with it, for code that reads them: the original tileset's when it is
 * installed with the same tiles, else the first frame reduced to the
 * fixed palette.  Without terrain, the world takes terrain from those
 * indices, which only the original's describe.
 *
 * tileset_from_blob() takes the blob packs_load() returned for "id".
 * Both return 0, or -1 if the tileset is missing or malformed.
 */
int	tileset_load(struct packs *packs, const char *id, struct tileset *out);

/* A sheet's names: [ground] by GROUND_*, [coast] with their codes. */
extern const char *const tile_ground_names[4];
struct tile_coast {
	const char	*name;
	uint8_t		 code;
};
extern const struct tile_coast tile_coast_names[3];
int	tileset_from_blob(struct packs *packs, const char *id,
	    const struct blob *b, struct tileset *out);

/*
 * Tile "i" as a picture to draw with fb_image(): its indices, and its art
 * at animation pass "pass" when the tileset has replacement art.  The
 * picture points into the tileset; it is not to be freed.
 */
void	tileset_tile(const struct tileset *ts, size_t i, unsigned pass,
	    struct image *out);

#endif
