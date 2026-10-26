/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_ISO9660_H
#define FLIBUSTIER_ISO9660_H

#include <stddef.h>
#include <stdint.h>

/*
 * Minimal read-only ISO 9660 reader, enough to load files straight from
 * the GOG DATA.DAT image.  Path lookup is case-insensitive and ignores the
 * ";1" version suffix.  Joliet and Rock Ridge extensions are ignored.
 */

struct iso;

struct iso	*iso_open(const char *path);
void		 iso_close(struct iso *iso);

/*
 * Read the whole file at "path" ('/' separated, relative to the image
 * root).  On success, *data is a malloc'ed buffer the caller frees.
 * Returns 0 on success, -1 if the file is missing or unreadable.
 */
int		 iso_read(struct iso *iso, const char *path, uint8_t **data,
		    size_t *len);

/*
 * Call "fn" for each entry of directory "path" ("" is the root), with
 * the name stripped of its ";1" suffix.  Returns 0, or -1 if the
 * directory is missing.
 */
int		 iso_list(struct iso *iso, const char *path,
		    void (*fn)(const char *name, int is_dir, void *arg),
		    void *arg);

#endif
