/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 *
 * Format and pack tests on synthetic data; they need no game files.
 */

#define _POSIX_C_SOURCE 200809L

#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "data/exe.h"
#include "data/pack.h"
#include "data/sha256.h"
#include "assets/font.h"
#include "assets/fonts.h"
#include "assets/img.h"
#include "assets/pcx.h"
#include "assets/pic.h"
#include "assets/png.h"
#include "assets/rsd.h"
#include "assets/sheet.h"
#include "assets/sprite.h"
#include "assets/text.h"
#include "assets/tiles.h"
#include "assets/tileset.h"
#include "assets/towns.h"
#include "gfx/fb.h"
#include "gfx/text.h"

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

/* 6x4 RGBA, each 2x2 block one colour: red, green, blue, then white,
 * black, near red. */
static const uint8_t png_6x4[] = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
	0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x04,
	0x08, 0x06, 0x00, 0x00, 0x00, 0xad, 0x04, 0x4e, 0x43, 0x00, 0x00, 0x00,
	0x22, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
	0x1f, 0x84, 0xe1, 0x10, 0xc2, 0x45, 0xb0, 0x30, 0x25, 0xa0, 0x80, 0x01,
	0x24, 0x09, 0xc4, 0xbf, 0xb8, 0x58, 0xc1, 0x18, 0xa7, 0x04, 0x00, 0xeb,
	0x8d, 0x33, 0xf5, 0x48, 0x32, 0x5b, 0x98, 0x00, 0x00, 0x00, 0x00, 0x49,
	0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

static int
write_bytes(const char *dir, const char *name, const uint8_t *data,
    size_t len)
{
	char path[1024];
	FILE *fp;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if ((fp = fopen(path, "wb")) == NULL)
		return (-1);
	if (fwrite(data, 1, len, fp) != len) {
		fclose(fp);
		return (-1);
	}
	return (fclose(fp));
}

static void
remove_files(const char *dir, const char *const *names)
{
	char path[1024];

	for (; *names != NULL; names++) {
		snprintf(path, sizeof(path), "%s/%s", dir, *names);
		unlink(path);
	}
	rmdir(dir);
}

static void
test_png(void)
{
	struct image img;

	/* Drawn for a 960 pixel high screen: half size in 640x480. */
	CHECK(png_decode(png_6x4, sizeof(png_6x4), 960, &img) == 0);
	CHECK(img.w == 3 && img.h == 2);
	CHECK(img.rgba_w == 6 && img.rgba_h == 4);
	CHECK(img.pixels == NULL && img.rgba != NULL);
	CHECK(img.rgba != NULL && img.rgba[0] == 255 && img.rgba[1] == 0 &&
	    img.rgba[3] == 255);
	/* Art pixel (4, 2): the near red block. */
	CHECK(img.rgba != NULL && img.rgba[4 * (2 * 6 + 4)] == 250 &&
	    img.rgba[4 * (2 * 6 + 4) + 1] == 10);
	image_free(&img);
	/* For 1080p, 6x4 art covers 2.67x1.78 pixels: rounded. */
	CHECK(png_decode(png_6x4, sizeof(png_6x4), 1080, &img) == 0);
	CHECK(img.w == 3 && img.h == 2);
	image_free(&img);
	CHECK(png_decode(png_6x4, sizeof(png_6x4) - 20, 960, &img) == -1);
	CHECK(png_decode(png_6x4, sizeof(png_6x4), 0, &img) == -1);
	CHECK(png_decode(png_6x4, sizeof(png_6x4), 4000, &img) == -1);
}

/*
 * Replacement art: true colours at the pack's art height, with the
 * original picture's palette and pixels when it is there, else a fixed
 * palette.
 */
