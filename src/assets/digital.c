/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdlib.h>
#include <string.h>

#include "digital.h"

/* In the image after the MZ header: the effects' start and end tables. */
#define STARTS_OFF	0x9f
#define ENDS_OFF	0xb1

static unsigned
word(const uint8_t *p)
{
	return (p[0] | p[1] << 8);
}

int
digital_decode(const uint8_t *data, size_t len, struct digital *out)
{
	const uint8_t *im;
	size_t hdr, imlen;
	unsigned s, e;
	int i;

	memset(out, 0, sizeof(*out));
	if (len < 0x20 || data[0] != 'M' || data[1] != 'Z')
		return (-1);
	hdr = (size_t)word(data + 8) * 16;
	if (hdr + ENDS_OFF + 2 * DIGITAL_EFFECTS > len)
		return (-1);
	im = data + hdr;
	imlen = len - hdr;
	for (i = 0; i < DIGITAL_EFFECTS; i++) {
		s = word(im + STARTS_OFF + 2 * i);
		e = word(im + ENDS_OFF + 2 * i);
		if (s >= e || e > imlen ||
		    (out->pcm[i] = malloc(e - s)) == NULL) {
			digital_free(out);
			return (-1);
		}
		memcpy(out->pcm[i], im + s, e - s);
		out->len[i] = e - s;
	}
	return (0);
}

void
digital_free(struct digital *d)
{
	int i;

	for (i = 0; i < DIGITAL_EFFECTS; i++)
		free(d->pcm[i]);
	memset(d, 0, sizeof(*d));
}
