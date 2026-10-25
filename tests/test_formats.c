/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Format and pack tests on synthetic data; they need no game files.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "data/exe.h"
#include "data/pack.h"
#include "data/sha256.h"
#include "assets/font.h"
#include "assets/img.h"
#include "assets/pcx.h"
#include "assets/rsd.h"
#include "assets/sprite.h"
#include "assets/text.h"
#include "assets/tileset.h"
#include "assets/towns.h"

static int failures;

#define CHECK(cond) do {						\
	if (!(cond)) {							\
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,	\
		    __LINE__, #cond);					\
		failures++;						\
	}								\
} while (0)

/*
 * 3x2 image, bytes per line 4.  The first run (5 x 0x07) crosses the end
 * of row 0, as the original encoder allows.  0xC5 must be escaped.
 */
static size_t
make_pcx(uint8_t *buf)
{
	static const uint8_t rle[] = { 0xC5, 0x07, 0xC1, 0xC5, 0x02, 0x03 };
	size_t n;

	memset(buf, 0, 128);
	buf[0] = 10;		/* manufacturer */
	buf[1] = 5;		/* version */
	buf[2] = 1;		/* RLE */
	buf[3] = 8;		/* bpp */
	buf[8] = 2;		/* xmax */
	buf[10] = 1;		/* ymax */
	buf[65] = 1;		/* planes */
	buf[66] = 4;		/* bytes per line */
	n = 128;
	memcpy(buf + n, rle, sizeof(rle));
	n += sizeof(rle);
	buf[n++] = 0x0C;
	memset(buf + n, 0xAB, 768);
	buf[n] = 0x12;
	return (n + 768);
}

static void
test_pcx(void)
{
	uint8_t buf[1024];
	struct image img;
	size_t len;
	static const uint8_t want[] = { 7, 7, 7, 7, 0xC5, 2 };

	len = make_pcx(buf);
	CHECK(pcx_decode(buf, len, &img) == 0);
	CHECK(img.w == 3 && img.h == 2);
	CHECK(img.pixels != NULL &&
	    memcmp(img.pixels, want, sizeof(want)) == 0);
	CHECK(img.has_palette && img.palette[0] == 0x12 &&
	    img.palette[767] == 0xAB);
	image_free(&img);

	/* Truncated RLE data must fail, not read past the buffer. */
	memmove(buf + 129, buf + 134, len - 134);
	CHECK(pcx_decode(buf, len - 5, &img) == -1);
	CHECK(pcx_decode(buf, 100, &img) == -1);
}

static void
test_rsd(void)
{
	uint8_t px[304];
	static const uint8_t ok[] = {
		0x82,			/* skip 2 */
		0x02, 9, 8,		/* copy 2 */
		0x80, 0x2C, 0x01,	/* skip 300, cursor at 304 */
		0x80, 0x00, 0x00,	/* end */
	};
	static const uint8_t overflow[] = { 0x80, 0x2E, 0x01, 0x03, 1, 2, 3 };
	static const uint8_t zero_op[] = { 0x00, 0x80, 0x00, 0x00 };

	memset(px, 0, sizeof(px));
	CHECK(rsd_apply(ok, sizeof(ok), px, sizeof(px)) == 0);
	CHECK(px[1] == 0 && px[2] == 9 && px[3] == 8 && px[4] == 0);
	/* Skipping past the image, or a missing end marker, is an error. */
	CHECK(rsd_apply(ok, sizeof(ok), px, sizeof(px) - 1) == -1);
	CHECK(rsd_apply(ok, sizeof(ok) - 3, px, sizeof(px)) == -1);
	/* Copy of 3 bytes at 302 would write past 304 pixels. */
	CHECK(rsd_apply(overflow, sizeof(overflow), px, sizeof(px)) == -1);
	CHECK(rsd_apply(zero_op, sizeof(zero_op), px, sizeof(px)) == -1);
}

