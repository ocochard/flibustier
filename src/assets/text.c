/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#include <stdlib.h>
#include <string.h>

#include "text.h"

/* Unicode code points of Mac Roman bytes 0x80..0xFF. */
static const uint16_t macroman[128] = {
	0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
	0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
	0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
	0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
	0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
	0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
	0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
	0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
	0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
	0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
	0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
	0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
	0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
	0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
	0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
	0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

static unsigned
le16(const uint8_t *p)
{
	return ((unsigned)p[0] | (unsigned)p[1] << 8);
}

char *
macroman_to_utf8(const uint8_t *s, size_t len)
{
	char *out, *p;
	size_t i;
	unsigned c;

	/* Every Mac Roman byte needs at most 3 UTF-8 bytes. */
	if ((out = malloc(len * 3 + 1)) == NULL)
		return (NULL);
	for (p = out, i = 0; i < len; i++) {
		c = s[i] < 0x80 ? s[i] : macroman[s[i] - 0x80];
		if (c < 0x80)
			*p++ = (char)c;
		else if (c < 0x800) {
			*p++ = (char)(0xC0 | c >> 6);
			*p++ = (char)(0x80 | (c & 0x3F));
		} else {
			*p++ = (char)(0xE0 | c >> 12);
			*p++ = (char)(0x80 | (c >> 6 & 0x3F));
			*p++ = (char)(0x80 | (c & 0x3F));
		}
	}
	*p = '\0';
	return (out);
}

int
macroman_unicode(int c)
{
	return (c < 0x80 ? c : macroman[(c - 0x80) & 0x7F]);
}

int
utf8_next_macroman(const char **s)
{
	const unsigned char *p = (const unsigned char *)*s;
	unsigned c, n, i;

	if (*p < 0x80) {
		*s += 1;
		return (*p);
	}
	if ((*p & 0xE0) == 0xC0) {
		c = *p & 0x1F;
		n = 1;
	} else if ((*p & 0xF0) == 0xE0) {
		c = *p & 0x0F;
		n = 2;
	} else {
		*s += 1;	/* invalid or 4-byte: no Mac Roman glyph */
		return ('?');
	}
	for (i = 1; i <= n; i++) {
		if ((p[i] & 0xC0) != 0x80) {
			*s += i;
			return ('?');
		}
		c = c << 6 | (p[i] & 0x3F);
	}
	*s += n + 1;
	for (i = 0; i < 128; i++)
		if (macroman[i] == c)
			return ((int)(0x80 + i));
	return ('?');
}

void
strtab_free(struct strtab *tab)
{
	size_t i;

	for (i = 0; i < tab->count; i++)
		free(tab->str[i]);
	free(tab->str);
	memset(tab, 0, sizeof(*tab));
}

void
strgroups_free(struct strgroups *groups)
{
	size_t i;

	for (i = 0; i < groups->count; i++)
		strtab_free(&groups->groups[i]);
	free(groups->groups);
	free(groups->declared);
	memset(groups, 0, sizeof(*groups));
}

/*
 * Both formats start with a u16 offset table; entry i spans from its
 * offset to the next one (or to the end of the file).  N = offset[0] / 2.
 */
static int
span_table(const uint8_t *data, size_t len, size_t *n)
{
	size_t i, prev, cur;

	if (len < 2)
		return (-1);
	*n = le16(data) / 2;
	if (*n == 0 || 2 * *n > len)
		return (-1);
	for (prev = 2 * *n, i = 0; i < *n; i++) {
		cur = le16(data + 2 * i);
		if (cur < prev || cur > len)
			return (-1);
		prev = cur;
	}
	return (0);
}

static size_t
span_end(const uint8_t *data, size_t len, size_t n, size_t i)
{
	return (i + 1 < n ? le16(data + 2 * (i + 1)) : len);
}

/* Split [p, p + len) into NUL-terminated strings; a missing final NUL is
 * tolerated, since one French entry lacks it. */
static int
split(const uint8_t *p, size_t len, struct strtab *out)
{
	size_t start, i, n;
	char **str;

	memset(out, 0, sizeof(*out));
	for (start = 0; start < len; start = i + 1) {
		for (i = start; i < len && p[i] != '\0'; i++)
			;
		n = out->count;
		if ((str = realloc(out->str, (n + 1) * sizeof(*str))) == NULL)
			return (-1);
		out->str = str;
		if ((str[n] = macroman_to_utf8(p + start, i - start)) == NULL)
			return (-1);
		out->count++;
	}
	return (0);
}

int
cstr_decode(const uint8_t *data, size_t len, struct strtab *out)
{
	size_t i, n, start, end, k;

	memset(out, 0, sizeof(*out));
	if (span_table(data, len, &n) != 0)
		return (-1);
	if ((out->str = calloc(n, sizeof(*out->str))) == NULL)
		return (-1);
	out->count = n;
	for (i = 0; i < n; i++) {
		start = le16(data + 2 * i);
		end = span_end(data, len, n, i);
		for (k = start; k < end && data[k] != '\0'; k++)
			;
		if ((out->str[i] = macroman_to_utf8(data + start,
		    k - start)) == NULL) {
			strtab_free(out);
			return (-1);
		}
	}
	return (0);
}

/*
 * Each group is a u16 count followed by NUL-terminated strings.  The
 * stored count is wrong in a few groups of the original files (one more
 * or two fewer strings than declared), so the strings actually present
 * are kept and the stored count is reported in "declared".
 */
int
cstn_decode(const uint8_t *data, size_t len, struct strgroups *out)
{
	size_t i, n, start, end;

	memset(out, 0, sizeof(*out));
	if (span_table(data, len, &n) != 0)
		return (-1);
	out->groups = calloc(n, sizeof(*out->groups));
	out->declared = calloc(n, sizeof(*out->declared));
	if (out->groups == NULL || out->declared == NULL) {
		strgroups_free(out);
		return (-1);
	}
	out->count = n;
	for (i = 0; i < n; i++) {
		start = le16(data + 2 * i);
		end = span_end(data, len, n, i);
		if (end - start < 2)
			continue;	/* empty group */
		out->declared[i] = le16(data + start);
		if (split(data + start + 2, end - start - 2,
		    &out->groups[i]) != 0) {
			strgroups_free(out);
			return (-1);
		}
	}
	return (0);
}
