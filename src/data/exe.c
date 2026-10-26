/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <string.h>

#include "exe.h"
#include "sha256.h"

/*
 * Era block addresses come from the function that loads an era: it
 * copies 0xF24 bytes from one of these addresses into the live town
 * table (VA 0x13555 in the GOG build).  The tile class table is read by
 * the sailing setup (VA 0x2ab09) to find open sea.  Start scenarios are
 * read by the scenario loader (VA 0x1386c).  The sailing loop (VA 0x231b8)
 * uses the sine and colour class tables, and the per-era block that VA
 * 0x1361c copies, which starts with the speed table.  The battle loop
 * (VA 0x7db8) uses the hit box tables.
 */
static const struct exe_layout layouts[] = {
	{
		"GOG CD release (PIRATESG.EXE, 289503 bytes)",
		"0f196be3fe64651e4c858578c14d7848355c2d793a30206ef5440b75b5e0bce3",
		0x47a7,
		{ 0x38d58, 0x39c7c, 0x3aba0, 0x3bac4, 0x3c9e8, 0x3d90c },
		0x4103c,
		0x35b90,
		0x36628,
		0x4113c,
		0x34d68,
		0x325ae,
		0x32b0c,
		0x32b20,
		0x338a0,
		0x389b8,
		0x37080,
		0x348c4,
		0x330f0,
		0x339f4,
		0x34668,
	},
};

const struct exe_layout *
exe_identify(const uint8_t *data, size_t len)
{
	char hex[65];
	size_t i;

	sha256_hex(data, len, hex);
	for (i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++)
		if (strcmp(hex, layouts[i].sha256) == 0)
			return (&layouts[i]);
	return (NULL);
}

const uint8_t *
exe_at(const struct exe_layout *layout, const uint8_t *data, size_t datalen,
    uint32_t va, size_t len)
{
	size_t off;

	off = layout->image_offset + va;
	if (off < layout->image_offset || off > datalen || len > datalen - off)
		return (NULL);
	return (data + off);
}
