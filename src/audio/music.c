/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdlib.h>
#include <string.h>

#include "music.h"
#include "opl3.h"

/*
 * The driver's tables in its data segment (PSOUND "Pirates! Pro
 * 7-13-93"): see asset-formats.md, "The FM music engine".
 */
#define T_FREQ		0xae66	/* 12 F-numbers, words */
#define T_CHIPS		0xae82	/* per voice: bank mask, 1, 2 or 3 */
#define T_CHANNEL	0xae8c	/* per voice: channel in its bank(s) */
#define T_OPS		0xae96	/* per voice: 4 operator offsets */
#define T_PAN		0xade6	/* level added by pan, per side */
#define T_INST		0xaeba	/* 64-byte instruments */
#define T_VARS		0xb882	/* the scripts' variables */

#define SILENCE		0xadd6	/* a stream that ends the voice */

#define VOICES		9
#define TICK_DIV	0x4c90	/* the timer's divisor: 60.9 Hz */
#define NOISE_DIV	0x0f88	/* while percussion plays: 300 Hz */
#define NOISE_STEPS	5	/* fast ticks per music tick */
#define PIT_HZ		1193182u
#define DATA_LEN	0x10000

struct voice {
	uint8_t		dur;		/* ticks left; 0 idle */
	int8_t		slide;		/* F-number change per tick */
	int8_t		vstep, pstep;	/* volume and pan ramp steps */
	uint8_t		note, inst;
	int8_t		vel;
	uint8_t		gate, rel;	/* release early; ticks to key-off */
	uint8_t		vcnt, vrate, pcnt, prate;
	uint8_t		pan;
	int8_t		volume;
	uint8_t		algo;		/* 4-operator connection, 0-3 */
	int8_t		lvl[4];		/* operator levels, 63 loud */
	uint16_t	ptr, aux, loop1, loop2, start, ret;
	int16_t		rep1, rep2;
	uint8_t		perc;		/* percussion ticks left */
	uint16_t	noise_mask, noise_base, noise_step;
	int8_t		detune, transpose;
	uint8_t		fading, relfix;
};

struct music {
	struct opl3	*chip;
	uint8_t		*mem;		/* the driver's data, scripts alter */
	struct fmmusic	 drv;		/* streams per command */
	struct voice	 v[VOICES];
	uint8_t		 shadow[0x200];
	uint16_t	 seed;
	uint32_t	 acc;		/* timer phase, PIT counts */
	int		 vi;		/* the voice being stepped */
	int		 noisy;		/* the fast timer, for percussion */
	int		 steps;		/* fast ticks to the next music tick */
	int		 fading;	/* the fade runs */
};

static unsigned
byte(const struct music *m, uint32_t off)
{
	return (m->mem[off & 0xffff]);
}

static unsigned
word(const struct music *m, uint32_t off)
{
	return (byte(m, off) | byte(m, off + 1) << 8);
}

static uint8_t *
var(struct music *m, int8_t i)
{
	return (&m->mem[(T_VARS + i) & 0xffff]);
}

/* A register write to the voice's bank(s) (mask: 1 first, 2 second). */
static void
reg(struct music *m, unsigned mask, unsigned r, unsigned val)
{
	val &= 0xff;
	r &= 0xff;
	if (mask & 1) {
		m->shadow[r] = (uint8_t)val;
		opl3_write(m->chip, r, val);
	}
	if (mask & 2) {
		m->shadow[0x100 + r] = (uint8_t)val;
		opl3_write(m->chip, 0x100 + r, val);
	}
}

static unsigned
chips(const struct music *m)
{
	return (byte(m, T_CHIPS + m->vi));
}

static unsigned
channel(const struct music *m)
{
	return (byte(m, T_CHANNEL + m->vi));
}

/* The shadow the driver reads back: the second bank's when it writes
 * there. */
static unsigned
readback(const struct music *m, unsigned r)
{
	return (m->shadow[((chips(m) >> 1) << 8) + (r & 0xff)]);
}

static unsigned
op(const struct music *m, int k)
{
	return (byte(m, T_OPS + 4 * m->vi + k));
}

static uint32_t
inst(const struct voice *v)
{
	return (T_INST + 64u * v->inst);
}

