/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#define _POSIX_C_SOURCE 200809L

#include <sys/stat.h>

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iso9660.h"
#include "pack.h"

#define MAX_PACKS	16
#define MAX_PATH	1024
#define MAX_FILE	(64L * 1024 * 1024)
#define SCREEN_H	480	/* the original art height */
#define MIN_ART_H	240
#define MAX_ART_H	4320
#define MANIFEST	"pack.ini"
#define ORIGINAL_EXE	"PIRATESG.EXE"
#define CD_DIR		"PIRATESG.CD"

enum pack_kind {
	PACK_ORIGINAL,
	PACK_DIR
};

struct manifest_entry {
	char	*id;
	char	*file;
};

struct pack {
	enum pack_kind		 kind;
	char			 name[64];
	char			 license[64];
	int			 art_height;		/* pack.ini */
	char			 root[MAX_PATH];	/* directory packs */
	struct iso		*iso;			/* original from image */
	struct manifest_entry	*entries;
	size_t			 nentries;
};

struct packs {
	struct pack	*pack[MAX_PACKS];	/* index 0 is the lowest */
	size_t		 count;
	char		 err[256];
};

/* Mapping of logical ID prefixes to original file locations. */
static const struct {
	const char	*prefix;
	const char	*dir;
	const char	*ext;
} original_rules[] = {
	{ "pic/",	"PCX/",	".PCX" },
	{ "patch/",	"PCX/",	".RSD" },
	{ "spr/",	"SPR/",	".SPR" },
	{ "cur/",	"SPR/",	".CUR" },
	{ "grp/",	"SPR/",	".GRP" },
	{ "img/",	"",	".IMG" },
	{ "img/",	"SPR/",	".IMG" },
	{ "tiles/",	"",	".IMB" },
	{ "tiles15/",	"",	".BMP" },
	{ "font/",	"",	".CHR" },
	{ "ui/",	"",	".CRS" },
	{ "driver/",	"",	".PIR" },
};

static void
seterr(struct packs *packs, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(packs->err, sizeof(packs->err), fmt, ap);
	va_end(ap);
}

static int
is_dir(const char *path)
{
	struct stat st;

	return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static int
read_file(const char *path, uint8_t **data, size_t *len)
{
	FILE *fp;
	long size;
	uint8_t *buf;

	if ((fp = fopen(path, "rb")) == NULL)
		return (-1);
	if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0 ||
	    size > MAX_FILE || fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return (-1);
	}
	if ((buf = malloc(size > 0 ? (size_t)size : 1)) == NULL) {
		fclose(fp);
		return (-1);
	}
	if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
		free(buf);
		fclose(fp);
		return (-1);
	}
	fclose(fp);
	*data = buf;
	*len = (size_t)size;
	return (0);
}

static int
strcaseeq(const char *a, const char *b)
{
	for (; *a != '\0' && *b != '\0'; a++, b++)
		if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
			return (0);
	return (*a == *b);
}

/*
 * Resolve "rel" ('/' separated) under "root", matching each component
 * case-insensitively, since DOS file names may have any case on disk.
 */
static int
resolve_ci(const char *root, const char *rel, char *out, size_t outlen)
{
	char part[256];
	const char *p, *sep;
	size_t n, used;
	DIR *d;
	struct dirent *de;
	int found;

	if ((size_t)snprintf(out, outlen, "%s", root) >= outlen)
		return (-1);
	for (p = rel; *p != '\0'; p = *sep == '\0' ? sep : sep + 1) {
		if ((sep = strchr(p, '/')) == NULL)
			sep = p + strlen(p);
		if ((n = (size_t)(sep - p)) == 0)
			continue;
		if (n >= sizeof(part))
			return (-1);
		memcpy(part, p, n);
		part[n] = '\0';
		if ((d = opendir(out)) == NULL)
			return (-1);
		found = 0;
		while ((de = readdir(d)) != NULL) {
			if (strcaseeq(de->d_name, part)) {
				used = strlen(out);
				if ((size_t)snprintf(out + used, outlen - used,
				    "/%s", de->d_name) >= outlen - used) {
					closedir(d);
					return (-1);
				}
				found = 1;
				break;
			}
		}
		closedir(d);
		if (!found)
			return (-1);
	}
	return (0);
}

