/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "datafields.h"

static void
range(enum data_type t, int64_t *lo, int64_t *hi)
{
	switch (t) {
	case DATA_U8:
		*lo = 0;
		*hi = UINT8_MAX;
		break;
	case DATA_I8:
		*lo = INT8_MIN;
		*hi = INT8_MAX;
		break;
	case DATA_U16:
		*lo = 0;
		*hi = UINT16_MAX;
		break;
	case DATA_I16:
		*lo = INT16_MIN;
		*hi = INT16_MAX;
		break;
	default:
		*lo = INT32_MIN;
		*hi = INT32_MAX;
		break;
	}
}

static size_t
type_size(enum data_type t)
{
	return (t == DATA_U8 || t == DATA_I8 ? 1 : t == DATA_I32 ? 4 : 2);
}

static void
put(enum data_type t, uint8_t *p, int64_t v)
{
	uint8_t u8 = (uint8_t)v;
	int8_t i8 = (int8_t)v;
	uint16_t u16 = (uint16_t)v;
	int16_t i16 = (int16_t)v;
	int32_t i32 = (int32_t)v;

	switch (t) {
	case DATA_U8:
		memcpy(p, &u8, 1);
		break;
	case DATA_I8:
		memcpy(p, &i8, 1);
		break;
	case DATA_U16:
		memcpy(p, &u16, 2);
		break;
	case DATA_I16:
		memcpy(p, &i16, 2);
		break;
	default:
		memcpy(p, &i32, 4);
		break;
	}
}

static int64_t
get(enum data_type t, const uint8_t *p)
{
	uint8_t u8;
	int8_t i8;
	uint16_t u16;
	int16_t i16;
	int32_t i32;

	switch (t) {
	case DATA_U8:
		memcpy(&u8, p, 1);
		return (u8);
	case DATA_I8:
		memcpy(&i8, p, 1);
		return (i8);
	case DATA_U16:
		memcpy(&u16, p, 2);
		return (u16);
	case DATA_I16:
		memcpy(&i16, p, 2);
		return (i16);
	default:
		memcpy(&i32, p, 4);
		return (i32);
	}
}

/* Elements below dimension "d" of "f". */
static size_t
span(const struct data_field *f, int d)
{
	size_t n = 1;

	for (; d < 3 && f->dims[d] > 0; d++)
		n *= (size_t)f->dims[d];
	return (n);
}

/* The value or array "j" at dimension "d" into "p". */
static int
decode_at(const struct json *j, const struct data_field *f, int d,
    uint8_t *p)
{
	size_t i, step;
	int64_t lo, hi;

	if (d == 3 || f->dims[d] == 0) {
		range(f->type, &lo, &hi);
		if (j->type != JSON_INT || j->num < lo || j->num > hi)
			return (-1);
		put(f->type, p, j->num);
		return (0);
	}
	if (j->type != JSON_ARRAY || j->count != (size_t)f->dims[d])
		return (-1);
	step = span(f, d + 1) * type_size(f->type);
	for (i = 0; i < j->count; i++)
		if (decode_at(j->items[i], f, d + 1, p + i * step) != 0)
			return (-1);
	return (0);
}

int
data_decode(const struct json *obj, const struct data_field *f, size_t n,
    void *base, const char *id, char *err, size_t errlen)
{
	const struct json *v;
	size_t i, k;

	if (obj == NULL || obj->type != JSON_OBJECT) {
		snprintf(err, errlen, "%s: not a JSON object", id);
		return (-1);
	}
	for (k = 0; k < obj->count; k++) {
		for (i = 0; i < n && strcmp(obj->keys[k], f[i].name) != 0; i++)
			continue;
		if (i == n) {
			snprintf(err, errlen, "%s: unknown field \"%s\"", id,
			    obj->keys[k]);
			return (-1);
		}
	}
	for (i = 0; i < n; i++) {
		if ((v = json_get(obj, f[i].name)) == NULL) {
			snprintf(err, errlen, "%s: no \"%s\"", id, f[i].name);
			return (-1);
		}
		if (decode_at(v, &f[i], 0, (uint8_t *)base + f[i].off) != 0) {
			snprintf(err, errlen, "%s: bad \"%s\" (see "
			    "asset-packs.md)", id, f[i].name);
			return (-1);
		}
	}
	return (0);
}

static void
encode_at(struct jbuf *b, const struct data_field *f, int d,
    const uint8_t *p, int indent)
{
	size_t i, step;
	int k, inner;

	if (d == 3 || f->dims[d] == 0) {
		jb_int(b, get(f->type, p));
		return;
	}
	step = span(f, d + 1) * type_size(f->type);
	inner = d + 1 == 3 || f->dims[d + 1] == 0;
	jb_raw(b, "[");
	for (i = 0; i < (size_t)f->dims[d]; i++) {
		if (inner) {
			if (i > 0)
				jb_raw(b, ", ");
		} else {
			jb_raw(b, i > 0 ? ",\n" : "\n");
			for (k = 0; k < indent + 2; k++)
				jb_raw(b, " ");
		}
		encode_at(b, f, d + 1, p + i * step, indent + 2);
	}
	if (!inner) {
		jb_raw(b, "\n");
		for (k = 0; k < indent; k++)
			jb_raw(b, " ");
	}
	jb_raw(b, "]");
}

void
data_encode(struct jbuf *b, const struct data_field *f, size_t n,
    const void *base)
{
	size_t i;

	jb_raw(b, "{");
	for (i = 0; i < n; i++) {
		jb_raw(b, i > 0 ? ",\n  " : "\n  ");
		jb_str(b, f[i].name);
		jb_raw(b, ": ");
		encode_at(b, &f[i], 0, (const uint8_t *)base + f[i].off, 2);
	}
	jb_raw(b, "\n}\n");
}