static unsigned
clamp63(int x)
{
	int8_t b = (int8_t)x;

	return (b < 0 ? 0 : b > 63 ? 63 : (unsigned)b);
}

static unsigned
rnd(struct music *m, unsigned range)
{
	m->seed = (uint16_t)(m->seed + 0x9248);
	m->seed = (uint16_t)(m->seed >> 3 | m->seed << 13);
	return (range ? (m->seed & 0x7fff) % range : 0);
}

/* The levels (VA 0xe26 of the driver): carriers follow the note. */
static void
levels(struct music *m, struct voice *v)
{
	uint32_t in = inst(v);
	int8_t a = (int8_t)(0x7e - (uint8_t)v->vel - v->volume);
	unsigned ksl;
	int k, side;

	if (m->vi < 6) {
		static const int ksl_at[4] = { 0x07, 0x15, 0x23, 0x31 };

		/* Centred, heard on both sides: 6 quieter. */
		if ((int8_t)v->pan > 0x2a && (int8_t)v->pan < 0x55)
			a = (int8_t)(a + 6);
		for (k = 3; k >= 0; k--) {
			if ((k == 1 && v->algo != 1) ||
			    (k == 0 && !(v->algo & 2)) ||
			    (k == 2 && v->algo != 3))
				continue;
			ksl = (byte(m, in + ksl_at[k]) << 6) & 0xff;
			reg(m, chips(m), 0x40 + op(m, k),
			    clamp63(a - v->lvl[k]) | ksl);
		}
		return;
	}
	/* Voices 6-8: both banks, panned by level. */
	for (k = 1; k >= 0; k--) {
		if (k == 0 && v->algo != 0)
			break;
		ksl = (byte(m, in + (k ? 0x15 : 0x07)) << 6) & 0xff;
		for (side = 0; side < 2; side++)
			reg(m, 1u << side, 0x40 + op(m, k), clamp63(a -
			    v->lvl[k] + (int)byte(m, T_PAN + (side ?
			    0x7f - (int8_t)v->pan : (int8_t)v->pan))) | ksl);
	}
}

/* The stereo bits of a 4-operator voice (VA 0x12ba). */
static unsigned
stereo(const struct voice *v)
{
	return (v->pan > 0x2a && v->pan < 0x55 ? 0x30 : v->pan < 0x2b ?
	    0x10 : 0x20);
}

static void
pan(struct music *m, struct voice *v)
{
	unsigned c = 0xc0 + channel(m);

	if (m->vi >= 6)
		return;
	reg(m, chips(m), c, stereo(v) | (readback(m, c) & 0x0f));
	reg(m, chips(m), c + 3, stereo(v) | (readback(m, c + 3) & 0x0f));
}

static void
pitch(struct music *m, struct voice *v)
{
	unsigned n = (uint8_t)(v->note + v->transpose), ch = channel(m);
	unsigned f = (word(m, T_FREQ + 2 * (n % 12)) + v->detune) & 0xffff;

	reg(m, chips(m), 0xa0 + ch, f);
	reg(m, chips(m), 0xb0 + ch, (readback(m, 0xb0 + ch) & 0x20) |
	    ((f >> 8) | (n / 12) << 2));
}

static void
key_off(struct music *m)
{
	unsigned r = 0xb0 + channel(m);

	reg(m, chips(m), r, readback(m, r) & 0xdf);
}

static void
key_on(struct music *m, struct voice *v)
{
	unsigned r = 0xb0 + channel(m);

	levels(m, v);
	if (v->perc != 0) {
		/* Percussion: noise from the fast timer (VA 0x484). */
		if (!m->noisy) {
			m->noisy = 1;
			m->steps = NOISE_STEPS;
		}
		return;
	}
	pitch(m, v);
	reg(m, chips(m), r, readback(m, r) | 0x20);
}

static void
slide(struct music *m, struct voice *v)
{
	unsigned ch = channel(m), b = readback(m, 0xb0 + ch);
	unsigned f = (((b & 0x1f) << 8) + readback(m, 0xa0 + ch) +
	    v->slide) & 0xffff;

	reg(m, chips(m), 0xa0 + ch, f);
	reg(m, chips(m), 0xb0 + ch, (b & 0x20) | (f >> 8));
}

/* An instrument (VA 0x1616): four operators of 14 bytes, two for
 * voices 6-8. */
