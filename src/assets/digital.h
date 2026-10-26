/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_DIGITAL_H
#define FLIBUSTIER_DIGITAL_H

#include <stddef.h>
#include <stdint.h>

/*
 * The digitised effects of DIGITAL.PIR (asset driver/digital): 8-bit
 * unsigned mono PCM at DIGITAL_RATE Hz.  See asset-formats.md.
 */
#define DIGITAL_EFFECTS	9
#define DIGITAL_RATE	7330

struct digital {
	uint8_t		*pcm[DIGITAL_EFFECTS];
	size_t		 len[DIGITAL_EFFECTS];
};

/* Returns 0, or -1 if the driver image is malformed. */
int	digital_decode(const uint8_t *data, size_t len, struct digital *out);
void	digital_free(struct digital *d);

#endif
