/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "credits.h"
#include "sim/json.h"

/*
 * A copy of credits line "s" for drawing: no line breaks, and the
 * menu and drop capital marks ("~" first, any "`") left out.  NULL with
 * a message in "err".
 */
static char *
credit_line(const char *s, char *err, size_t errlen)
{
	char *out, *d;

	if (strpbrk(s, "\r\n") != NULL) {
		snprintf(err, errlen, "a line break in \"%s\"", s);
		return (NULL);
	}
	if ((out = d = malloc(strlen(s) + 1)) == NULL) {
		snprintf(err, errlen, "out of memory");
		return (NULL);
	}
	if (*s == '~')
		s++;
	for (; *s != '\0'; s++)
		if (*s != '`')
			*d++ = *s;
	*d = '\0';
	return (out);
}

static int
hex_colour(const char *s, uint8_t rgb[3])
{
	unsigned v[3];
	int k;

	if (strlen(s) != 7 || s[0] != '#' || strspn(s + 1,
	    "0123456789abcdefABCDEF") != 6 ||
	    sscanf(s + 1, "%2x%2x%2x", &v[0], &v[1], &v[2]) != 3)
		return (-1);
	for (k = 0; k < 3; k++)
		rgb[k] = (uint8_t)v[k];
	return (0);
}

static int
decode_block(const struct json *j, size_t i, struct credit_block *b,
    char *err, size_t errlen)
{
	const struct json *h, *lines, *needs;
	char why[96];
	size_t k;

	h = json_get(j, "heading");
	lines = json_get(j, "lines");
	needs = json_get(j, "needs");
	if (j->type != JSON_OBJECT || (h != NULL && h->type != JSON_STRING) ||
	    (lines != NULL && lines->type != JSON_ARRAY) ||
	    (needs != NULL && needs->type != JSON_STRING)) {
		snprintf(err, errlen, "block %zu: \"heading\" and \"needs\" "
		    "must be strings and \"lines\" an array", i);
		return (-1);
	}
	if ((h == NULL || h->str[0] == '\0') &&
	    (lines == NULL || lines->count == 0)) {
		snprintf(err, errlen, "block %zu is empty", i);
		return (-1);
	}
	if (lines != NULL && lines->count > CREDITS_MAX_LINES) {
		snprintf(err, errlen, "block %zu: more than %d lines", i,
		    CREDITS_MAX_LINES);
		return (-1);
	}
	if ((b->heading = credit_line(h != NULL ? h->str : "", why,
	    sizeof(why))) == NULL)
		goto fail;
	if (needs != NULL && (b->needs = credit_line(needs->str, why,
	    sizeof(why))) == NULL)
		goto fail;
	if (lines == NULL || lines->count == 0)
		return (0);
	if ((b->lines = calloc(lines->count, sizeof(*b->lines))) == NULL) {
		snprintf(why, sizeof(why), "out of memory");
		goto fail;
	}
	for (k = 0; k < lines->count; k++) {
		if (lines->items[k]->type != JSON_STRING) {
			snprintf(why, sizeof(why), "line %zu is not a string",
			    k);
			goto fail;
		}
		if ((b->lines[k] = credit_line(lines->items[k]->str, why,
		    sizeof(why))) == NULL)
			goto fail;
		b->nlines++;
	}
	return (0);
fail:
	snprintf(err, errlen, "block %zu: %s", i, why);
	return (-1);
}

int
credits_decode(const uint8_t *data, size_t len, struct credits *out,
    char *err, size_t errlen)
{
	const struct json *blocks, *ink;
	struct json *root;
	char *text;
	size_t i;

	memset(out, 0, sizeof(*out));
	if ((text = malloc(len + 1)) == NULL) {
		snprintf(err, errlen, "out of memory");
		return (-1);
	}
	memcpy(text, data, len);
	text[len] = '\0';
	root = json_parse(text, err, errlen);
	free(text);
	if (root == NULL)
		return (-1);
	if ((blocks = json_get(root, "blocks")) == NULL ||
	    blocks->type != JSON_ARRAY || blocks->count < 1 ||
	    blocks->count > CREDITS_MAX_BLOCKS) {
		snprintf(err, errlen, "\"blocks\" must be an array of 1 to %d",
		    CREDITS_MAX_BLOCKS);
		goto fail;
	}
	if ((ink = json_get(root, "ink")) != NULL &&
	    (ink->type != JSON_STRING || hex_colour(ink->str, out->ink) != 0)) {
		snprintf(err, errlen, "\"ink\" must be \"#RRGGBB\"");
		goto fail;
	}
	if ((out->blocks = calloc(blocks->count, sizeof(*out->blocks))) ==
	    NULL) {
		snprintf(err, errlen, "out of memory");
		goto fail;
	}
	for (i = 0; i < blocks->count; i++) {
		out->nblocks++;
		if (decode_block(blocks->items[i], i, &out->blocks[i], err,
		    errlen) != 0)
			goto fail;
	}
	json_free(root);
	return (0);
fail:
	json_free(root);
	credits_free(out);
	return (-1);
}

static void
block_free(struct credit_block *b)
{
	size_t k;

	free(b->heading);
	for (k = 0; k < b->nlines; k++)
		free(b->lines[k]);
	free(b->lines);
	free(b->needs);
}

void
credits_free(struct credits *c)
{
	size_t i;

	for (i = 0; i < c->nblocks; i++)
		block_free(&c->blocks[i]);
	free(c->blocks);
	memset(c, 0, sizeof(*c));
}

void
credits_drop(struct credits *c, size_t i)
{
	if (i >= c->nblocks)
		return;
	block_free(&c->blocks[i]);
	memmove(&c->blocks[i], &c->blocks[i + 1],
	    (c->nblocks - i - 1) * sizeof(*c->blocks));
	c->nblocks--;
}
