/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Test helper: write a BlFrame as an uncompressed (stored-deflate) PNG. */
#ifndef BL_PNG_H
#define BL_PNG_H

#include "bl_ui.h"
#include <stdio.h>
#include <stdlib.h>

static uint32_t bl_png_crc_table[256];

static uint32_t bl_png_crc(uint32_t crc, const uint8_t *p, size_t n)
{
    if (!bl_png_crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            bl_png_crc_table[i] = c;
        }
    crc = ~crc;
    while (n--) crc = bl_png_crc_table[(crc ^ *p++) & 255] ^ (crc >> 8);
    return ~crc;
}

static void bl_png_be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }

static void bl_png_chunk(FILE *o, const char *type, const uint8_t *data, uint32_t n)
{
    uint8_t hdr[8];
    bl_png_be32(hdr, n);
    for (int i = 0; i < 4; i++) hdr[4 + i] = (uint8_t)type[i];
    fwrite(hdr, 1, 8, o);
    if (n) fwrite(data, 1, n, o);
    uint32_t crc = bl_png_crc(0, hdr + 4, 4);
    crc = bl_png_crc(crc, data, n);
    uint8_t c[4]; bl_png_be32(c, crc); fwrite(c, 1, 4, o);
}

static int bl_png_write(const char *path, const BlFrame *f)
{
    size_t row = (size_t)f->width * 3 + 1, raw_n = row * f->height;
    uint8_t *raw = malloc(raw_n);
    for (uint32_t y = 0; y < f->height; y++) {
        const uint32_t *s = (const uint32_t *)(const void *)(f->pixels + (size_t)y * f->stride);
        uint8_t *d = raw + y * row;
        *d++ = 0;
        for (uint32_t x = 0; x < f->width; x++) { *d++ = (uint8_t)(s[x] >> 16); *d++ = (uint8_t)(s[x] >> 8); *d++ = (uint8_t)s[x]; }
    }
    size_t blocks = (raw_n + 65534) / 65535, z_n = 2 + raw_n + blocks * 5 + 4;
    uint8_t *z = malloc(z_n), *q = z;
    *q++ = 0x78; *q++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < raw_n; off += 65535) {
        size_t n = raw_n - off < 65535 ? raw_n - off : 65535;
        *q++ = off + n == raw_n;
        *q++ = (uint8_t)n; *q++ = (uint8_t)(n >> 8); *q++ = (uint8_t)~n; *q++ = (uint8_t)(~n >> 8);
        for (size_t i = 0; i < n; i++) *q++ = raw[off + i];
    }
    bl_png_be32(q, (b << 16) | a); q += 4;
    FILE *o = fopen(path, "wb");
    if (!o) { free(raw); free(z); return -1; }
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    fwrite(sig, 1, 8, o);
    uint8_t ihdr[13];
    bl_png_be32(ihdr, f->width); bl_png_be32(ihdr + 4, f->height);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    bl_png_chunk(o, "IHDR", ihdr, 13);
    bl_png_chunk(o, "IDAT", z, (uint32_t)(q - z));
    bl_png_chunk(o, "IEND", NULL, 0);
    fclose(o);
    free(raw); free(z);
    return 0;
}

#endif
