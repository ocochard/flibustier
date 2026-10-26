/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <new>

#include "external/ymfm/ymfm_opl.h"
#include "opl3.h"

struct opl3 {
	ymfm::ymfm_interface	intf;
	ymfm::ymf262		chip;

	opl3() : chip(intf) { chip.reset(); }
};

static int16_t
clip(int32_t v)
{
	return ((int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v));
}

extern "C" struct opl3 *
opl3_new(void)
{
	return (new (std::nothrow) opl3());
}

extern "C" void
opl3_free(struct opl3 *o)
{
	delete o;
}

extern "C" void
opl3_write(struct opl3 *o, unsigned reg, unsigned val)
{
	if (reg & 0x100)
		o->chip.write_address_hi((uint8_t)reg);
	else
		o->chip.write_address((uint8_t)reg);
	o->chip.write_data((uint8_t)val);
}

extern "C" void
opl3_generate(struct opl3 *o, int16_t *lr, size_t frames)
{
	ymfm::ymf262::output_data out;
	size_t i;

	/* Outputs A and B are the left and right channels. */
	for (i = 0; i < frames; i++) {
		o->chip.generate(&out);
		lr[2 * i] = clip(out.data[0]);
		lr[2 * i + 1] = clip(out.data[1]);
	}
}
