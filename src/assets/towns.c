/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <string.h>

#include "towns.h"

#define BLOCK_LEN	0xF24
#define HEADER_LEN	36
#define RECORD_LEN	80
#define NAME_OFF	64
#define NAME_LEN	16
#define LAST_INDEX_OFF	0x20

const int town_era_years[EXE_ERAS] = { 1560, 1600, 1620, 1640, 1660, 1680 };

static unsigned
le16(const uint8_t *p)
{
	return ((unsigned)p[0] | (unsigned)p[1] << 8);
}

static void
parse_town(const uint8_t *r, struct town *t)
{
	size_t i;

	for (i = 0; i < TOWN_FIELDS; i++)
		t->raw[i] = (uint16_t)le16(r + 2 * i);
	memcpy(t->name, r + NAME_OFF, NAME_LEN);
	t->name[NAME_LEN] = '\0';
	t->flags = t->raw[0];
	t->x = t->raw[1];
	t->y = t->raw[2];
	t->nation = t->raw[3];
	t->forts = t->raw[4];
	t->soldiers = t->raw[5];
	t->citizens = t->raw[6];
	t->gold = t->raw[7];
	t->wealth = t->raw[8];
	t->id = t->raw[14];
}

int
towns_load(const struct exe_layout *layout, const uint8_t *exe, size_t len,
    int era, struct era_towns *out)
{
	const uint8_t *b;
	size_t i, used;

	memset(out, 0, sizeof(*out));
	if (era < 0 || era >= EXE_ERAS)
		return (-1);
	if ((b = exe_at(layout, exe, len, layout->town_va[era],
	    BLOCK_LEN)) == NULL)
		return (-1);
	for (i = 0; i < 18; i++)
		out->header[i] = (uint16_t)le16(b + 2 * i);
	used = le16(b + LAST_INDEX_OFF) + 1;
	if (used < 2 || used > TOWN_MAX)
		return (-1);
	out->year = town_era_years[era];
	out->count = used - 2;
	for (i = 0; i < used; i++) {
		if (i < out->count)
			parse_town(b + HEADER_LEN + i * RECORD_LEN,
			    &out->towns[i]);
		else
			parse_town(b + HEADER_LEN + i * RECORD_LEN,
			    &out->channel[i - out->count]);
		if (i < out->count && (out->towns[i].name[0] == '\0' ||
		    out->towns[i].nation > NATION_HOLLAND))
			return (-1);
	}
	return (0);
}
