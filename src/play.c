/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * raylib front end of the game shell: input in, framebuffer out.
 */

#include <stdio.h>
#include <stdlib.h>

#include "raylib.h"

#include "crt.h"
#include "play.h"
#include "sim/save.h"
#include "ui/game.h"
#include "ui/script.h"

#define WINDOW_SCALE	2

static const struct {
	int	raylib;
	int	ui;
} keymap[] = {
	{ KEY_UP, UI_KEY_UP }, { KEY_DOWN, UI_KEY_DOWN },
	{ KEY_LEFT, UI_KEY_LEFT }, { KEY_RIGHT, UI_KEY_RIGHT },
	{ KEY_ENTER, UI_KEY_ENTER }, { KEY_KP_ENTER, UI_KEY_ENTER },
	{ KEY_ESCAPE, UI_KEY_ESCAPE }, { KEY_BACKSPACE, UI_KEY_BACKSPACE },
	{ KEY_LEFT_SHIFT, UI_KEY_SHIFT }, { KEY_RIGHT_SHIFT, UI_KEY_SHIFT },
	/* The numeric keypad: its arrows, and its corners as diagonals. */
	{ KEY_KP_8, UI_KEY_UP }, { KEY_KP_2, UI_KEY_DOWN },
	{ KEY_KP_4, UI_KEY_LEFT }, { KEY_KP_6, UI_KEY_RIGHT },
	{ KEY_KP_7, UI_KEY_UP_LEFT }, { KEY_KP_9, UI_KEY_UP_RIGHT },
	{ KEY_KP_1, UI_KEY_DOWN_LEFT }, { KEY_KP_3, UI_KEY_DOWN_RIGHT },
};

/* The save directory, or "" (saving disabled) with a warning. */
static void
resolve_saves(const char *saves, char *dir, size_t len)
{
	char err[256];

	if (saves != NULL) {
		snprintf(dir, len, "%s", saves);
		return;
	}
	if (save_dir(dir, len, err, sizeof(err)) != 0) {
		fprintf(stderr, "saved games disabled: %s\n", err);
		dir[0] = '\0';
	}
}

/* Where the 640x480 screen sits in the window, keeping its shape. */
static Rectangle
letterbox(void)
{
	float sx = (float)GetScreenWidth() / FB_W;
	float sy = (float)GetScreenHeight() / FB_H;
	float s = sx < sy ? sx : sy;

	return ((Rectangle){ (GetScreenWidth() - FB_W * s) / 2,
	    (GetScreenHeight() - FB_H * s) / 2, FB_W * s, FB_H * s });
}

static void
feed_input(struct game *g, Vector2 *last_mouse)
{
	struct ui_event ev;
	Rectangle box = letterbox();
	Vector2 m = GetMousePosition();
	size_t i;
	int c;

	for (i = 0; i < sizeof(keymap) / sizeof(keymap[0]); i++) {
		if (IsKeyPressed(keymap[i].raylib) ||
		    IsKeyPressedRepeat(keymap[i].raylib)) {
			ev.type = EV_KEY;
			ev.key = keymap[i].ui;
			game_event(g, &ev);
		}
		if (IsKeyReleased(keymap[i].raylib)) {
			ev.type = EV_KEYUP;
			ev.key = keymap[i].ui;
			game_event(g, &ev);
		}
	}
	while ((c = GetCharPressed()) != 0) {
		ev.type = EV_CHAR;
		ev.ch = c;
		game_event(g, &ev);
	}
	ev.x = (int)((m.x - box.x) * FB_W / box.width);
	ev.y = (int)((m.y - box.y) * FB_H / box.height);
	if (m.x != last_mouse->x || m.y != last_mouse->y) {
		ev.type = EV_MOVE;
		game_event(g, &ev);
		*last_mouse = m;
	}
	if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
		ev.type = EV_CLICK;
		game_event(g, &ev);
	}
}

