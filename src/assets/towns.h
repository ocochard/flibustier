/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_TOWNS_H
#define STMALO_TOWNS_H

#include <stddef.h>
#include <stdint.h>

#include "data/exe.h"

#define TOWN_MAX	48	/* records per era block, used or not */
#define TOWN_FIELDS	32

enum nation {
	NATION_SPAIN,
	NATION_ENGLAND,
	NATION_FRANCE,
	NATION_HOLLAND
};

/* Town flag bits (field 0). */
#define TOWN_INLAND	0x40	/* reached by land; one approach point */

/*
 * Initial state of a town at the start of an era.  Named fields are
 * those whose meaning the game code confirms; see
 * aidd_docs/memory/internal/asset-formats.md for the evidence.  "raw"
 * keeps every field for the ones not understood yet.
 */
struct town {
	char		 name[17];
	unsigned	 flags;		/* f0 */
	int		 x, y;		/* f1, f2: overview map, 640x480 */
	int		 nation;	/* f3: enum nation */
	int		 forts;		/* f4 */
	int		 soldiers;	/* f5 */
	int		 citizens;	/* f6 */
	int		 gold;		/* f7 */
	int		 wealth;	/* f8: 0..255, 64 per wealth level */
	int		 id;		/* f14: same town across eras */
	uint16_t	 raw[TOWN_FIELDS];
};

/*
 * One era block: a 36-byte header (not understood yet), then 48 town
 * records.  The header's u16 at offset 0x20 is the index of the last
 * record in use.  The last two records in use are not towns but two
 * "Florida Channel" sea points, kept apart in "channel".
 */
struct era_towns {
	int		 year;
	size_t		 count;
	struct town	 towns[TOWN_MAX];
	struct town	 channel[2];
	uint16_t	 header[18];
};

extern const int town_era_years[EXE_ERAS];

/* era: 0 (1560) to 5 (1680).  Returns 0, or -1 on malformed data. */
int	towns_load(const struct exe_layout *layout, const uint8_t *exe,
	    size_t len, int era, struct era_towns *out);

#endif
