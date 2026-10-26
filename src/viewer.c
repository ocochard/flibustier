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
};

static int
collect_ids(struct viewer *vw)
{
	char **ids;
	size_t n, i, k;

	if (packs_list(vw->packs, &ids, &n) != 0)
		return (-1);
	vw->ids = malloc((n + view_virtual_count()) * sizeof(*vw->ids));
	vw->shown = malloc((n + view_virtual_count()) * sizeof(*vw->shown));
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

static void
open_selected(struct viewer *vw)
{
	const char *id;

	view_close(vw->view);
	vw->view = NULL;
	vw->frame = 0;
	vw->pic = 0;
	vw->err[0] = '\0';
	if ((id = selected_id(vw)) != NULL &&
	    (vw->view = view_open(vw->packs, id, vw->err,
	    sizeof(vw->err))) != NULL)
		vw->frame = view_start_frame(vw->view);
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
	    "Tab palette  type to filter  Esc quit", LIST_W + 8, y + 24,
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
	if (collect_ids(&vw) != 0) {
		fprintf(stderr, "cannot list assets\n");
		return (1);
	}
	apply_filter(&vw);
	if (start_id != NULL)
		for (i = 0; i < vw.nshown; i++)
			if (strcmp(vw.ids[vw.shown[i]], start_id) == 0)
				vw.sel = i;
	open_selected(&vw);

	SetTraceLogLevel(LOG_WARNING);
	InitWindow(WIN_W, WIN_H, "flibustier asset viewer");
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
	CloseWindow();
	view_close(vw.view);
	for (i = view_virtual_count(); i < vw.nids; i++)
		free(vw.ids[i]);
	free(vw.ids);
	free(vw.shown);
	free(vw.pics);
	return (rc);
}
