/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "script.h"

#define SETTLE_TICKS	20	/* longer than a fade out and in */

static const struct {
	const char	*name;
	int		 key;
} keys[] = {
	{ "up", UI_KEY_UP }, { "down", UI_KEY_DOWN }, { "left", UI_KEY_LEFT },
	{ "right", UI_KEY_RIGHT }, { "enter", UI_KEY_ENTER },
	{ "esc", UI_KEY_ESCAPE }, { "bs", UI_KEY_BACKSPACE },
	{ "shift", UI_KEY_SHIFT }, { "upleft", UI_KEY_UP_LEFT },
	{ "upright", UI_KEY_UP_RIGHT }, { "downleft", UI_KEY_DOWN_LEFT },
	{ "downright", UI_KEY_DOWN_RIGHT },
};

static void
settle(struct game *g, int ticks)
{
	while (ticks-- > 0 && !g->quit)
		game_tick(g);
}

int
game_script(struct game *g, const char *script)
{
	struct ui_event ev;
	char step[128];
	const char *p = script, *end;
	size_t n, i;
	int x, y, ticks;

	while (*p != '\0' && !g->quit) {
		while (*p == ' ')
			p++;
		if (*p == '\0')
			break;
		end = strchr(p, ' ');
		n = end != NULL ? (size_t)(end - p) : strlen(p);
		if (n >= sizeof(step)) {
			snprintf(g->err, sizeof(g->err), "script step too long");
			return (-1);
		}
		memcpy(step, p, n);
		step[n] = '\0';
		p += n;
		memset(&ev, 0, sizeof(ev));
		/* Draw first so that pointer steps see the current layout. */
		game_draw(g);
		if (strncmp(step, "type:", 5) == 0) {
			ev.type = EV_CHAR;
			for (i = 5; step[i] != '\0'; i++) {
				ev.ch = (unsigned char)step[i];
				game_event(g, &ev);
			}
		} else if (sscanf(step, "click:%d,%d", &x, &y) == 2 ||
		    sscanf(step, "move:%d,%d", &x, &y) == 2) {
			ev.type = step[0] == 'c' ? EV_CLICK : EV_MOVE;
			ev.x = x;
			ev.y = y;
			game_event(g, &ev);
		} else if (strncmp(step, "hold:", 5) == 0) {
			char key[16];

			if (sscanf(step, "hold:%15[a-z]:%d", key, &ticks) != 2) {
				snprintf(g->err, sizeof(g->err),
				    "bad script step \"%s\"", step);
				return (-1);
			}
			for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
				if (strcmp(key, keys[i].name) == 0)
					break;
			if (i == sizeof(keys) / sizeof(keys[0])) {
				snprintf(g->err, sizeof(g->err),
				    "unknown key \"%s\"", key);
				return (-1);
			}
			ev.type = EV_KEY;
			ev.key = keys[i].key;
			game_event(g, &ev);
			settle(g, ticks);
			ev.type = EV_KEYUP;
			game_event(g, &ev);
			continue;
		} else if (sscanf(step, "wait:%d", &ticks) == 1) {
			settle(g, ticks);
			continue;
		} else {
			for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
				if (strcmp(step, keys[i].name) == 0)
					break;
			if (i == sizeof(keys) / sizeof(keys[0])) {
				snprintf(g->err, sizeof(g->err),
				    "unknown script step \"%s\"", step);
				return (-1);
			}
			ev.type = EV_KEY;
			ev.key = keys[i].key;
			game_event(g, &ev);
		}
		settle(g, SETTLE_TICKS);
	}
	game_draw(g);
	return (0);
}