static void
instrument(struct music *m, struct voice *v)
{
	uint32_t in = inst(v), blk;
	int nops = m->vi < 6 ? 4 : 2, k;
	unsigned c = 0xc0 + channel(m), fb;

	for (k = 0; k < nops; k++) {
		reg(m, chips(m), 0x80 + op(m, k), 0xff);
		reg(m, chips(m), 0x40 + op(m, k), 0x3f);
	}
	v->algo = (uint8_t)byte(m, in + 0xd);
	fb = (byte(m, in + 0xa) << 1) & 0xff;
	if (m->vi < 6) {
		reg(m, chips(m), c, stereo(v) | fb | (v->algo >> 1));
		reg(m, chips(m), c + 3, stereo(v) | fb | (v->algo & 1));
	} else {
		reg(m, 1, c, fb | (v->algo & 1) | 0x10);
		reg(m, 2, c, fb | (v->algo & 1) | 0x20);
	}
	for (k = 0; k < nops; k++) {
		blk = in + 14u * k;
		v->lvl[k] = (int8_t)byte(m, blk + 6);
		reg(m, chips(m), 0x20 + op(m, k), byte(m, blk + 9) |
		    byte(m, blk + 5) << 4 | byte(m, blk + 4) << 5 |
		    byte(m, blk + 0xc) << 6 | byte(m, blk + 0xb) << 7);
		reg(m, chips(m), 0x60 + op(m, k), byte(m, blk) << 4 |
		    byte(m, blk + 1));
		reg(m, chips(m), 0x80 + op(m, k), byte(m, blk + 2) << 4 |
		    byte(m, blk + 3));
		reg(m, chips(m), 0xe0 + op(m, k), byte(m, blk + 8));
	}
	/* Fixed levels for the modulators; carriers wait for the note. */
	reg(m, chips(m), 0x40 + op(m, 0), (byte(m, in + 7) << 6 & 0xff) |
	    (v->algo & 2 ? 0x3f : 0x3f - byte(m, in + 6)));
	reg(m, chips(m), 0x40 + op(m, 1), (byte(m, in + 0x15) << 6 & 0xff) |
	    (v->algo == 1 ? 0x3f : 0x3f - byte(m, in + 0x14)));
	if (nops > 2) {
		reg(m, chips(m), 0x40 + op(m, 3),
		    (byte(m, in + 0x31) << 6 & 0xff) | 0x3f);
		reg(m, chips(m), 0x40 + op(m, 2),
		    (byte(m, in + 0x23) << 6 & 0xff) |
		    (v->algo == 3 ? 0x3f : 0x3f - byte(m, in + 0x22)));
	}
	v->perc = (uint8_t)byte(m, in + 0x38);
	v->noise_mask = (uint16_t)word(m, in + 0x3a);
	v->noise_base = (uint16_t)word(m, in + 0x3c);
	v->noise_step = (uint16_t)word(m, in + 0x3e);
}

