/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_PACK_H
#define FLIBUSTIER_PACK_H

#include <stddef.h>
#include <stdint.h>

/*
 * Asset packs.  Game code requests assets by logical ID ("pic/town",
 * "spr/tavern", "text/eng/cstr", "music/title"), never by original file
 * name.  Packs are stacked: the most recently added pack has the highest
 * priority and the first pack providing an ID wins, so a pack may
 * override a single asset.
 *
 * Two kinds of packs exist:
 * - the original game data, either the GOG DATA.DAT image or a directory
 *   holding the extracted files, mapped to IDs by built-in rules;
 * - a directory with a "pack.ini" manifest mapping IDs to files.
 */

struct packs;

struct blob {
	uint8_t		*data;
	size_t		 len;
	char		 ext[8];	/* lowercase source extension: "pcx" */
	const char	*pack;		/* name of the pack that provided it */
	int		 art_height;	/* screen height its art is drawn for */
	int		 pack_index;	/* for packs_load_beside() */
	char		 file[256];	/* path in a directory pack, or "" */
};

struct packs	*packs_new(void);
void		 packs_free(struct packs *packs);

/* Both return 0 on success, -1 on error (see packs_error()). */
int		 packs_add_original(struct packs *packs, const char *path);
int		 packs_add_dir(struct packs *packs, const char *path);

/* Returns 0 and fills "out", or -1 if no pack provides "id". */
int		 packs_load(struct packs *packs, const char *id,
		    struct blob *out);
void		 blob_free(struct blob *blob);

/* Whether some pack provides "id" (it is loaded to find out). */
int		 packs_has(struct packs *packs, const char *id);
/* The same from the manifests alone, without reading a directory pack's
 * file: for large files such as recordings. */
int		 packs_provides(struct packs *packs, const char *id);

/*
 * The same, from the highest-priority pack whose file for "id" has
 * extension "ext" ("pcx"): for example the original picture behind a
 * replacement, for its palette.
 */
int		 packs_load_ext(struct packs *packs, const char *id,
		    const char *ext, struct blob *out);

/*
 * Load file "rel", relative to the directory of "from"'s file, from the
 * same directory pack: for example the image of a sprite sheet.  "rel"
 * may not climb out of the pack.
 */
int		 packs_load_beside(struct packs *packs, const struct blob *from,
		    const char *rel, struct blob *out);

/*
 * List every ID provided by at least one pack, sorted and unique.  On
 * success *ids is a malloc'ed array to release with packs_list_free().
 */
int		 packs_list(struct packs *packs, char ***ids, size_t *count);
void		 packs_list_free(char **ids, size_t count);

/* Packs in priority order, 0 is the highest. */
size_t		 packs_count(const struct packs *packs);
const char	*packs_name(const struct packs *packs, size_t i);
const char	*packs_license(const struct packs *packs, size_t i);

/*
 * The largest art height of all packs (480 for the original art): the
 * resolution to render at.
 */
int		 packs_art_height(const struct packs *packs);

const char	*packs_error(const struct packs *packs);

#endif
