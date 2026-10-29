/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data/fsys.h"
#include "proto.h"
#include "save.h"

#define SAVE_FORMAT	"flibustier-save"
#define SAVE_VERSION	1
#define MAX_SAVE	(4 * 1024 * 1024)

int
save_write(const char *path, const struct sim *s, const char *desc,
    char *err, size_t errlen)
{
	struct jbuf compact = { 0 }, b = { 0 };
	struct json *j;
	char tmp[1024];
	FILE *fp;
	int ok;

	jb_raw(&compact, "{\"format\":\"" SAVE_FORMAT "\",\"version\":");
	jb_int(&compact, SAVE_VERSION);
	jb_raw(&compact, ",\"description\":");
	jb_str(&compact, desc);
	jb_raw(&compact, ",\"state\":");
	sim_to_json(s, &compact);
	jb_raw(&compact, "}");
	/* Saves are for people too: one value per line. */
	j = compact.err ? NULL : json_parse(compact.s, err, errlen);
	jb_free(&compact);
	if (j == NULL) {
		snprintf(err, errlen, "cannot build the save");
		return (-1);
	}
	json_pretty(j, &b);
	json_free(j);
	jb_raw(&b, "\n");
	if (b.err) {
		snprintf(err, errlen, "out of memory");
		jb_free(&b);
		return (-1);
	}
	/* Write a temporary file, then rename: a crash never leaves a half
	 * written save in the slot. */
	if ((size_t)snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= sizeof(tmp) ||
	    (fp = fopen(tmp, "wb")) == NULL) {
		snprintf(err, errlen, "%s: %s", path, strerror(errno));
		jb_free(&b);
		return (-1);
	}
	ok = fwrite(b.s, 1, b.len, fp) == b.len;
	ok = fclose(fp) == 0 && ok;
	jb_free(&b);
	if (!ok || fsys_replace(tmp, path) != 0) {
		snprintf(err, errlen, "%s: %s", path, strerror(errno));
		remove(tmp);
		return (-1);
	}
	return (0);
}

static char *
read_all(const char *path, char *err, size_t errlen)
{
	FILE *fp;
	char *s;
	long n;

	if ((fp = fopen(path, "rb")) == NULL) {
		snprintf(err, errlen, "%s: %s", path, strerror(errno));
		return (NULL);
	}
	if (fseek(fp, 0, SEEK_END) != 0 || (n = ftell(fp)) < 0 ||
	    n > MAX_SAVE || fseek(fp, 0, SEEK_SET) != 0 ||
	    (s = malloc((size_t)n + 1)) == NULL) {
		snprintf(err, errlen, "%s: cannot read", path);
		fclose(fp);
		return (NULL);
	}
	if (fread(s, 1, (size_t)n, fp) != (size_t)n) {
		snprintf(err, errlen, "%s: cannot read", path);
		free(s);
		fclose(fp);
		return (NULL);
	}
	fclose(fp);
	s[n] = '\0';
	return (s);
}

int
save_read(const char *path, struct sim *s, char *desc, size_t desclen,
    char *err, size_t errlen)
{
	struct json *j;
	const char *format, *d;
	char *text;
	int rc = -1;

	if ((text = read_all(path, err, errlen)) == NULL)
		return (-1);
	j = json_parse(text, err, errlen);
	free(text);
	if (j == NULL)
		return (-1);
	format = json_get_str(j, "format");
	d = json_get_str(j, "description");
	if (format == NULL || strcmp(format, SAVE_FORMAT) != 0 ||
	    json_get_int(j, "version", 0) != SAVE_VERSION || d == NULL)
		snprintf(err, errlen, "%s: not a saved game", path);
	else if (s == NULL || sim_from_json(s, json_get(j, "state"), err,
	    errlen) == 0) {
		if (desc != NULL)
			snprintf(desc, desclen, "%s", d);
		rc = 0;
	}
	json_free(j);
	return (rc);
}

void
save_slot_path(const char *dir, int slot, char *out, size_t len)
{
	snprintf(out, len, "%s/save%d.json", dir, slot);
}

/* mkdir -p */
static int
make_dirs(char *path)
{
	char *p;

	for (p = path + 1; *p != '\0'; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (fsys_mkdir(path) != 0 && errno != EEXIST) {
			*p = '/';
			return (-1);
		}
		*p = '/';
	}
	return (fsys_mkdir(path) != 0 && errno != EEXIST ? -1 : 0);
}

int
save_dir(char *out, size_t len, char *err, size_t errlen)
{
	const char *v;

	if ((v = getenv("FLIBUSTIER_SAVES")) != NULL && *v != '\0')
		snprintf(out, len, "%s", v);
#ifdef _WIN32
	else if ((v = getenv("APPDATA")) != NULL && *v != '\0')
		snprintf(out, len, "%s/flibustier/saves", v);
#endif
	else if ((v = getenv("XDG_DATA_HOME")) != NULL && *v != '\0')
		snprintf(out, len, "%s/flibustier/saves", v);
	else if ((v = getenv("HOME")) != NULL && *v != '\0')
		snprintf(out, len, "%s/.local/share/flibustier/saves", v);
	else {
		snprintf(err, errlen, "no save directory: set FLIBUSTIER_SAVES");
		return (-1);
	}
	if (make_dirs(out) != 0) {
		snprintf(err, errlen, "%s: %s", out, strerror(errno));
		return (-1);
	}
	return (0);
}
