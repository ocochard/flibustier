/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#include "data/pack.h"
#include "play.h"
#include "view/view.h"
#include "viewer.h"

#define MAX_PACKS	8

static void
usage(void)
{
	fprintf(stderr,
	    "usage: stmalo [--data PATH] [--pack DIR]... [command]\n"
	    "\n"
	    "  --data PATH   GOG DATA.DAT, its install directory, or an\n"
	    "                extracted CD (default: $STMALO_DATA)\n"
	    "  --pack DIR    asset pack with a pack.ini, over the data;\n"
	    "                later packs take priority\n"
	    "  --lang LANG   eng, fre or ger (default: eng)\n"
	    "  --saves DIR   saved games (default: $STMALO_SAVES, else\n"
	    "                ~/.local/share/stmalo/saves)\n"
	    "\n"
	    "commands:\n"
	    "  (none)        play [--screenshot OUT.png [--frames N]]\n"
	    "  --script STEPS OUT.png\n"
	    "                play without a window, then save the screen\n"
	    "  --viewer [--id ID] [--screenshot OUT.png [--frames N]]\n"
	    "                browse assets\n"
	    "  --list        print every asset ID\n"
	    "  --export ID OUT.png [--frame N] [--tick N]\n"
	    "                render one asset frame to a PNG, no window\n");
	exit(2);
}

static int
export_png(struct packs *packs, const char *id, const char *out,
    size_t frame, unsigned tick)
{
	struct view *v;
	const struct rgb *img;
	char err[256];
	Image im;
	int ok;

	if ((v = view_open(packs, id, err, sizeof(err))) == NULL) {
		fprintf(stderr, "%s\n", err);
		return (1);
	}
	if (view_render(v, frame, tick, &img) != 0) {
		fprintf(stderr, "%s: cannot render frame %zu\n", id, frame);
		view_close(v);
		return (1);
	}
	im.data = img->px;
	im.width = img->w;
	im.height = img->h;
	im.mipmaps = 1;
	im.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
	SetTraceLogLevel(LOG_WARNING);
	ok = ExportImage(im, out);
	printf("%s: %s\n", id, view_info(v));
	view_close(v);
	return (ok ? 0 : 1);
}

static int
list_ids(struct packs *packs)
{
	char **ids;
	size_t n, i;

	for (i = 0; i < view_virtual_count(); i++)
		printf("%s\n", view_virtual_id(i));
	if (packs_list(packs, &ids, &n) != 0)
		return (1);
	for (i = 0; i < n; i++)
		printf("%s\n", ids[i]);
	packs_list_free(ids, n);
	return (0);
}

int
main(int argc, char **argv)
{
	const char *data, *pack[MAX_PACKS], *id = NULL, *out = NULL;
	const char *shot = NULL, *lang = "eng", *script = NULL, *saves = NULL;
	enum { PLAY, SCRIPT, VIEWER, LIST, EXPORT } cmd = PLAY;
	struct packs *packs;
	size_t npacks = 0, i, frame = 0;
	unsigned tick = 0;
	int shot_frames = 30, rc, a;

	data = getenv("STMALO_DATA");
	for (a = 1; a < argc; a++) {
		if (strcmp(argv[a], "--data") == 0 && a + 1 < argc)
			data = argv[++a];
		else if (strcmp(argv[a], "--pack") == 0 && a + 1 < argc &&
		    npacks < MAX_PACKS)
			pack[npacks++] = argv[++a];
		else if (strcmp(argv[a], "--viewer") == 0)
			cmd = VIEWER;
		else if (strcmp(argv[a], "--lang") == 0 && a + 1 < argc)
			lang = argv[++a];
		else if (strcmp(argv[a], "--saves") == 0 && a + 1 < argc)
			saves = argv[++a];
		else if (strcmp(argv[a], "--script") == 0 && a + 2 < argc) {
			cmd = SCRIPT;
			script = argv[++a];
			out = argv[++a];
		} else if (strcmp(argv[a], "--list") == 0)
			cmd = LIST;
		else if (strcmp(argv[a], "--export") == 0 && a + 2 < argc) {
			cmd = EXPORT;
			id = argv[++a];
			out = argv[++a];
		} else if (strcmp(argv[a], "--id") == 0 && a + 1 < argc)
			id = argv[++a];
		else if (strcmp(argv[a], "--frame") == 0 && a + 1 < argc)
			frame = strtoul(argv[++a], NULL, 10);
		else if (strcmp(argv[a], "--tick") == 0 && a + 1 < argc)
			tick = (unsigned)strtoul(argv[++a], NULL, 10);
		else if (strcmp(argv[a], "--screenshot") == 0 && a + 1 < argc)
			shot = argv[++a];
		else if (strcmp(argv[a], "--frames") == 0 && a + 1 < argc)
			shot_frames = atoi(argv[++a]);
		else
			usage();
	}

	if ((packs = packs_new()) == NULL)
		return (1);
	if (data != NULL && packs_add_original(packs, data) != 0) {
		fprintf(stderr, "%s\n", packs_error(packs));
		packs_free(packs);
		return (1);
	}
	for (i = 0; i < npacks; i++)
		if (packs_add_dir(packs, pack[i]) != 0) {
			fprintf(stderr, "%s\n", packs_error(packs));
			packs_free(packs);
			return (1);
		}
	if (packs_count(packs) == 0) {
		fprintf(stderr, "no game data: use --data or set STMALO_DATA\n");
		packs_free(packs);
		return (1);
	}

	if (strcmp(lang, "eng") != 0 && strcmp(lang, "fre") != 0 &&
	    strcmp(lang, "ger") != 0)
		usage();
	switch (cmd) {
	case PLAY:
		rc = play_run(packs, lang, saves, shot, shot_frames);
		break;
	case SCRIPT:
		rc = play_headless(packs, lang, saves, script, out);
		break;
	case LIST:
		rc = list_ids(packs);
		break;
	case EXPORT:
		rc = export_png(packs, id, out, frame, tick);
		break;
	default:
		rc = viewer_run(packs, id, shot, shot_frames);
		break;
	}
	packs_free(packs);
	return (rc);
}
