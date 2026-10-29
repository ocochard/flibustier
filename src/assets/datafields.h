/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_DATAFIELDS_H
#define FLIBUSTIER_DATAFIELDS_H

#include <stddef.h>

#include "data/pack.h"
#include "sim/json.h"

/*
 * The open game data's tables (asset-packs.md, "Open game data"): a JSON
 * object whose every key is one named field of a C structure, an integer
 * or nested arrays of fixed sizes.  One list of fields describes both
 * directions, so the loader and the export cannot disagree.
 */
enum data_type {
	DATA_U8,
	DATA_I8,
	DATA_U16,
	DATA_I16,
	DATA_I32,
	DATA_INT,
	DATA_LONG,	/* within the range of 32 bits */
	DATA_STR	/* char[dims[0]], NUL included */
};

struct data_field {
	const char	*name;
	size_t		 off;		/* offsetof() in the structure */
	enum data_type	 type;
	int		 dims[3];	/* array sizes, outer first; 0 ends */
};

/* Parse JSON "id" from the packs: 1 if none, -1 if malformed, 0. */
int	data_load_json(struct packs *packs, const char *id, struct json **out,
	    char *err, size_t errlen);

/*
 * Fill "base" from "obj", which must give every field, in range, with
 * nothing else.  On error, "err" names "id" and the field.
 */
int	data_decode(const struct json *obj, const struct data_field *f,
	    size_t n, void *base, const char *id, char *err, size_t errlen);

/* Write the fields of "base" as a JSON object, one field a line, its
 * lines indented by "indent", with no newline after it. */
void	data_encode(struct jbuf *b, const struct data_field *f, size_t n,
	    const void *base, int indent);

#endif
