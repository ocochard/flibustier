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
#include "data/pack.h"
#include "sim/json.h"

#define TOWN_MAX	48	/* records per era block, used or not */
#define TOWN_FIELDS	32
#define CONVOY_STOPS	15	/* a convoy's route */

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
	/* The fields the game state starts from (struct sim_town). */
	int		 standing;	/* f9 */
	int		 garrison;	/* f10 */
	int		 governor;	/* f11 */
	int		 family;	/* f12 */
	int		 barmaid;	/* f13 */
	int		 land_x, land_y;	/* f15, f16 */
	int		 fort_x, fort_y;	/* f17, f18 */
	int		 ship_x, ship_y, ship_heading;	/* f19-f21 */
	int		 party_x, party_y;	/* f22, f23 */
	int		 sea_x, sea_y, sea_heading;	/* f26-f28 */
	/* f29, f30: the town's dot on the Map of the Caribbean
	 * (pic/citymap). */
	int		 citymap_x, citymap_y;
	uint16_t	 raw[TOWN_FIELDS];	/* original data only */
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
	/* The treasure fleet's and the silver train's stops, town indices
	 * or -1 (the block's first 32 bytes, VA 0x4605e, 0x4606e). */
	int		 fleet_route[CONVOY_STOPS];
	int		 train_route[CONVOY_STOPS];
};

extern const int town_era_years[EXE_ERAS];

/* era: 0 (1560) to 5 (1680).  Returns 0, or -1 on malformed data. */
int	towns_load(const struct exe_layout *layout, const uint8_t *exe,
	    size_t len, int era, struct era_towns *out);

/*
 * The towns of an era from any pack: "data/towns/<year>" when a pack
 * gives it (JSON, see towns_decode()), else the executable's table.
 * Returns 0, or -1 with a message in "err".
 */
int	towns_for_era(struct packs *packs, const struct exe_layout *layout,
	    const uint8_t *exe, size_t len, int era, struct era_towns *out,
	    char *err, size_t errlen);

/*
 * Open town data, one JSON file per era:
 *
 *	{ "towns": [ { "name": "Havana", "id": 6, "nation": "spain",
 *	    "x": 245, "y": 179, "flags": 16, "forts": 4, "soldiers": 300,
 *	    "citizens": 9000, "gold": 50, "wealth": 195, "standing": 0,
 *	    "garrison": 0, "governor": 3, "family": 0, "barmaid": 0,
 *	    "land": [x, y], "fort": [x, y], "ship": [x, y, heading],
 *	    "party": [x, y], "sea": [x, y, heading],
 *	    "citymap": [x, y] }, ... ] }
 *
 * Every member is required; numbers are 0..65535, nations "spain",
 * "england", "france" or "holland".  towns_encode() writes that form.
 */
int	towns_decode(const uint8_t *data, size_t len, int era,
	    struct era_towns *out, char *err, size_t errlen);
void	towns_encode(const struct era_towns *era, struct jbuf *b);

#endif
