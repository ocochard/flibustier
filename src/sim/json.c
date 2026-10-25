/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

#define MAX_DEPTH	32

struct parser {
	const char	*p;
	char		*err;
	size_t		 errlen;
	int		 failed;
};

static void
fail(struct parser *ps, const char *msg)
{
	if (!ps->failed)
		snprintf(ps->err, ps->errlen, "%s", msg);
	ps->failed = 1;
}

static void
skip_ws(struct parser *ps)
{
	while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' ||
	    *ps->p == '\r')
		ps->p++;
}

void
json_free(struct json *j)
{
	size_t i;

	if (j == NULL)
		return;
	for (i = 0; i < j->count; i++) {
		if (j->keys != NULL)
			free(j->keys[i]);
		json_free(j->items[i]);
	}
	free(j->keys);
	free(j->items);
	free(j->str);
	free(j);
}

static int
hexval(char c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	return (-1);
}

static char *
parse_string(struct parser *ps)
{
	struct jbuf b = { 0 };
	unsigned cp;
	int i, h;
	char c, u[4];

	if (*ps->p != '"') {
		fail(ps, "expected string");
		return (NULL);
	}
	ps->p++;
	while (*ps->p != '"') {
		c = *ps->p++;
		if (c == '\0' || (unsigned char)c < 0x20) {
			fail(ps, "unterminated string");
			jb_free(&b);
			return (NULL);
		}
		if (c != '\\') {
			u[0] = c;
			u[1] = '\0';
			jb_raw(&b, u);
			continue;
		}
		c = *ps->p++;
		switch (c) {
		case '"': case '\\': case '/':
			u[0] = c;
			u[1] = '\0';
			break;
		case 'b': strcpy(u, "\b"); break;
		case 'f': strcpy(u, "\f"); break;
		case 'n': strcpy(u, "\n"); break;
		case 'r': strcpy(u, "\r"); break;
		case 't': strcpy(u, "\t"); break;
		case 'u':
			for (cp = 0, i = 0; i < 4; i++) {
				if ((h = hexval(*ps->p++)) < 0) {
					fail(ps, "bad \\u escape");
					jb_free(&b);
					return (NULL);
				}
				cp = cp << 4 | (unsigned)h;
			}
			if (cp == 0 || (cp >= 0xD800 && cp < 0xE000)) {
				fail(ps, "unsupported \\u escape");
				jb_free(&b);
				return (NULL);
			}
			if (cp < 0x80) {
				u[0] = (char)cp;
				u[1] = '\0';
			} else if (cp < 0x800) {
				u[0] = (char)(0xC0 | cp >> 6);
				u[1] = (char)(0x80 | (cp & 0x3F));
				u[2] = '\0';
			} else {
				u[0] = (char)(0xE0 | cp >> 12);
				u[1] = (char)(0x80 | (cp >> 6 & 0x3F));
				u[2] = (char)(0x80 | (cp & 0x3F));
				u[3] = '\0';
			}
			break;
		default:
			fail(ps, "bad escape");
			jb_free(&b);
			return (NULL);
		}
		jb_raw(&b, u);
	}
	ps->p++;
	if (b.err) {
		fail(ps, "out of memory");
		jb_free(&b);
		return (NULL);
	}
	if (b.s == NULL)
		jb_raw(&b, "");
	return (b.s);
}

static struct json *parse_value(struct parser *ps, int depth);

static int
append(struct json *j, char *key, struct json *item)
{
	struct json **items;
	char **keys;

	items = realloc(j->items, (j->count + 1) * sizeof(*items));
	if (items == NULL)
		return (-1);
	j->items = items;
	if (j->type == JSON_OBJECT) {
		keys = realloc(j->keys, (j->count + 1) * sizeof(*keys));
		if (keys == NULL)
			return (-1);
		j->keys = keys;
		j->keys[j->count] = key;
	}
	j->items[j->count++] = item;
	return (0);
}