/* Commands until a note; 1 when the voice was stopped. */
static int
events(struct music *m, struct voice *v)
{
	unsigned p, b, n;
	int guard;

	for (guard = 0; guard < 512; guard++) {
		p = v->ptr;
		b = byte(m, p);
		if (!((b & 0x80) && (int8_t)b > (int8_t)0xbd)) {
			v->note = (uint8_t)b;
			v->dur = (uint8_t)byte(m, p + 1);
			v->ptr = (uint16_t)(p + 2);
			if (v->note == 0 || v->dur == 0)
				key_off(m);
			else {
				v->rel = v->relfix ? v->relfix :
				    (uint8_t)(v->dur - v->gate);
				key_on(m, v);
			}
			return (0);
		}
		switch (b) {
		case 0xbe: case 0xc0: case 0xc1: case 0xc3:
			v->ptr = (uint16_t)(p + 2);	/* globals: unused */
			break;
		case 0xbf: case 0xc4:
			v->ptr = (uint16_t)(p + 3);
			break;
		case 0xc2:
			v->ptr = (uint16_t)(p + 4);
			break;
		case 0xd4:		/* goto when var == value */
			if (*var(m, (int8_t)byte(m, p + 1)) ==
			    (int8_t)byte(m, p + 2))
				v->ptr = (uint16_t)word(m, p + 3);
			else
				v->ptr = (uint16_t)(p + 5);
			break;
		case 0xdb:		/* var a %= var b */
			n = *var(m, (int8_t)byte(m, p + 2));
			if (n != 0)
				*var(m, (int8_t)byte(m, p + 1)) %= n;
			v->ptr = (uint16_t)(p + 3);
			break;
		case 0xe5:
			(*var(m, (int8_t)byte(m, p + 1)))--;
			v->ptr = (uint16_t)(p + 2);
			break;
		case 0xe6:
			(*var(m, (int8_t)byte(m, p + 1)))++;
			v->ptr = (uint16_t)(p + 2);
			break;
		case 0xe9:
			*var(m, (int8_t)byte(m, p + 1)) =
			    (uint8_t)byte(m, p + 2);
			v->ptr = (uint16_t)(p + 3);
			break;
		case 0xeb:		/* poke a random value in a..b */
			n = byte(m, p + 2) - byte(m, p + 1) + 1;
			m->mem[(p + 4 + (int8_t)byte(m, p + 3)) & 0xffff] =
			    (uint8_t)(rnd(m, n & 0xffff) + byte(m, p + 1));
			v->ptr = (uint16_t)(p + 4);
			break;
		case 0xed:		/* skip a data block */
			v->ptr = (uint16_t)(p + 3 + (int8_t)byte(m, p + 1));
			break;
		case 0xee:
			v->transpose = (int8_t)byte(m, p + 1);
			v->ptr = (uint16_t)(p + 2);
			break;
		case 0xef:
			v->prate = (uint8_t)byte(m, p + 1);
			v->pstep = (int8_t)byte(m, p + 2);
			v->pcnt = 1;
			v->ptr = (uint16_t)(p + 3);
			break;
		case 0xf0:
			v->pan = (uint8_t)byte(m, p + 1);
			v->ptr = (uint16_t)(p + 2);
			pan(m, v);
			break;
		case 0xf1: {
			int8_t vol = (int8_t)(((int8_t)byte(m, p + 1) >> 1) -
			    50);

			if (!v->fading || vol < v->volume)
				v->volume = vol;
			v->ptr = (uint16_t)(p + 2);
			break;
		}
		case 0xf2:
			v->detune = (int8_t)byte(m, p + 1);
			v->ptr = (uint16_t)(p + 2);
			break;
		case 0xf3:
			if (!v->fading) {
				v->vrate = (uint8_t)byte(m, p + 1);
				v->vstep = (int8_t)byte(m, p + 2);
				v->vcnt = 1;
			}
			v->ptr = (uint16_t)(p + 3);
			break;
		case 0xf4:
			v->vel = (int8_t)((int8_t)byte(m, p + 1) >> 1);
			v->ptr = (uint16_t)(p + 2);
			break;
		case 0xf5:
			v->slide = (int8_t)byte(m, p + 1);
			v->ptr = (uint16_t)(p + 2);
			break;
		case 0xf6:
			v->relfix = (uint8_t)byte(m, p + 1);
			v->gate = 0;
			v->ptr = (uint16_t)(p + 2);
			break;
		case 0xf7:
			v->gate = (uint8_t)byte(m, p + 1);
			v->relfix = 0;
			v->ptr = (uint16_t)(p + 2);
			break;
		case 0xf8:
			v->inst = (uint8_t)byte(m, p + 1);
			v->ptr = (uint16_t)(p + 2);
			instrument(m, v);
			break;
		case 0xf9:		/* return */
			if (v->ret) {
				v->ptr = v->ret;
				v->ret = 0;
			} else
				v->ptr = (uint16_t)(p + 1);
			break;
		case 0xfa:		/* call */
			v->ret = (uint16_t)(p + 3);
			v->ptr = (uint16_t)word(m, p + 1);
			break;
		case 0xfb:		/* goto */
			v->ptr = (uint16_t)word(m, p + 1);
			break;
		case 0xfc:		/* a new start */
			v->aux = v->ptr = v->loop1 = v->loop2 = v->start =
			    (uint16_t)word(m, p + 1);
			break;
		case 0xfd:		/* back to the start */
			v->aux = v->ptr = v->loop1 = v->loop2 = v->start;
			v->slide = v->vstep = v->pstep = 0;
			v->detune = v->volume = v->vel = 0;
			v->vcnt = v->pcnt = 0;
			v->rep1 = v->rep2 = 0;
			v->gate = 0;
			break;
		case 0xfe:		/* outer repeat */
			if (v->rep2 == 0) {
				n = byte(m, p + 1);
				if (n == 0) {
					v->ptr = v->loop2 = v->loop1 =
					    (uint16_t)(p + 2);
					v->rep1 = v->rep2 = 0;
				} else {
					v->rep2 = (int8_t)n;
					v->ptr = v->loop1 = v->loop2;
				}
			} else if (--v->rep2 != 0)
				v->ptr = v->loop1 = v->loop2;
			else
				v->ptr = v->loop2 = v->loop1 =
				    (uint16_t)(p + 2);
			break;
		case 0xff:		/* inner repeat */
			if (v->rep1 == 0) {
				n = byte(m, p + 1);
				if (n == 0) {
					v->ptr = v->loop1 = (uint16_t)(p + 2);
					v->rep1 = 0;
				} else {
					v->rep1 = (int8_t)n;
					v->ptr = v->loop1;
				}
			} else if (--v->rep1 != 0)
				v->ptr = v->loop1;
			else
				v->ptr = v->loop1 = (uint16_t)(p + 2);
			break;
		default:		/* not used by the tunes: stop */
			key_off(m);
			v->dur = 0;
			return (1);
		}
	}
	v->dur = 0;			/* a script looping without notes */
	return (1);
}

