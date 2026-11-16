/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Raw DEFLATE (RFC 1951) in the inflateBack() model of zlib: input pulled
 * through in(), output pushed through out() from one 32 KB window that is
 * also the history for back references. See bl_inflate.h.
 * Based on the structure of puff.c (zlib contrib/puff, Mark Adler), zlib licence. */
#include "bl_inflate.h"

#include <stdlib.h>
#include <string.h>

#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288
#define WMASK (BL_INFLATE_WINDOW - 1u)

void *(*bl_zip_malloc)(size_t n) = malloc;

typedef struct State {
    BlInflateIn in;
    void *in_ctx;
    const unsigned char *next;    /* input not yet used */
    size_t avail;
    BlInflateOut out;
    void *out_ctx;
    unsigned char *window;        /* BL_INFLATE_WINDOW bytes: output buffer and history */
    size_t have;                  /* bytes in the window not yet given to out() */
    unsigned long long total, max_out;
    uint32_t bitbuf;
    int bitcnt;
    int err;                      /* first error (BL_INFLATE_*), 0 none */
} State;

typedef struct Huffman {
    short count[MAXBITS + 1];    /* codes of each length */
    short symbol[FIXLCODES];     /* symbols ordered by code */
} Huffman;

static int next_byte(State *s)
{
    if (!s->avail) {
        s->avail = s->in(s->in_ctx, &s->next);
        if (!s->avail || !s->next) { s->avail = 0; if (!s->err) s->err = BL_INFLATE_DAMAGED; return -1; }
    }
    s->avail--;
    return *s->next++;
}

/* need bits (at most 16), least significant first. Sets err past the end. */
static int bits(State *s, int need)
{
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        int c = next_byte(s);
        if (c < 0) return 0;
        val |= (uint32_t)c << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1u));
}

static int flush(State *s)
{
    if (s->have && !s->err && s->out(s->out_ctx, s->window, s->have) != 0) s->err = BL_INFLATE_OUT_FAILED;
    s->have = 0;
    return s->err ? -1 : 0;
}

static int put(State *s, unsigned char c)
{
    if (s->total >= s->max_out) { s->err = BL_INFLATE_TOO_BIG; return -1; }
    s->window[s->have++] = c;
    s->total++;
    return s->have == BL_INFLATE_WINDOW ? flush(s) : 0;
}

static int stored(State *s)
{
    s->bitbuf = 0;                 /* to a byte boundary */
    s->bitcnt = 0;
    unsigned char h[4];
    for (int i = 0; i < 4; i++) { int c = next_byte(s); if (c < 0) return -1; h[i] = (unsigned char)c; }
    unsigned len = h[0] | (unsigned)h[1] << 8, nlen = h[2] | (unsigned)h[3] << 8;
    if (len != (~nlen & 0xffffu)) { s->err = BL_INFLATE_DAMAGED; return -1; }
    while (len) {
        if (!s->avail && next_byte(s) >= 0) { s->next--; s->avail++; }   /* refill */
        if (!s->avail) return -1;
        size_t n = s->avail < len ? s->avail : len;
        if (n > BL_INFLATE_WINDOW - s->have) n = BL_INFLATE_WINDOW - s->have;
        if (s->total + n > s->max_out) { s->err = BL_INFLATE_TOO_BIG; return -1; }
        memcpy(s->window + s->have, s->next, n);
        s->next += n; s->avail -= n; s->have += n; s->total += n; len -= (unsigned)n;
        if (s->have == BL_INFLATE_WINDOW && flush(s) != 0) return -1;
    }
    return 0;
}

/* Canonical code from lengths. Returns 0 complete, >0 incomplete, <0 over-subscribed. */
static int construct(Huffman *h, const short *length, int n)
{
    short offs[MAXBITS + 1];
    for (int len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;           /* no codes: complete, but decode fails */
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++) if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (short)sym;
    return left;
}

static int decode(State *s, const Huffman *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        code |= bits(s, 1);
        if (s->err) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->err = BL_INFLATE_DAMAGED;
    return -1;
}

static const short LBASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short LEXT[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short DBASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const short DEXT[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int codes(State *s, const Huffman *lencode, const Huffman *distcode)
{
    for (;;) {
        int sym = decode(s, lencode);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (put(s, (unsigned char)sym) != 0) return -1;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) { s->err = BL_INFLATE_DAMAGED; return -1; }
            size_t len = (size_t)LBASE[sym] + (size_t)bits(s, LEXT[sym]);
            int dsym = decode(s, distcode);
            if (dsym < 0) return -1;
            if (dsym >= 30) { s->err = BL_INFLATE_DAMAGED; return -1; }
            size_t dist = (size_t)DBASE[dsym] + (size_t)bits(s, DEXT[dsym]);
            if (s->err) return -1;
            if (dist > s->total || dist > BL_INFLATE_WINDOW) { s->err = BL_INFLATE_DAMAGED; return -1; }
            /* The window is a ring: the byte dist back is at (have - dist) mod 32 K. */
            for (size_t i = 0; i < len; i++)
                if (put(s, s->window[(s->have + BL_INFLATE_WINDOW - dist) & WMASK]) != 0) return -1;
        }
    }
}

