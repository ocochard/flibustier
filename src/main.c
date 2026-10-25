/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include "raylib.h"

/* Pirates! Gold renders at SVGA 640x480; scale it to the window. */
#define GAME_WIDTH	640
#define GAME_HEIGHT	480
#define WINDOW_SCALE	2

int
main(void)
{
	RenderTexture2D screen;
	Rectangle src, dst;
	float scale;

	SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
	InitWindow(GAME_WIDTH * WINDOW_SCALE, GAME_HEIGHT * WINDOW_SCALE,
	    "stmalo");
	SetTargetFPS(60);

	screen = LoadRenderTexture(GAME_WIDTH, GAME_HEIGHT);
	SetTextureFilter(screen.texture, TEXTURE_FILTER_POINT);

	while (!WindowShouldClose()) {
		BeginTextureMode(screen);
		ClearBackground(DARKBLUE);
		DrawText("stmalo", 10, 10, 20, RAYWHITE);
		EndTextureMode();

		/* Letterbox the game screen, keeping its aspect ratio. */
		scale = (float)GetScreenWidth() / GAME_WIDTH;
		if ((float)GetScreenHeight() / GAME_HEIGHT < scale)
			scale = (float)GetScreenHeight() / GAME_HEIGHT;
		/* Render textures are stored upside down. */
		src = (Rectangle){ 0, 0, GAME_WIDTH, -GAME_HEIGHT };
		dst = (Rectangle){
			(GetScreenWidth() - GAME_WIDTH * scale) / 2,
			(GetScreenHeight() - GAME_HEIGHT * scale) / 2,
			GAME_WIDTH * scale, GAME_HEIGHT * scale };

		BeginDrawing();
		ClearBackground(BLACK);
		DrawTexturePro(screen.texture, src, dst, (Vector2){ 0, 0 }, 0,
		    WHITE);
		EndDrawing();
	}

	UnloadRenderTexture(screen);
	CloseWindow();
	return (0);
}
