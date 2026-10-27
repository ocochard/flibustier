/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_SOUNDEXPORT_H
#define FLIBUSTIER_SOUNDEXPORT_H

struct packs;

/*
 * Write the original's sound effects (DIGITAL.PIR's, and the FM ones
 * rendered from PSOUND.PIR) to "dir" as WAV files named as the pack IDs
 * that replace them.  Returns an exit status.
 */
int	sounds_export(struct packs *packs, const char *dir);

#endif
