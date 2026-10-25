/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdlib.h>
#include <string.h>

#include "image.h"

void
image_free(struct image *img)
{
	free(img->pixels);
	memset(img, 0, sizeof(*img));
}
