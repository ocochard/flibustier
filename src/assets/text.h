/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_TEXT_H
#define STMALO_TEXT_H

#include <stddef.h>
#include <stdint.h>

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

void	strtab_free(struct strtab *tab);
void	strgroups_free(struct strgroups *groups);

/* Returns a malloc'ed NUL-terminated UTF-8 copy, or NULL. */
char	*macroman_to_utf8(const uint8_t *s, size_t len);

/*
 * Decode one UTF-8 character at *s, advance *s past it, and return its
 * Mac Roman byte (the original fonts' encoding), or '?' when it has none.
 */
int	utf8_next_macroman(const char **s);

#endif