static int
write_text(const char *dir, const char *name, const char *text)
{
	char path[1024];
	FILE *fp;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if ((fp = fopen(path, "w")) == NULL)
		return (-1);
	fputs(text, fp);
	return (fclose(fp));
}

static void
test_packs(void)
{
	char low[] = "/tmp/stmalo-test-XXXXXX";
	char high[] = "/tmp/stmalo-test-XXXXXX";
	char path[1024];
	struct packs *packs;
	struct blob b;

	if (mkdtemp(low) == NULL || mkdtemp(high) == NULL) {
		CHECK(!"mkdtemp");
		return;
	}
	CHECK(write_text(low, "pack.ini",
	    "# base pack\n[pack]\nname = Low\nlicense = CC0-1.0\n"
	    "[assets]\nmusic/title = title.ogg\npic/town = town.png\n") == 0);
	CHECK(write_text(low, "title.ogg", "low-title") == 0);
	CHECK(write_text(low, "town.png", "low-town") == 0);
	CHECK(write_text(high, "pack.ini",
	    "[pack]\nname = High\n[assets]\nmusic/title = Remix.OGG\n") == 0);
	CHECK(write_text(high, "Remix.OGG", "high-title") == 0);

	packs = packs_new();
	CHECK(packs != NULL);
	CHECK(packs_add_dir(packs, low) == 0);
	CHECK(packs_add_dir(packs, high) == 0);
	CHECK(packs_count(packs) == 2);
	CHECK(strcmp(packs_name(packs, 0), "High") == 0);
	CHECK(strcmp(packs_license(packs, 1), "CC0-1.0") == 0);

	/* The higher pack overrides one asset; the rest falls through. */
	CHECK(packs_load(packs, "music/title", &b) == 0);
	CHECK(b.len == 10 && memcmp(b.data, "high-title", 10) == 0);
	CHECK(strcmp(b.ext, "ogg") == 0 && strcmp(b.pack, "High") == 0);
	blob_free(&b);
	CHECK(packs_load(packs, "pic/town", &b) == 0);
	CHECK(b.len == 8 && strcmp(b.pack, "Low") == 0);
	blob_free(&b);

	CHECK(packs_load(packs, "pic/missing", &b) == -1);
	CHECK(packs_load(packs, "../etc/passwd", &b) == -1);
	CHECK(packs_load(packs, "Pic/Town", &b) == -1);
	packs_free(packs);

	/* Manifests may not point outside their pack. */
	CHECK(write_text(high, "pack.ini",
	    "[assets]\npic/town = ../town.png\n") == 0);
	packs = packs_new();
	CHECK(packs_add_dir(packs, high) == -1);
	CHECK(strstr(packs_error(packs), "syntax error") != NULL);
	packs_free(packs);

	snprintf(path, sizeof(path), "%s/pack.ini", low);
	unlink(path);
	snprintf(path, sizeof(path), "%s/title.ogg", low);
	unlink(path);
	snprintf(path, sizeof(path), "%s/town.png", low);
	unlink(path);
	snprintf(path, sizeof(path), "%s/pack.ini", high);
	unlink(path);
	snprintf(path, sizeof(path), "%s/Remix.OGG", high);
	unlink(path);
	rmdir(low);
	rmdir(high);
}

static void
put16(uint8_t *p, unsigned v)
{
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)(v >> 8);
}

