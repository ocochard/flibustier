/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef STMALO_SAVE_H
#define STMALO_SAVE_H

#include <stddef.h>

#include "sim.h"

/*
 * Saved games: one JSON file per slot, holding a description (shown in
 * the slot list) and the full simulation state.  See sim-protocol.md.
 */

#define SAVE_SLOTS	10
#define SAVE_DESC_LEN	80

/* All return 0, or -1 with a message in "err". */
int	save_write(const char *path, const struct sim *s, const char *desc,
	    char *err, size_t errlen);
/* "s" may be NULL to read only the description. */
int	save_read(const char *path, struct sim *s, char *desc, size_t desclen,
	    char *err, size_t errlen);

/* "<dir>/save<slot>.json" */
void	save_slot_path(const char *dir, int slot, char *out, size_t len);

/*
 * The save directory: $STMALO_SAVES, else $XDG_DATA_HOME/stmalo/saves,
 * else $HOME/.local/share/stmalo/saves.  Created when missing.
 */
int	save_dir(char *out, size_t len, char *err, size_t errlen);

#endif
