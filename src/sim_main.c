/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * flibustier-sim: the simulation without graphics, driven by JSON lines on
 * stdin (one command per line, one response per line on stdout).  A file
 * of commands is a replay: the same file always gives the same output.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data/exe.h"
#include "data/pack.h"
#include "sim/proto.h"

#define MAX_PACKS	16	/* as many as the pack stack holds */

struct game_data {
	struct packs		*packs;
	uint8_t			*exe;
	size_t			 len;
	const struct exe_layout	*layout;
};

static int
load_era(void *arg, int era, int scenario, struct era_towns *towns,
    struct scenario *scn)
{
	struct game_data *gd = arg;
	char err[256];

	if (towns_for_era(gd->packs, gd->layout, gd->exe, gd->len, era, towns,
	    err, sizeof(err)) != 0) {
		fprintf(stderr, "%s\n", err);
		return (-1);
	}
	return (scenario_load(gd->layout, gd->exe, gd->len, scenario, scn));
}

static int
load_world_cb(void *arg, int era, struct world *w)
{
	struct game_data *gd = arg;
	char err[256];

	if (world_load(w, gd->packs, gd->layout, gd->exe, gd->len, era, err,
	    sizeof(err)) != 0) {
		fprintf(stderr, "%s\n", err);
		return (-1);
	}
	return (0);
}

/* Read a whole line of any length; returns NULL at end of input. */
static char *
read_line(FILE *fp)
{
	size_t len = 0, cap = 0;
	char *s = NULL, *t;
	int c;

	while ((c = fgetc(fp)) != EOF && c != '\n') {
		if (len + 2 > cap) {
			cap = cap > 0 ? cap * 2 : 1024;
			if ((t = realloc(s, cap)) == NULL) {
				free(s);
				return (NULL);
			}
			s = t;
		}
		s[len++] = (char)c;
	}
	if (c == EOF && len == 0) {
		free(s);
		return (NULL);
	}
	if (s == NULL && (s = malloc(1)) == NULL)
		return (NULL);
	s[len] = '\0';
	return (s);
}

int
main(int argc, char **argv)
{
	struct game_data gd;
	struct session ss;
	struct packs *packs;
	struct blob b;
	struct jbuf out;
	const char *data, *pack[MAX_PACKS];
	char *line;
	int a, quit = 0;
	size_t npacks = 0, i;

	data = getenv("FLIBUSTIER_DATA");
	for (a = 1; a < argc; a++) {
		if (strcmp(argv[a], "--data") == 0 && a + 1 < argc)
			data = argv[++a];
		else if (strcmp(argv[a], "--pack") == 0 && a + 1 < argc &&
		    npacks < MAX_PACKS)
			pack[npacks++] = argv[++a];
		else {
			fprintf(stderr, "usage: flibustier-sim [--data PATH] "
			    "[--pack DIR]... < commands.jsonl\n");
			return (2);
		}
	}
	if (data == NULL) {
		fprintf(stderr, "no game data: use --data or set FLIBUSTIER_DATA\n");
		return (1);
	}
	if ((packs = packs_new()) == NULL ||
	    packs_add_original(packs, data) != 0)
		goto bad;
	for (i = 0; i < npacks; i++)
		if (packs_add_dir(packs, pack[i]) != 0)
			goto bad;
	if (packs_load(packs, "program/main", &b) != 0) {
bad:
		fprintf(stderr, "%s\n", packs != NULL ? packs_error(packs) :
		    "out of memory");
		packs_free(packs);
		return (1);
	}
	gd.packs = packs;
	gd.exe = b.data;
	gd.len = b.len;
	if ((gd.layout = exe_identify(gd.exe, gd.len)) == NULL) {
		fprintf(stderr, "unknown PIRATESG.EXE build\n");
		blob_free(&b);
		packs_free(packs);
		return (1);
	}

	memset(&ss, 0, sizeof(ss));
	ss.load = load_era;
	ss.load_world = load_world_cb;
	ss.load_arg = &gd;
	while (!quit && (line = read_line(stdin)) != NULL) {
		if (line[0] == '\0' || line[0] == '#') {
			free(line);	/* blank lines and comments in replays */
			continue;
		}
		memset(&out, 0, sizeof(out));
		quit = proto_handle(&ss, line, &out);
		if (out.err)
			printf("{\"ok\":false,\"error\":\"out of memory\"}\n");
		else
			printf("%s\n", out.s);
		fflush(stdout);
		jb_free(&out);
		free(line);
	}
	session_free(&ss);
	blob_free(&b);
	packs_free(packs);
	return (0);
}