static void
test_sprites(void)
{
	uint8_t buf[256];
	struct sprite_bank bank;
	struct sprite_group grp;
	size_t n;

	/*
	 * Two run-coded frames.  Frame 0 is 3x2: skip 1, 2 pixels on row 0,
	 * then skip to row 1 with the 640 stride (640 - 3 + 1), 1 pixel.
	 */
	memset(buf, 0, sizeof(buf));
	put16(buf, 4);			/* frame 0 offset = 2 * 2 frames */
	put16(buf + 2, 30);		/* frame 1 offset */
	put16(buf + 4, 3);
	put16(buf + 6, 2);
	put16(buf + 8, 0x49);		/* key */
	put16(buf + 10, 1);
	put16(buf + 12, 2);
	buf[14] = 5;
	buf[15] = 6;
	put16(buf + 16, 638);
	put16(buf + 18, 1);
	buf[20] = 7;
	put16(buf + 21, 0xFFFF);	/* 7 padding bytes follow */
	put16(buf + 30, 1);		/* frame 1: 1x1, one run */
	put16(buf + 32, 1);
	put16(buf + 34, 0);
	put16(buf + 36, 0);
	put16(buf + 38, 1);
	buf[40] = 9;
	put16(buf + 41, 0xFFFF);
	n = 43;
	CHECK(bank_decode_runs(buf, n, &bank) == 0);
	CHECK(bank.count == 2 && bank.frames[0].w == 3 &&
	    bank.frames[0].key == 0x49);
	CHECK(bank.frames[0].pixels[1] == 5 && bank.frames[0].pixels[2] == 6 &&
	    bank.frames[0].pixels[4] == 7);
	CHECK(bank.frames[0].mask[0] == 0 && bank.frames[0].mask[1] == 1 &&
	    bank.frames[0].mask[3] == 0 && bank.frames[0].mask[4] == 1);
	CHECK(bank.frames[1].pixels[0] == 9);
	bank_free(&bank);

	/* A run wrapping its row, or a missing end marker, is rejected. */
	put16(buf + 12, 3);
	CHECK(bank_decode_runs(buf, n, &bank) == -1);
	put16(buf + 12, 2);
	CHECK(bank_decode_runs(buf, 41, &bank) == -1);
	/* An offset outside the file is rejected. */
	put16(buf + 2, 200);
	CHECK(bank_decode_runs(buf, n, &bank) == -1);
	put16(buf + 2, 30);

	/* GRP: one u32 offset to the bank above. */
	memset(buf + 100, 0, 4);
	buf[100] = 4;
	memcpy(buf + 104, buf, n);
	CHECK(group_decode(buf + 100, 4 + n, &grp) == 0);
	CHECK(grp.count == 1 && grp.banks[0].count == 2);
	group_free(&grp);

	/* Raw frame: 2x2 pixels then 4 trailing bytes. */
	memset(buf, 0, sizeof(buf));
	put16(buf, 2);
	put16(buf + 2, 2);
	put16(buf + 4, 2);
	put16(buf + 6, 0xFE);
	memcpy(buf + 8, "\1\2\3\4", 4);
	CHECK(bank_decode_raw(buf, 16, &bank) == 0);
	CHECK(bank.count == 1 && bank.frames[0].mask == NULL &&
	    bank.frames[0].pixels[3] == 4 && bank.frames[0].key == 0xFE);
	bank_free(&bank);
	CHECK(bank_decode_raw(buf, 11, &bank) == -1);
}

static void
test_img(void)
{
	/* 3x2 RLE: a zero-length run, row 0 = 7 7 then a run of 2 x 8
	 * overflowing the row by one pixel, row 1 = literal 1 2 3. */
	static const uint8_t rle[] = {
		3, 0, 2, 0, 1, 0,
		0x80, 0x02, 7, 0x02, 8, 0x83, 1, 2, 3,
	};
	static const uint8_t want[] = { 7, 7, 8, 1, 2, 3 };
	static const uint8_t raw[] = { 2, 0, 1, 0, 0, 0, 4, 5 };
	struct image img;

	CHECK(img_decode(rle, sizeof(rle), &img) == 0);
	CHECK(img.w == 3 && img.h == 2 &&
	    memcmp(img.pixels, want, sizeof(want)) == 0);
	image_free(&img);
	CHECK(img_decode(rle, sizeof(rle) - 1, &img) == -1);
	CHECK(img_decode(raw, sizeof(raw), &img) == 0);
	CHECK(img.w == 2 && img.pixels[1] == 5 && !img.has_palette);
	image_free(&img);
	CHECK(img_decode(raw, sizeof(raw) - 1, &img) == -1);
}

