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
 * (VA 0x7db8) uses the hit box tables, and draws its shots' arc from
 * the height and frame tables (VA 0x8405); its bow cross (VA 0xf903)
 * sits on a circle of offsets.  The era loader also copies the era's
 * arrival cells, which the sailing loop looks the ship's cell up in
 * (VA 0x29dd9).
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
		0x32928,
		0x327e0,
		0x33fd0,
		0x33628,
		0x3400c,
		0x32e68,
		0x3235c,
		0x35968,
		0x34668,
		0x34228,
		0x33b18,
		0x32f90,
		0x32544,
		0x32564,
		0x40fbc,
		{ 0x3e830, 0x3ed70, 0x3f2da, 0x3f922, 0x40030, 0x4075c },
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

int
exe_open(struct packs *packs, struct blob *out,
    const struct exe_layout **layout)
{
	memset(out, 0, sizeof(*out));
	*layout = NULL;
	if (packs_load(packs, "program/main", out) != 0) {
		memset(out, 0, sizeof(*out));
		return (0);
	}
	if ((*layout = exe_identify(out->data, out->len)) == NULL) {
		blob_free(out);
		memset(out, 0, sizeof(*out));
		return (-1);
	}
	return (0);
}
