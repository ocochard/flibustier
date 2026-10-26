/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <string.h>

#include "scenario.h"

#define NAME_LEN	20
#define CREW_OFF	0x14
#define GUNS_OFF	0x16
#define NATION_OFF	0x17
#define GOLD_OFF	0x18	/* tens of gold pieces */
#define FOOD_OFF	0x1a	/* cargo, in tons */
#define GOODS_OFF	0x1b
#define SUGAR_OFF	0x1c
#define FOOD_FRAC_OFF	0x1e	/* VA 0x138d4 */
#define CAPACITY_OFF	0x1f	/* hold, in tens of tons */
#define NSHIPS_OFF	0x20
#define SHIPS_OFF	0x21
#define RELATIONS_OFF	0x34
#define STARTS_OFF	0x44

static unsigned
le16(const uint8_t *p)
{
	return ((unsigned)p[0] | (unsigned)p[1] << 8);
}

int
scenario_career(int era, enum scenario_nation nation)
{
	return (era * 4 + (int)nation);
}

int
scenario_load(const struct exe_layout *layout, const uint8_t *exe,
    size_t len, int index, struct scenario *out)
{
	const uint8_t *r;
	int a, b;

	memset(out, 0, sizeof(*out));
	if (index < 0 || index >= SCENARIO_COUNT)
		return (-1);
	if ((r = exe_at(layout, exe, len, layout->scenario_va +
	    (uint32_t)index * SCENARIO_LEN, SCENARIO_LEN)) == NULL)
		return (-1);
	memcpy(out->raw, r, SCENARIO_LEN);
	memcpy(out->name, r, NAME_LEN);
	out->name[NAME_LEN] = '\0';
	out->crew = (int)le16(r + CREW_OFF);
	out->guns = r[GUNS_OFF];
	out->nation = r[NATION_OFF];
	out->gold = (long)le16(r + GOLD_OFF) * 10;
	out->food = r[FOOD_OFF];
	out->food_frac = r[FOOD_FRAC_OFF];
	out->goods = r[GOODS_OFF];
	out->sugar = r[SUGAR_OFF];
	out->capacity = r[CAPACITY_OFF];
	out->nships = r[NSHIPS_OFF];
	memcpy(out->ships, r + SHIPS_OFF, SCENARIO_SHIPS);
	for (a = 0; a < SCENARIO_STARTS; a++) {
		out->start[a][0] = (uint16_t)le16(r + STARTS_OFF + 4 * a);
		out->start[a][1] = (uint16_t)le16(r + STARTS_OFF + 4 * a + 2);
	}
	if (out->nships > SCENARIO_SHIPS || out->nation > 3)
		return (-1);
	for (a = 0; a < 4; a++)
		for (b = 0; b < 4; b++) {
			out->relations[a][b] = (int8_t)r[RELATIONS_OFF + a * 4 + b];
			if (out->relations[a][b] < -1 || out->relations[a][b] > 1)
				return (-1);
		}
	return (0);
}
