/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <string.h>

#include "scenario.h"

#define NAME_LEN	20
#define RELATIONS_OFF	0x34

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
	for (a = 0; a < 4; a++)
		for (b = 0; b < 4; b++) {
			out->relations[a][b] = (int8_t)r[RELATIONS_OFF + a * 4 + b];
			if (out->relations[a][b] < -1 || out->relations[a][b] > 1)
				return (-1);
		}
	return (0);
}