/* IDs are lowercase words separated by '/'; this also forbids "..". */
static int
valid_id(const char *id)
{
	const char *p;

	if (*id == '\0' || *id == '/')
		return (0);
	for (p = id; *p != '\0'; p++) {
		if (*p == '/' && (p[1] == '/' || p[1] == '\0'))
			return (0);
		if (!(islower((unsigned char)*p) || isdigit((unsigned char)*p) ||
		    *p == '_' || *p == '-' || *p == '/'))
			return (0);
	}
	return (1);
}

static void
set_ext(struct blob *out, const char *path)
{
	const char *dot, *slash;
	size_t i;

	out->ext[0] = '\0';
	dot = strrchr(path, '.');
	slash = strrchr(path, '/');
	if (dot == NULL || (slash != NULL && dot < slash))
		return;
	for (i = 0; dot[i + 1] != '\0' && i < sizeof(out->ext) - 1; i++)
		out->ext[i] = (char)tolower((unsigned char)dot[i + 1]);
	out->ext[i] = '\0';
}

static int
original_read(struct pack *pk, const char *rel, uint8_t **data, size_t *len)
{
	char path[MAX_PATH];

	if (pk->iso != NULL) {
		if ((size_t)snprintf(path, sizeof(path), "%s/%s", CD_DIR,
		    rel) >= sizeof(path))
			return (-1);
		return (iso_read(pk->iso, path, data, len));
	}
	if (resolve_ci(pk->root, rel, path, sizeof(path)) != 0)
		return (-1);
	return (read_file(path, data, len));
}

/* Candidate original file names for an ID, tried in order. */
static int
original_load(struct pack *pk, const char *id, struct blob *out)
{
	char rel[MAX_PATH], upper[128];
	const char *name, *lang;
	size_t i, j;

	if (strcmp(id, "program/main") == 0) {
		snprintf(rel, sizeof(rel), "%s", ORIGINAL_EXE);
		goto try;
	}
	/* "text/<lang>/<name>" maps to NAME.LANG, for example CSTR.ENG. */
	if (strncmp(id, "text/", 5) == 0) {
		lang = id + 5;
		if ((name = strchr(lang, '/')) == NULL || name - lang != 3 ||
		    strchr(name + 1, '/') != NULL)
			return (-1);
		snprintf(rel, sizeof(rel), "%s.%.3s", name + 1, lang);
		for (j = 0; rel[j] != '\0'; j++)
			rel[j] = (char)toupper((unsigned char)rel[j]);
		goto try;
	}
	for (i = 0; i < sizeof(original_rules) / sizeof(original_rules[0]);
	    i++) {
		if (strncmp(id, original_rules[i].prefix,
		    strlen(original_rules[i].prefix)) != 0)
			continue;
		name = id + strlen(original_rules[i].prefix);
		if (strchr(name, '/') != NULL || strlen(name) >= sizeof(upper))
			continue;
		for (j = 0; name[j] != '\0'; j++)
			upper[j] = (char)toupper((unsigned char)name[j]);
		upper[j] = '\0';
		snprintf(rel, sizeof(rel), "%s%s%s", original_rules[i].dir,
		    upper, original_rules[i].ext);
		if (original_read(pk, rel, &out->data, &out->len) == 0) {
			set_ext(out, rel);
			return (0);
		}
	}
	return (-1);
try:
	if (original_read(pk, rel, &out->data, &out->len) != 0)
		return (-1);
	set_ext(out, rel);
	return (0);
}

static int
dir_load(struct pack *pk, const char *id, struct blob *out)
{
	char path[MAX_PATH];
	size_t i;

	for (i = 0; i < pk->nentries; i++) {
		if (strcmp(pk->entries[i].id, id) != 0)
			continue;
		if ((size_t)snprintf(path, sizeof(path), "%s/%s", pk->root,
		    pk->entries[i].file) >= sizeof(path))
			return (-1);
		if (read_file(path, &out->data, &out->len) != 0)
			return (-1);
		set_ext(out, pk->entries[i].file);
		return (0);
	}
	return (-1);
}

static void
pack_free(struct pack *pk)
{
	size_t i;

	if (pk == NULL)
		return;
	iso_close(pk->iso);
	for (i = 0; i < pk->nentries; i++) {
		free(pk->entries[i].id);
		free(pk->entries[i].file);
	}
	free(pk->entries);
	free(pk);
}

