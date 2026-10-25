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

#include "play.h"
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
};

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

	for (i = 0; i < sizeof(keymap) / sizeof(keymap[0]); i++)
		if (IsKeyPressed(keymap[i].raylib) ||
		    IsKeyPressedRepeat(keymap[i].raylib)) {
			ev.type = EV_KEY;
			ev.key = keymap[i].ui;
			game_event(g, &ev);
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
play_run(struct packs *packs, const char *lang, const char *shot,
    int shot_frames)
{
	static struct game g;
	static uint8_t rgb[FB_W * FB_H * 3];
	Vector2 last_mouse = { -1, -1 };
	Texture2D tex;
	Image im, screen;
	int frames = 0, rc = 0;

	if (game_init(&g, packs, lang) != 0) {
		fprintf(stderr, "%s\n", g.err);
		game_free(&g);
		return (1);
	}
	SetTraceLogLevel(LOG_WARNING);
	SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
	InitWindow(FB_W * WINDOW_SCALE, FB_H * WINDOW_SCALE, "stmalo");
	SetExitKey(KEY_NULL);	/* Escape is a game key */
	SetTargetFPS(60);
	im = (Image){ rgb, FB_W, FB_H, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8 };
	tex = LoadTextureFromImage(im);
	SetTextureFilter(tex, TEXTURE_FILTER_POINT);
	while (!WindowShouldClose() && !g.quit) {
		feed_input(&g, &last_mouse);
		game_tick(&g);
		game_draw(&g);
		fb_to_rgb(&g.fb, g.brightness, rgb);
		UpdateTexture(tex, rgb);
		BeginDrawing();
		ClearBackground(BLACK);
		DrawTexturePro(tex, (Rectangle){ 0, 0, FB_W, FB_H },
		    letterbox(), (Vector2){ 0, 0 }, 0, WHITE);
		EndDrawing();
		if (shot != NULL && ++frames >= shot_frames) {
			screen = LoadImageFromScreen();
			if (!ExportImage(screen, shot))
				rc = 1;
			UnloadImage(screen);
			break;
		}
	}
	UnloadTexture(tex);
	CloseWindow();
	game_free(&g);
	return (rc);
}

int
play_headless(struct packs *packs, const char *lang, const char *script,
    const char *out)
{
	static struct game g;
	static uint8_t rgb[FB_W * FB_H * 3];
	Image im;
	int rc = 0;

	if (game_init(&g, packs, lang) != 0 || game_script(&g, script) != 0) {
		fprintf(stderr, "%s\n", g.err);
		game_free(&g);
		return (1);
	}
	fb_to_rgb(&g.fb, g.brightness, rgb);
	im = (Image){ rgb, FB_W, FB_H, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8 };
	SetTraceLogLevel(LOG_WARNING);
	if (out != NULL && !ExportImage(im, out))
		rc = 1;
	printf("screen: %s\n", g.screen != NULL ? "shown" : "quit");
	game_free(&g);
	return (rc);
}