static int fixed(State *s)
{
    static Huffman lencode, distcode;
    static int built;
    if (!built) {
        short lengths[FIXLCODES];
        int sym = 0;
        for (; sym < 144; sym++) lengths[sym] = 8;
        for (; sym < 256; sym++) lengths[sym] = 9;
        for (; sym < 280; sym++) lengths[sym] = 7;
        for (; sym < FIXLCODES; sym++) lengths[sym] = 8;
        construct(&lencode, lengths, FIXLCODES);
        for (sym = 0; sym < MAXDCODES; sym++) lengths[sym] = 5;
        construct(&distcode, lengths, MAXDCODES);
        built = 1;
    }
    return codes(s, &lencode, &distcode);
}

static int dynamic(State *s)
{
    static const short ORDER[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    short lengths[MAXLCODES + MAXDCODES];
    Huffman lencode, distcode;
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (s->err) return -1;
    if (nlen > MAXLCODES || ndist > MAXDCODES) { s->err = BL_INFLATE_DAMAGED; return -1; }
    int index = 0;
    for (; index < ncode; index++) lengths[ORDER[index]] = (short)bits(s, 3);
    for (; index < 19; index++) lengths[ORDER[index]] = 0;
    if (s->err) return -1;
    if (construct(&lencode, lengths, 19) != 0) { s->err = BL_INFLATE_DAMAGED; return -1; }
    index = 0;
    while (index < nlen + ndist) {
        int sym = decode(s, &lencode);
        if (sym < 0) return -1;
        if (sym < 16) { lengths[index++] = (short)sym; continue; }
        short len = 0;
        int repeat;
        if (sym == 16) {
            if (index == 0) { s->err = BL_INFLATE_DAMAGED; return -1; }
            len = lengths[index - 1];
            repeat = 3 + bits(s, 2);
        } else if (sym == 17) {
            repeat = 3 + bits(s, 3);
        } else {
            repeat = 11 + bits(s, 7);
        }
        if (s->err) return -1;
        if (index + repeat > nlen + ndist) { s->err = BL_INFLATE_DAMAGED; return -1; }
        while (repeat--) lengths[index++] = len;
    }
    if (lengths[256] == 0) { s->err = BL_INFLATE_DAMAGED; return -1; }   /* no end-of-block code */
    int err = construct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) { s->err = BL_INFLATE_DAMAGED; return -1; }
    err = construct(&distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) { s->err = BL_INFLATE_DAMAGED; return -1; }
    return codes(s, &lencode, &distcode);
}

int bl_inflate_back(BlInflateIn in, void *in_ctx, BlInflateOut out, void *out_ctx,
                    unsigned long long max_out, unsigned long long *out_len)
{
    if (out_len) *out_len = 0;
    if (!in || !out) return BL_INFLATE_DAMAGED;
    State s;
    memset(&s, 0, sizeof s);
    s.in = in; s.in_ctx = in_ctx; s.out = out; s.out_ctx = out_ctx; s.max_out = max_out;
    s.window = bl_zip_malloc(BL_INFLATE_WINDOW);
    if (!s.window) return BL_INFLATE_NOMEM;
    int last;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (s.err) break;
        int rc = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : -1;
        if (rc != 0 && !s.err) s.err = BL_INFLATE_DAMAGED;
    } while (!s.err && !last);
    if (!s.err) flush(&s);
    free(s.window);
    if (s.err) return s.err;
    if (out_len) *out_len = s.total;
    return BL_INFLATE_OK;
}

/* ---- the whole stream in memory ---- */

typedef struct Mem { const unsigned char *p; size_t len; int given; unsigned char *out; size_t cap, pos; } Mem;

static size_t mem_in(void *ctx, const unsigned char **buf)
{
    Mem *m = ctx;
    if (m->given) return 0;
    m->given = 1;
    *buf = m->p;
    return m->len;
}

static int mem_out(void *ctx, const unsigned char *buf, size_t len)
{
    Mem *m = ctx;
    if (len > m->cap - m->pos) return -1;
    memcpy(m->out + m->pos, buf, len);
    m->pos += len;
    return 0;
}

int bl_inflate(const unsigned char *in, size_t in_len, unsigned char *out, size_t out_cap, size_t *out_len)
{
    if (out_len) *out_len = 0;
    if (!in || (!out && out_cap)) return -1;
    Mem m = { in, in_len, 0, out, out_cap, 0 };
    unsigned long long got = 0;
    int r = bl_inflate_back(mem_in, &m, mem_out, &m, out_cap, &got);
    if (r != 0) return -1;
    if (out_len) *out_len = (size_t)got;
    return 0;
}

uint32_t bl_crc32_update(uint32_t crc, const unsigned char *p, size_t n)
{
    static uint32_t table[256];
    static int built;
    if (!built) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        built = 1;
    }
    uint32_t c = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint32_t bl_crc32(const unsigned char *p, size_t n) { return bl_crc32_update(0, p, n); }
