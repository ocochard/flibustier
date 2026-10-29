/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_TILESET_H
#define FLIBUSTIER_TILESET_H

#include <stddef.h>
#include <stdint.h>

/* Square tiles of one size (16x16 sailing, 32x32 sea battle). */
struct tileset {
	int		 tile_w;
	int		 tile_h;
	size_t		 count;
	uint8_t		*pixels;	/* count * tile_w * tile_h indices */
	uint8_t		 palette[256 * 3];	/* 8 bits per channel */
	int		 has_palette;

	/* Replacement art (tile sheets, see tiles.h): "frames" sets of all
	 * tiles, each "frame_rows" rows of "columns" tiles of art_tile
	 * pixels square; or NULL. */
	uint8_t		*rgba;		/* rgba_w * rgba_h * 4 */
	int		 rgba_w, rgba_h;
	int		 art_tile, columns, frame_rows;
	int		 frames;
	int		 period;	/* animation passes per frame */

	/* Replacement terrain (tile sheets): the class (TERRAIN_*) of
	 * every tile pixel, count * tile_w * tile_h, and the land battle
	 * ground (GROUND_*) of every tile, count; or NULL. */
	uint8_t		*terrain;
	uint8_t		*ground;
	/* Map tiles' shores (tile sheets, [coast]): count codes 0x11,
	 * 0x22, 0x33 or 0; or NULL. */
	uint8_t		*coast;
};

/*
 * Decode an IMB tileset: a 12-byte header, a 6-bit VGA palette (all zero
 * in the 16-colour variants), padding, then the tile records.
 * Returns 0 on success, -1 on malformed input.
 */
int	tileset_decode(const uint8_t *data, size_t len, struct tileset *out);
void	tileset_free(struct tileset *ts);

#endif
