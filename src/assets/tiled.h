/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_TILED_H
#define STMALO_TILED_H

#include <stddef.h>
#include <stdint.h>

#include "sim/json.h"

/*
 * Tile grids as Tiled maps (JSON, .tmj; https://www.mapeditor.org/), so
 * open packs can author them in Tiled.  Supported: orthogonal fixed-size
 * maps with one tileset and tile layers stored as CSV (Tiled's "Tile
 * Layer Format: CSV"), every cell holding an unflipped tile.  Tile layers
 * are read in order; object and image layers are ignored, group layers
 * refused (their order would be ambiguous).
 *
 * tiled_read() reads exactly "nlayers" layers of w x h cells into
 * "out", nlayers * w * h tile numbers (0..255, the tileset's own).
 * Returns 0, or -1 with a message in "err".
 */
int	tiled_read(const uint8_t *data, size_t len, int w, int h,
	    size_t nlayers, uint8_t *out, char *err, size_t errlen);

/*
 * Write such a map: "nlayers" layers of w x h tile numbers from "grid",
 * cells of tile_w x tile_h pixels, the tileset in file "tileset" (.tsj).
 * Layers are named "<name> <i>", or "<name>" for a single one.
 */
void	tiled_write(struct jbuf *b, int w, int h, int tile_w, int tile_h,
	    size_t nlayers, const uint8_t *grid, const char *name,
	    const char *tileset);

/*
 * A Tiled tileset (.tsj) of "count" tiles of "tile" pixels from image
 * "image" (w x h pixels), for authoring maps against a tile sheet.
 */
void	tiled_write_tileset(struct jbuf *b, const char *name, int tile,
	    int count, const char *image, int w, int h);

#endif
