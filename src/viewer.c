/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Interactive asset viewer: a list of every asset ID on the left, the
 * selected asset on the right, rendered by the view module.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#include "assets/digital.h"
#include "assets/fmmusic.h"
#include "audio/music.h"
#include "ui/game.h"
#include "view/view.h"
#include "viewer.h"

#define WIN_W		1280
#define WIN_H		800
#define LIST_W		260
#define LINE_H		18
#define FONT_SIZE	16
#define STATUS_H	44
#define TICK_SECONDS	0.1	/* sea animation step; real rate unknown */

struct viewer {
	struct packs	 *packs;
	char		**ids;		/* pack IDs then virtual IDs */
	size_t		  nids;
	size_t		 *shown;	/* indices matching the filter */
	size_t		  nshown;
	char		  filter[64];
	size_t		  sel;		/* index into shown */
	size_t		  top;		/* first visible list row */

	struct view	 *view;
	char		  err[256];
	size_t		  frame;
	char		**pics;		/* pic/ IDs, for palette cycling */
	size_t		  npics;
	size_t		  pic;		/* 0 = default, else pics[pic - 1] */

	Texture2D	  tex;
	int		  tex_w, tex_h;

	/*
	 * Sounds: "audio/digital/N" (DIGITAL.PIR), "audio/fm/N" (the FM
	 * driver's tunes and effects, rendered by the music engine), and
	 * the pack recordings under "sound/" and "music/".
	 */
	struct digital	  dg;
	struct fmmusic	  fm;
	int		  have_dg, have_fm;
	int		  device;	/* an audio device is open */
	int		  is_audio;	/* the selection is a sound */
	Sound		  snd;		/* a digitised effect or recording */
	int		  snd_on;
	struct music	 *player;	/* an FM command being rendered */
	AudioStream	  fm_stream;
	Music		  rec;		/* a long recording, streamed */
	struct blob	  rec_data;
	int		  rec_on;
	double		  started;
	char		  audio_info[160];
};

#define AUDIO_CHUNK	2048	/* frames per FM stream buffer */

static int
is_audio_id(const char *id)
{
	return (strncmp(id, "audio/", 6) == 0 ||
	    strncmp(id, "sound/", 6) == 0 || strncmp(id, "music/", 6) == 0);
}

static void
load_drivers(struct viewer *vw)
{
	struct blob b;

	if (packs_load(vw->packs, "driver/digital", &b) == 0) {
		vw->have_dg = digital_decode(b.data, b.len, &vw->dg) == 0;
		blob_free(&b);
	}
	if (packs_load(vw->packs, "driver/psound", &b) == 0) {
		vw->have_fm = fmmusic_decode(b.data, b.len, &vw->fm) == 0;
		blob_free(&b);
	}
}

/* The names the game gives them: "audio/fm/09-title". */
static void
add_audio_id(struct viewer *vw, const char *kind, int n, const char *name)
{
	char id[96];

	if (name != NULL && strchr(name, '/') != NULL)
		name = strchr(name, '/') + 1;
	if (name != NULL)
		snprintf(id, sizeof(id), "audio/%s/%02d-%s", kind, n, name);
	else
		snprintf(id, sizeof(id), "audio/%s/%02d", kind, n);
	if ((vw->ids[vw->nids] = strdup(id)) != NULL)
		vw->nids++;
}

static void
add_audio_ids(struct viewer *vw)
{
	int i;

	if (vw->have_dg)
		for (i = 0; i < DIGITAL_EFFECTS; i++)
			if (vw->dg.pcm[i] != NULL)
				add_audio_id(vw, "digital", i, game_sfx_id(i));
	if (vw->have_fm)
		for (i = 1; i < FMMUSIC_COMMANDS; i++)
			if (vw->fm.nstreams[i] > 0)
				add_audio_id(vw, "fm", i, game_tune_id(i) != NULL ?
				    game_tune_id(i) : game_fm_id(i));
}

static int
collect_ids(struct viewer *vw)
{
	char **ids;
	size_t n, i, k;

	if (packs_list(vw->packs, &ids, &n) != 0)
		return (-1);
	k = n + view_virtual_count() + DIGITAL_EFFECTS + FMMUSIC_COMMANDS;
	vw->ids = malloc(k * sizeof(*vw->ids));
	vw->shown = malloc(k * sizeof(*vw->shown));
	vw->pics = malloc((n + 1) * sizeof(*vw->pics));
	if (vw->ids == NULL || vw->shown == NULL || vw->pics == NULL) {
		packs_list_free(ids, n);
		return (-1);
	}
	for (k = 0; k < view_virtual_count(); k++)
		vw->ids[vw->nids++] = (char *)view_virtual_id(k);
	for (i = 0; i < n; i++) {
		vw->ids[vw->nids++] = ids[i];
		if (strncmp(ids[i], "pic/", 4) == 0)
			vw->pics[vw->npics++] = ids[i];
	}
	free(ids);	/* the strings now belong to vw->ids */
	add_audio_ids(vw);
	return (0);
}

