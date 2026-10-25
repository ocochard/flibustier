/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <string.h>

#include "rsd.h"

/*
 * Opcodes, with a cursor starting at pixel 0:
 *   80 lo hi	skip a 16-bit pixel count; a count of 0 ends the stream
 *   81..FF	skip (op & 0x7F) pixels
 *   01..7F	copy the next op bytes as pixels
 */
int
rsd_apply(const uint8_t *data, size_t len, uint8_t *pixels, size_t npixels)
{
	size_t i, pos, n;
	uint8_t op;

	i = 0;
	pos = 0;
	while (i < len) {
		op = data[i++];
		if (op == 0x80) {
			if (i + 2 > len)
				return (-1);
			n = (size_t)data[i] | (size_t)data[i + 1] << 8;
			i += 2;
			if (n == 0)
				return (0);
			pos += n;
		} else if (op > 0x80)
			pos += op & 0x7F;
		else if (op > 0) {
			if (i + op > len || pos + op > npixels)
				return (-1);
			memcpy(pixels + pos, data + i, op);
			i += op;
			pos += op;
		} else
			return (-1);
		if (pos > npixels)
			return (-1);
	}
	/* Streams end with the 80 00 00 marker. */
	return (-1);
}