int
play_run(struct packs *packs, const char *lang, const char *saves,
    const char *shot, int shot_frames, int crt_on)
{
	static struct game g;
	uint8_t *rgb;
	Vector2 last_mouse = { -1, -1 };
	Texture2D tex;
	Image im, screen;
	char dir[512];
	int frames = 0, rc = 0, k, big_k, tw, th;
	RenderTexture2D big;
	struct crt crt = { 0 };
	Rectangle box;

	resolve_saves(saves, dir, sizeof(dir));
	if (game_init(&g, packs, lang, dir) != 0) {
		fprintf(stderr, "%s\n", g.err);
		game_free(&g);
		return (1);
	}
	tw = g.fb.out_w;	/* the frame, at the art's resolution */
	th = g.fb.out_h;
	if ((rgb = malloc((size_t)tw * th * 3)) == NULL) {
		fprintf(stderr, "out of memory\n");
		game_free(&g);
		return (1);
	}
	SetTraceLogLevel(getenv("FLIBUSTIER_GL_LOG") != NULL ? LOG_INFO : LOG_WARNING);
	SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
	InitWindow(FB_W * WINDOW_SCALE, FB_H * WINDOW_SCALE, "flibustier");
	SetExitKey(KEY_NULL);	/* Escape is a game key */
	HideCursor();		/* the game draws its own sword pointer */
	SetTargetFPS(60);
	im = (Image){ rgb, tw, th, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8 };
	tex = LoadTextureFromImage(im);
	SetTextureFilter(tex, TEXTURE_FILTER_POINT);
	/*
	 * "Sharp bilinear": the frame is first enlarged by the largest
	 * whole factor that fits, pixel for pixel, then smoothed only over
	 * the remaining fraction, so that strokes keep an even width at any
	 * window size.
	 */
	big = (RenderTexture2D){ 0 };
	big_k = 0;
	if (crt_load(&crt) != 0 && crt_on)
		fprintf(stderr, "the CRT look is not available here\n");
	while (!WindowShouldClose() && !g.quit) {
		feed_input(&g, &last_mouse);
		game_tick(&g);
		game_draw(&g);
		fb_to_rgb(&g.fb, g.brightness, rgb);
		UpdateTexture(tex, rgb);
		if (IsKeyPressed(KEY_F2))
			crt_on = !crt_on;
		box = letterbox();
		if (crt_on && crt.ok) {
			BeginDrawing();
			ClearBackground(BLACK);
			crt_draw(&crt, tex, box);
			EndDrawing();
			goto shown;
		}
		k = (int)(box.width / tw);
		if (k < 1)
			k = 1;
		if (k != big_k) {
			if (big_k > 0)
				UnloadRenderTexture(big);
			big = LoadRenderTexture(tw * k, th * k);
			SetTextureFilter(big.texture, TEXTURE_FILTER_BILINEAR);
			big_k = k;
		}
		BeginTextureMode(big);
		DrawTexturePro(tex, (Rectangle){ 0, 0, tw, th },
		    (Rectangle){ 0, 0, tw * k, th * k }, (Vector2){ 0, 0 },
		    0, WHITE);
		EndTextureMode();
		BeginDrawing();
		ClearBackground(BLACK);
		/* Render textures are stored upside down: flip the source. */
		DrawTexturePro(big.texture, (Rectangle){ 0, 0, tw * k,
		    -th * k }, box, (Vector2){ 0, 0 }, 0, WHITE);
		EndDrawing();
shown:
		if (shot != NULL && ++frames >= shot_frames) {
			screen = LoadImageFromScreen();
			if (!ExportImage(screen, shot))
				rc = 1;
			UnloadImage(screen);
			break;
		}
	}
	if (big_k > 0)
		UnloadRenderTexture(big);
	crt_unload(&crt);
	UnloadTexture(tex);
	CloseWindow();
	free(rgb);
	game_free(&g);
	return (rc);
}

int
play_headless(struct packs *packs, const char *lang, const char *saves,
    const char *script, const char *out)
{
	static struct game g;
	uint8_t *rgb;
	char dir[512];
	Image im;
	int rc = 0;

	resolve_saves(saves, dir, sizeof(dir));
	if (game_init(&g, packs, lang, dir) != 0 ||
	    game_script(&g, script) != 0) {
		fprintf(stderr, "%s\n", g.err);
		game_free(&g);
		return (1);
	}
	if ((rgb = malloc((size_t)g.fb.out_w * g.fb.out_h * 3)) == NULL) {
		fprintf(stderr, "out of memory\n");
		game_free(&g);
		return (1);
	}
	fb_to_rgb(&g.fb, g.brightness, rgb);
	im = (Image){ rgb, g.fb.out_w, g.fb.out_h, 1,
	    PIXELFORMAT_UNCOMPRESSED_R8G8B8 };
	SetTraceLogLevel(getenv("FLIBUSTIER_GL_LOG") != NULL ? LOG_INFO : LOG_WARNING);
	if (out != NULL && !ExportImage(im, out))
		rc = 1;
	free(rgb);
	printf("screen: %s\n", g.screen != NULL ? "shown" : "quit");
	game_free(&g);
	return (rc);
}
