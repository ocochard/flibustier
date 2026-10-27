/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Write the original's sound effects as WAV files, named as the pack IDs
 * that replace them ("broadside.wav" for "sound/broadside"), to compare
 * a replacement with what it replaces.  The files hold original data:
 * they are for listening, never for a pack.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/digital.h"
#include "assets/fmmusic.h"
#include "audio/music.h"
#include "data/pack.h"
#include "soundexport.h"
#include "sim/sim.h"
#include "ui/game.h"

#define FM_SECONDS	4	/* at most, rendered until the voices end */
#define FM_CHUNK	1024

static void
put16(FILE *fp, unsigned v)
{
	fputc((int)(v & 0xff), fp);
	fputc((int)(v >> 8 & 0xff), fp);
}

static void
put32(FILE *fp, unsigned long v)
{
	put16(fp, (unsigned)(v & 0xffff));
	put16(fp, (unsigned)(v >> 16 & 0xffff));
}

/* A PCM WAV file: "bits" 8 (unsigned) or 16, "channels" 1 or 2. */
static int
write_wav(const char *path, const void *data, size_t bytes, unsigned rate,
    unsigned bits, unsigned channels)
{
	FILE *fp;
	int rc;

	if ((fp = fopen(path, "wb")) == NULL)
		return (-1);
	fputs("RIFF", fp);
	put32(fp, 36 + (unsigned long)bytes);
	fputs("WAVEfmt ", fp);
	put32(fp, 16);
	put16(fp, 1);
	put16(fp, channels);
	put32(fp, rate);
	put32(fp, (unsigned long)rate * channels * bits / 8);
	put16(fp, channels * bits / 8);
	put16(fp, bits);
	fputs("data", fp);
	put32(fp, (unsigned long)bytes);
	fwrite(data, 1, bytes, fp);
	rc = ferror(fp) ? -1 : 0;
	if (fclose(fp) != 0)
		rc = -1;
	return (rc);
}

/* "dir/name.wav" for pack ID "sound/name". */
static void
wav_path(char *out, size_t len, const char *dir, const char *id)
{
	const char *slash = strchr(id, '/');

	snprintf(out, len, "%s/%s.wav", dir, slash != NULL ? slash + 1 : id);
}

/* FM effect "cmd" rendered on a fresh driver until it falls silent. */
static int
export_fm(const struct fmmusic *d, int cmd, const char *path)
{
	size_t cap = (size_t)MUSIC_RATE * FM_SECONDS, n = 0;
	int16_t *buf;
	struct music *m;
	int rc;

	if ((buf = malloc(cap * 2 * sizeof(*buf))) == NULL ||
	    (m = music_new(d)) == NULL) {
		free(buf);
		return (-1);
	}
	music_command(m, cmd);
	while (n + FM_CHUNK <= cap) {
		music_render(m, buf + 2 * n, FM_CHUNK);
		n += FM_CHUNK;
		if (!music_active(m))
			break;
	}
	music_free(m);
	rc = write_wav(path, buf, n * 2 * sizeof(*buf), MUSIC_RATE, 16, 2);
	free(buf);
	return (rc);
}

int
sounds_export(struct packs *packs, const char *dir)
{
	struct digital dg;
	struct fmmusic fm;
	struct blob b;
	char path[1024];
	int i, n = 0, rc = 0;

	if (packs_load(packs, "driver/digital", &b) == 0) {
		if (digital_decode(b.data, b.len, &dg) == 0) {
			for (i = 0; i < DIGITAL_EFFECTS; i++) {
				if (dg.pcm[i] == NULL)
					continue;
				wav_path(path, sizeof(path), dir, game_sfx_id(i));
				if (write_wav(path, dg.pcm[i], dg.len[i],
				    DIGITAL_RATE, 8, 1) != 0) {
					fprintf(stderr, "%s: cannot write\n", path);
					rc = 1;
				} else
					n++;
			}
			digital_free(&dg);
		}
		blob_free(&b);
	}
	if (packs_load(packs, "driver/psound", &b) == 0) {
		if (fmmusic_decode(b.data, b.len, &fm) == 0) {
			for (i = 0; i < 64; i++) {
				if (game_fm_id(i) == NULL)
					continue;
				wav_path(path, sizeof(path), dir, game_fm_id(i));
				if (export_fm(&fm, i, path) != 0) {
					fprintf(stderr, "%s: cannot write\n", path);
					rc = 1;
				} else
					n++;
			}
			fmmusic_free(&fm);
		}
		blob_free(&b);
	}
	printf("%d sounds written to %s (original data: for listening only)\n",
	    n, dir);
	return (n > 0 ? rc : 1);
}
