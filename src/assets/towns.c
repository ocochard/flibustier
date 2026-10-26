/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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
	t->standing = t->raw[9];
	t->garrison = t->raw[10];
	t->governor = t->raw[11];
	t->family = t->raw[12];
	t->barmaid = t->raw[13];
	t->id = t->raw[14];
	t->land_x = t->raw[15];
	t->land_y = t->raw[16];
	t->fort_x = t->raw[17];
	t->fort_y = t->raw[18];
	t->ship_x = t->raw[19];
	t->ship_y = t->raw[20];
	t->ship_heading = t->raw[21];
	t->party_x = t->raw[22];
	t->party_y = t->raw[23];
	t->sea_x = t->raw[26];
	t->sea_y = t->raw[27];
	t->sea_heading = t->raw[28];
	t->citymap_x = t->raw[29];
	t->citymap_y = t->raw[30];
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
	for (i = 0; i < CONVOY_STOPS; i++) {
		out->fleet_route[i] = (int8_t)b[i];
		out->train_route[i] = (int8_t)b[16 + i];
	}
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

static const char *const nation_names[] = {
	[NATION_SPAIN] = "spain", [NATION_ENGLAND] = "england",
	[NATION_FRANCE] = "france", [NATION_HOLLAND] = "holland",
};

/* Scalar members of a town record, in the order written. */
static const struct {
	const char	*key;
	size_t		 off;
} scalars[] = {
	{ "id", offsetof(struct town, id) },
	{ "x", offsetof(struct town, x) },
	{ "y", offsetof(struct town, y) },
	{ "flags", offsetof(struct town, flags) },
	{ "forts", offsetof(struct town, forts) },
	{ "soldiers", offsetof(struct town, soldiers) },
	{ "citizens", offsetof(struct town, citizens) },
	{ "gold", offsetof(struct town, gold) },
	{ "wealth", offsetof(struct town, wealth) },
	{ "standing", offsetof(struct town, standing) },
	{ "garrison", offsetof(struct town, garrison) },
	{ "governor", offsetof(struct town, governor) },
	{ "family", offsetof(struct town, family) },
	{ "barmaid", offsetof(struct town, barmaid) },
};

/* Point members: two or three numbers. */
static const struct {
	const char	*key;
	size_t		 n;
	size_t		 off[3];
} points[] = {
	{ "land", 2, { offsetof(struct town, land_x),
	    offsetof(struct town, land_y) } },
	{ "fort", 2, { offsetof(struct town, fort_x),
	    offsetof(struct town, fort_y) } },
	{ "ship", 3, { offsetof(struct town, ship_x),
	    offsetof(struct town, ship_y),
	    offsetof(struct town, ship_heading) } },
	{ "party", 2, { offsetof(struct town, party_x),
	    offsetof(struct town, party_y) } },
	{ "sea", 3, { offsetof(struct town, sea_x),
	    offsetof(struct town, sea_y),
	    offsetof(struct town, sea_heading) } },
	{ "citymap", 2, { offsetof(struct town, citymap_x),
	    offsetof(struct town, citymap_y) } },
};

#define FIELD(t, off)	(*(int *)((char *)(t) + (off)))

static int
number(const struct json *j, int *out)
{
	if (j == NULL || j->type != JSON_INT || j->num < 0 || j->num > 0xFFFF)
		return (-1);
	*out = (int)j->num;
	return (0);
}

static int
decode_town(const struct json *o, struct town *t, char *why, size_t len)
{
	const struct json *p;
	const char *s;
	size_t k, i;

	memset(t, 0, sizeof(*t));
	if (o->type != JSON_OBJECT)
		goto bad;
	if ((s = json_get_str(o, "name")) == NULL || *s == '\0' ||
	    strlen(s) >= sizeof(t->name)) {
		snprintf(why, len, "a town needs a name of 1 to %zu bytes",
		    sizeof(t->name) - 1);
		return (-1);
	}
	memcpy(t->name, s, strlen(s) + 1);
	if ((s = json_get_str(o, "nation")) == NULL)
		goto bad;
	for (t->nation = 0; t->nation <= NATION_HOLLAND; t->nation++)
		if (strcmp(s, nation_names[t->nation]) == 0)
			break;
	if (t->nation > NATION_HOLLAND)
		goto bad;
	for (k = 0; k < sizeof(scalars) / sizeof(scalars[0]); k++)
		if (number(json_get(o, scalars[k].key),
		    &FIELD(t, scalars[k].off)) != 0)
			goto bad;
	for (k = 0; k < sizeof(points) / sizeof(points[0]); k++) {
		p = json_get(o, points[k].key);
		if (p == NULL || p->type != JSON_ARRAY ||
		    p->count != points[k].n)
			goto bad;
		for (i = 0; i < points[k].n; i++)
			if (number(p->items[i], &FIELD(t, points[k].off[i])) !=
			    0)
				goto bad;
	}
	return (0);
bad:
	snprintf(why, len, "%s: a member is missing or out of range",
	    t->name[0] != '\0' ? t->name : "a town");
	return (-1);
}

