/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_PROTO_H
#define FLIBUSTIER_PROTO_H

#include "json.h"
#include "sim.h"

/*
 * Line protocol for bots, AI agents and tests: one JSON object per line
 * in, one per line out.  See aidd_docs/memory/internal/sim-protocol.md.
 */

/* Provides the start data of an era (0..5) and a scenario index. */
typedef int (*sim_loader)(void *arg, int era, int scenario,
    struct era_towns *towns, struct scenario *scn);

/* Provides the world of an era, for sailing; may be NULL. */
typedef int (*world_loader)(void *arg, int era, struct world *w);

struct session {
	struct sim	 sim;
	int		 started;
	sim_loader	 load;
	world_loader	 load_world;
	void		*load_arg;
	struct world	*world;		/* owned; the era of sim.world */
	struct daughter	 daughter;	/* the governor's, met */
	int		 conquest;	/* town + 1 whose sack allows a governor */
};

/* Release what the session allocated. */
void	session_free(struct session *ss);

/*
 * Handle one command line, appending the response (no newline) to "out".
 * Returns 1 when the command asks to quit, 0 otherwise.
 */
int	proto_handle(struct session *ss, const char *line, struct jbuf *out);

/*
 * Full state as a JSON object, and back.  sim_from_json returns 0 or -1;
 * it leaves s->world NULL.
 */
void	sim_to_json(const struct sim *s, struct jbuf *out);
int	sim_from_json(struct sim *s, const struct json *j, char *err,
	    size_t errlen);

#endif
