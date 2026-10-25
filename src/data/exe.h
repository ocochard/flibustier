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
};

/* Returns the layout of a known build, or NULL. */
const struct exe_layout	*exe_identify(const uint8_t *data, size_t len);

/* Pointer to "len" bytes at virtual address "va", or NULL if outside. */
const uint8_t		*exe_at(const struct exe_layout *layout,
			    const uint8_t *data, size_t datalen, uint32_t va,
			    size_t len);

#endif
