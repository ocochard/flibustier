/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iso9660.h"

#define SECTOR		2048
#define PVD_SECTOR	16
#define MAX_FILE	(64u * 1024 * 1024)

struct iso {
	FILE		*fp;
	uint32_t	 root_lba;
	uint32_t	 root_len;
};

struct entry {
	uint32_t	 lba;
	uint32_t	 len;
	int		 is_dir;
};

static uint32_t
le32(const uint8_t *p)
{
	return ((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	    (uint32_t)p[3] << 24);
}

static int
read_at(struct iso *iso, uint32_t lba, void *buf, size_t len)
{
	if (fseek(iso->fp, (long)lba * SECTOR, SEEK_SET) != 0)
		return (-1);
	return (fread(buf, 1, len, iso->fp) == len ? 0 : -1);
}

struct iso *
iso_open(const char *path)
{
	struct iso *iso;
	uint8_t pvd[SECTOR];

	if ((iso = calloc(1, sizeof(*iso))) == NULL)
		return (NULL);
	if ((iso->fp = fopen(path, "rb")) == NULL)
		goto fail;
	if (read_at(iso, PVD_SECTOR, pvd, sizeof(pvd)) != 0)
		goto fail;
	/* Primary volume descriptor: type 1, "CD001". */
	if (pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0)
		goto fail;
	/* Root directory record starts at offset 156. */
	iso->root_lba = le32(pvd + 156 + 2);
	iso->root_len = le32(pvd + 156 + 10);
	return (iso);
fail:
	iso_close(iso);
	return (NULL);
}

void
iso_close(struct iso *iso)
{
	if (iso == NULL)
		return;
	if (iso->fp != NULL)
		fclose(iso->fp);
	free(iso);
}

/* Compare an ISO name (without ";1" and trailing '.') to a path element. */
static int
name_match(const uint8_t *name, size_t nlen, const char *want, size_t wlen)
{
	size_t i;

	for (i = 0; i < nlen; i++)
		if (name[i] == ';')
			break;
	nlen = i;
	if (nlen > 0 && name[nlen - 1] == '.')
		nlen--;
	if (nlen != wlen)
		return (0);
	for (i = 0; i < nlen; i++)
		if (toupper(name[i]) != toupper((unsigned char)want[i]))
			return (0);
	return (1);
}

/*
 * Iterate over the records of a directory.  "fn" returns non-zero to stop;
 * iterate() then returns that value, 0 at the end, or -1 on error.
 */
static int
iterate(struct iso *iso, const struct entry *dir,
    int (*fn)(const uint8_t *rec, void *arg), void *arg)
{
	uint8_t sec[SECTOR];
	uint32_t s, off, nsec;
	uint8_t rlen, nlen;
	int rc;

	nsec = (dir->len + SECTOR - 1) / SECTOR;
	for (s = 0; s < nsec; s++) {
		if (read_at(iso, dir->lba + s, sec, SECTOR) != 0)
			return (-1);
		/* Records never cross sectors; a zero length ends the sector. */
		for (off = 0; off + 33 <= SECTOR; off += rlen) {
			rlen = sec[off];
			if (rlen == 0)
				break;
			nlen = sec[off + 32];
			if (rlen < 33 || off + rlen > SECTOR ||
			    33u + nlen > rlen)
				return (-1);
			if ((rc = fn(sec + off, arg)) != 0)
				return (rc);
		}
	}
	return (0);
}

static void
rec_entry(const uint8_t *rec, struct entry *out)
{
	out->lba = le32(rec + 2);
	out->len = le32(rec + 10);
	out->is_dir = (rec[25] & 0x02) != 0;
}

struct find_arg {
	const char	*want;
	size_t		 wlen;
	struct entry	*out;
};

static int
find_cb(const uint8_t *rec, void *arg)
{
	struct find_arg *fa = arg;

	if (!name_match(rec + 33, rec[32], fa->want, fa->wlen))
		return (0);
	rec_entry(rec, fa->out);
	return (1);
}

static int
lookup(struct iso *iso, const char *path, struct entry *cur)
{
	struct find_arg fa;
	const char *p, *sep;

	cur->lba = iso->root_lba;
	cur->len = iso->root_len;
	cur->is_dir = 1;
	for (p = path; *p != '\0'; p = *sep == '\0' ? sep : sep + 1) {
		if ((sep = strchr(p, '/')) == NULL)
			sep = p + strlen(p);
		if (sep == p)
			continue;
		fa.want = p;
		fa.wlen = (size_t)(sep - p);
		fa.out = cur;
		if (!cur->is_dir || iterate(iso, cur, find_cb, &fa) != 1)
			return (-1);
	}
	return (0);
}

struct list_arg {
	void	(*fn)(const char *, int, void *);
	void	 *arg;
};

static int
list_cb(const uint8_t *rec, void *arg)
{
	struct list_arg *la = arg;
	struct entry e;
	char name[256];
	size_t n, i;

	n = rec[32];
	/* Skip the "." and ".." records, encoded as bytes 0 and 1. */
	if (n == 1 && (rec[33] == 0 || rec[33] == 1))
		return (0);
	for (i = 0; i < n && rec[33 + i] != ';'; i++)
		name[i] = (char)rec[33 + i];
	if (i > 0 && name[i - 1] == '.')
		i--;
	name[i] = '\0';
	rec_entry(rec, &e);
	la->fn(name, e.is_dir, la->arg);
	return (0);
}

int
iso_list(struct iso *iso, const char *path,
    void (*fn)(const char *name, int is_dir, void *arg), void *arg)
{
	struct entry dir;
	struct list_arg la;

	if (lookup(iso, path, &dir) != 0 || !dir.is_dir)
		return (-1);
	la.fn = fn;
	la.arg = arg;
	return (iterate(iso, &dir, list_cb, &la) < 0 ? -1 : 0);
}

int
iso_read(struct iso *iso, const char *path, uint8_t **data, size_t *len)
{
	struct entry cur;
	uint8_t *buf;

	if (lookup(iso, path, &cur) != 0 || cur.is_dir || cur.len > MAX_FILE)
		return (-1);
	/* Allocate at least one byte so empty files are not NULL. */
	if ((buf = malloc(cur.len > 0 ? cur.len : 1)) == NULL)
		return (-1);
	if (cur.len > 0 && read_at(iso, cur.lba, buf, cur.len) != 0) {
		free(buf);
		return (-1);
	}
	*data = buf;
	*len = cur.len;
	return (0);
}
