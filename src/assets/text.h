/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_TEXT_H
#define FLIBUSTIER_TEXT_H

#include <stddef.h>
#include <stdint.h>

#include "data/pack.h"
#include "sim/json.h"

/*
 * Original string tables, converted from Mac Roman to UTF-8.  Markup and
 * control bytes below 0x80 (such as '~' menu lines, '`' paragraph
 * leads, '^' apostrophes, "%s" substitutions, '\r' line breaks) are kept
 * as is for the text renderer.
 */

struct strtab {
	size_t	  count;
	char	**str;		/* UTF-8, never NULL; empty entries are "" */
};

/* CSTN: groups of word lists (names, months, ship types...). */
struct strgroups {
	size_t		 count;
	struct strtab	*groups;
	unsigned	*declared;	/* count stored in the file; see below */
};

/* Both return 0 on success, -1 on malformed input. */
int	cstr_decode(const uint8_t *data, size_t len, struct strtab *out);
int	cstn_decode(const uint8_t *data, size_t len, struct strgroups *out);

/*
 * The open format of the same tables (asset-packs.md, "Open text"):
 * JSON {"strings": {"<index>": "..."}} and {"groups": {"<index>":
 * ["...", ...]}}.  Absent entries are empty, '\n' becomes '\r'.  Both
 * return 0, or -1 with a message in "err".
 */
int	cstr_json_decode(const uint8_t *data, size_t len, struct strtab *out,
	    char *err, size_t errlen);
int	cstn_json_decode(const uint8_t *data, size_t len,
	    struct strgroups *out, char *err, size_t errlen);
/* Write a table in the open format, '\r' as "\n". */
void	cstr_json_encode(const struct strtab *tab, struct jbuf *b);
void	cstn_json_encode(const struct strgroups *groups, struct jbuf *b);

/*
 * Decode a "text/<lang>/cstr" or ".../cstn" asset: the open format for a
 * ".json" file, else the original's.  0, or -1 with a message in "err".
 */
int	strtab_from_blob(const struct blob *b, struct strtab *out, char *err,
	    size_t errlen);
int	strgroups_from_blob(const struct blob *b, struct strgroups *out,
	    char *err, size_t errlen);

void	strtab_free(struct strtab *tab);
void	strgroups_free(struct strgroups *groups);

/* Returns a malloc'ed NUL-terminated UTF-8 copy, or NULL. */
char	*macroman_to_utf8(const uint8_t *s, size_t len);

/*
 * Decode one UTF-8 character at *s, advance *s past it, and return its
 * Mac Roman byte (the original fonts' encoding), or '?' when it has none.
 */
int	utf8_next_macroman(const char **s);

/* The Unicode code point of Mac Roman character "c" (0..255). */
int	macroman_unicode(int c);

#endif