/* One voice's tick (VA 0x1ca0 and on of the driver). */
static void
voice_tick(struct music *m, struct voice *v)
{
	int changed = 0;

	if (v->dur == 0)
		return;
	if (v->rel != 0 && --v->rel == 0)
		key_off(m);
	if (--v->dur == 0 && events(m, v))
		return;
	if (v->slide != 0)
		slide(m, v);
	if (v->vcnt != 0 || v->pcnt != 0) {
		if (--v->vcnt == 0) {
			v->vcnt = v->vrate;
			if (v->vstep != 0) {
				v->volume = (int8_t)(v->volume + v->vstep);
				changed = 1;
			}
		}
		if (--v->pcnt == 0) {
			v->pcnt = v->prate;
			if (v->pstep != 0) {
				v->pan = (uint8_t)(v->pan + v->pstep);
				pan(m, v);
				changed = 1;
			}
		}
	}
	if (changed)
		levels(m, v);
}

/* A frequency with key-on, as the percussion writes it (VA 0x30b). */
static void
noise_write(struct music *m, unsigned f)
{
	unsigned ch = channel(m);

	reg(m, chips(m), 0xa0 + ch, f);
	reg(m, chips(m), 0xb0 + ch, (f >> 8) | 0x20);
}

/* A fast tick: each percussion voice at a random frequency (VA
 * 0x401). */
static void
noise(struct music *m)
{
	struct voice *v;

	for (m->vi = 0; m->vi < VOICES; m->vi++) {
		v = &m->v[m->vi];
		if (v->perc == 0)
			continue;
		(void)rnd(m, 0);
		noise_write(m, ((m->seed & v->noise_mask) + v->noise_base) &
		    0xffff);
	}
}

/*
 * The fade (VA 0x784): a fading voice's volume falls a step every 4
 * ticks; at -40 it plays the silent stream.
 */
static void
fade(struct music *m)
{
	struct voice *v;
	int i;

	if (!m->fading)
		return;
	for (i = 0; i < VOICES; i++) {
		v = &m->v[i];
		if (v->dur == 0 || !v->fading)
			continue;
		if (v->volume == (int8_t)0xd8) {
			v->ptr = SILENCE;
			v->fading = 0;
			continue;
		}
		v->vstep = -1;
		v->vrate = 4;
		if (v->vcnt == 0)
			v->vcnt = 1;
	}
}

/* A music tick (VA 0x383): the voices, the fade, the percussion's
 * sweep. */
static void
tick(struct music *m)
{
	struct voice *v;
	int any = 0;

	for (m->vi = 0; m->vi < VOICES; m->vi++)
		voice_tick(m, &m->v[m->vi]);
	fade(m);
	for (m->vi = 0; m->vi < VOICES; m->vi++) {
		v = &m->v[m->vi];
		if (v->perc == 0)
			continue;
		any = 1;
		v->noise_base = (uint16_t)(v->noise_base + v->noise_step);
		if (--v->perc == 0)
			noise_write(m, v->noise_base);
	}
	if (!any)
		m->noisy = 0;		/* back to the normal timer */
}