static struct json *
parse_container(struct parser *ps, int depth, enum json_type type)
{
	struct json *j, *item;
	char *key = NULL, close = type == JSON_ARRAY ? ']' : '}';

	if ((j = calloc(1, sizeof(*j))) == NULL) {
		fail(ps, "out of memory");
		return (NULL);
	}
	j->type = type;
	ps->p++;
	skip_ws(ps);
	if (*ps->p == close) {
		ps->p++;
		return (j);
	}
	for (;;) {
		skip_ws(ps);
		if (type == JSON_OBJECT) {
			if ((key = parse_string(ps)) == NULL)
				break;
			skip_ws(ps);
			if (*ps->p++ != ':') {
				fail(ps, "expected ':'");
				break;
			}
		}
		if ((item = parse_value(ps, depth + 1)) == NULL)
			break;
		if (append(j, key, item) != 0) {
			fail(ps, "out of memory");
			json_free(item);
			break;
		}
		key = NULL;
		skip_ws(ps);
		if (*ps->p == ',') {
			ps->p++;
			continue;
		}
		if (*ps->p == close) {
			ps->p++;
			return (j);
		}
		fail(ps, "expected ',' or closing bracket");
		break;
	}
	free(key);
	json_free(j);
	return (NULL);
}

static struct json *
parse_value(struct parser *ps, int depth)
{
	struct json *j;
	int64_t v;
	int neg;

	if (depth > MAX_DEPTH) {
		fail(ps, "nested too deeply");
		return (NULL);
	}
	skip_ws(ps);
	if (*ps->p == '{')
		return (parse_container(ps, depth, JSON_OBJECT));
	if (*ps->p == '[')
		return (parse_container(ps, depth, JSON_ARRAY));
	if ((j = calloc(1, sizeof(*j))) == NULL) {
		fail(ps, "out of memory");
		return (NULL);
	}
	if (*ps->p == '"') {
		j->type = JSON_STRING;
		if ((j->str = parse_string(ps)) == NULL) {
			free(j);
			return (NULL);
		}
	} else if (strncmp(ps->p, "true", 4) == 0) {
		j->type = JSON_BOOL;
		j->num = 1;
		ps->p += 4;
	} else if (strncmp(ps->p, "false", 5) == 0) {
		j->type = JSON_BOOL;
		ps->p += 5;
	} else if (strncmp(ps->p, "null", 4) == 0) {
		ps->p += 4;
	} else if (*ps->p == '-' || (*ps->p >= '0' && *ps->p <= '9')) {
		neg = *ps->p == '-';
		if (neg)
			ps->p++;
		if (*ps->p < '0' || *ps->p > '9') {
			fail(ps, "bad number");
			free(j);
			return (NULL);
		}
		for (v = 0; *ps->p >= '0' && *ps->p <= '9'; ps->p++) {
			if (v > (INT64_MAX - 9) / 10) {
				fail(ps, "number too large");
				free(j);
				return (NULL);
			}
			v = v * 10 + (*ps->p - '0');
		}
		if (*ps->p == '.' || *ps->p == 'e' || *ps->p == 'E') {
			fail(ps, "only integers are supported");
			free(j);
			return (NULL);
		}
		j->type = JSON_INT;
		j->num = neg ? -v : v;
	} else {
		fail(ps, "unexpected character");
		free(j);
		return (NULL);
	}
	return (j);
}

struct json *
json_parse(const char *text, char *err, size_t errlen)
{
	struct parser ps;
	struct json *j;

	ps.p = text;
	ps.err = err;
	ps.errlen = errlen;
	ps.failed = 0;
	if ((j = parse_value(&ps, 0)) == NULL)
		return (NULL);
	skip_ws(&ps);
	if (*ps.p != '\0') {
		fail(&ps, "trailing characters");
		json_free(j);
		return (NULL);
	}
	return (j);
}

