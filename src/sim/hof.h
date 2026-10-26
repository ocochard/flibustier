/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_HOF_H
#define FLIBUSTIER_HOF_H

#include <stddef.h>

/*
 * The Hall of Fame: the 10 best careers, best first, kept in a text file
 * with one tab-separated line per entry:
 *	score	rating	name	title
 * after a "# flibustier hall of fame 1" header line.
 */

#define HOF_ENTRIES	10

struct hof_entry {
	long	score;
	int	rating;		/* rank in the original's scale, 0 = best */
	char	name[41];
	char	title[24];
};

struct hof {
	size_t			count;
	struct hof_entry	entry[HOF_ENTRIES];
};

/* A missing file is an empty Hall of Fame.  Returns 0 or -1. */
int	hof_load(const char *path, struct hof *h, char *err, size_t errlen);
int	hof_save(const char *path, const struct hof *h, char *err,
	    size_t errlen);

/*
 * Insert "e" in score order (ties go after older entries).  Returns its
 * position, or -1 when it does not make the top 10.
 */
int	hof_add(struct hof *h, const struct hof_entry *e);

/*
 * Rating (0 = best, 21 = worst) of a career score, as the original
 * computes it at retirement: see game-rules.md.
 */
int	hof_rating(long score);

/* "<dir>/halloffame.txt" */
void	hof_path(const char *dir, char *out, size_t len);

#endif