static void
test_tileset(void)
{
	static uint8_t buf[0x318 + 2 * (10 + 4 + 2)];
	struct tileset ts;
	size_t off;
	int i;

	memset(buf, 0, sizeof(buf));
	put16(buf, 3);
	put16(buf + 2, 1);
	put16(buf + 6, 2);		/* tile count */
	buf[0x0C + 3] = 63;		/* colour 1 red = full */
	for (off = 0x318, i = 0; i < 2; i++, off += 16) {
		put16(buf + off, 4 + 6);
		put16(buf + off + 2, 2);
		put16(buf + off + 4, 2);
		put16(buf + off + 6, 1);
		put16(buf + off + 8, 1);
		memset(buf + off + 10, i + 1, 4);
	}
	CHECK(tileset_decode(buf, sizeof(buf), &ts) == 0);
	CHECK(ts.count == 2 && ts.tile_w == 2 && ts.pixels[4] == 2);
	CHECK(ts.has_palette && ts.palette[3] == 255 && ts.palette[0] == 0);
	tileset_free(&ts);
	CHECK(tileset_decode(buf, sizeof(buf) - 1, &ts) == -1);
	put16(buf + 0x318 + 16 + 2, 3);	/* second tile of another size */
	CHECK(tileset_decode(buf, sizeof(buf), &ts) == -1);
}

static void
test_font(void)
{
	static uint8_t buf[144 * 8];
	struct font font;

	/* Text font with 1-row glyphs: entry = ink, advance, 4 bytes. */
	memset(buf, 0, sizeof(buf));
	buf[0] = 0x01;
	buf[1] = 0xFF;
	buf[8] = 3;			/* '!' ink */
	buf[10] = 4;			/* '!' advance */
	buf[13] = 0x80;			/* pixel at column 8 */
	CHECK(font_decode(buf, sizeof(buf), &font) == 0);
	CHECK(font.count == 143 && font.first == 0x21 && font.height == 1);
	CHECK(font.glyphs[0].ink == 3 && font.glyphs[0].advance == 4);
	CHECK(font_pixel(&font, 0, 8, 0) == 1 && font_pixel(&font, 0, 7, 0) == 0);
	CHECK(font_pixel(&font, 0, 99, 0) == 0);
	font_free(&font);
	CHECK(font_decode(buf, sizeof(buf) - 1, &font) == -1);
	buf[0] = 0;
	CHECK(font_decode(buf, sizeof(buf), &font) == -1);
}

static void
test_text(void)
{
	/* CSTR: 3 entries, the second empty, the last unterminated. */
	static const uint8_t cstr[] = {
		6, 0, 11, 0, 11, 0,
		'C', 0x8E, 't', 'e', 0,
		'~', 'Y', 'a',
	};
	/* CSTN: 2 groups; group 0 declares 1 string but holds 2. */
	static const uint8_t cstn[] = {
		4, 0, 10, 0,
		1, 0, 'a', 0, 0xA7, 0,
		0, 0,
	};
	struct strtab tab;
	struct strgroups g;
	char *u;

	CHECK(cstr_decode(cstr, sizeof(cstr), &tab) == 0);
	CHECK(tab.count == 3);
	CHECK(strcmp(tab.str[0], "C\xc3\xa9te") == 0);	/* é */
	CHECK(strcmp(tab.str[1], "") == 0 && strcmp(tab.str[2], "~Ya") == 0);
	strtab_free(&tab);

	CHECK(cstn_decode(cstn, sizeof(cstn), &g) == 0);
	CHECK(g.count == 2 && g.declared[0] == 1 && g.groups[0].count == 2);
	CHECK(strcmp(g.groups[0].str[1], "\xc3\x9f") == 0);	/* ß */
	CHECK(g.groups[1].count == 0);
	strgroups_free(&g);

	/* Offsets going backwards are rejected. */
	CHECK(cstr_decode((const uint8_t *)"\4\0\2\0", 4, &tab) == -1);

	u = macroman_to_utf8((const uint8_t *)"\xd1\xca", 2);
	CHECK(u != NULL && strcmp(u, "\xe2\x80\x94\xc2\xa0") == 0);
	free(u);
}

