/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_OPL3_H
#define FLIBUSTIER_OPL3_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * An emulated YMF262 (OPL3), from the vendored ymfm, behind a C
 * interface: registers 0x000-0x0ff are the first bank, 0x100-0x1ff the
 * second.  It runs at its own rate (OPL3_RATE), in stereo.
 */
#define OPL3_CLOCK	14318181u
#define OPL3_RATE	49716u		/* OPL3_CLOCK / 288 */

struct opl3;

struct opl3	*opl3_new(void);
void		 opl3_free(struct opl3 *o);
void		 opl3_write(struct opl3 *o, unsigned reg, unsigned val);
/* "frames" stereo frames, left then right. */
void		 opl3_generate(struct opl3 *o, int16_t *lr, size_t frames);

#ifdef __cplusplus
}
#endif

#endif