static void
apply_filter(struct viewer *vw)
{
	size_t i;

	vw->nshown = 0;
	for (i = 0; i < vw->nids; i++)
		if (vw->filter[0] == '\0' || strstr(vw->ids[i], vw->filter))
			vw->shown[vw->nshown++] = i;
	if (vw->sel >= vw->nshown)
		vw->sel = vw->nshown > 0 ? vw->nshown - 1 : 0;
}

static const char *
selected_id(const struct viewer *vw)
{
	return (vw->nshown > 0 ? vw->ids[vw->shown[vw->sel]] : NULL);
}

/* Upload the rendered frame, recreating the texture when its size changes. */
static void
upload(struct viewer *vw, const struct rgb *img)
{
	Image im;

	if (vw->tex.id != 0 && vw->tex_w == img->w && vw->tex_h == img->h) {
		UpdateTexture(vw->tex, img->px);
		return;
	}
	if (vw->tex.id != 0)
		UnloadTexture(vw->tex);
	im.data = img->px;
	im.width = img->w;
	im.height = img->h;
	im.mipmaps = 1;
	im.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
	vw->tex = LoadTextureFromImage(im);
	SetTextureFilter(vw->tex, TEXTURE_FILTER_POINT);
	vw->tex_w = img->w;
	vw->tex_h = img->h;
}

static void
audio_stop(struct viewer *vw)
{
	if (vw->snd_on)
		UnloadSound(vw->snd);
	vw->snd_on = 0;
	if (vw->rec_on) {
		UnloadMusicStream(vw->rec);
		blob_free(&vw->rec_data);
	}
	vw->rec_on = 0;
	if (vw->player != NULL) {
		UnloadAudioStream(vw->fm_stream);
		music_free(vw->player);
		vw->player = NULL;
	}
}

static void
audio_start(struct viewer *vw, const char *id)
{
	char type[16];
	Wave w;
	int n;

	audio_stop(vw);
	vw->audio_info[0] = '\0';
	if (!vw->device) {
		snprintf(vw->audio_info, sizeof(vw->audio_info),
		    "no audio device");
		return;
	}
	vw->started = GetTime();
	if (sscanf(id, "audio/digital/%d", &n) == 1) {
		w = (Wave){ (unsigned)vw->dg.len[n], DIGITAL_RATE, 8, 1,
		    vw->dg.pcm[n] };
		vw->snd = LoadSoundFromWave(w);
		vw->snd_on = vw->snd.frameCount > 0;
		if (vw->snd_on)
			PlaySound(vw->snd);
		snprintf(vw->audio_info, sizeof(vw->audio_info),
		    "digitised effect, 8-bit mono %d Hz, %.2f s",
		    DIGITAL_RATE, (double)vw->dg.len[n] / DIGITAL_RATE);
	} else if (sscanf(id, "audio/fm/%d", &n) == 1) {
		if ((vw->player = music_new(&vw->fm)) == NULL)
			return;
		music_command(vw->player, n);
		SetAudioStreamBufferSizeDefault(AUDIO_CHUNK);
		vw->fm_stream = LoadAudioStream(MUSIC_RATE, 16, 2);
		PlayAudioStream(vw->fm_stream);
		snprintf(vw->audio_info, sizeof(vw->audio_info),
		    "FM driver command %d, OPL3 at %u Hz, %d stream(s)", n,
		    MUSIC_RATE, vw->fm.nstreams[n]);
	} else if (packs_load(vw->packs, id, &vw->rec_data) == 0) {
		snprintf(type, sizeof(type), ".%s", vw->rec_data.ext);
		vw->rec = LoadMusicStreamFromMemory(type, vw->rec_data.data,
		    (int)vw->rec_data.len);
		if (vw->rec.frameCount == 0) {
			blob_free(&vw->rec_data);
			snprintf(vw->audio_info, sizeof(vw->audio_info),
			    "cannot play this %s file", type + 1);
			return;
		}
		vw->rec_on = 1;
		vw->rec.looping = false;
		PlayMusicStream(vw->rec);
		snprintf(vw->audio_info, sizeof(vw->audio_info),
		    "%s recording, %.1f s", type + 1,
		    GetMusicTimeLength(vw->rec));
	}
}

