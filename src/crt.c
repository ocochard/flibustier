/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * An optional CRT look for the window, written for this project: each
 * row of the 640x480 frame is drawn as a beam whose height grows with
 * its brightness, leaving darker scanlines between rows; neighbouring
 * pixels blend with a sharpened ramp; a faint red, green and blue
 * column mask follows the screen's pixels.  The work is done in linear
 * light.
 */

#include <stdio.h>

#include "raylib.h"
#include "rlgl.h"

#include "crt.h"

static const char crt_body[] =
"#if __VERSION__ >= 130\n"
"#define IN in\n"
"#define TEX texture\n"
"out vec4 crt_out;\n"
"#define OUT crt_out\n"
"#else\n"
"#define IN varying\n"
"#define TEX texture2D\n"
"#define OUT gl_FragColor\n"
"#endif\n"
"IN vec2 fragTexCoord;\n"
"uniform sampler2D texture0;\n"
"uniform vec2 srcSize;\n"
"vec3 lin(vec3 c) { return pow(c, vec3(2.4)); }\n"
"vec3 fetch(float x, float y) {\n"
"	return lin(TEX(texture0, (vec2(floor(x), y) + 0.5) / srcSize).rgb);\n"
"}\n"
"void main() {\n"
"	vec2 p = fragTexCoord * srcSize;\n"
"	float wx = smoothstep(0.25, 0.75, fract(p.x - 0.5));\n"
"	float row = floor(p.y), dy = fract(p.y) - 0.5;\n"
"	vec3 c = vec3(0.0);\n"
"	for (int k = -1; k <= 1; k++) {\n"
"		float y = clamp(row + float(k), 0.0, srcSize.y - 1.0);\n"
"		vec3 s = mix(fetch(p.x - 0.5, y), fetch(p.x + 0.5, y), wx);\n"
"		float d = dy - float(k);\n"
"		float w = mix(0.28, 0.42, max(max(s.r, s.g), s.b));\n"
"		c += s * exp(-(d * d) / (2.0 * w * w));\n"
"	}\n"
"	float m = mod(gl_FragCoord.x, 3.0);\n"
"	vec3 mask = vec3(0.82);\n"
"	if (m < 1.0) mask.r = 1.0; else if (m < 2.0) mask.g = 1.0;\n"
"	else mask.b = 1.0;\n"
"	c *= mask * 1.3;\n"
"	OUT = vec4(pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);\n"
"}\n";

int
crt_load(struct crt *crt)
{
	char src[sizeof(crt_body) + 32];
	float size[2] = { 640.0f, 480.0f };

	snprintf(src, sizeof(src), "#version %s\n%s",
	    rlGetVersion() == RL_OPENGL_21 ? "120" : "330", crt_body);
	crt->shader = LoadShaderFromMemory(NULL, src);
	crt->ok = IsShaderValid(crt->shader) &&
	    crt->shader.id != rlGetShaderIdDefault();
	if (!crt->ok)
		return (-1);
	crt->size_loc = GetShaderLocation(crt->shader, "srcSize");
	SetShaderValue(crt->shader, crt->size_loc, size, SHADER_UNIFORM_VEC2);
	return (0);
}

void
crt_draw(const struct crt *crt, Texture2D frame, Rectangle box)
{
	float size[2] = { (float)frame.width, (float)frame.height };

	BeginShaderMode(crt->shader);
	SetShaderValue(crt->shader, crt->size_loc, size, SHADER_UNIFORM_VEC2);
	DrawTexturePro(frame, (Rectangle){ 0, 0, (float)frame.width,
	    (float)frame.height }, box, (Vector2){ 0, 0 }, 0, WHITE);
	EndShaderMode();
}

void
crt_unload(struct crt *crt)
{
	if (crt->ok)
		UnloadShader(crt->shader);
	crt->ok = 0;
}
