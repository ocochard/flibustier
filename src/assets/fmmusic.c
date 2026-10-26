/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdlib.h>
#include <string.h>

#include "fmmusic.h"

#define BANNER		"Pirates! Pro 7-13-93"
#define BANNER_OFF	0x10
#define ENTRIES_OFF	0x32	/* the entry offset table */
#define DATA_LEN	0x10000

/* The voice starters a command handler calls or jumps to. */
#define START_FREE	0x541
#define START_EFFECT	0x510
#define START_VOICE0	0x596	/* voice k at 0x596 + 5k, up to 7 */
#define START_VOICE8	0x5be

static unsigned
word(const uint8_t *p)
{
	return (p[0] | p[1] << 8);
}

static int
starter(unsigned target)
{
	if (target == START_FREE)
		return (FMMUSIC_FREE);
	if (target == START_EFFECT)
		return (FMMUSIC_EFFECT);
	if (target == START_VOICE8)
		return (8);
	if (target >= START_VOICE0 && target < START_VOICE0 + 40 &&
	    (target - START_VOICE0) % 5 == 0)
		return ((int)(target - START_VOICE0) / 5);
	return (-100);
}

/*
 * A handler starts its streams with "lea cx, [stream]" then a call or
 * a jump to a starter; it ends with a near return or that jump.
 */
static void
scan(struct fmmusic *m, int cmd, const uint8_t *im, size_t len,
    uint32_t at)
{
	uint32_t p, end = at + 160 < len ? at + 160 : (uint32_t)len;
	int16_t rel;
	int v;

	for (p = at; p + 7 <= end; p++) {
		if (im[p] == 0xc3)
			return;
		if (im[p] == 0x8d && im[p + 1] == 0x0e &&
		    (im[p + 4] == 0xe8 || im[p + 4] == 0xe9)) {
			rel = (int16_t)word(im + p + 5);
			v = starter((uint16_t)(p + 7 + rel));
			if (v != -100 && m->nstreams[cmd] < FMMUSIC_STREAMS) {
				m->stream[cmd][m->nstreams[cmd]] =
				    (uint16_t)word(im + p + 2);
				m->voice[cmd][m->nstreams[cmd]++] = v;
			}
			if (im[p + 4] == 0xe9)
				return;
			p += 6;
		} else if (im[p] == 0xe9)
			return;
	}
}

int
fmmusic_decode(const uint8_t *file, size_t len, struct fmmusic *out)
{
	const uint8_t *im;
	size_t hdr, imlen, ds = 0;
	uint32_t e, p, table = 0;
	int c;

	memset(out, 0, sizeof(*out));
	if (len < 0x40 || file[0] != 'M' || file[1] != 'Z')
		return (-1);
	hdr = (size_t)word(file + 8) * 16;
	if (hdr + 0x100 > len ||
	    memcmp(file + hdr + BANNER_OFF, BANNER, strlen(BANNER)) != 0)
		return (-1);
	im = file + hdr;
	imlen = len - hdr;
	/* Entry 0 loads the data segment ("mov ax, seg; mov ds, ax"); entry
	 * 1 dispatches through "call cs:[bx + table]". */
	e = word(im + ENTRIES_OFF);
	for (p = e; p + 5 < imlen && p < e + 32; p++)
		if (im[p] == 0xb8 && im[p + 3] == 0x8e && im[p + 4] == 0xd8) {
			ds = word(im + p + 1) * 16u;
			break;
		}
	e = word(im + ENTRIES_OFF + 2);
	for (p = e; p + 5 < imlen && p < e + 48; p++)
		if (im[p] == 0x2e && im[p + 1] == 0xff && im[p + 2] == 0x97) {
			table = word(im + p + 3);
			break;
		}
	if (ds == 0 || ds >= imlen || table == 0 ||
	    table + 2 * FMMUSIC_COMMANDS > imlen ||
	    (out->data = calloc(1, DATA_LEN)) == NULL) {
		memset(out, 0, sizeof(*out));
		return (-1);
	}
	memcpy(out->data, im + ds, imlen - ds < DATA_LEN ? imlen - ds :
	    DATA_LEN);
	for (c = 0; c < FMMUSIC_COMMANDS; c++)
		scan(out, c, im, imlen, word(im + table + 2 * c));
	return (0);
}

void
fmmusic_free(struct fmmusic *m)
{
	free(m->data);
	memset(m, 0, sizeof(*m));
}
