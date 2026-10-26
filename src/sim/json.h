/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_JSON_H
#define FLIBUSTIER_JSON_H

#include <stddef.h>
#include <stdint.h>

/*
 * Minimal JSON for the simulation protocol and state snapshots.  Numbers
 * are integers only (the simulation has no floating point); strings are
 * UTF-8 with the standard escapes, \u limited to the BMP.
 */

enum json_type {
	JSON_NULL,
	JSON_BOOL,
	JSON_INT,
	JSON_STRING,
	JSON_ARRAY,
	JSON_OBJECT,
	JSON_REAL	/* json_parse_reals() only: num is the integer part */
};

struct json {
	enum json_type	  type;
	int64_t		  num;		/* JSON_BOOL, JSON_INT */
	char		 *str;		/* JSON_STRING */
	size_t		  count;	/* JSON_ARRAY, JSON_OBJECT */
	char		**keys;		/* JSON_OBJECT */
	struct json	**items;
};

/* Returns NULL on error, with a message in "err". */
struct json	*json_parse(const char *text, char *err, size_t errlen);

/*
 * The same, for files of other programs (Tiled maps): numbers with a
 * fraction or an exponent are accepted, as JSON_REAL.
 */
struct json	*json_parse_reals(const char *text, char *err, size_t errlen);
void		 json_free(struct json *j);

/* Object member, or NULL. */
const struct json *json_get(const struct json *obj, const char *key);
/* Integer member, or "def" when absent or not an integer. */
int64_t		 json_get_int(const struct json *obj, const char *key,
		    int64_t def);
/* String member, or NULL. */
const char	*json_get_str(const struct json *obj, const char *key);

/* Growable output buffer. */
struct jbuf {
	char	*s;
	size_t	 len;
	size_t	 cap;
	int	 err;	/* set on allocation failure */
};

void	jb_raw(struct jbuf *b, const char *s);
void	jb_int(struct jbuf *b, int64_t v);
void	jb_str(struct jbuf *b, const char *s);	/* quoted and escaped */
void	jb_free(struct jbuf *b);

/*
 * Write "j" for people: one member or item per line, indented by two
 * spaces per level; arrays of numbers stay on one line.
 */
void	json_pretty(const struct json *j, struct jbuf *b);

#endif