/* A convoy's route: up to 15 town indices, -1 for a stop not used. */
static int
decode_route(const struct json *j, int *route, size_t count, char *err,
    size_t errlen)
{
	size_t i;

	if (j == NULL)
		return (0);
	if (j->type != JSON_ARRAY || j->count > CONVOY_STOPS) {
		snprintf(err, errlen, "towns: a route of at most %d stops",
		    CONVOY_STOPS);
		return (-1);
	}
	for (i = 0; i < j->count; i++) {
		if (j->items[i]->type != JSON_INT || j->items[i]->num < -1 ||
		    j->items[i]->num >= (int64_t)count) {
			snprintf(err, errlen, "towns: a route stop is a town "
			    "index or -1");
			return (-1);
		}
		route[i] = (int)j->items[i]->num;
	}
	return (0);
}

static void
encode_route(const int *route, struct jbuf *b)
{
	int i;

	for (i = 0; i < CONVOY_STOPS; i++) {
		jb_raw(b, i == 0 ? "[" : ", ");
		jb_int(b, route[i]);
	}
	jb_raw(b, "]");
}

int
towns_decode(const uint8_t *data, size_t len, int era, struct era_towns *out,
    char *err, size_t errlen)
{
	const struct json *list;
	struct json *root;
	char *text;
	size_t i;
	int rc = -1;

	memset(out, 0, sizeof(*out));
	if (era < 0 || era >= EXE_ERAS || (text = malloc(len + 1)) == NULL) {
		snprintf(err, errlen, "towns: bad era or out of memory");
		return (-1);
	}
	memcpy(text, data, len);
	text[len] = '\0';
	root = json_parse(text, err, errlen);
	free(text);
	if (root == NULL)
		return (-1);
	list = json_get(root, "towns");
	if (list == NULL || list->type != JSON_ARRAY || list->count == 0 ||
	    list->count > TOWN_MAX) {
		snprintf(err, errlen, "towns: a list of 1 to %d towns",
		    TOWN_MAX);
		goto out;
	}
	out->year = town_era_years[era];
	out->count = list->count;
	for (i = 0; i < list->count; i++)
		if (decode_town(list->items[i], &out->towns[i], err,
		    errlen) != 0)
			goto out;
	/* The convoys' routes, as town indices; none when absent. */
	for (i = 0; i < CONVOY_STOPS; i++)
		out->fleet_route[i] = out->train_route[i] = -1;
	if (decode_route(json_get(root, "treasure_fleet"), out->fleet_route,
	    out->count, err, errlen) != 0 ||
	    decode_route(json_get(root, "silver_train"), out->train_route,
	    out->count, err, errlen) != 0)
		goto out;
	rc = 0;
out:
	json_free(root);
	return (rc);
}

void
towns_encode(const struct era_towns *era, struct jbuf *b)
{
	const struct town *t;
	size_t i, k, n;

	jb_raw(b, "{\"towns\": [\n");
	for (i = 0; i < era->count; i++) {
		t = &era->towns[i];
		jb_raw(b, i > 0 ? ",\n  {\"name\": " : "  {\"name\": ");
		jb_str(b, t->name);
		jb_raw(b, ", \"nation\": ");
		jb_str(b, t->nation >= 0 && t->nation <= NATION_HOLLAND ?
		    nation_names[t->nation] : "");
		for (k = 0; k < sizeof(scalars) / sizeof(scalars[0]); k++) {
			jb_raw(b, k % 5 == 4 ? ",\n   \"" : ", \"");
			jb_raw(b, scalars[k].key);
			jb_raw(b, "\": ");
			jb_int(b, FIELD(t, scalars[k].off));
		}
		for (k = 0; k < sizeof(points) / sizeof(points[0]); k++) {
			jb_raw(b, k == 0 ? ",\n   \"" : ", \"");
			jb_raw(b, points[k].key);
			jb_raw(b, "\": [");
			for (n = 0; n < points[k].n; n++) {
				if (n > 0)
					jb_raw(b, ", ");
				jb_int(b, FIELD(t, points[k].off[n]));
			}
			jb_raw(b, "]");
		}
		jb_raw(b, "}");
	}
	jb_raw(b, "\n],\n\"treasure_fleet\": ");
	encode_route(era->fleet_route, b);
	jb_raw(b, ",\n\"silver_train\": ");
	encode_route(era->train_route, b);
	jb_raw(b, "}\n");
}

int
towns_for_era(struct packs *packs, const struct exe_layout *layout,
    const uint8_t *exe, size_t len, int era, struct era_towns *out,
    char *err, size_t errlen)
{
	struct blob b;
	char id[32], why[192];
	int rc;

	if (era < 0 || era >= EXE_ERAS) {
		snprintf(err, errlen, "bad era");
		return (-1);
	}
	snprintf(id, sizeof(id), "data/towns/%d", town_era_years[era]);
	if (packs != NULL && packs_load(packs, id, &b) == 0) {
		rc = towns_decode(b.data, b.len, era, out, why, sizeof(why));
		blob_free(&b);
		if (rc != 0)
			snprintf(err, errlen, "%s: %s", id, why);
		return (rc);
	}
	if (towns_load(layout, exe, len, era, out) != 0) {
		snprintf(err, errlen, "bad town table for %d",
		    town_era_years[era]);
		return (-1);
	}
	return (0);
}