static void
test_pic(void)
{
	static const char *const low_files[] = { "pack.ini", "town.pcx", NULL };
	static const char *const high_files[] = { "pack.ini", "town.png",
	    NULL };
	static const uint8_t want_orig[] = { 7, 7, 7, 7, 0xC5, 2 };
	/* Cube indices of red, green, blue, white, black and near red. */
	static const uint8_t want_fixed[] = { 180, 30, 5, 215, 0, 180 };
	char low[] = "/tmp/stmalo-test-XXXXXX";
	char high[] = "/tmp/stmalo-test-XXXXXX";
	uint8_t pcx[1024];
	struct packs *packs;
	struct image img;
	struct blob b;
	size_t len;

	if (mkdtemp(low) == NULL || mkdtemp(high) == NULL) {
		CHECK(!"mkdtemp");
		return;
	}
	len = make_pcx(pcx);
	CHECK(write_text(low, "pack.ini", "[assets]\npic/town = town.pcx\n")
	    == 0);
	CHECK(write_bytes(low, "town.pcx", pcx, len) == 0);
	CHECK(write_text(high, "pack.ini", "[pack]\nart_height = 960\n"
	    "[assets]\npic/town = town.png\n") == 0);
	CHECK(write_bytes(high, "town.png", png_6x4, sizeof(png_6x4)) == 0);

	/* Over the original: its palette and pixels. */
	packs = packs_new();
	CHECK(packs_add_dir(packs, low) == 0);
	CHECK(packs_add_dir(packs, high) == 0);
	CHECK(packs_art_height(packs) == 960);
	CHECK(packs_load(packs, "pic/town", &b) == 0);
	CHECK(b.art_height == 960 && strcmp(b.ext, "png") == 0);
	blob_free(&b);
	CHECK(packs_load_ext(packs, "pic/town", "pcx", &b) == 0);
	CHECK(b.art_height == 480 && strcmp(b.ext, "pcx") == 0);
	blob_free(&b);
	CHECK(packs_load_ext(packs, "pic/town", "img", &b) == -1);
	CHECK(pic_load(packs, "pic/town", &img) == 0);
	CHECK(img.w == 3 && img.h == 2 && img.rgba != NULL);
	CHECK(img.pixels != NULL &&
	    memcmp(img.pixels, want_orig, sizeof(want_orig)) == 0);
	CHECK(img.palette[0] == 0x12);
	image_free(&img);
	packs_free(packs);

	/* Alone: reduced to the fixed palette. */
	packs = packs_new();
	CHECK(packs_add_dir(packs, high) == 0);
	CHECK(pic_load(packs, "pic/town", &img) == 0);
	CHECK(img.pixels != NULL &&
	    memcmp(img.pixels, want_fixed, sizeof(want_fixed)) == 0);
	CHECK(img.has_palette && img.palette[3 * 180] == 255 &&
	    img.palette[3 * 180 + 1] == 0);
	image_free(&img);
	packs_free(packs);

	/* An art height is a whole number from 240 to 4320. */
	packs = packs_new();
	CHECK(write_text(high, "pack.ini", "[pack]\nart_height = 100\n") == 0);
	CHECK(packs_add_dir(packs, high) == -1);
	CHECK(write_text(high, "pack.ini", "[pack]\nart_height = 1080p\n")
	    == 0);
	CHECK(packs_add_dir(packs, high) == -1);
	CHECK(packs_art_height(packs) == 480);
	packs_free(packs);

	remove_files(low, low_files);
	remove_files(high, high_files);
}

/* Canvas pixel (x, y) of a converted frame "w" pixels wide. */
static const uint8_t *
at(const uint8_t *rgb, int w, int x, int y)
{
	return (rgb + 3 * ((size_t)y * w + x));
}

/*
 * The canvas: true-colour art sampled at the output resolution, palette
 * colours enlarged, by a whole or a fractional factor.
 */