static int
push(struct packs *packs, struct pack *pk)
{
	if (packs->count == MAX_PACKS) {
		seterr(packs, "too many packs (max %d)", MAX_PACKS);
		pack_free(pk);
		return (-1);
	}
	packs->pack[packs->count++] = pk;
	return (0);
}

struct packs *
packs_new(void)
{
	return (calloc(1, sizeof(struct packs)));
}

void
packs_free(struct packs *packs)
{
	size_t i;

	if (packs == NULL)
		return;
	for (i = 0; i < packs->count; i++)
		pack_free(packs->pack[i]);
	free(packs);
}

/*
 * Accepts the DATA.DAT image itself, the GOG install directory holding
 * it, a directory with the extracted CD contents, or PIRATESG.CD itself.
 */
int
packs_add_original(struct packs *packs, const char *path)
{
	struct pack *pk;
	char found[MAX_PATH];
	uint8_t *data;
	size_t len;

	if ((pk = calloc(1, sizeof(*pk))) == NULL) {
		seterr(packs, "out of memory");
		return (-1);
	}
	pk->kind = PACK_ORIGINAL;
	pk->art_height = SCREEN_H;
	snprintf(pk->name, sizeof(pk->name), "Pirates! Gold (original)");
	snprintf(pk->license, sizeof(pk->license), "proprietary");

	if (!is_dir(path))
		pk->iso = iso_open(path);
	else if (resolve_ci(path, "DATA.DAT", found, sizeof(found)) == 0)
		pk->iso = iso_open(found);
	else if (resolve_ci(path, ORIGINAL_EXE, found, sizeof(found)) == 0)
		snprintf(pk->root, sizeof(pk->root), "%s", path);
	else if (resolve_ci(path, CD_DIR "/" ORIGINAL_EXE, found,
	    sizeof(found)) == 0)
		resolve_ci(path, CD_DIR, pk->root, sizeof(pk->root));

	if (pk->iso == NULL && pk->root[0] == '\0') {
		seterr(packs, "%s: no Pirates! Gold data found", path);
		pack_free(pk);
		return (-1);
	}
	/* Check the image really holds the game. */
	if (original_read(pk, ORIGINAL_EXE, &data, &len) != 0) {
		seterr(packs, "%s: %s not found", path, ORIGINAL_EXE);
		pack_free(pk);
		return (-1);
	}
	free(data);
	return (push(packs, pk));
}

static char *
trim(char *s)
{
	char *end;

	while (isspace((unsigned char)*s))
		s++;
	end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		*--end = '\0';
	return (s);
}

/* Manifest file names are relative and may not climb out of the pack. */
static int
valid_file(const char *file)
{
	const char *p;

	if (*file == '\0' || *file == '/')
		return (0);
	for (p = file; *p != '\0'; p++)
		if (p[0] == '.' && p[1] == '.')
			return (0);
	return (1);
}

static int
parse_manifest(struct packs *packs, struct pack *pk, char *text)
{
	char *line, *next, *eq, *key, *val, *end;
	long height;
	struct manifest_entry *e;
	enum { NONE, PACK, ASSETS } section = NONE;
	int lineno = 0;

	for (line = text; line != NULL; line = next) {
		lineno++;
		if ((next = strchr(line, '\n')) != NULL)
			*next++ = '\0';
		line = trim(line);
		if (*line == '\0' || *line == '#' || *line == ';')
			continue;
		if (strcmp(line, "[pack]") == 0) {
			section = PACK;
			continue;
		}
		if (strcmp(line, "[assets]") == 0) {
			section = ASSETS;
			continue;
		}
		if ((eq = strchr(line, '=')) == NULL || section == NONE)
			goto bad;
		*eq = '\0';
		key = trim(line);
		val = trim(eq + 1);
		if (section == PACK) {
			if (strcmp(key, "name") == 0)
				snprintf(pk->name, sizeof(pk->name), "%s", val);
			else if (strcmp(key, "license") == 0)
				snprintf(pk->license, sizeof(pk->license), "%s",
				    val);
			else if (strcmp(key, "art_height") == 0) {
				height = strtol(val, &end, 10);
				if (*val == '\0' || *end != '\0' ||
				    height < MIN_ART_H || height > MAX_ART_H)
					goto bad;
				pk->art_height = (int)height;
			}
			/* Unknown keys are ignored for forward compatibility. */
			continue;
		}
		if (!valid_id(key) || !valid_file(val))
			goto bad;
		e = realloc(pk->entries, (pk->nentries + 1) * sizeof(*e));
		if (e == NULL)
			goto nomem;
		pk->entries = e;
		e = &pk->entries[pk->nentries];
		e->id = malloc(strlen(key) + 1);
		e->file = malloc(strlen(val) + 1);
		if (e->id == NULL || e->file == NULL) {
			free(e->id);
			free(e->file);
			goto nomem;
		}
		strcpy(e->id, key);
		strcpy(e->file, val);
		pk->nentries++;
	}
	return (0);
bad:
	seterr(packs, "%s/%s:%d: syntax error", pk->root, MANIFEST, lineno);
	return (-1);
nomem:
	seterr(packs, "out of memory");
	return (-1);
}