const struct json *
json_get(const struct json *obj, const char *key)
{
	size_t i;

	if (obj == NULL || obj->type != JSON_OBJECT)
		return (NULL);
	for (i = 0; i < obj->count; i++)
		if (strcmp(obj->keys[i], key) == 0)
			return (obj->items[i]);
	return (NULL);
}

int64_t
json_get_int(const struct json *obj, const char *key, int64_t def)
{
	const struct json *j = json_get(obj, key);

	return (j != NULL && j->type == JSON_INT ? j->num : def);
}

const char *
json_get_str(const struct json *obj, const char *key)
{
	const struct json *j = json_get(obj, key);

	return (j != NULL && j->type == JSON_STRING ? j->str : NULL);
}

static void
jb_reserve(struct jbuf *b, size_t more)
{
	size_t cap;
	char *s;

	if (b->err || b->len + more + 1 <= b->cap)
		return;
	cap = b->cap > 0 ? b->cap : 256;
	while (cap < b->len + more + 1)
		cap *= 2;
	if ((s = realloc(b->s, cap)) == NULL) {
		b->err = 1;
		return;
	}
	b->s = s;
	b->cap = cap;
}

void
jb_raw(struct jbuf *b, const char *s)
{
	size_t n = strlen(s);

	jb_reserve(b, n);
	if (b->err)
		return;
	memcpy(b->s + b->len, s, n + 1);
	b->len += n;
}

void
jb_int(struct jbuf *b, int64_t v)
{
	char tmp[32];

	snprintf(tmp, sizeof(tmp), "%lld", (long long)v);
	jb_raw(b, tmp);
}

void
jb_str(struct jbuf *b, const char *s)
{
	char tmp[8];

	jb_raw(b, "\"");
	for (; *s != '\0'; s++) {
		if (*s == '"' || *s == '\\') {
			tmp[0] = '\\';
			tmp[1] = *s;
			tmp[2] = '\0';
		} else if ((unsigned char)*s < 0x20)
			snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned char)*s);
		else {
			tmp[0] = *s;
			tmp[1] = '\0';
		}
		jb_raw(b, tmp);
	}
	jb_raw(b, "\"");
}

void
jb_free(struct jbuf *b)
{
	free(b->s);
	memset(b, 0, sizeof(*b));
}

static int
numbers_only(const struct json *j)
{
	size_t i;

	for (i = 0; i < j->count; i++)
		if (j->items[i]->type != JSON_INT)
			return (0);
	return (1);
}

static void
indent(struct jbuf *b, int level)
{
	int i;

	jb_raw(b, "\n");
	for (i = 0; i < level; i++)
		jb_raw(b, "  ");
}

static void
pretty(const struct json *j, struct jbuf *b, int level)
{
	size_t i;
	int flat;

	switch (j->type) {
	case JSON_NULL:
		jb_raw(b, "null");
		break;
	case JSON_BOOL:
		jb_raw(b, j->num ? "true" : "false");
		break;
	case JSON_INT:
		jb_int(b, j->num);
		break;
	case JSON_STRING:
		jb_str(b, j->str);
		break;
	case JSON_ARRAY:
	case JSON_OBJECT:
		flat = j->type == JSON_ARRAY && numbers_only(j);
		jb_raw(b, j->type == JSON_ARRAY ? "[" : "{");
		for (i = 0; i < j->count; i++) {
			if (i > 0)
				jb_raw(b, flat ? ", " : ",");
			if (!flat)
				indent(b, level + 1);
			if (j->type == JSON_OBJECT) {
				jb_str(b, j->keys[i]);
				jb_raw(b, ": ");
			}
			pretty(j->items[i], b, level + 1);
		}
		if (!flat && j->count > 0)
			indent(b, level);
		jb_raw(b, j->type == JSON_ARRAY ? "]" : "}");
		break;
	}
}

void
json_pretty(const struct json *j, struct jbuf *b)
{
	pretty(j, b, 0);
}
