/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * The world's land as a mask (P5), from the player's game data at make
 * time (never stored): helpers for tools/artgen. */
#include <stdio.h>
#include <stdlib.h>
#include "data/pack.h"
#include "ui/game.h"
#include "ui/script.h"
#include "sim/internal.h"
#include "assets/world.h"
int main(int c, char **v){ static struct game g; struct packs *p=packs_new(); packs_add_original(p,v[1]);
 if (game_init(&g,p,"eng",NULL)) return 1;
 game_script(&g, "enter enter enter enter down down down down enter down enter right enter enter type:Lo enter enter esc enter enter");
 const struct world *w = g.sim.world;
 /* argv[3], argv[4]: the mask's size (default a pixel a cell); each of
  * its pixels is the share of land among 4 x 4 samples of the world's
  * own terrain under it, so islands keep their shapes. */
 int W = c > 4 ? atoi(v[3]) : WORLD_CELLS_W, H = c > 4 ? atoi(v[4]) :
     WORLD_CELLS_H;
 FILE *f = fopen(v[2], "wb");
 fprintf(f, "P5\n%d %d\n255\n", W, H);
 for (int y = 0; y < H; y++)
  for (int x = 0; x < W; x++) {
   int land = 0;
   for (int sy = 0; sy < 4; sy++)
    for (int sx = 0; sx < 4; sx++) {
     long px = ((long)x * 4 + sx) * WORLD_PX_W / (W * 4L) + WORLD_PX_W / (W * 8L);
     long py = ((long)y * 4 + sy) * WORLD_PX_H / (H * 4L) + WORLD_PX_H / (H * 8L);
     land += world_terrain(w, (int)px, (int)py) == TERRAIN_LAND;
    }
   fputc(land * 255 / 16, f);
  }
 fclose(f);
 return 0;}