int
packs_add_dir(struct packs *packs, const char *path)
{
	struct pack *pk;
	char manifest[MAX_PATH];
	uint8_t *data, *text;
	size_t len;

	if ((pk = calloc(1, sizeof(*pk))) == NULL) {
		seterr(packs, "out of memory");
		return (-1);
	}
	pk->kind = PACK_DIR;
	pk->art_height = SCREEN_H;
	snprintf(pk->root, sizeof(pk->root), "%s", path);
	snprintf(pk->name, sizeof(pk->name), "%s", path);
	snprintf(pk->license, sizeof(pk->license), "unknown");
	if ((size_t)snprintf(manifest, sizeof(manifest), "%s/%s", path,
	    MANIFEST) >= sizeof(manifest) ||
	    read_file(manifest, &data, &len) != 0) {
		seterr(packs, "%s: cannot read %s", path, MANIFEST);
		pack_free(pk);
		return (-1);
	}
	if ((text = realloc(data, len + 1)) == NULL) {
		free(data);
		seterr(packs, "out of memory");
		pack_free(pk);
		return (-1);
	}
	text[len] = '\0';
	if (parse_manifest(packs, pk, (char *)text) != 0) {
		free(text);
		pack_free(pk);
		return (-1);
	}
	free(text);
	return (push(packs, pk));
}

int
packs_load(struct packs *packs, const char *id, struct blob *out)
{
	return (packs_load_ext(packs, id, NULL, out));
}

int
packs_load_ext(struct packs *packs, const char *id, const char *ext,
    struct blob *out)
{
	struct pack *pk;
	size_t i;
	int rc;

	memset(out, 0, sizeof(*out));
	if (!valid_id(id)) {
		seterr(packs, "invalid asset id \"%s\"", id);
		return (-1);
	}
	for (i = packs->count; i-- > 0;) {
		pk = packs->pack[i];
		rc = pk->kind == PACK_ORIGINAL ? original_load(pk, id, out) :
		    dir_load(pk, id, out);
		if (rc == 0 && ext != NULL && strcmp(out->ext, ext) != 0) {
			blob_free(out);
			continue;
		}
		if (rc == 0) {
			out->pack = pk->name;
			out->art_height = pk->art_height;
			return (0);
		}
	}
	seterr(packs, "asset \"%s\" not found in any pack", id);
	return (-1);
}

struct idlist {
	char	**ids;
	size_t	  count;
	int	  err;
};

static void
idlist_add(struct idlist *l, const char *id)
{
	char **ids, *copy;

	if (l->err || !valid_id(id))
		return;
	ids = realloc(l->ids, (l->count + 1) * sizeof(*ids));
	copy = malloc(strlen(id) + 1);
	if (ids == NULL || copy == NULL) {
		if (ids != NULL)
			l->ids = ids;
		free(copy);
		l->err = 1;
		return;
	}
	strcpy(copy, id);
	l->ids = ids;
	l->ids[l->count++] = copy;
}

struct original_dir {
	struct idlist	*list;
	const char	*dir;	/* rule directory: "", "PCX/" or "SPR/" */
};

