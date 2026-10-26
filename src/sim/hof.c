/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hof.h"

#define HOF_HEADER	"# flibustier hall of fame 1"

int
hof_rating(long score)
{
	long r;

	if (score < 1) {
		r = 20 - score / 10;
		if (r > 22)
			r = 22;
	} else {
		r = 19 - score / 5;
		if (r < 1)
			r = 1;
	}
	return ((int)r - 1);
}

void
hof_path(const char *dir, char *out, size_t len)
{
	snprintf(out, len, "%s/halloffame.txt", dir);
}

/* Copy a field, turning tabs and line breaks into spaces. */
static void
field(char *dst, size_t len, const char *src, size_t n)
{
	size_t i;

	for (i = 0; i < n && i + 1 < len; i++)
		dst[i] = src[i] == '\t' || src[i] == '\n' || src[i] == '\r' ?
		    ' ' : src[i];
	dst[i] = '\0';
}

int
hof_add(struct hof *h, const struct hof_entry *e)
{
	size_t i, pos;

	for (pos = 0; pos < h->count; pos++)
		if (e->score > h->entry[pos].score)
			break;
	if (pos == HOF_ENTRIES)
		return (-1);
	if (h->count < HOF_ENTRIES)
		h->count++;
	for (i = h->count - 1; i > pos; i--)
		h->entry[i] = h->entry[i - 1];
	h->entry[pos] = *e;
	field(h->entry[pos].name, sizeof(h->entry[pos].name), e->name,
	    strlen(e->name));
	field(h->entry[pos].title, sizeof(h->entry[pos].title), e->title,
	    strlen(e->title));
	return ((int)pos);
}

int
hof_load(const char *path, struct hof *h, char *err, size_t errlen)
{
	struct hof_entry e;
	char line[256], *p, *tab;
	FILE *fp;
	int lineno = 0;

	memset(h, 0, sizeof(*h));
	if ((fp = fopen(path, "r")) == NULL) {
		if (errno == ENOENT)
			return (0);
		snprintf(err, errlen, "%s: %s", path, strerror(errno));
		return (-1);
	}
	while (fgets(line, sizeof(line), fp) != NULL) {
		lineno++;
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '#' || line[0] == '\0')
			continue;
		memset(&e, 0, sizeof(e));
		e.score = strtol(line, &p, 10);
		if (*p != '\t')
			goto bad;
		e.rating = (int)strtol(p + 1, &p, 10);
		if (*p != '\t' || (tab = strchr(p + 1, '\t')) == NULL)
			goto bad;
		field(e.name, sizeof(e.name), p + 1, (size_t)(tab - p - 1));
		field(e.title, sizeof(e.title), tab + 1, strlen(tab + 1));
		hof_add(h, &e);	/* keeps the order even if edited by hand */
	}
	fclose(fp);
	return (0);
bad:
	snprintf(err, errlen, "%s:%d: bad entry", path, lineno);
	fclose(fp);
	memset(h, 0, sizeof(*h));
	return (-1);
}

int
hof_save(const char *path, const struct hof *h, char *err, size_t errlen)
{
	char tmp[1024];
	FILE *fp;
	size_t i;
	int ok;

	if ((size_t)snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= sizeof(tmp) ||
	    (fp = fopen(tmp, "w")) == NULL) {
		snprintf(err, errlen, "%s: %s", path, strerror(errno));
		return (-1);
	}
	ok = fprintf(fp, "%s\n", HOF_HEADER) > 0;
	for (i = 0; i < h->count; i++)
		ok = fprintf(fp, "%ld\t%d\t%s\t%s\n", h->entry[i].score,
		    h->entry[i].rating, h->entry[i].name,
		    h->entry[i].title) > 0 && ok;
	ok = fclose(fp) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		snprintf(err, errlen, "%s: %s", path, strerror(errno));
		remove(tmp);
		return (-1);
	}
	return (0);
}