/* Keep the streams fed; true while something plays. */
static int
audio_update(struct viewer *vw)
{
	static int16_t buf[2 * AUDIO_CHUNK];

	if (vw->snd_on)
		return (IsSoundPlaying(vw->snd));
	if (vw->rec_on) {
		UpdateMusicStream(vw->rec);
		return (IsMusicStreamPlaying(vw->rec));
	}
	if (vw->player == NULL)
		return (0);
	while (IsAudioStreamProcessed(vw->fm_stream)) {
		music_render(vw->player, buf, AUDIO_CHUNK);
		UpdateAudioStream(vw->fm_stream, buf, AUDIO_CHUNK);
	}
	return (music_active(vw->player));
}

static void
open_selected(struct viewer *vw)
{
	const char *id;

	view_close(vw->view);
	vw->view = NULL;
	vw->frame = 0;
	vw->pic = 0;
	vw->err[0] = '\0';
	audio_stop(vw);
	vw->is_audio = (id = selected_id(vw)) != NULL && is_audio_id(id);
	if (vw->is_audio) {
		audio_start(vw, id);
		return;
	}
	if (id != NULL &&
	    (vw->view = view_open(vw->packs, id, vw->err,
	    sizeof(vw->err))) != NULL)
		vw->frame = view_start_frame(vw->view);
}

static void
handle_keys(struct viewer *vw)
{
	size_t frames, page, len;
	int c, reopen = 0;

	frames = vw->view != NULL ? view_frames(vw->view) : 0;
	/* World views page by a row of positions (vertical scroll). */
	page = vw->view != NULL && view_row_frames(vw->view) > 0 ?
	    view_row_frames(vw->view) : 10;

	if ((IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) &&
	    vw->sel + 1 < vw->nshown) {
		vw->sel++;
		reopen = 1;
	}
	if ((IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) &&
	    vw->sel > 0) {
		vw->sel--;
		reopen = 1;
	}
	if ((IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) &&
	    vw->frame + 1 < frames)
		vw->frame++;
	if ((IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) &&
	    vw->frame > 0)
		vw->frame--;
	if ((IsKeyPressed(KEY_PAGE_DOWN) || IsKeyPressedRepeat(KEY_PAGE_DOWN)) &&
	    vw->frame + page < frames)
		vw->frame += page;
	if ((IsKeyPressed(KEY_PAGE_UP) || IsKeyPressedRepeat(KEY_PAGE_UP)) &&
	    vw->frame >= page)
		vw->frame -= page;
	if (IsKeyPressed(KEY_HOME))
		vw->frame = 0;
	if (IsKeyPressed(KEY_END) && frames > 0)
		vw->frame = frames - 1;
	if (IsKeyPressed(KEY_ENTER) && vw->is_audio)
		reopen = 1;	/* play it again */
	if (IsKeyPressed(KEY_TAB) && vw->view != NULL && vw->npics > 0) {
		vw->pic = (vw->pic + 1) % (vw->npics + 1);
		view_set_palette(vw->view,
		    vw->pic == 0 ? NULL : vw->pics[vw->pic - 1]);
	}

	/* Typing filters the list. */
	len = strlen(vw->filter);
	while ((c = GetCharPressed()) != 0)
		if (c >= 0x20 && c < 0x7F && len + 1 < sizeof(vw->filter)) {
			vw->filter[len++] = (char)c;
			vw->filter[len] = '\0';
			apply_filter(vw);
			reopen = 1;
		}
	if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) &&
	    len > 0) {
		vw->filter[len - 1] = '\0';
		apply_filter(vw);
		reopen = 1;
	}
	if (reopen)
		open_selected(vw);
}

static void
draw_list(struct viewer *vw)
{
	size_t rows, i, n;
	int y;

	DrawRectangle(0, 0, LIST_W, WIN_H, (Color){ 25, 25, 35, 255 });
	DrawText(TextFormat("filter: %s_", vw->filter), 8, 6, FONT_SIZE,
	    YELLOW);
	rows = (size_t)(WIN_H - 30) / LINE_H;
	if (vw->sel < vw->top)
		vw->top = vw->sel;
	if (vw->sel >= vw->top + rows)
		vw->top = vw->sel - rows + 1;
	for (i = 0; i < rows && vw->top + i < vw->nshown; i++) {
		n = vw->top + i;
		y = 30 + (int)i * LINE_H;
		if (n == vw->sel)
			DrawRectangle(0, y - 1, LIST_W, LINE_H,
			    (Color){ 70, 70, 120, 255 });
		DrawText(vw->ids[vw->shown[n]], 8, y, FONT_SIZE, RAYWHITE);
	}
}

