/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data/fsys.h"

#include "text.h"
#include "textfiles.h"

static int
write_file(const char *path, const struct jbuf *b, char *err, size_t errlen)
{
	FILE *fp;

	if (b->err || (fp = fopen(path, "wb")) == NULL) {
		snprintf(err, errlen, "cannot write %s", path);
		return (-1);
	}
	fwrite(b->s, 1, b->len, fp);
	if (fclose(fp) != 0) {
		snprintf(err, errlen, "cannot write %s", path);
		return (-1);
	}
	return (0);
}

static int
make_dir(const char *path, char *err, size_t errlen)
{
	if (fsys_mkdir(path) != 0 && errno != EEXIST) {
		snprintf(err, errlen, "cannot create %s", path);
		return (-1);
	}
	return (0);
}

int
text_has_lang(struct packs *packs, const char *lang)
{
	char id[32];
	size_t i;

	if (strlen(lang) != 3)
		return (0);
	for (i = 0; i < 3; i++)
		if (lang[i] < 'a' || lang[i] > 'z')
			return (0);
	snprintf(id, sizeof(id), "text/%s/cstr", lang);
	if (!packs_has(packs, id))
		return (0);
	snprintf(id, sizeof(id), "text/%s/cstn", lang);
	return (packs_has(packs, id));
}

/* Write one language's tables to "dir"/<lang>/. */
static int
export_lang(struct packs *packs, const char *dir, const char *lang,
    char *err, size_t errlen)
{
	struct strtab tab;
	struct strgroups groups;
	struct jbuf b = { 0 };
	struct blob blob;
	char path[1024], id[32], why[128];
	int rc;

	snprintf(path, sizeof(path), "%s/%s", dir, lang);
	if (make_dir(path, err, errlen) != 0)
		return (-1);
	snprintf(id, sizeof(id), "text/%s/cstr", lang);
	if (packs_load(packs, id, &blob) != 0) {
		snprintf(err, errlen, "missing %s", id);
		return (-1);
	}
	rc = strtab_from_blob(&blob, &tab, why, sizeof(why));
	blob_free(&blob);
	if (rc != 0) {
		snprintf(err, errlen, "%s: %s", id, why);
		return (-1);
	}
	cstr_json_encode(&tab, &b);
	strtab_free(&tab);
	snprintf(path, sizeof(path), "%s/%s/cstr.json", dir, lang);
	rc = write_file(path, &b, err, errlen);
	jb_free(&b);
	if (rc != 0)
		return (-1);
	snprintf(id, sizeof(id), "text/%s/cstn", lang);
	if (packs_load(packs, id, &blob) != 0) {
		snprintf(err, errlen, "missing %s", id);
		return (-1);
	}
	rc = strgroups_from_blob(&blob, &groups, why, sizeof(why));
	blob_free(&blob);
	if (rc != 0) {
		snprintf(err, errlen, "%s: %s", id, why);
		return (-1);
	}
	cstn_json_encode(&groups, &b);
	strgroups_free(&groups);
	snprintf(path, sizeof(path), "%s/%s/cstn.json", dir, lang);
	rc = write_file(path, &b, err, errlen);
	jb_free(&b);
	return (rc);
}

int
text_export(struct packs *packs, const char *dir, char *err, size_t errlen)
{
	struct jbuf ini = { 0 };
	char **ids, lang[4], line[96], path[1024];
	size_t n, i;
	int rc = -1, found = 0;

	if (make_dir(dir, err, errlen) != 0)
		return (-1);
	if (packs_list(packs, &ids, &n) != 0) {
		snprintf(err, errlen, "cannot list the assets");
		return (-1);
	}
	jb_raw(&ini, "# Exported from the original Pirates! Gold data: for "
	    "local\n# checks only, do not distribute.\n[pack]\n"
	    "name = Text exported from the original\n"
	    "license = proprietary\n\n[assets]\n");
	for (i = 0; i < n; i++) {
		if (strncmp(ids[i], "text/", 5) != 0 ||
		    strlen(ids[i]) != 13 || strcmp(ids[i] + 8, "/cstr") != 0)
			continue;
		memcpy(lang, ids[i] + 5, 3);
		lang[3] = '\0';
		if (!text_has_lang(packs, lang))
			continue;
		if (export_lang(packs, dir, lang, err, errlen) != 0)
			goto out;
		snprintf(line, sizeof(line), "text/%s/cstr = %s/cstr.json\n"
		    "text/%s/cstn = %s/cstn.json\n", lang, lang, lang, lang);
		jb_raw(&ini, line);
		found++;
	}
	if (found == 0) {
		snprintf(err, errlen, "no text to export");
		goto out;
	}
	snprintf(path, sizeof(path), "%s/pack.ini", dir);
	rc = write_file(path, &ini, err, errlen);
out:
	jb_free(&ini);
	packs_list_free(ids, n);
	return (rc);
}
