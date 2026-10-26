/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_FMMUSIC_H
#define FLIBUSTIER_FMMUSIC_H

#include <stddef.h>
#include <stdint.h>

/*
 * The FM music driver for OPL3 cards, PSOUND.PIR (asset driver/psound):
 * the music and FM effects as scripts for nine voices, with their
 * instruments and tables, read from the player's copy.  See
 * asset-formats.md, "The FM music engine".
 */
#define FMMUSIC_COMMANDS	46	/* driver commands: tunes, effects */
#define FMMUSIC_STREAMS		9	/* at most a stream per voice */
#define FMMUSIC_FREE		-1	/* the first free voice of 0-5 */
#define FMMUSIC_EFFECT		-2	/* the first free voice of 6-8 */

struct fmmusic {
	uint8_t		*data;		/* the data segment, 64 KiB */
	/* Per command, its streams (data offsets) and their voices: 0-8,
	 * or FMMUSIC_FREE, FMMUSIC_EFFECT. */
	int		 nstreams[FMMUSIC_COMMANDS];
	uint16_t	 stream[FMMUSIC_COMMANDS][FMMUSIC_STREAMS];
	int		 voice[FMMUSIC_COMMANDS][FMMUSIC_STREAMS];
};

/* Returns 0, or -1 if the image is not the known driver. */
int	fmmusic_decode(const uint8_t *file, size_t len, struct fmmusic *out);
void	fmmusic_free(struct fmmusic *m);

#endif
