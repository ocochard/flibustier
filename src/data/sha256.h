/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Olivier Cochard-Labbé
 */

#ifndef FLIBUSTIER_SHA256_H
#define FLIBUSTIER_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_LEN	32

/* One-shot SHA-256 (FIPS 180-4), used to identify game executables. */
void	sha256(const uint8_t *data, size_t len, uint8_t out[SHA256_LEN]);

/* Lowercase hex form: 64 characters plus NUL. */
void	sha256_hex(const uint8_t *data, size_t len, char out[65]);

#endif
