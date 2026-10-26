/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_MUSIC_H
#define FLIBUSTIER_MUSIC_H

#include <stddef.h>
#include <stdint.h>

#include "assets/fmmusic.h"

/*
 * The FM music driver's tunes and effects played on an emulated OPL3:
 * its scripts stepped 60.9 times a second, as its timer does.  See
 * "Music" in game-rules.md.
 */
#define MUSIC_RATE	49716u		/* the chip's own rate, stereo */

struct music;

/* A player for the driver "d", which it copies. */
struct music	*music_new(const struct fmmusic *d);
void		 music_free(struct music *m);

/* Start driver command "cmd": a tune stops the music first. */
void		 music_command(struct music *m, int cmd);
void		 music_stop(struct music *m);

/* Fill "lr" with "frames" stereo frames at MUSIC_RATE. */
void		 music_render(struct music *m, int16_t *lr, size_t frames);

/* Voices still playing, for tests. */
int		 music_active(const struct music *m);

#endif
