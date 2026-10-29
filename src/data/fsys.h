/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_FSYS_H
#define FLIBUSTIER_FSYS_H

/*
 * The few file system calls that POSIX and Windows spell differently.
 * Each returns what its POSIX namesake returns and sets errno.
 */

/* mkdir(path, 0755). */
int	fsys_mkdir(const char *path);

/* rename(), replacing an existing destination on Windows too. */
int	fsys_replace(const char *from, const char *to);

/* mkdtemp(): template ends in XXXXXX, replaced by the new directory's name. */
char	*fsys_mkdtemp(char *template);

#endif
