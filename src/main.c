/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#include "assets/textfiles.h"
#include "assets/worldfiles.h"
#include "data/pack.h"
#include "play.h"
#include "soundexport.h"
#include "view/view.h"
#include "viewer.h"

#define MAX_PACKS	8

static void
usage(void)
{
	fprintf(stderr,
	    "usage: flibustier [--data PATH] [--pack DIR]... [command]\n"
	    "\n"
	    "  --data PATH   GOG DATA.DAT, its install directory, or an\n"
	    "                extracted CD (default: $FLIBUSTIER_DATA)\n"
	    "  --pack DIR    asset pack with a pack.ini, over the data;\n"
	    "                later packs take priority\n"
	    "  --lang LANG   eng, fre or ger, or en, fr, de, or a language\n"
	    "                a pack adds (default: eng)\n"
	    "  --saves DIR   saved games (default: $FLIBUSTIER_SAVES, else\n"
	    "                ~/.local/share/flibustier/saves)\n"
	    "\n"
	    "commands:\n"
	    "  (none)        play [--crt] [--screenshot OUT.png [--frames N]]\n"
	    "                (--crt, or F2 while playing: a CRT look)\n"
	    "  --debug       the original's debug mode: while sailing N stops\n"
	    "                sightings, E brings them back, R makes you near\n"
	    "                invulnerable, F ends it; a right click on the\n"
	    "                cabin's map moves the ship there\n"
	    "  --script STEPS OUT.png\n"
	    "                play without a window, then save the screen\n"
	    "  --viewer [--id ID] [--screenshot OUT.png [--frames N]]\n"
	    "                browse assets\n"
	    "  --list        print every asset ID\n"
	    "  --export-sounds DIR\n"
	    "                write the original's sound effects as WAV, named\n"
	    "                as the pack IDs that replace them, to compare\n"
	    "                (original data: do not share)\n"
	    "  --export-world DIR\n"
	    "                write the world grids as an open pack of Tiled\n"
	    "                maps, and the map and battle tiles' terrain\n"
	    "                for tools (from the original data: do not\n"
	    "                share)\n"
	    "  --export-text DIR\n"
	    "                write the string tables in the open text format\n"
	    "                (from the original data: do not share)\n"
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

/*
 * The game's language code for "name", which may be the ISO one, or a
 * language whose text a pack adds; NULL when there is none.
 */
static const char *
game_lang(struct packs *packs, const char *name)
{
	static const char *codes[][3] = {
		{ "eng", "en", "english" },
		{ "fre", "fr", "fra" },
		{ "ger", "de", "deu" },
	};
	size_t i, k;

	for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
		for (k = 0; k < 3; k++)
			if (strcmp(name, codes[i][k]) == 0)
				return (codes[i][0]);
	return (text_has_lang(packs, name) ? name : NULL);
}

int
main(int argc, char **argv)
{
	const char *data, *pack[MAX_PACKS], *id = NULL, *out = NULL;
	const char *shot = NULL, *lang = "eng", *script = NULL, *saves = NULL;
	enum { PLAY, SCRIPT, VIEWER, LIST, EXPORT, EXPORT_WORLD,
	    EXPORT_SOUNDS, EXPORT_TEXT } cmd = PLAY;
	struct packs *packs;
	size_t npacks = 0, i, frame = 0;
	unsigned tick = 0;
	int shot_frames = 30, rc, a, crt = 0, debug = 0;
	char err[256];

	data = getenv("FLIBUSTIER_DATA");
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
		} else if (strcmp(argv[a], "--export-world") == 0 &&
		    a + 1 < argc) {
			cmd = EXPORT_WORLD;
			out = argv[++a];
		} else if (strcmp(argv[a], "--export-text") == 0 &&
		    a + 1 < argc) {
			cmd = EXPORT_TEXT;
			out = argv[++a];
		} else if (strcmp(argv[a], "--export-sounds") == 0 &&
		    a + 1 < argc) {
			cmd = EXPORT_SOUNDS;
			out = argv[++a];
		} else if (strcmp(argv[a], "--id") == 0 && a + 1 < argc)
			id = argv[++a];
		else if (strcmp(argv[a], "--frame") == 0 && a + 1 < argc)
			frame = strtoul(argv[++a], NULL, 10);
		else if (strcmp(argv[a], "--tick") == 0 && a + 1 < argc)
			tick = (unsigned)strtoul(argv[++a], NULL, 10);
		else if (strcmp(argv[a], "--crt") == 0)
			crt = 1;
		else if (strcmp(argv[a], "--debug") == 0)
			debug = 1;
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
		fprintf(stderr, "no game data: use --data or set FLIBUSTIER_DATA\n");
		packs_free(packs);
		return (1);
	}

	if ((lang = game_lang(packs, lang)) == NULL) {
		fprintf(stderr, "unknown language: use eng, fre or ger, or a "
		    "language a pack gives text for\n");
		packs_free(packs);
		return (1);
	}
	switch (cmd) {
	case PLAY:
		rc = play_run(packs, lang, saves, shot, shot_frames, crt,
		    debug);
		break;
	case SCRIPT:
		rc = play_headless(packs, lang, saves, script, out, debug);
		break;
	case LIST:
		rc = list_ids(packs);
		break;
	case EXPORT:
		rc = export_png(packs, id, out, frame, tick);
		break;
	case EXPORT_SOUNDS:
		rc = sounds_export(packs, out);
		break;
	case EXPORT_WORLD:
		if ((rc = world_export(packs, out, err, sizeof(err))) != 0)
			fprintf(stderr, "%s\n", err);
		break;
	case EXPORT_TEXT:
		if ((rc = text_export(packs, out, err, sizeof(err))) != 0)
			fprintf(stderr, "%s\n", err);
		break;
	default:
		rc = viewer_run(packs, id, shot, shot_frames);
		break;
	}
	packs_free(packs);
	return (rc);
}