static void
test_fb(void)
{
	static struct fb fb;
	struct image img;
	uint8_t *rgb;
	const uint8_t *p;

	CHECK(fb_init(&fb, 100) == -1);
	CHECK(fb_init(&fb, 5000) == -1);
	CHECK(fb_init(&fb, 1081) == 0);		/* rounded to 4:3 */
	CHECK(fb.out_w == 1440 && fb.out_h == 1080);
	if ((rgb = malloc((size_t)1440 * 1080 * 3)) == NULL) {
		CHECK(!"malloc");
		fb_free(&fb);
		return;
	}
	CHECK(png_decode(png_6x4, sizeof(png_6x4), 960, &img) == 0);
	image_fixed_palette(&img);
	CHECK(image_quantize(&img) == 0);

	/* 2x: the 6x4 art fills the 3x2 picture's 6x4 canvas pixels. */
	CHECK(fb_init(&fb, 960) == 0);
	CHECK(fb.out_w == 1280 && fb.out_h == 960);
	fb_clear(&fb, 0);
	fb_image(&fb, &img, 0, 0, 1);
	fb_rect(&fb, 1, 0, 1, 1, 9);		/* over the green block */
	fb_put(&fb, 2 * FB_W + 2, 5);		/* past the picture */
	CHECK(fb.px[1] == 9 && fb.px[0] == 180);	/* 640x480 indices */
	fb_to_rgb(&fb, 256, rgb);
	p = at(rgb, 1280, 0, 0);		/* red art */
	CHECK(p[0] == 255 && p[1] == 0 && p[2] == 0);
	p = at(rgb, 1280, 3, 1);		/* palette entry 9 */
	CHECK(p[0] == 0 && p[1] == 51 && p[2] == 153);
	p = at(rgb, 1280, 5, 3);		/* near red art */
	CHECK(p[0] == 250 && p[1] == 10 && p[2] == 5);
	p = at(rgb, 1280, 4, 4);		/* entry 5 */
	CHECK(p[0] == 0 && p[1] == 0 && p[2] == 255);
	/* Fades dim the art as well. */
	fb_to_rgb(&fb, 128, rgb);
	CHECK(rgb[0] == 127);

	/* 2.25x: 640x480 pixels cover 2 or 3 canvas pixels each way. */
	CHECK(fb_init(&fb, 1080) == 0);
	fb_clear(&fb, 0);
	fb_image(&fb, &img, 0, 0, 1);		/* canvas 0..5 x 0..3 */
	fb_rect(&fb, 3, 0, 1, 1, 9);		/* canvas 6..8 x 0..1 */
	fb_to_rgb(&fb, 256, rgb);
	p = at(rgb, 1440, 5, 3);		/* near red art, 1:1 */
	CHECK(p[0] == 250 && p[1] == 10 && p[2] == 5);
	p = at(rgb, 1440, 8, 1);
	CHECK(p[0] == 0 && p[1] == 51 && p[2] == 153);
	p = at(rgb, 1440, 9, 0);		/* entry 0, black */
	CHECK(p[0] == 0 && p[1] == 0 && p[2] == 0);

	/* Without true colours, the picture's indices are enlarged. */
	image_drop_rgba(&img);
	fb_image(&fb, &img, 0, 0, 0);
	fb_to_rgb(&fb, 256, rgb);
	p = at(rgb, 1440, 1, 1);		/* entry 180 */
	CHECK(p[0] == 255 && p[1] == 0 && p[2] == 0);
	p = at(rgb, 1440, 2, 1);		/* entry 30, green */
	CHECK(p[0] == 0 && p[1] == 255 && p[2] == 0);
	image_free(&img);
	fb_free(&fb);
	free(rgb);
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

#define IMB_LEN	(0x318 + 2 * (10 + 4 + 2))

/* IMB tileset: two 2x2 tiles of colours 1 and 2, colour 1 red. */
static void
make_imb(uint8_t *buf)
{
	size_t off;
	int i;

	memset(buf, 0, IMB_LEN);
	put16(buf, 3);
	put16(buf + 2, 1);
	put16(buf + 6, 2);		/* tile count */
	buf[0x09 + 3] = 63;		/* colour 1 red = full */
	for (off = 0x318, i = 0; i < 2; i++, off += 16) {
		put16(buf + off, 4 + 6);
		put16(buf + off + 2, 2);
		put16(buf + off + 4, 2);
		put16(buf + off + 6, 1);
		put16(buf + off + 8, 1);
		memset(buf + off + 10, i + 1, 4);
	}
}

static void
test_tileset(void)
{
	static uint8_t buf[IMB_LEN];
	struct tileset ts;

	make_imb(buf);
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
	CHECK(font.glyphs[0].advance_sub == 4 * FONT_SUB);
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

/* 4x2 RGBA: a 2x2 white block, a column of half-transparent red, then a
 * clear one. */
static const uint8_t png_4x2a[] = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
	0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02,
	0x08, 0x06, 0x00, 0x00, 0x00, 0x7f, 0xa8, 0x7d, 0x63, 0x00, 0x00, 0x00,
	0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0x0f, 0x03, 0x0c,
	0x0c, 0x0d, 0x0c, 0x20, 0x80, 0x2e, 0x00, 0x00, 0x7d, 0x40, 0x12, 0xef,
	0x8a, 0xc8, 0xf4, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
	0xae, 0x42, 0x60, 0x82,
};

/* Raw bank: one 2x2 frame, key 0xFE. */
static size_t
make_raw_bank(uint8_t *buf)
{
	memset(buf, 0, 16);
	put16(buf, 2);
	put16(buf + 2, 2);
	put16(buf + 4, 2);
	put16(buf + 6, 0xFE);
	memcpy(buf + 8, "\1\2\3\4", 4);
	return (16);
}

/* Write "text" as the sheet "name" of pack "dir" and try to load it. */
static int
try_sheet(const char *dir, const char *text)
{
	struct packs *packs = packs_new();
	struct sprite_bank bank;
	int rc;

	if (write_text(dir, "sprites/flag.sheet", text) != 0 ||
	    packs_add_dir(packs, dir) != 0) {
		packs_free(packs);
		return (-2);
	}
	rc = bank_load(packs, "spr/flag", BANK_RUNS, &bank);
	bank_free(&bank);
	packs_free(packs);
	return (rc);
}

/*
 * Sprite sheets: frames cut from a PNG, sized by the pack's art height,
 * indices from the original bank when there, else from the art.
 */
static void
test_sheets(void)
{
	static const char *const low_files[] = { "pack.ini", "boat.cur", NULL };
	static const char *const high_files[] = { "pack.ini", "boat.sheet",
	    "atlas.png", "fight.sheet", "sprites/flag.sheet",
	    "sprites/flag.png", NULL };
	static const char ini[] = "[pack]\nart_height = 960\n[assets]\n"
	    "cur/boat = boat.sheet\nspr/flag = sprites/flag.sheet\n"
	    "grp/fight = fight.sheet\n";
	static const char flag[] = "[sheet]\nimage = flag.png\n\n[frames]\n"
	    "# frame = x y w h\n1 = 0 0 2 2\n0 = 0 0 4 2\n";
	static struct fb fb;
	char low[] = "/tmp/stmalo-test-XXXXXX";
	char high[] = "/tmp/stmalo-test-XXXXXX";
	char path[1024];
	uint8_t raw[16], *rgb;
	struct packs *packs;
	struct sprite_bank bank;
	struct sprite_group grp;
	const struct frame *f;
	const uint8_t *p;

	if (mkdtemp(low) == NULL || mkdtemp(high) == NULL) {
		CHECK(!"mkdtemp");
		return;
	}
	snprintf(path, sizeof(path), "%s/sprites", high);
	CHECK(mkdir(path, 0755) == 0);
	CHECK(write_text(low, "pack.ini", "[assets]\ncur/boat = boat.cur\n")
	    == 0);
	CHECK(write_bytes(low, "boat.cur", raw, make_raw_bank(raw)) == 0);
	CHECK(write_text(high, "pack.ini", ini) == 0);
	CHECK(write_bytes(high, "atlas.png", png_6x4, sizeof(png_6x4)) == 0);
	CHECK(write_text(high, "boat.sheet", "[sheet]\nimage = atlas.png\n"
	    "[frames]\n0 = 0 0 4 4\n") == 0);
	CHECK(write_text(high, "fight.sheet", "[sheet]\nimage = atlas.png\n"
	    "[frames]\n0.0 = 0 0 2 2\n1.0 = 2 0 2 2\n1.1 = 0 0 4 2\n") == 0);
	CHECK(write_bytes(high, "sprites/flag.png", png_4x2a,
	    sizeof(png_4x2a)) == 0);
	CHECK(write_text(high, "sprites/flag.sheet", flag) == 0);

	/* Over the original: its indices and key, the sheet's colours. */
	packs = packs_new();
	CHECK(packs_add_dir(packs, low) == 0);
	CHECK(packs_add_dir(packs, high) == 0);
	CHECK(bank_load(packs, "cur/boat", BANK_RAW, &bank) == 0);
	CHECK(bank.count == 1);
	if (bank.count == 1) {
		f = &bank.frames[0];
		CHECK(f->w == 2 && f->h == 2 && f->key == 0xFE);
		CHECK(memcmp(f->pixels, "\1\2\3\4", 4) == 0);
		CHECK(f->rgba != NULL && f->rgba_w == 4 && f->rgba_h == 4);
	}
	bank_free(&bank);

	/* Alone, from a subdirectory: alpha gives the mask. */
	CHECK(bank_load(packs, "spr/flag", BANK_RUNS, &bank) == 0);
	CHECK(bank.count == 2);
	if (bank.count == 2) {
		f = &bank.frames[0];
		CHECK(f->w == 2 && f->h == 1);
		CHECK(f->mask != NULL && f->mask[0] == 1 && f->mask[1] == 0);
		CHECK(f->pixels[0] == 215 && f->pixels[1] == 0);
		CHECK(bank.frames[1].w == 1 && bank.frames[1].h == 1);
	}

	/* Drawn at 2x: opaque art copied, half alpha blended, clear kept. */
	if (bank.count == 2 && fb_init(&fb, 960) == 0 &&
	    (rgb = malloc((size_t)1280 * 960 * 3)) != NULL) {
		palette_fixed(fb.pal);
		fb_clear(&fb, 9);
		fb_frame(&fb, &bank.frames[0], 1, 1);
		CHECK(fb.px[FB_W + 1] == 215 && fb.px[FB_W + 2] == 9);
		fb_to_rgb(&fb, 256, rgb);
		p = at(rgb, 1280, 2, 2);
		CHECK(p[0] == 255 && p[1] == 255 && p[2] == 255);
		p = at(rgb, 1280, 4, 3);
		CHECK(p[0] == 128 && p[1] == 25 && p[2] == 76);
		p = at(rgb, 1280, 5, 2);
		CHECK(p[0] == 0 && p[1] == 51 && p[2] == 153);
		/* Clipped to 640x480 column 1: canvas columns 2 and 3. */
		fb_clear(&fb, 9);
		fb_frame_clip(&fb, &bank.frames[0], 0, 0, 1, 0, 1, 0);
		fb_to_rgb(&fb, 256, rgb);
		CHECK(at(rgb, 1280, 1, 0)[1] == 51 && at(rgb, 1280, 2, 0)[1] ==
		    25 && at(rgb, 1280, 3, 0)[1] == 51);
		free(rgb);
	}
	fb_free(&fb);
	bank_free(&bank);

	/* Groups: "bank.frame". */
	CHECK(group_load(packs, "grp/fight", &grp) == 0);
	CHECK(grp.count == 2 && grp.banks[0].count == 1 &&
	    grp.banks[1].count == 2);
	if (grp.count == 2 && grp.banks[1].count == 2)
		CHECK(grp.banks[1].frames[1].w == 2);
	group_free(&grp);
	packs_free(packs);

	/* Gaps, rectangles past the image, bad lines, no image. */
	CHECK(try_sheet(high, flag) == 0);
	CHECK(try_sheet(high, "[sheet]\nimage = flag.png\n[frames]\n"
	    "0 = 0 0 1 1\n2 = 0 0 1 1\n") == -1);
	CHECK(try_sheet(high, "[sheet]\nimage = flag.png\n[frames]\n"
	    "0 = 0 0 5 2\n") == -1);
	CHECK(try_sheet(high, "[sheet]\nimage = flag.png\n[frames]\n"
	    "0 = 0 0 2\n") == -1);
	CHECK(try_sheet(high, "[sheet]\nimage = flag.png\n[frames]\n"
	    "0 = 0 0 0 2\n") == -1);
	CHECK(try_sheet(high, "[sheet]\nimage = ../flag.png\n[frames]\n"
	    "0 = 0 0 1 1\n") == -1);
	CHECK(try_sheet(high, "[frames]\n0 = 0 0 1 1\n") == -1);

	remove_files(low, low_files);
	snprintf(path, sizeof(path), "%s/sprites", high);
	remove_files(high, high_files);
	rmdir(path);
	rmdir(high);
}

/* A TrueType font of the system for the font tests, or NULL. */
static const char *
test_font_file(void)
{
	static const char *const paths[] = {
		"/usr/local/share/fonts/dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/TTF/DejaVuSans.ttf",
		"/usr/share/fonts/dejavu/DejaVuSans.ttf",
		"/Library/Fonts/Arial Unicode.ttf",
		"C:/Windows/Fonts/arial.ttf",
	};
	const char *env = getenv("STMALO_TEST_FONT");
	size_t i;

	if (env != NULL && access(env, R_OK) == 0)
		return (env);
	for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
		if (access(paths[i], R_OK) == 0)
			return (paths[i]);
	return (NULL);
}

static int
copy_file(const char *from, const char *dir, const char *name)
{
	FILE *in, *out;
	char path[1024], buf[8192];
	size_t n;
	int rc = 0;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if ((in = fopen(from, "rb")) == NULL)
		return (-1);
	if ((out = fopen(path, "wb")) == NULL) {
		fclose(in);
		return (-1);
	}
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
		if (fwrite(buf, 1, n, out) != n)
			rc = -1;
	fclose(in);
	return (fclose(out) != 0 ? -1 : rc);
}

/* Load font/pirate1 from "dir" after writing "desc" as its descriptor. */
static int
try_font(const char *dir, const char *desc)
{
	struct packs *packs = packs_new();
	struct font f;
	int rc;

	if (write_text(dir, "f.font", desc) != 0 ||
	    packs_add_dir(packs, dir) != 0) {
		packs_free(packs);
		return (-2);
	}
	rc = font_load(packs, "font/pirate1", 960, &f);
	font_free(&f);
	packs_free(packs);
	return (rc);
}

/*
 * Replacement fonts: 640x480 metrics with fractional advances, 1-bit
 * glyphs for the screen, coverage glyphs for the canvas.
 */
static void
test_fonts(void)
{
	static const char *const files[] = { "pack.ini", "f.font", "f.ttf",
	    "caps.font", NULL };
	static const char good[] = "[font]\nfile = f.ttf\nheight = 25\n"
	    "size = 22.5\nbaseline = 19\n";
	static struct fb fb;
	const char *ttf = test_font_file();
	char dir[] = "/tmp/stmalo-test-XXXXXX";
	struct packs *packs;
	struct font f, caps;
	const struct glyph *a;
	uint8_t *rgb;
	size_t i, n;
	int x, y, inked;

	if (ttf == NULL) {
		printf("font tests skipped: no TrueType font found "
		    "(set STMALO_TEST_FONT)\n");
		return;
	}
	if (mkdtemp(dir) == NULL) {
		CHECK(!"mkdtemp");
		return;
	}
	CHECK(copy_file(ttf, dir, "f.ttf") == 0);
	CHECK(write_text(dir, "pack.ini", "[pack]\nart_height = 960\n"
	    "[assets]\nfont/pirate1 = f.font\nfont/caps = caps.font\n") == 0);
	CHECK(write_text(dir, "f.font", good) == 0);
	CHECK(write_text(dir, "caps.font", "[font]\nfile = f.ttf\n"
	    "height = 34\n") == 0);
	packs = packs_new();
	CHECK(packs_add_dir(packs, dir) == 0);

	CHECK(font_load(packs, "font/pirate1", 960, &f) == 0);
	CHECK(f.height == 25 && f.first == 0x21 && f.count == 0xFF - 0x21 + 1);
	CHECK(f.space_sub > 0 && f.hd != NULL && f.hd_height == 960);
	if (f.count > 'A' - 0x21) {
		a = &f.glyphs['A' - 0x21];
		CHECK(a->advance_sub > 0 &&
		    a->advance == (a->advance_sub + FONT_SUB / 2) / FONT_SUB);
		CHECK(f.hd['A' - 0x21].cov != NULL);
		/* "A" stands on the baseline, 19 pixels down. */
		inked = 0;
		for (y = 0; y < f.height; y++)
			for (x = 0; x < f.cell_w; x++)
				if (font_pixel(&f, 'A' - 0x21, x, y)) {
					inked++;
					CHECK(y < 19);
				}
		CHECK(inked > 20);
		/* Widths keep fractions: ten letters are ten advances. */
		CHECK(text_width(&f, "AAAAAAAAAA") ==
		    (10 * a->advance_sub + FONT_SUB / 2) / FONT_SUB);
	}

	/* On a 2x canvas: ink on the screen and blended on the canvas. */
	if (fb_init(&fb, 960) == 0 &&
	    (rgb = malloc((size_t)1280 * 960 * 3)) != NULL) {
		palette_fixed(fb.pal);
		fb_clear(&fb, 215);		/* white */
		fb_glyph(&fb, &f, 'I' - 0x21, 100 * FONT_SUB, 100, 0);
		inked = 0;
		for (i = 0; i < sizeof(fb.px); i++)
			inked += fb.px[i] == 0;
		CHECK(inked > 10);
		fb_to_rgb(&fb, 256, rgb);
		n = 0;
		inked = 0;
		for (i = 0; i < (size_t)1280 * 960; i++) {
			if (rgb[3 * i] == 0)
				inked++;
			else if (rgb[3 * i] < 255)
				n++;		/* antialiased edge */
		}
		CHECK(inked > 40 && n > 0);
		free(rgb);
	}
	fb_free(&fb);
	font_free(&f);

	/* Drop capitals, in caps_char() order. */
	CHECK(font_load(packs, "font/caps", 960, &caps) == 0);
	CHECK(caps.count == CAPS_COUNT && caps.height == 34 &&
	    caps.glyphs[0].ink > 0);
	CHECK(caps_char(0) == 'A' && caps_index('a') == 0 &&
	    caps_index(caps_char(30)) == 30 && caps_char(CAPS_COUNT) == -1);
	font_free(&caps);
	packs_free(packs);

	CHECK(try_font(dir, good) == 0);
	CHECK(try_font(dir, "[font]\nheight = 25\n") == -1);
	CHECK(try_font(dir, "[font]\nfile = f.ttf\n") == -1);
	CHECK(try_font(dir, "[font]\nfile = f.ttf\nheight = 2.5\n") == -1);
	CHECK(try_font(dir, "[font]\nfile = f.ttf\nheight = 25\nsize = x\n")
	    == -1);
	CHECK(try_font(dir, "[font]\nfile = none.ttf\nheight = 25\n") == -1);
	CHECK(try_font(dir, "[font]\nfile = pack.ini\nheight = 25\n") == -1);
	remove_files(dir, files);
}

/*
 * 8x16 RGBA tile atlas: 4x4 tiles, two per row.  Frame 0 (rows 0-7):
 * red, green, blue, white; frame 1 (rows 8-15): all black.
 */
static const uint8_t png_tiles[] = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
	0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x10,
	0x08, 0x06, 0x00, 0x00, 0x00, 0x2b, 0x8a, 0x3e, 0x7d, 0x00, 0x00, 0x00,
	0x20, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
	0x1f, 0x19, 0x63, 0x81, 0x34, 0x57, 0x80, 0x26, 0x8f, 0x01, 0xe8, 0xa3,
	0x00, 0xe2, 0x10, 0x3c, 0x78, 0x24, 0x28, 0x00, 0x00, 0xb3, 0x23, 0xdf,
	0x21, 0x1e, 0xbc, 0xcb, 0x86, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
	0x44, 0xae, 0x42, 0x60, 0x82,
};