/* Map an original file name back to its ID, the reverse of the rules. */
static void
original_file(const char *name, int dir, void *arg)
{
	struct original_dir *od = arg;
	char base[128], ext[8], id[160];
	const char *dot;
	size_t i, n;

	if (dir || (dot = strrchr(name, '.')) == NULL ||
	    (n = (size_t)(dot - name)) == 0 || n >= sizeof(base) ||
	    strlen(dot + 1) >= sizeof(ext))
		return;
	for (i = 0; i < n; i++)
		base[i] = (char)tolower((unsigned char)name[i]);
	base[n] = '\0';
	for (i = 0; dot[1 + i] != '\0'; i++)
		ext[i] = (char)toupper((unsigned char)dot[1 + i]);
	ext[i] = '\0';
	if (od->dir[0] == '\0' && strcaseeq(name, ORIGINAL_EXE)) {
		idlist_add(od->list, "program/main");
		return;
	}
	if (od->dir[0] == '\0' && (strcmp(base, "cstr") == 0 ||
	    strcmp(base, "cstn") == 0) && strlen(ext) == 3) {
		snprintf(id, sizeof(id), "text/%c%c%c/%s",
		    tolower((unsigned char)ext[0]),
		    tolower((unsigned char)ext[1]),
		    tolower((unsigned char)ext[2]), base);
		idlist_add(od->list, id);
		return;
	}
	for (i = 0; i < sizeof(original_rules) / sizeof(original_rules[0]);
	    i++) {
		if (strcmp(original_rules[i].dir, od->dir) != 0 ||
		    strcmp(original_rules[i].ext + 1, ext) != 0)
			continue;
		snprintf(id, sizeof(id), "%s%s", original_rules[i].prefix, base);
		idlist_add(od->list, id);
		return;
	}
}

static void
original_list(struct pack *pk, struct idlist *list)
{
	static const char *dirs[] = { "", "PCX/", "SPR/" };
	struct original_dir od;
	char path[MAX_PATH], sub[16];
	DIR *d;
	struct dirent *de;
	size_t i;

	od.list = list;
	for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
		od.dir = dirs[i];
		/* Rule directories end with '/', listings take none. */
		snprintf(sub, sizeof(sub), "%.*s", (int)(strlen(dirs[i]) > 0 ?
		    strlen(dirs[i]) - 1 : 0), dirs[i]);
		if (pk->iso != NULL) {
			snprintf(path, sizeof(path), "%s/%s", CD_DIR, sub);
			iso_list(pk->iso, path, original_file, &od);
			continue;
		}
		if (resolve_ci(pk->root, sub, path, sizeof(path)) != 0 ||
		    (d = opendir(path)) == NULL)
			continue;
		while ((de = readdir(d)) != NULL)
			original_file(de->d_name, 0, &od);
		closedir(d);
	}
}

static int
idcmp(const void *a, const void *b)
{
	return (strcmp(*(char *const *)a, *(char *const *)b));
}

int
packs_list(struct packs *packs, char ***ids, size_t *count)
{
	struct idlist list;
	struct pack *pk;
	size_t i, j;

	memset(&list, 0, sizeof(list));
	for (i = 0; i < packs->count; i++) {
		pk = packs->pack[i];
		if (pk->kind == PACK_ORIGINAL)
			original_list(pk, &list);
		else
			for (j = 0; j < pk->nentries; j++)
				idlist_add(&list, pk->entries[j].id);
	}
	if (list.err) {
		packs_list_free(list.ids, list.count);
		seterr(packs, "out of memory");
		return (-1);
	}
	if (list.count > 0)
		qsort(list.ids, list.count, sizeof(*list.ids), idcmp);
	/* Drop duplicates: several packs may provide the same ID. */
	for (i = j = 0; i < list.count; i++) {
		if (j > 0 && strcmp(list.ids[j - 1], list.ids[i]) == 0)
			free(list.ids[i]);
		else
			list.ids[j++] = list.ids[i];
	}
	*ids = list.ids;
	*count = j;
	return (0);
}

void
packs_list_free(char **ids, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		free(ids[i]);
	free(ids);
}

void
blob_free(struct blob *blob)
{
	free(blob->data);
	memset(blob, 0, sizeof(*blob));
}

size_t
packs_count(const struct packs *packs)
{
	return (packs->count);
}

const char *
packs_name(const struct packs *packs, size_t i)
{
	return (i < packs->count ? packs->pack[packs->count - 1 - i]->name :
	    NULL);
}

const char *
packs_license(const struct packs *packs, size_t i)
{
	return (i < packs->count ?
	    packs->pack[packs->count - 1 - i]->license : NULL);
}

int
packs_art_height(const struct packs *packs)
{
	size_t i;
	int h = SCREEN_H;

	for (i = 0; i < packs->count; i++)
		if (packs->pack[i]->art_height > h)
			h = packs->pack[i]->art_height;
	return (h);
}

const char *
packs_error(const struct packs *packs)
{
	return (packs->err);
}