static void
voice_start(struct music *m, int i, uint16_t stream)
{
	struct voice *v = &m->v[i];

	memset(v, 0, sizeof(*v));
	v->aux = v->ptr = v->loop1 = v->loop2 = v->start = stream;
	v->pan = 0x40;
	v->dur = 1;
}

/* Stop with a fade (VA 0x72d): the voices playing fade out, then end
 * on the silent stream. */
void
music_stop(struct music *m)
{
	int i;

	for (i = 0; i < VOICES; i++)
		if (m->v[i].dur != 0) {
			m->v[i].fading = 0xff;
			m->v[i].start = SILENCE;
		}
	m->fading = 1;
}

void
music_command(struct music *m, int cmd)
{
	const struct fmmusic *d = &m->drv;
	int k, i, want;

	if (cmd < 0 || cmd >= FMMUSIC_COMMANDS || d->nstreams[cmd] == 0)
		return;
	if (d->voice[cmd][0] != FMMUSIC_EFFECT)
		music_stop(m);
	for (k = 0; k < d->nstreams[cmd]; k++) {
		want = d->voice[cmd][k];
		if (want >= 0)
			i = want;
		else {
			int lo = want == FMMUSIC_EFFECT ? 6 : 0;

			/* A free voice, else a fading one (VA 0x541). */
			for (i = lo; i < lo + (lo ? 3 : 6) && m->v[i].dur; i++)
				;
			if (i == lo + (lo ? 3 : 6))
				for (i = lo; i < lo + (lo ? 3 : 6) &&
				    m->v[i].fading != 0xff; i++)
					;
			if (i == lo + (lo ? 3 : 6))
				continue;
		}
		m->vi = i;
		key_off(m);
		voice_start(m, i, d->stream[cmd][k]);
	}
}

struct music *
music_new(const struct fmmusic *d)
{
	struct music *m;
	unsigned r;

	if ((m = calloc(1, sizeof(*m))) == NULL ||
	    (m->mem = malloc(DATA_LEN)) == NULL ||
	    (m->chip = opl3_new()) == NULL) {
		music_free(m);
		return (NULL);
	}
	memcpy(m->mem, d->data, DATA_LEN);
	memcpy(m->drv.nstreams, d->nstreams, sizeof(d->nstreams));
	memcpy(m->drv.stream, d->stream, sizeof(d->stream));
	memcpy(m->drv.voice, d->voice, sizeof(d->voice));
	/* OPL3 mode, the six 4-operator pairs, deep vibrato and tremolo,
	 * every operator silent (VA 0xe8, 0x1ad). */
	reg(m, 2, 0x05, 0x01);
	reg(m, 2, 0x04, 0x3f);
	reg(m, 3, 0xbd, 0xc0);
	for (r = 0x40; r < 0x56; r++)
		reg(m, 3, r, 0x3f);
	return (m);
}

void
music_free(struct music *m)
{
	if (m == NULL)
		return;
	if (m->chip != NULL)
		opl3_free(m->chip);
	free(m->mem);
	free(m);
}

void
music_render(struct music *m, int16_t *lr, size_t frames)
{
	/* A tick every TICK_DIV PIT counts; a sample every PIT_HZ /
	 * MUSIC_RATE of them. */
	uint32_t div;

	while (frames-- > 0) {
		m->acc += PIT_HZ;
		for (;;) {
			div = m->noisy ? NOISE_DIV : TICK_DIV;
			if (m->acc < div * MUSIC_RATE)
				break;
			m->acc -= div * MUSIC_RATE;
			if (!m->noisy)
				tick(m);
			else {
				noise(m);
				if (--m->steps == 0) {
					m->steps = NOISE_STEPS;
					tick(m);
				}
			}
		}
		opl3_generate(m->chip, lr, 1);
		lr += 2;
	}
}

int
music_active(const struct music *m)
{
	int i, n = 0;

	for (i = 0; i < VOICES; i++)
		n += m->v[i].dur != 0;
	return (n);
}
