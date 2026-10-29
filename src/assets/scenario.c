/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "datafields.h"
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
#define STANDING_OFF	0x2a	/* 5 signed: the nations, then pirates */
#define RANK_OFF	0x2f	/* 5 */
#define RELATIONS_OFF	0x34
#define STARTS_OFF	0x44
#define DATE_OFF	0x56	/* an expedition's: years x 360 + day */
#define MONTHS_OFF	0x58	/* months already at sea */

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

#define SCN_FIELD(name, type, d0, d1)					\
	{ #name, offsetof(struct scenario, name), type, { d0, d1, 0 } }

/* data/scenarios: each scenario's fields. */
static const struct data_field fields[] = {
	SCN_FIELD(name, DATA_STR, sizeof(((struct scenario *)0)->name), 0),
	SCN_FIELD(nation, DATA_INT, 0, 0),
	SCN_FIELD(relations, DATA_I8, 4, 4),
	SCN_FIELD(standing, DATA_I8, 5, 0),
	SCN_FIELD(rank, DATA_U8, 5, 0),
	SCN_FIELD(crew, DATA_INT, 0, 0),
	SCN_FIELD(guns, DATA_INT, 0, 0),
	SCN_FIELD(gold, DATA_LONG, 0, 0),
	SCN_FIELD(food, DATA_INT, 0, 0),
	SCN_FIELD(food_frac, DATA_INT, 0, 0),
	SCN_FIELD(goods, DATA_INT, 0, 0),
	SCN_FIELD(sugar, DATA_INT, 0, 0),
	SCN_FIELD(capacity, DATA_INT, 0, 0),
	SCN_FIELD(nships, DATA_INT, 0, 0),
	SCN_FIELD(ships, DATA_U8, SCENARIO_SHIPS, 0),
	SCN_FIELD(start, DATA_U16, SCENARIO_STARTS, 2),
	SCN_FIELD(date, DATA_INT, 0, 0),
	SCN_FIELD(months, DATA_I8, 0, 0),
};
#define NFIELDS	(sizeof(fields) / sizeof(fields[0]))

/* What the rules rely on, from either source. */
static int
valid(const struct scenario *s)
{
	int a, b;

	if (s->nships < 0 || s->nships > SCENARIO_SHIPS || s->nation < 0 ||
	    s->nation > 3)
		return (0);
	for (a = 0; a < 4; a++)
		for (b = 0; b < 4; b++)
			if (s->relations[a][b] < -1 || s->relations[a][b] > 1)
				return (0);
	return (1);
}

/* Scenario "index" of the original executable. */
static int
from_exe(const struct exe_layout *layout, const uint8_t *exe, size_t len,
    int index, struct scenario *out)
{
	const uint8_t *r;
	int a, b;

	if ((r = exe_at(layout, exe, len, layout->scenario_va +
	    (uint32_t)index * SCENARIO_LEN, SCENARIO_LEN)) == NULL)
		return (-1);
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
	for (a = 0; a < 5; a++) {
		out->standing[a] = (int8_t)r[STANDING_OFF + a];
		out->rank[a] = r[RANK_OFF + a];
	}
	out->date = (int)le16(r + DATE_OFF);
	out->months = (int8_t)r[MONTHS_OFF];
	for (a = 0; a < SCENARIO_STARTS; a++) {
		out->start[a][0] = (uint16_t)le16(r + STARTS_OFF + 4 * a);
		out->start[a][1] = (uint16_t)le16(r + STARTS_OFF + 4 * a + 2);
	}
	for (a = 0; a < 4; a++)
		for (b = 0; b < 4; b++)
			out->relations[a][b] = (int8_t)r[RELATIONS_OFF + a * 4 + b];
	return (valid(out) ? 0 : -1);
}

int
scenario_load(struct packs *packs, const struct exe_layout *layout,
    const uint8_t *exe, size_t len, int index, struct scenario *out,
    char *err, size_t errlen)
{
	const struct json *list;
	struct json *j;
	int rc;

	memset(out, 0, sizeof(*out));
	if (index < 0 || index >= SCENARIO_COUNT) {
		snprintf(err, errlen, "no scenario %d", index);
		return (-1);
	}
	if ((rc = data_load_json(packs, "data/scenarios", &j, err,
	    errlen)) < 0)
		return (-1);
	if (rc == 1) {
		if (layout == NULL) {
			snprintf(err, errlen, "no data/scenarios: give the "
			    "original data or a pack that has it");
			return (-1);
		}
		if (from_exe(layout, exe, len, index, out) != 0) {
			snprintf(err, errlen, "no start scenario %d in the "
			    "executable", index);
			return (-1);
		}
		return (0);
	}
	list = json_get(j, "scenarios");
	if (list == NULL || list->type != JSON_ARRAY ||
	    list->count != SCENARIO_COUNT) {
		snprintf(err, errlen, "data/scenarios: \"scenarios\" must "
		    "list %d scenarios", SCENARIO_COUNT);
		rc = -1;
	} else if ((rc = data_decode(list->items[index], fields, NFIELDS, out,
	    "data/scenarios", err, errlen)) == 0 && !valid(out)) {
		snprintf(err, errlen, "data/scenarios: scenario %d out of "
		    "range (see asset-packs.md)", index);
		rc = -1;
	}
	json_free(j);
	return (rc);
}

int
scenarios_encode(struct packs *packs, const struct exe_layout *layout,
    const uint8_t *exe, size_t len, struct jbuf *b, char *err,
    size_t errlen)
{
	struct scenario s;
	int i;

	jb_raw(b, "{\"scenarios\": [\n  ");
	for (i = 0; i < SCENARIO_COUNT; i++) {
		if (scenario_load(packs, layout, exe, len, i, &s, err,
		    errlen) != 0)
			return (-1);
		if (i > 0)
			jb_raw(b, ",\n  ");
		data_encode(b, fields, NFIELDS, &s, 2);
	}
	jb_raw(b, "\n]}\n");
	return (0);
}
