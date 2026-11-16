/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Raw DEFLATE decoder (RFC 1951), written for the launcher from the RFC:
 * stored, fixed and dynamic Huffman blocks.
 *
 * Streaming follows zlib's inflateBack() model (zlib infback.c): the caller
 * supplies input through an in() callback, which may hand over any number of
 * bytes at a time (1 byte to the whole stream), and takes output through an
 * out() callback; the decoder keeps one 32 KB sliding window that is both the
 * history for back references and the output buffer, flushed to out() each
 * time it fills and at the end. So a member of any size decodes in 32 KB of
 * memory (the console's game process cannot allocate more than about 12 MB in
 * one piece). Reading past the input, a back reference before the start of
 * the output, or more than max_out bytes is an error.
 */
#ifndef BL_INFLATE_H
#define BL_INFLATE_H

#include <stddef.h>
#include <stdint.h>

#define BL_INFLATE_WINDOW 32768u

enum { BL_INFLATE_OK = 0, BL_INFLATE_DAMAGED = -1, BL_INFLATE_TOO_BIG = -3, BL_INFLATE_NOMEM = -4, BL_INFLATE_OUT_FAILED = -5 };

/* Input: *buf gets the next bytes, the return value their count; 0 means the
 * input has ended (or could not be read: the decoder then fails). */
typedef size_t (*BlInflateIn)(void *ctx, const unsigned char **buf);
/* Output: 0 ok, anything else stops the decoder (BL_INFLATE_OUT_FAILED). */
typedef int (*BlInflateOut)(void *ctx, const unsigned char *buf, size_t len);

/* Decode one raw DEFLATE stream. *out_len (may be NULL) gets the bytes
 * produced. BL_INFLATE_OK or one of the errors above. */
int bl_inflate_back(BlInflateIn in, void *in_ctx, BlInflateOut out, void *out_ctx,
                    unsigned long long max_out, unsigned long long *out_len);

/* The whole stream in memory: 0 ok (*out_len = bytes written), <0 damaged
 * stream or out of room. */
int bl_inflate(const unsigned char *in, size_t in_len, unsigned char *out, size_t out_cap, size_t *out_len);

/* CRC-32 (ISO-HDLC, as in zip); crc32_update starts from 0 and chains. */
uint32_t bl_crc32(const unsigned char *p, size_t n);
uint32_t bl_crc32_update(uint32_t crc, const unsigned char *p, size_t n);

/* Every allocation of the decoder and the zip reader goes through this
 * (tests cap it to prove no large buffer is needed). */
extern void *(*bl_zip_malloc)(size_t n);

#endif