/* Load tiles/sea from "dir" after writing "desc" as its sheet. */
static int
try_tiles(const char *dir, const char *desc)
{
	struct packs *packs = packs_new();
	struct tileset ts;
	int rc;

	if (write_text(dir, "sea.tiles", desc) != 0 ||
	    packs_add_dir(packs, dir) != 0) {
		packs_free(packs);
		return (-2);
	}
	rc = tileset_load(packs, "tiles/sea", &ts);
	tileset_free(&ts);
	packs_free(packs);
	return (rc);
}

/*
 * Tile sheets: tiles in rows, animation frames below each other, indices
 * from the original tileset when there, else from the first frame.
 */
static void
test_tiles(void)
{
	static const char *const low_files[] = { "pack.ini", "pair.imb", NULL };
	static const char *const high_files[] = { "pack.ini", "atlas.png",
	    "sea.tiles", "pair.tiles", NULL };
	static const char sea[] = "[tiles]\nimage = atlas.png\ntile = 4\n"
	    "count = 4\nframes = 2\nperiod = 4\n";
	static uint8_t imb[IMB_LEN];
	static struct fb fb;
	char low[] = "/tmp/stmalo-test-XXXXXX";
	char high[] = "/tmp/stmalo-test-XXXXXX";
	struct packs *packs;
	struct tileset ts;
	struct image t;
	uint8_t *rgb;
	const uint8_t *p;

	if (mkdtemp(low) == NULL || mkdtemp(high) == NULL) {
		CHECK(!"mkdtemp");
		return;
	}
	make_imb(imb);
	CHECK(write_text(low, "pack.ini", "[assets]\ntiles/pair = pair.imb\n")
	    == 0);
	CHECK(write_bytes(low, "pair.imb", imb, sizeof(imb)) == 0);
	CHECK(write_text(high, "pack.ini", "[pack]\nart_height = 960\n"
	    "[assets]\ntiles/sea = sea.tiles\ntiles/pair = pair.tiles\n") == 0);
	CHECK(write_bytes(high, "atlas.png", png_tiles, sizeof(png_tiles)) ==
	    0);
	CHECK(write_text(high, "sea.tiles", sea) == 0);
	CHECK(write_text(high, "pair.tiles", "[tiles]\nimage = atlas.png\n"
	    "tile = 4\ncount = 2\n") == 0);

	/* Over the original: its indices and palette. */
	packs = packs_new();
	CHECK(packs_add_dir(packs, low) == 0);
	CHECK(packs_add_dir(packs, high) == 0);
	CHECK(tileset_load(packs, "tiles/pair", &ts) == 0);
	CHECK(ts.count == 2 && ts.tile_w == 2 && ts.rgba != NULL);
	CHECK(ts.pixels != NULL && ts.pixels[0] == 1 && ts.pixels[4] == 2);
	CHECK(ts.has_palette && ts.palette[3] == 255);
	tileset_free(&ts);

	/* Alone: indices from frame 0; the pass picks the frame. */
	CHECK(tileset_load(packs, "tiles/sea", &ts) == 0);
	CHECK(ts.count == 4 && ts.tile_w == 2 && ts.columns == 2 &&
	    ts.frames == 2);
	CHECK(ts.pixels != NULL && ts.pixels[0] == 180 && ts.pixels[4] == 30 &&
	    ts.pixels[12] == 215);
	tileset_tile(&ts, 1, 3, &t);		/* green, frame 0 */
	CHECK(t.w == 2 && t.pixels == ts.pixels + 4 && t.rgba_w == 4 &&
	    t.rgba_stride == 8);
	CHECK(t.rgba != NULL && t.rgba[0] == 0 && t.rgba[1] == 255);
	tileset_tile(&ts, 1, 4, &t);		/* frame 1: black */
	CHECK(t.rgba != NULL && t.rgba[1] == 0 && t.rgba[3] == 255);
	tileset_tile(&ts, 1, 8, &t);		/* back to frame 0 */
	CHECK(t.rgba != NULL && t.rgba[1] == 255);

	/* Drawn at 2x, a tile's art fills its 4x4 canvas pixels. */
	if (fb_init(&fb, 960) == 0 &&
	    (rgb = malloc((size_t)1280 * 960 * 3)) != NULL) {
		fb_clear(&fb, 0);
		tileset_tile(&ts, 2, 0, &t);	/* blue */
		fb_image(&fb, &t, 2, 0, 0);
		tileset_tile(&ts, 3, 0, &t);	/* white */
		fb_image(&fb, &t, 4, 0, 0);
		fb_to_rgb(&fb, 256, rgb);
		p = at(rgb, 1280, 7, 3);
		CHECK(p[0] == 0 && p[1] == 0 && p[2] == 255);
		p = at(rgb, 1280, 8, 3);
		CHECK(p[0] == 255 && p[1] == 255 && p[2] == 255);
		CHECK(fb.px[2] == 5 && fb.px[4] == 215);
		free(rgb);
	}
	fb_free(&fb);
	tileset_free(&ts);
	packs_free(packs);

	/* Errors: too short for its frames, no tile size, bad numbers. */
	CHECK(try_tiles(high, sea) == 0);
	CHECK(try_tiles(high, "[tiles]\nimage = atlas.png\ntile = 4\n"
	    "count = 4\nframes = 3\n") == -1);
	CHECK(try_tiles(high, "[tiles]\nimage = atlas.png\n") == -1);
	CHECK(try_tiles(high, "[tiles]\nimage = atlas.png\ntile = 9\n") == -1);
	CHECK(try_tiles(high, "[tiles]\nimage = atlas.png\ntile = 4\n"
	    "frames = 0\n") == -1);
	CHECK(try_tiles(high, "[tiles]\nimage = none.png\ntile = 4\n") == -1);

	remove_files(low, low_files);
	remove_files(high, high_files);
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
	test_png();
	test_pic();
	test_fb();
	test_sheets();
	test_fonts();
	test_tiles();
	if (failures > 0) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return (1);
	}
	printf("all format tests passed\n");
	return (0);
}
