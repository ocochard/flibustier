/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_CRT_H
#define STMALO_CRT_H

#include "raylib.h"

/* The optional CRT look of the window (a fragment shader). */
struct crt {
	Shader	shader;
	int	size_loc;
	int	ok;
};

/* Returns 0, or -1 when the shader cannot be used. */
int	crt_load(struct crt *crt);
/* Draw the 640x480 frame texture into "box" through the shader. */
void	crt_draw(const struct crt *crt, Texture2D frame, Rectangle box);
void	crt_unload(struct crt *crt);

#endif