static void
draw_asset(struct viewer *vw, unsigned tick)
{
	const struct rgb *img;
	float sx, sy, scale;
	int aw = WIN_W - LIST_W, ah = WIN_H - STATUS_H;
	Rectangle src, dst;

	if (vw->is_audio) {
		DrawText(selected_id(vw), LIST_W + 10, 10, FONT_SIZE * 2,
		    RAYWHITE);
		DrawText(vw->audio_info, LIST_W + 10, 60, FONT_SIZE, GRAY);
		DrawText(audio_update(vw) ? TextFormat("playing  %.1f s",
		    GetTime() - vw->started) : "stopped", LIST_W + 10, 90,
		    FONT_SIZE, YELLOW);
		return;
	}
	if (vw->view == NULL || view_render(vw->view, vw->frame, tick,
	    &img) != 0) {
		DrawText(vw->err[0] != '\0' ? vw->err : "nothing to show",
		    LIST_W + 10, 10, FONT_SIZE, RED);
		return;
	}
	upload(vw, img);
	/* Integer scale when the asset fits, otherwise shrink to fit. */
	sx = (float)aw / img->w;
	sy = (float)ah / img->h;
	scale = sx < sy ? sx : sy;
	if (scale >= 1.0f)
		scale = (float)(int)scale;
	src = (Rectangle){ 0, 0, (float)img->w, (float)img->h };
	dst = (Rectangle){ LIST_W + (aw - img->w * scale) / 2,
	    (ah - img->h * scale) / 2, img->w * scale, img->h * scale };
	DrawTexturePro(vw->tex, src, dst, (Vector2){ 0, 0 }, 0, WHITE);
}

static void
draw_status(struct viewer *vw)
{
	int y = WIN_H - STATUS_H;
	size_t frames = vw->view != NULL ? view_frames(vw->view) : 0;

	DrawRectangle(LIST_W, y, WIN_W - LIST_W, STATUS_H,
	    (Color){ 15, 15, 20, 255 });
	if (vw->view != NULL)
		DrawText(TextFormat("%s  frame %zu/%zu  %s",
		    selected_id(vw), frames > 0 ? vw->frame + 1 : 0, frames,
		    view_info(vw->view)), LIST_W + 8, y + 4, FONT_SIZE,
		    RAYWHITE);
	DrawText("Up/Down asset  Left/Right frame  PgUp/PgDn page  "
	    "Tab palette  Enter replay sound  type to filter  Esc quit", LIST_W + 8, y + 24,
	    FONT_SIZE, GRAY);
}

int
viewer_run(struct packs *packs, const char *start_id, const char *shot,
    int shot_frames)
{
	struct viewer vw;
	double t0;
	size_t i;
	int frames = 0, rc = 0;
	Image screen;

	memset(&vw, 0, sizeof(vw));
	vw.packs = packs;
	load_drivers(&vw);
	if (collect_ids(&vw) != 0) {
		fprintf(stderr, "cannot list assets\n");
		return (1);
	}
	apply_filter(&vw);
	if (start_id != NULL)
		for (i = 0; i < vw.nshown; i++)
			if (strcmp(vw.ids[vw.shown[i]], start_id) == 0)
				vw.sel = i;

	SetTraceLogLevel(LOG_WARNING);
	InitWindow(WIN_W, WIN_H, "flibustier asset viewer");
	InitAudioDevice();
	vw.device = IsAudioDeviceReady();
	open_selected(&vw);
	SetTargetFPS(60);
	t0 = GetTime();
	while (!WindowShouldClose()) {
		handle_keys(&vw);
		BeginDrawing();
		ClearBackground((Color){ 45, 45, 55, 255 });
		draw_asset(&vw, (unsigned)((GetTime() - t0) / TICK_SECONDS));
		draw_list(&vw);
		draw_status(&vw);
		EndDrawing();
		if (shot != NULL && ++frames >= shot_frames) {
			screen = LoadImageFromScreen();
			if (!ExportImage(screen, shot))
				rc = 1;
			UnloadImage(screen);
			break;
		}
	}
	if (vw.tex.id != 0)
		UnloadTexture(vw.tex);
	audio_stop(&vw);
	if (vw.device)
		CloseAudioDevice();
	CloseWindow();
	view_close(vw.view);
	if (vw.have_dg)
		digital_free(&vw.dg);
	if (vw.have_fm)
		fmmusic_free(&vw.fm);
	for (i = view_virtual_count(); i < vw.nids; i++)
		free(vw.ids[i]);
	free(vw.ids);
	free(vw.shown);
	free(vw.pics);
	return (rc);
}