static void
test_sha256(void)
{
	static uint8_t big[1000];
	char hex[65];

	sha256_hex((const uint8_t *)"", 0, hex);
	CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb924"
	    "27ae41e4649b934ca495991b7852b855") == 0);
	sha256_hex((const uint8_t *)"abc", 3, hex);
	CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223"
	    "b00361a396177a9cb410ff61f20015ad") == 0);
	/* 56 bytes: the length no longer fits in the first padding block. */
	sha256_hex((const uint8_t *)
	    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
	    hex);
	CHECK(strcmp(hex, "248d6a61d20638b8e5c026930c3e6039"
	    "a33ce45964ff2167f6ecedd419db06c1") == 0);
	memset(big, 'a', sizeof(big));
	sha256_hex(big, sizeof(big), hex);
	CHECK(strcmp(hex, "41edece42d63e8d9bf515a9ba6932e1c"
	    "20cbc9f5a5d134645adb5db1b9737ea3") == 0);
	CHECK(exe_identify(big, sizeof(big)) == NULL);
}

static void
test_towns(void)
{
	static uint8_t exe[16 + 0xF24];
	struct exe_layout layout;
	struct era_towns era;
	uint8_t *r;
	int i;

	memset(&layout, 0, sizeof(layout));
	layout.image_offset = 16;
	for (i = 0; i < EXE_ERAS; i++)
		layout.town_va[i] = 0;
	memset(exe, 0, sizeof(exe));
	put16(exe + 16 + 0x20, 2);	/* last record index: 3 records */
	r = exe + 16 + 36;
	put16(r, TOWN_INLAND);
	put16(r + 2, 245);
	put16(r + 4, 179);
	put16(r + 8, 4);		/* forts */
	put16(r + 16, 195);		/* wealth */
	put16(r + 28, 6);		/* id */
	memcpy(r + 64, "Havana", 6);
	memcpy(r + 80 + 64, "Channel A", 9);
	memcpy(r + 160 + 64, "Channel B", 9);
	CHECK(towns_load(&layout, exe, sizeof(exe), 4, &era) == 0);
	CHECK(era.year == 1660 && era.count == 1);
	CHECK(strcmp(era.towns[0].name, "Havana") == 0);
	CHECK(era.towns[0].x == 245 && era.towns[0].y == 179 &&
	    era.towns[0].forts == 4 && era.towns[0].wealth == 195 &&
	    era.towns[0].id == 6 && (era.towns[0].flags & TOWN_INLAND));
	CHECK(strcmp(era.channel[1].name, "Channel B") == 0);

	/* A name filling all 16 bytes stays terminated. */
	memcpy(r + 64, "ABCDEFGHIJKLMNOP", 16);
	CHECK(towns_load(&layout, exe, sizeof(exe), 0, &era) == 0);
	CHECK(strlen(era.towns[0].name) == 16);

	CHECK(towns_load(&layout, exe, sizeof(exe) - 1, 0, &era) == -1);
	CHECK(towns_load(&layout, exe, sizeof(exe), 6, &era) == -1);
	put16(r + 6, 7);		/* no such nation */
	CHECK(towns_load(&layout, exe, sizeof(exe), 0, &era) == -1);
	put16(r + 6, 0);
	put16(exe + 16 + 0x20, 48);	/* 49 records do not fit */
	CHECK(towns_load(&layout, exe, sizeof(exe), 0, &era) == -1);
}

int
main(void)
{
	test_pcx();
	test_rsd();
	test_sprites();
	test_img();
	test_tileset();
	test_font();
	test_text();
	test_sha256();
	test_towns();
	test_packs();
	if (failures > 0) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return (1);
	}
	printf("all format tests passed\n");
	return (0);
}
