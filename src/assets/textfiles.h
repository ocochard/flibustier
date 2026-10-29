/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_TEXTFILES_H
#define FLIBUSTIER_TEXTFILES_H

#include <stddef.h>

#include "data/pack.h"

/*
 * Write every language's string tables the packs give into directory
 * "dir" (created if needed) as an open pack: <lang>/cstr.json,
 * <lang>/cstn.json and a pack.ini.  For checking the open format and
 * its indices: exported from the original data, the files must not be
 * distributed.  Returns 0, or -1 with a message in "err".
 */
int	text_export(struct packs *packs, const char *dir, char *err,
	    size_t errlen);

/*
 * Whether the packs give both tables of language "lang" (three lowercase
 * letters).
 */
int	text_has_lang(struct packs *packs, const char *lang);

#endif
