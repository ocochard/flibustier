/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_SCENARIO_H
#define STMALO_SCENARIO_H

#include <stdint.h>

#include "data/exe.h"

#define SCENARIO_COUNT		30
#define SCENARIO_CAREERS	24	/* then the famous expeditions */
#define SCENARIO_LEN		90

/* Career starts are stored per era in this nation order. */
enum scenario_nation {
	SCN_ENGLAND,
	SCN_FRANCE,
	SCN_HOLLAND,
	SCN_SPAIN
};

/*
 * A start scenario.  "relations" uses the town nation order (Spain,
 * England, France, Holland): -1 war, 0 peace, +1 alliance.  "raw" keeps
 * the whole record for the fields not understood yet.
 */
#define SCENARIO_SHIPS	8
#define SCENARIO_STARTS	4

struct scenario {
	char		 name[21];	/* empty for the unused Dutch 1560 slot */
	int8_t		 relations[4][4];
	int		 crew;
	int		 guns;
	int		 nation;	/* town nation order */
	long		 gold;
	int		 food, goods, sugar;	/* tons */
	int		 food_frac;	/* food eaten into, 256ths */
	int		 capacity;	/* hold, tens of tons */
	int		 nships;
	uint8_t		 ships[SCENARIO_SHIPS];	/* ship types, 0 = pinnace */
	uint16_t	 start[SCENARIO_STARTS][2];	/* x, y in map cells */
	uint8_t		 raw[SCENARIO_LEN];
};

/* Returns 0, or -1 if out of range or the data is malformed. */
int	scenario_load(const struct exe_layout *layout, const uint8_t *exe,
	    size_t len, int index, struct scenario *out);

/* Index of the career start for era 0..5 and a scenario nation. */
int	scenario_career(int era, enum scenario_nation nation);

#endif
