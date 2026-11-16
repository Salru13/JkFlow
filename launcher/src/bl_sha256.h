/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#ifndef BL_SHA256_H
#define BL_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct BlSha256 { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t fill; } BlSha256;

void bl_sha256_init(BlSha256 *s);
void bl_sha256_update(BlSha256 *s, const void *data, size_t n);
void bl_sha256_final_hex(BlSha256 *s, char out[65]);
void bl_sha256_hex(const void *data, size_t n, char out[65]);

#endif
