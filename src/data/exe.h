/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_EXE_H
#define STMALO_EXE_H

#include <stddef.h>
#include <stdint.h>

#define EXE_ERAS	6

/*
 * Where game data tables live inside a known build of PIRATESG.EXE.
 * The program is a FlashTek X-32 flat image: virtual address 0 is at
 * file offset "image_offset".  Data offsets differ between builds, so
 * each supported build is identified by the SHA-256 of the file.
 */
struct exe_layout {
	const char	*name;
	const char	*sha256;
	size_t		 image_offset;
	uint32_t	 town_va[EXE_ERAS];	/* era blocks, 1560 to 1680 */
	uint32_t	 tile_class_va;		/* 256 bytes: class per map tile */
	uint32_t	 scenario_va;		/* 30 start scenarios */
	uint32_t	 sine_va;		/* 128 bytes: half-circle sine */
	uint32_t	 colour_class_va;	/* 256 bytes: terrain per colour */
	uint32_t	 era_block_va;		/* 0x200 bytes per era: speeds */
	uint32_t	 hit_steps_va;		/* 5 x u16: hit box widening */
	uint32_t	 half_w_va;		/* 10 x u16: hit box half widths */
	uint32_t	 half_h_va;		/* 10 x u16: hit box half heights */
	uint32_t	 sword_moves_va;	/* sword fight: flag heights, then
						 * frames per move */
	uint32_t	 sword_va;		/* sword fight tables block */
	uint32_t	 cursor_va;		/* mouse pointer: u16 w, h, pixels */
	uint32_t	 board_va;		/* trading board layout, 10-byte
						 * records */
	uint32_t	 tavern_spots_va;	/* 10 x {i16 x0, y0, x1, y1, on} */
	uint32_t	 town_spots_va;		/* 8 x {i16 x0, y0, x1, y1, on} */
	uint32_t	 story_va;		/* career stories: byte scripts */
	uint32_t	 credits_va;		/* 6 x 4 i16 rects in the credits
						 * picture, 20 RGB ink steps */
};

/* Returns the layout of a known build, or NULL. */
const struct exe_layout	*exe_identify(const uint8_t *data, size_t len);

/* Pointer to "len" bytes at virtual address "va", or NULL if outside. */
const uint8_t		*exe_at(const struct exe_layout *layout,
			    const uint8_t *data, size_t datalen, uint32_t va,
			    size_t len);

#endif
