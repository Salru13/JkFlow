/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Blur launcher software renderer. See bl_ui.h. */
#include "bl_ui.h"
#include "bl_font.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef __SSE2__
#include <emmintrin.h>
#endif

/* ------------------------------------------------------------------ */
/* Pixels and colour                                                  */
/* ------------------------------------------------------------------ */

static inline uint32_t *row_ptr(const BlFrame *f, int y)
{
    return (uint32_t *)(void *)(f->pixels + (size_t)y * f->stride);
}

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Blend s over d with alpha a (0..255). */
static inline uint32_t mix(uint32_t d, uint32_t s, int a)
{
    if (a <= 0) return d;
    if (a >= 255) return s & 0xFFFFFF;
    uint32_t r = (((d >> 16) & 255) * (unsigned)(255 - a) + ((s >> 16) & 255) * (unsigned)a + 127) / 255;
    uint32_t g = (((d >> 8) & 255) * (unsigned)(255 - a) + ((s >> 8) & 255) * (unsigned)a + 127) / 255;
    uint32_t b = ((d & 255) * (unsigned)(255 - a) + (s & 255) * (unsigned)a + 127) / 255;
    return (r << 16) | (g << 8) | b;
}

/* Add s scaled by a (0..255) to d, saturating. */
static inline uint32_t add(uint32_t d, uint32_t s, int a)
{
    if (a <= 0) return d;
    unsigned r = ((d >> 16) & 255) + (((s >> 16) & 255) * (unsigned)a + 127) / 255;
    unsigned g = ((d >> 8) & 255) + (((s >> 8) & 255) * (unsigned)a + 127) / 255;
    unsigned b = (d & 255) + ((s & 255) * (unsigned)a + 127) / 255;
    return ((r > 255 ? 255u : r) << 16) | ((g > 255 ? 255u : g) << 8) | (b > 255 ? 255u : b);
}

/* lroundf (half away from zero) for |v| < 2^23, without the libm call: the
 * fraction a - trunc(a) of a float is exact, so the result is identical. */
static inline int round_away(float v)
{
    float a = v < 0.0f ? -v : v;
    int i = (int)a;
    if (a - (float)i >= 0.5f) i++;
    return v < 0.0f ? -i : i;
}

static inline uint32_t lerp_rgb(uint32_t a, uint32_t b, float t)
{
    t = clampf(t, 0.0f, 1.0f);
    int ra = (int)((a >> 16) & 255), ga = (int)((a >> 8) & 255), ba = (int)(a & 255);
    int rb = (int)((b >> 16) & 255), gb = (int)((b >> 8) & 255), bb = (int)(b & 255);
    return ((uint32_t)(ra + round_away((float)(rb - ra) * t)) << 16)
         | ((uint32_t)(ga + round_away((float)(gb - ga) * t)) << 8)
         | (uint32_t)(ba + round_away((float)(bb - ba) * t));
}

/* mix(d, s, a) for one fixed s and a over a run of pixels; four at a time
 * with SSE2: per byte (d * (255 - a) + s * a + 127) / 255, where v / 255 is
 * (v + 1 + (v >> 8)) >> 8 for every v up to 65152 (checked for all). */
static void mix_span(uint32_t *row, int n, uint32_t s, int a)
{
    if (n <= 0 || a <= 0) return;
    int x = 0;
    if (a >= 255) { for (; x < n; x++) row[x] = s & 0xFFFFFF; return; }
#ifdef __SSE2__
    const __m128i zero = _mm_setzero_si128(), one = _mm_set1_epi16(1), mask = _mm_set1_epi32(0x00FFFFFF);
    const __m128i ka = _mm_set1_epi16((short)(255 - a));
    const short cb = (short)((s & 255) * (unsigned)a + 127), cg = (short)(((s >> 8) & 255) * (unsigned)a + 127),
                cr = (short)(((s >> 16) & 255) * (unsigned)a + 127);
    const __m128i kc = _mm_setr_epi16(cb, cg, cr, 0, cb, cg, cr, 0);
    for (; x + 4 <= n; x += 4) {
        __m128i d = _mm_loadu_si128((const __m128i *)(const void *)(row + x));
        __m128i lo = _mm_add_epi16(_mm_mullo_epi16(_mm_unpacklo_epi8(d, zero), ka), kc);
        __m128i hi = _mm_add_epi16(_mm_mullo_epi16(_mm_unpackhi_epi8(d, zero), ka), kc);
        lo = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(lo, one), _mm_srli_epi16(lo, 8)), 8);
        hi = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(hi, one), _mm_srli_epi16(hi, 8)), 8);
        _mm_storeu_si128((__m128i *)(void *)(row + x), _mm_and_si128(_mm_packus_epi16(lo, hi), mask));
    }
#endif
    for (; x < n; x++) row[x] = mix(row[x], s, a);
}

/* add(d, s, a) for one fixed s and a over a run of pixels (saturating). */
static void add_span(uint32_t *row, int n, uint32_t s, int a)
{
    if (n <= 0 || a <= 0) return;
    uint32_t k = add(0, s, a);                       /* the amount per channel */
    int x = 0;
#ifdef __SSE2__
    const __m128i kv = _mm_set1_epi32((int)k), mask = _mm_set1_epi32(0x00FFFFFF);
    for (; x + 4 <= n; x += 4) {
        __m128i d = _mm_loadu_si128((const __m128i *)(const void *)(row + x));
        _mm_storeu_si128((__m128i *)(void *)(row + x), _mm_and_si128(_mm_adds_epu8(d, kv), mask));
    }
#endif
    for (; x < n; x++) row[x] = add(row[x], s, a);
}

/* The run [*a, *b) of a row (within [x0, x1)) where plain(x) holds, for a
 * predicate that holds on one run around mid and falls off monotonically on
 * both sides. */
typedef int (*SpanTest)(const void *ctx, int x);
static int find_span(SpanTest plain, const void *ctx, int x0, int x1, int mid, int *a, int *b)
{
    if (x1 <= x0) return 0;
    /* A clipped row: the run's part inside [x0, x1) holds the end nearest mid. */
    if (mid < x0) mid = x0;
    if (mid >= x1) mid = x1 - 1;
    if (!plain(ctx, mid)) return 0;
    int lo = x0, hi = mid;                           /* first plain in [lo, hi] */
    while (lo < hi) { int m = lo + (hi - lo) / 2; if (plain(ctx, m)) hi = m; else lo = m + 1; }
    *a = lo;
    lo = mid; hi = x1 - 1;                           /* last plain in [lo, hi] */
    while (lo < hi) { int m = lo + (hi - lo + 1) / 2; if (plain(ctx, m)) lo = m; else hi = m - 1; }
    *b = lo + 1;
    return 1;
}

/* Clip [x, x+w) x [y, y+h) to the frame. Returns 0 when empty. */
static int clip(const BlFrame *f, long long x, long long y, long long w, long long h,
                int *x0, int *y0, int *x1, int *y1)
{
    if (!f || !f->pixels || w <= 0 || h <= 0) return 0;
    long long ax = x < 0 ? 0 : x, ay = y < 0 ? 0 : y;
    long long bx = x + w > (long long)f->width ? (long long)f->width : x + w;
    long long by = y + h > (long long)f->height ? (long long)f->height : y + h;
    if (ax >= bx || ay >= by) return 0;
    *x0 = (int)ax; *y0 = (int)ay; *x1 = (int)bx; *y1 = (int)by;
    return 1;
}

void bl_fill(BlFrame *f, int x, int y, int w, int h, uint32_t rgb)
{
    int x0, y0, x1, y1;
    if (!clip(f, x, y, w, h, &x0, &y0, &x1, &y1)) return;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *r = row_ptr(f, yy);
        for (int xx = x0; xx < x1; xx++) r[xx] = rgb & 0xFFFFFF;
    }
}

void bl_fill_alpha(BlFrame *f, int x, int y, int w, int h, uint32_t rgb, int alpha)
{
    int x0, y0, x1, y1;
    if (!clip(f, x, y, w, h, &x0, &y0, &x1, &y1)) return;
    for (int yy = y0; yy < y1; yy++) mix_span(row_ptr(f, yy) + x0, x1 - x0, rgb, alpha);
}

void bl_fill_alpha_cols(BlFrame *f, int x, int y, int w, int h, uint32_t rgb, const int *alpha)
{
    int x0, y0, x1, y1;
    if (!alpha || !clip(f, x, y, w, h, &x0, &y0, &x1, &y1)) return;
    int n = x1 - x0, xs = 0;
#ifdef __SSE2__
    /* Per column: mix's two factors per channel and which lanes it rewrites
     * (alpha 0 leaves a pixel as it is, 255 sets it): four pixels at a time. */
    enum { MAXW = 4096 };
    static uint16_t ka[MAXW * 4], kc[MAXW * 4];
    static uint32_t keep[MAXW], set[MAXW];
    if (n <= MAXW) {
        for (int i = 0; i < n; i++) {
            int a = alpha[x0 - x + i];
            a = a < 0 ? 0 : a > 255 ? 255 : a;
            keep[i] = a == 0 ? 0xFFFFFFFFu : 0x00FFFFFFu;
            set[i] = 0;
            for (int c = 0; c < 4; c++) {
                unsigned sc = c == 3 ? 0 : (rgb >> (8 * c)) & 255;
                ka[i * 4 + c] = (uint16_t)(255 - a);
                kc[i * 4 + c] = (uint16_t)(sc * (unsigned)a + 127);
            }
        }
        const __m128i zero = _mm_setzero_si128(), one = _mm_set1_epi16(1);
        for (int yy = y0; yy < y1; yy++) {
            uint32_t *r = row_ptr(f, yy) + x0;
            int i = 0;
            for (; i + 4 <= n; i += 4) {
                __m128i d = _mm_loadu_si128((const __m128i *)(const void *)(r + i));
                __m128i lo = _mm_mullo_epi16(_mm_unpacklo_epi8(d, zero), _mm_loadu_si128((const __m128i *)(const void *)(ka + i * 4)));
                __m128i hi = _mm_mullo_epi16(_mm_unpackhi_epi8(d, zero), _mm_loadu_si128((const __m128i *)(const void *)(ka + i * 4 + 8)));
                lo = _mm_add_epi16(lo, _mm_loadu_si128((const __m128i *)(const void *)(kc + i * 4)));
                hi = _mm_add_epi16(hi, _mm_loadu_si128((const __m128i *)(const void *)(kc + i * 4 + 8)));
                lo = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(lo, one), _mm_srli_epi16(lo, 8)), 8);
                hi = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(hi, one), _mm_srli_epi16(hi, 8)), 8);
                __m128i v = _mm_and_si128(_mm_packus_epi16(lo, hi), _mm_loadu_si128((const __m128i *)(const void *)(keep + i)));
                _mm_storeu_si128((__m128i *)(void *)(r + i), _mm_or_si128(v, _mm_loadu_si128((const __m128i *)(const void *)(set + i))));
            }
            for (; i < n; i++) r[i] = mix(r[i], rgb, alpha[x0 - x + i]);
        }
        return;
    }
#endif
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *r = row_ptr(f, yy);
        for (int xx = x0 + xs; xx < x1; xx++) r[xx] = mix(r[xx], rgb, alpha[xx - x]);
    }
}

void bl_gradient_v(BlFrame *f, int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
    int x0, y0, x1, y1;
    if (!clip(f, x, y, w, h, &x0, &y0, &x1, &y1)) return;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t c = lerp_rgb(top, bottom, h > 1 ? (float)(yy - y) / (float)(h - 1) : 0.0f);
        uint32_t *r = row_ptr(f, yy);
        for (int xx = x0; xx < x1; xx++) r[xx] = c;
    }
}

/* ------------------------------------------------------------------ */
/* Glass cards, panels and pills                                      */
/* ------------------------------------------------------------------ */

#define GLASS_GLOW 36.0f

/* What a glass pixel needs from its row (ly only): the body colour and
 * alpha, the inner-glow scale and the bottom glow. */
typedef struct GlassRow { uint32_t body; int body_a; float t; int bottom_a; } GlassRow;

static inline GlassRow glass_row(float ly, float h)
{
    GlassRow g;
    float t = clampf(ly / h, 0.0f, 1.0f);
    g.t = t;
    g.body = t < 0.55f ? lerp_rgb(0x2878FF, BL_RGB_CARD, t / 0.55f)
                       : lerp_rgb(BL_RGB_CARD, 0x145AE6, (t - 0.55f) / 0.45f);
    g.body_a = t < 0.55f ? (int)(140.0f + 70.0f * t / 0.55f) : (int)(210.0f - 30.0f * (t - 0.55f) / 0.45f);
    float bottom = clampf(1.0f - (h - ly) / 34.0f, 0.0f, 1.0f);
    g.bottom_a = (int)(bottom * bottom * 120.0f);
    return g;
}

/* Signed distance from (px,py) to a rounded rectangle at (0,0,w,h); no
 * square root where it would be of 0 (sqrtf(0) is 0, same result). */
static inline float sd_round_rect_fast(float px, float py, float w, float h, float r)
{
    float qx = fabsf(px - w * 0.5f) - (w * 0.5f - r);
    float qy = fabsf(py - h * 0.5f) - (h * 0.5f - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float inside = qx > qy ? qx : qy;
    if (ox == 0.0f && oy == 0.0f) return (0.0f + inside) - r;   /* sqrtf(0) == 0 */
    return sqrtf(ox * ox + oy * oy) + (inside < 0 ? inside : 0) - r;
}

/* A pixel at least `depth` inside the rounded rectangle (its distance as
 * sd_round_rect_fast computes it), for find_span. */
typedef struct PlainCtx { int x; float w, h, r, ly, depth; } PlainCtx;

static int plain_inside(const void *ctx, int xx)
{
    const PlainCtx *c = ctx;
    float d = sd_round_rect_fast((float)(xx - c->x) + 0.5f, c->ly, c->w, c->h, c->r);
    return d <= 0.0f && -d >= c->depth;
}

/* Where a row's distance to the rounded rectangle does not depend on the
 * column (sd_round_rect_fast: outside rows over the straight edge, inside
 * rows where the row's own edge is the nearest), for find_span. */
typedef struct FlatCtx { int x; float w, r, qy; } FlatCtx;

static int flat_row(const void *ctx, int xx)
{
    const FlatCtx *c = ctx;
    float qx = fabsf((float)(xx - c->x) + 0.5f - c->w * 0.5f) - (c->w * 0.5f - c->r);
    return c->qy > 0 ? qx <= 0.0f : qx <= c->qy;
}

/* Shade one pixel of a glass card whose local coordinates are (lx, ly). */
static inline uint32_t glass_px_row(uint32_t dst, float lx, float ly, float w, float h, float r, int focused,
                                    const GlassRow *gr)
{
    float d = sd_round_rect_fast(lx, ly, w, h, r);
    if (d >= GLASS_GLOW) return dst;
    if (d > 0.0f) {
        float g = 1.0f - d / GLASS_GLOW;
        dst = add(dst, focused ? 0x1BC8FF : 0x198CFF, (int)((focused ? 170.0f : 120.0f) * g * g));
        if (d < 1.0f) dst = mix(dst, focused ? BL_RGB_CYAN : 0x46AAFF, (int)(235.0f * (1.0f - d)));
        return dst;
    }
    dst = mix(dst, gr->body, gr->body_a);
    float e = -d;                                   /* distance to the edge, inside */
    if (e < 46.0f) {
        float inner = clampf(1.0f - e / 46.0f, 0.0f, 1.0f);
        dst = add(dst, 0x1BC8FF, (int)(inner * inner * (60.0f + 110.0f * gr->t) * (focused ? 1.2f : 1.0f)));
    }
    dst = add(dst, 0x1BE6FF, gr->bottom_a);
    if (e < 3.0f) dst = mix(dst, focused ? BL_RGB_CYAN : 0x46AAFF, 235);
    return dst;
}

static inline uint32_t glass_px(uint32_t dst, float lx, float ly, float w, float h, float r, int focused)
{
    GlassRow gr = glass_row(ly, h);
    return glass_px_row(dst, lx, ly, w, h, r, focused, &gr);
}

/* glass_px_row for a run of pixels that share the distance d. */
static void glass_run(uint32_t *row, int n, float d, int focused, const GlassRow *gr)
{
    if (n <= 0 || d >= GLASS_GLOW) return;
    if (d > 0.0f) {
        float g = 1.0f - d / GLASS_GLOW;
        add_span(row, n, focused ? 0x1BC8FF : 0x198CFF, (int)((focused ? 170.0f : 120.0f) * g * g));
        if (d < 1.0f) mix_span(row, n, focused ? BL_RGB_CYAN : 0x46AAFF, (int)(235.0f * (1.0f - d)));
        return;
    }
    mix_span(row, n, gr->body, gr->body_a);
    float e = -d;
    if (e < 46.0f) {
        float inner = clampf(1.0f - e / 46.0f, 0.0f, 1.0f);
        add_span(row, n, 0x1BC8FF, (int)(inner * inner * (60.0f + 110.0f * gr->t) * (focused ? 1.2f : 1.0f)));
    }
    add_span(row, n, 0x1BE6FF, gr->bottom_a);
    if (e < 3.0f) mix_span(row, n, focused ? BL_RGB_CYAN : 0x46AAFF, 235);
}

void bl_glass(BlFrame *f, int x, int y, int w, int h, int radius, int focused)
{
    if (w <= 0 || h <= 0) return;
    float r = clampf((float)radius, 0.0f, (float)(w < h ? w : h) * 0.5f);
    int g = (int)GLASS_GLOW;
    int x0, y0, x1, y1;
    if (!clip(f, (long long)x - g, (long long)y - g, (long long)w + 2 * g, (long long)h + 2 * g, &x0, &y0, &x1, &y1)) return;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = row_ptr(f, yy);
        float ly = (float)(yy - y) + 0.5f;
        GlassRow gr = glass_row(ly, (float)h);
        /* Runs of pixels with one result per row, done as spans: the plain
         * middle (body mix and bottom glow only), else the run whose
         * distance is the row's own. The rest pixel by pixel. */
        PlainCtx pc = { x, (float)w, (float)h, r, ly, 46.0f };
        int a = x1, b = x1;
        if (find_span(plain_inside, &pc, x0, x1, x + w / 2, &a, &b)) {
            mix_span(row + a, b - a, gr.body, gr.body_a);
            add_span(row + a, b - a, 0x1BE6FF, gr.bottom_a);
        } else {
            FlatCtx fc = { x, (float)w, r, fabsf(ly - (float)h * 0.5f) - ((float)h * 0.5f - r) };
            if (find_span(flat_row, &fc, x0, x1, x + w / 2, &a, &b))
                glass_run(row + a, b - a, sd_round_rect_fast((float)(a - x) + 0.5f, ly, (float)w, (float)h, r), focused, &gr);
            else
                a = b = x1;
        }
        for (int xx = x0; xx < a; xx++)
            row[xx] = glass_px_row(row[xx], (float)(xx - x) + 0.5f, ly, (float)w, (float)h, r, focused, &gr);
        for (int xx = b; xx < x1; xx++)
            row[xx] = glass_px_row(row[xx], (float)(xx - x) + 0.5f, ly, (float)w, (float)h, r, focused, &gr);
    }
}

void bl_glass_tilted(BlFrame *f, int x, int y, int w, int h, int radius, float turn)
{
    if (w <= 0 || h <= 0) return;
    turn = clampf(turn, -1.0f, 1.0f);
    float k = fabsf(turn);
    float vis_w = (float)w * (1.0f - 0.30f * k);        /* foreshortened width */
    float xs = (float)x + ((float)w - vis_w) * 0.5f;
    float cy = (float)y + (float)h * 0.5f;
    float r = clampf((float)radius, 0.0f, (float)(w < h ? w : h) * 0.5f);
    int g = (int)GLASS_GLOW;
    int x0, y0, x1, y1;
    if (!clip(f, (long long)x - g, (long long)y - g, (long long)w + 2 * g, (long long)h + 2 * g, &x0, &y0, &x1, &y1)) return;
    enum { MAXW = 2048 };
    if (x1 - x0 <= MAXW) {
        /* Row by row (each pixel depends only on its own column and row). */
        float col_lx[MAXW], col_s[MAXW];
        for (int xx = x0; xx < x1; xx++) {
            float u = ((float)xx + 0.5f - xs) / vis_w;
            float uc = clampf(u, 0.0f, 1.0f);
            col_s[xx - x0] = turn < 0 ? (1.0f - 0.22f * k) + 0.22f * k * uc : 1.0f - 0.22f * k * uc;
            col_lx[xx - x0] = u * (float)w;
        }
        for (int yy = y0; yy < y1; yy++) {
            uint32_t *row = row_ptr(f, yy);
            float dy = (float)yy + 0.5f - cy;
            for (int xx = x0; xx < x1; xx++) {
                float ly = dy / col_s[xx - x0] + (float)h * 0.5f;
                row[xx] = glass_px(row[xx], col_lx[xx - x0], ly, (float)w, (float)h, r, 0);
            }
        }
        return;
    }
    for (int xx = x0; xx < x1; xx++) {
        float u = ((float)xx + 0.5f - xs) / vis_w;
        float uc = clampf(u, 0.0f, 1.0f);
        /* The far edge (left when turn < 0) is shorter. */
        float s = turn < 0 ? (1.0f - 0.22f * k) + 0.22f * k * uc : 1.0f - 0.22f * k * uc;
        float lx = u * (float)w;
        for (int yy = y0; yy < y1; yy++) {
            float ly = ((float)yy + 0.5f - cy) / s + (float)h * 0.5f;
            uint32_t *p = row_ptr(f, yy) + xx;
            *p = glass_px(*p, lx, ly, (float)w, (float)h, r, 0);
        }
    }
}

void bl_round_rect(BlFrame *f, int x, int y, int w, int h, int radius,
                   uint32_t top, uint32_t bottom, int fill_alpha,
                   uint32_t rim, int rim_w, uint32_t glow, int glow_r)
{
    if (w <= 0 || h <= 0) return;
    float r = clampf((float)radius, 0.0f, (float)(w < h ? w : h) * 0.5f);
    int g = (glow_r > 0 ? glow_r : 0) + 1;
    int x0, y0, x1, y1;
    if (!clip(f, (long long)x - g, (long long)y - g, (long long)w + 2 * g, (long long)h + 2 * g, &x0, &y0, &x1, &y1)) return;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = row_ptr(f, yy);
        float ly = (float)(yy - y) + 0.5f;
        uint32_t body = lerp_rgb(top, bottom, ly / (float)h);
        /* The plain middle (inside the rim) is one mix: done as a run. */
        PlainCtx pc = { x, (float)w, (float)h, r, ly, rim_w > 0 ? (float)rim_w : 0.0f };
        int a = x1, b = x1;
        if (find_span(plain_inside, &pc, x0, x1, x + w / 2, &a, &b)) {
            mix_span(row + a, b - a, body, fill_alpha);
        } else {
            /* Else the run whose distance is the row's own (see flat_row). */
            FlatCtx fc = { x, (float)w, r, fabsf(ly - (float)h * 0.5f) - ((float)h * 0.5f - r) };
            if (find_span(flat_row, &fc, x0, x1, x + w / 2, &a, &b)) {
                float d = sd_round_rect_fast((float)(a - x) + 0.5f, ly, (float)w, (float)h, r);
                int n = b - a;
                if (d > 0.0f) {
                    if (glow_r > 0 && d < (float)glow_r) {
                        float k = 1.0f - d / (float)glow_r;
                        add_span(row + a, n, glow, (int)(140.0f * k * k));
                    }
                    if (d < 1.0f) mix_span(row + a, n, rim_w > 0 ? rim : body, (int)((rim_w > 0 ? 235.0f : (float)fill_alpha) * (1.0f - d)));
                } else if (rim_w > 0 && -d < (float)rim_w) {
                    mix_span(row + a, n, rim, 235);
                } else {
                    mix_span(row + a, n, body, fill_alpha);
                }
            } else {
                a = b = x1;
            }
        }
        for (int xx = x0; xx < x1; xx++) {
            if (xx == a) { xx = b - 1; continue; }
            float d = sd_round_rect_fast((float)(xx - x) + 0.5f, ly, (float)w, (float)h, r);
            uint32_t c = row[xx];
            if (d > 0.0f) {
                if (glow_r > 0 && d < (float)glow_r) {
                    float k = 1.0f - d / (float)glow_r;
                    c = add(c, glow, (int)(140.0f * k * k));
                }
                if (d < 1.0f) c = mix(c, rim_w > 0 ? rim : body, (int)((rim_w > 0 ? 235.0f : (float)fill_alpha) * (1.0f - d)));
            } else if (rim_w > 0 && -d < (float)rim_w) {
                c = mix(c, rim, 235);
            } else {
                c = mix(c, body, fill_alpha);
            }
            row[xx] = c;
        }
    }
}

void bl_pill(BlFrame *f, int x, int y, int w, int h, int selected)
{
    if (w <= 0 || h <= 0) return;
    float r = (float)(h < w ? h : w) * 0.5f;
    if (r > 14.0f && !selected) r = 14.0f;
    float glow = selected ? 18.0f : 0.0f;
    int g = (int)glow + 1;
    int x0, y0, x1, y1;
    if (!clip(f, (long long)x - g, (long long)y - g, (long long)w + 2 * g, (long long)h + 2 * g, &x0, &y0, &x1, &y1)) return;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = row_ptr(f, yy);
        float ly = (float)(yy - y) + 0.5f, t = clampf(ly / (float)h, 0.0f, 1.0f);
        uint32_t body = t < 0.6f ? lerp_rgb(0x2C86FF, BL_RGB_BUTTON, t / 0.6f)
                                 : lerp_rgb(BL_RGB_BUTTON, 0x0D4FB0, (t - 0.6f) / 0.4f);
        /* The plain middle (inside the rim) as a run. */
        PlainCtx pc = { x, (float)w, (float)h, r, ly, selected ? 3.0f : 2.0f };
        int a = x1, b = x1;
        if (!find_span(plain_inside, &pc, x0, x1, x + w / 2, &a, &b)) a = b = x1;
        if (selected) mix_span(row + a, b - a, body, 255);
        else mix_span(row + a, b - a, 0x050F28, 150);
        for (int xx = x0; xx < x1; xx++) {
            if (xx == a) { xx = b - 1; continue; }
            float d = sd_round_rect_fast((float)(xx - x) + 0.5f, ly, (float)w, (float)h, r);
            uint32_t c = row[xx];
            if (d > 0.0f) {
                if (selected && d < glow) {
                    float k = 1.0f - d / glow;
                    c = add(c, BL_RGB_CYAN, (int)(110.0f * k * k));
                }
                if (d < 1.0f) c = mix(c, selected ? BL_RGB_CYAN : 0x4682DC, (int)((selected ? 235.0f : 115.0f) * (1.0f - d)));
            } else if (selected) {
                c = (-d < 3.0f) ? BL_RGB_CYAN : body;
            } else {
                c = (-d < 2.0f) ? mix(c, 0x4682DC, 115) : mix(c, 0x050F28, 150);
            }
            row[xx] = c;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Light                                                              */
/* ------------------------------------------------------------------ */

void bl_trail(BlFrame *f, int x0, int y0, int x1, int y1, int thickness,
              uint32_t rgb, int alpha, int hot_head)
{
    if (!f || !f->pixels || thickness <= 0 || alpha <= 0) return;
    float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
    float len2 = dx * dx + dy * dy;
    if (len2 < 1.0f) return;
    float len = sqrtf(len2);
    float nx = -dy / len, ny = dx / len;              /* unit normal */
    float reach = (float)thickness * 0.5f + 1.5f;
    int ya = clampi((int)floorf(fminf((float)y0, (float)y1) - reach), 0, (int)f->height);
    int yb = clampi((int)ceilf(fmaxf((float)y0, (float)y1) + reach) + 1, 0, (int)f->height);
    for (int yy = ya; yy < yb; yy++) {
        float py = (float)yy + 0.5f - (float)y0;
        /* Distance d(x) = (px)*nx + py*ny is linear in x; so is t(x). Find the
         * x interval where |d| <= reach and 0 <= t <= 1. */
        float lo = -1e9f, hi = 1e9f;
        if (fabsf(nx) > 1e-6f) {
            float a = (-reach - py * ny) / nx, b = (reach - py * ny) / nx;
            lo = fminf(a, b); hi = fmaxf(a, b);
        } else if (fabsf(py * ny) > reach) {
            continue;
        }
        float tx = dx / len2, t0 = py * dy / len2;      /* t(px) = px*tx + t0 */
        if (fabsf(tx) > 1e-9f) {
            float a = (0.0f - t0) / tx, b = (1.0f - t0) / tx;
            lo = fmaxf(lo, fminf(a, b)); hi = fminf(hi, fmaxf(a, b));
        } else if (t0 < 0.0f || t0 > 1.0f) {
            continue;
        }
        int xa = clampi((int)floorf(lo + (float)x0) - 1, 0, (int)f->width);
        int xb = clampi((int)ceilf(hi + (float)x0) + 1, 0, (int)f->width);
        uint32_t *row = row_ptr(f, yy);
        for (int xx = xa; xx < xb; xx++) {
            float px = (float)xx + 0.5f - (float)x0;
            float t = px * tx + t0;
            if (t < 0.0f || t > 1.0f) continue;
            float dist = fabsf(px * nx + py * ny);
            float cov = clampf(1.0f - dist / reach, 0.0f, 1.0f);
            if (cov <= 0.0f) continue;
            float along = t < 0.7f ? t / 0.7f : 1.0f;
            if (t > 0.97f) along *= (1.0f - t) / 0.03f;
            uint32_t c = rgb;
            if (hot_head && t > 0.88f) c = lerp_rgb(rgb, 0xFFFFFF, (t - 0.88f) / 0.12f);
            row[xx] = add(row[xx], c, (int)((float)alpha * cov * cov * along));
        }
    }
}

void bl_glow_dot(BlFrame *f, int cx, int cy, int radius, uint32_t rgb, int alpha)
{
    if (radius <= 0) return;
    int x0, y0, x1, y1;
    if (!clip(f, (long long)cx - radius, (long long)cy - radius, 2LL * radius + 1, 2LL * radius + 1, &x0, &y0, &x1, &y1)) return;
    float rr = (float)radius;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = row_ptr(f, yy);
        /* Outside |dx| < sqrt(r^2 - dy^2) + 2 the glow is 0 (k <= 0). */
        double dy = (double)(yy - cy), span2 = (double)radius * radius - dy * dy;
        int half = span2 > 0 ? (int)sqrt(span2) + 2 : 2;
        int xa = cx - half > x0 ? cx - half : x0, xb = cx + half + 1 < x1 ? cx + half + 1 : x1;
        for (int xx = xa; xx < xb; xx++) {
            float ddx = (float)(xx - cx), ddy = (float)(yy - cy);
            float k = 1.0f - sqrtf(ddx * ddx + ddy * ddy) / rr;
            if (k > 0.0f) row[xx] = add(row[xx], rgb, (int)((float)alpha * k * k));
        }
    }
}

void bl_blit_add(BlFrame *f, const uint8_t *rgb, int w, int h, int x, int y, int brightness_256)
{
    if (!rgb || brightness_256 <= 0) return;
    int x0, y0, x1, y1;
    if (!clip(f, x, y, w, h, &x0, &y0, &x1, &y1)) return;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = row_ptr(f, yy);
        const uint8_t *s = rgb + ((size_t)(yy - y) * (size_t)w + (size_t)(x0 - x)) * 3;
        for (int xx = x0; xx < x1; xx++, s += 3) {
            if (!(s[0] | s[1] | s[2])) continue;
            uint32_t d = row[xx];
            unsigned r = ((d >> 16) & 255) + ((unsigned)s[0] * (unsigned)brightness_256 >> 8);
            unsigned g = ((d >> 8) & 255) + ((unsigned)s[1] * (unsigned)brightness_256 >> 8);
            unsigned b = (d & 255) + ((unsigned)s[2] * (unsigned)brightness_256 >> 8);
            row[xx] = ((r > 255 ? 255u : r) << 16) | ((g > 255 ? 255u : g) << 8) | (b > 255 ? 255u : b);
        }
    }
}

void bl_image_scale(const uint8_t *src, int w, int h, uint8_t *dst, int dw, int dh)
{
    if (!src || !dst || w <= 0 || h <= 0 || dw <= 0 || dh <= 0) return;
    for (int y = 0; y < dh; y++) {
        float sy = clampf(((float)y + 0.5f) * (float)h / (float)dh - 0.5f, 0.0f, (float)(h - 1));
        int y0 = (int)sy, y1 = y0 + 1 < h ? y0 + 1 : y0;
        float fy = sy - (float)y0;
        for (int x = 0; x < dw; x++) {
            float sx = clampf(((float)x + 0.5f) * (float)w / (float)dw - 0.5f, 0.0f, (float)(w - 1));
            int x0 = (int)sx, x1 = x0 + 1 < w ? x0 + 1 : x0;
            float fx = sx - (float)x0;
            for (int c = 0; c < 3; c++) {
                float a = src[((size_t)y0 * (size_t)w + (size_t)x0) * 3 + (size_t)c];
                float b = src[((size_t)y0 * (size_t)w + (size_t)x1) * 3 + (size_t)c];
                float e = src[((size_t)y1 * (size_t)w + (size_t)x0) * 3 + (size_t)c];
                float g = src[((size_t)y1 * (size_t)w + (size_t)x1) * 3 + (size_t)c];
                float v = (a + (b - a) * fx) * (1.0f - fy) + (e + (g - e) * fx) * fy;
                dst[((size_t)y * (size_t)dw + (size_t)x) * 3 + (size_t)c] = (uint8_t)clampf(v + 0.5f, 0.0f, 255.0f);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Text                                                               */
/* ------------------------------------------------------------------ */

/* Decode one UTF-8 code point. On an invalid or truncated sequence, consume
 * one byte and return -1. */
static long next_cp(const unsigned char **p)
{
    const unsigned char *s = *p;
    unsigned c = s[0];
    if (c < 0x80) { *p = s + 1; return (long)c; }
    int n; unsigned cp, min;
    if ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1F; min = 0x80; }
    else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0F; min = 0x800; }
    else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07; min = 0x10000; }
    else { *p = s + 1; return -1; }
    for (int i = 1; i <= n; i++) {
        if ((s[i] & 0xC0) != 0x80) { *p = s + 1; return -1; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { *p = s + 1; return -1; }
    *p = s + 1 + n;
    return (long)cp;
}

static const BlFontData *font_of(BlFont font)
{
    if ((int)font < 0 || (int)font >= BL_FONT_DATA_COUNT) return &bl_font_data[BL_FONT_BODY];
    return &bl_font_data[font];
}

static const BlGlyph *glyph_of(const BlFontData *fd, long cp)
{
    int lo = 0, hi = (int)fd->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if ((long)fd->glyphs[mid].cp == cp) return &fd->glyphs[mid];
        if ((long)fd->glyphs[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

int bl_font_line_height(BlFont font) { return font_of(font)->line_h; }
int bl_font_ascent(BlFont font) { return font_of(font)->ascent; }

int bl_text_width(BlFont font, const char *utf8)
{
    if (!utf8) return 0;
    const BlFontData *fd = font_of(font);
    const unsigned char *p = (const unsigned char *)utf8;
    int w = 0;
    while (*p) {
        long cp = next_cp(&p);
        const BlGlyph *g = cp < 0 ? NULL : glyph_of(fd, cp);
        if (g) w += g->adv;
    }
    return w;
}

/* Draw text coverage. mode 0: blend rgb; mode 1: additive with alpha scale. */
static int draw_text(BlFrame *f, BlFont font, int x, int y, const char *utf8, uint32_t rgb, int mode, int scale)
{
    if (!utf8) return 0;
    const BlFontData *fd = font_of(font);
    const unsigned char *p = (const unsigned char *)utf8;
    long long pen = x;
    while (*p) {
        long cp = next_cp(&p);
        const BlGlyph *g = cp < 0 ? NULL : glyph_of(fd, cp);
        if (!g) continue;
        if (f && f->pixels && g->w && g->h) {
            long long gx = pen + g->left, gy = (long long)y + fd->ascent - g->top;
            int x0, y0, x1, y1;
            if (clip(f, gx, gy, g->w, g->h, &x0, &y0, &x1, &y1)) {
                for (int yy = y0; yy < y1; yy++) {
                    uint32_t *row = row_ptr(f, yy);
                    const uint8_t *a = fd->alpha + g->off + (size_t)(yy - gy) * g->w + (size_t)(x0 - gx);
                    for (int xx = x0; xx < x1; xx++, a++) {
                        if (!*a) continue;
                        row[xx] = mode ? add(row[xx], rgb, (*a * scale) >> 8) : mix(row[xx], rgb, *a);
                    }
                }
            }
        }
        pen += g->adv;
    }
    return (int)(pen - x);
}

int bl_text(BlFrame *f, BlFont font, int x, int y, const char *utf8, uint32_t rgb)
{
    return draw_text(f, font, x, y, utf8, rgb, 0, 256);
}

/* A soft halo: the glyphs stamped additively around a circle, then the text. */
static const signed char RING[12][2] = {
    { 5, 0 }, { 4, 3 }, { 0, 5 }, { -4, 3 }, { -5, 0 }, { -4, -3 }, { 0, -5 }, { 4, -3 },
    { 2, 0 }, { 0, 2 }, { -2, 0 }, { 0, -2 },
};

/* The halo of a text as one image (owner 2026-11-15: smooth menus). The
 * stamps only add light, saturating per channel, so their sum per pixel
 * (each stamp rounded exactly as add() rounds it) added once gives the same
 * pixels. Top byte 0xFF: a pixel a stamp touched (add() rewrites it). */
typedef struct Halo {
    int font, x0, y0, w, h;          /* box relative to the text's (x, y) */
    uint32_t glow, used;
    char text[96];
    uint32_t *px;
} Halo;
enum { HALOS = 24 };
static Halo g_halo[HALOS];
static unsigned g_halo_clock;
static size_t g_halo_bytes;

static const Halo *halo_get(BlFont font, const char *utf8, uint32_t glow)
{
    size_t len = strlen(utf8);
    if (len >= sizeof g_halo[0].text) return NULL;
    Halo *h = &g_halo[0];
    for (int i = 0; i < HALOS; i++) {
        Halo *c = &g_halo[i];
        if (c->px && c->font == (int)font && c->glow == glow && !strcmp(c->text, utf8)) { c->used = ++g_halo_clock; return c; }
        if (c->used < h->used) h = c;
    }
    /* The box: every glyph of the text at every ring offset. */
    const BlFontData *fd = font_of(font);
    long long minx = 0, miny = 0, maxx = 0, maxy = 0, pen = 0;
    int any = 0;
    for (const unsigned char *p = (const unsigned char *)utf8; *p;) {
        long cp = next_cp(&p);
        const BlGlyph *g = cp < 0 ? NULL : glyph_of(fd, cp);
        if (!g) continue;
        if (g->w && g->h) {
            long long gx = pen + g->left, gy = (long long)fd->ascent - g->top;
            if (!any || gx < minx) minx = gx;
            if (!any || gy < miny) miny = gy;
            if (!any || gx + g->w > maxx) maxx = gx + g->w;
            if (!any || gy + g->h > maxy) maxy = gy + g->h;
            any = 1;
        }
        pen += g->adv;
    }
    if (!any) return NULL;
    int x0 = (int)minx - 5, y0 = (int)miny - 5, w = (int)(maxx - minx) + 10, hh = (int)(maxy - miny) + 10;
    size_t n = (size_t)w * (size_t)hh;
    if (n > 512 * 1024) return NULL;
    uint16_t *acc = calloc(n * 3, sizeof *acc);
    uint32_t *px = malloc(n * 4);
    if (!acc || !px) { free(acc); free(px); return NULL; }
    uint8_t *touched = (uint8_t *)(void *)px;                 /* reused below, byte per pixel first */
    memset(touched, 0, n);
    unsigned gr = (glow >> 16) & 255, gg = (glow >> 8) & 255, gb = glow & 255;
    for (int i = 0; i < 12; i++) {
        int scale = i < 8 ? 26 : 40;
        long long pen2 = RING[i][0];
        for (const unsigned char *p = (const unsigned char *)utf8; *p;) {
            long cp = next_cp(&p);
            const BlGlyph *g = cp < 0 ? NULL : glyph_of(fd, cp);
            if (!g) continue;
            for (int yy = 0; yy < g->h; yy++)
                for (int xx = 0; xx < g->w; xx++) {
                    int a = fd->alpha[g->off + (size_t)yy * g->w + (size_t)xx];
                    if (!a) continue;
                    int amt = (a * scale) >> 8;
                    if (amt <= 0) continue;
                    int X = (int)(pen2 + g->left) + xx - x0, Y = fd->ascent - g->top + RING[i][1] + yy - y0;
                    size_t k = (size_t)Y * (size_t)w + (size_t)X;
                    acc[k * 3] = (uint16_t)(acc[k * 3] + (gr * (unsigned)amt + 127) / 255);
                    acc[k * 3 + 1] = (uint16_t)(acc[k * 3 + 1] + (gg * (unsigned)amt + 127) / 255);
                    acc[k * 3 + 2] = (uint16_t)(acc[k * 3 + 2] + (gb * (unsigned)amt + 127) / 255);
                    touched[k] = 1;
                }
            pen2 += g->adv;
        }
    }
    for (size_t k = n; k-- > 0;) {                           /* backwards: touched[] shares px */
        unsigned r = acc[k * 3] > 255 ? 255 : acc[k * 3], g = acc[k * 3 + 1] > 255 ? 255 : acc[k * 3 + 1];
        unsigned b = acc[k * 3 + 2] > 255 ? 255 : acc[k * 3 + 2];
        uint32_t t = touched[k] ? 0xFF000000u : 0;
        px[k] = t | r << 16 | g << 8 | b;
    }
    free(acc);
    if (h->px) { free(h->px); g_halo_bytes -= (size_t)h->w * (size_t)h->h * 4; }
    h->font = (int)font; h->glow = glow; h->x0 = x0; h->y0 = y0; h->w = w; h->h = hh; h->px = px;
    memcpy(h->text, utf8, len + 1);
    h->used = ++g_halo_clock;
    g_halo_bytes += n * 4;
    return h;
}

void bl_ui_release(void)
{
    for (int i = 0; i < HALOS; i++) { free(g_halo[i].px); memset(&g_halo[i], 0, sizeof g_halo[i]); }
    g_halo_bytes = 0;
}

size_t bl_ui_cache_bytes(void) { return g_halo_bytes; }

int bl_text_glow(BlFrame *f, BlFont font, int x, int y, const char *utf8, uint32_t rgb, uint32_t glow)
{
    const Halo *h = f && f->pixels && utf8 ? halo_get(font, utf8, glow) : NULL;
    if (!h) {
        for (size_t i = 0; i < sizeof RING / sizeof RING[0]; i++)
            draw_text(f, font, x + RING[i][0], y + RING[i][1], utf8, glow, 1, i < 8 ? 26 : 40);
        return draw_text(f, font, x, y, utf8, rgb, 0, 256);
    }
    int x0, y0, x1, y1;
    if (clip(f, (long long)x + h->x0, (long long)y + h->y0, h->w, h->h, &x0, &y0, &x1, &y1)) {
        for (int yy = y0; yy < y1; yy++) {
            uint32_t *row = row_ptr(f, yy);
            const uint32_t *hp = h->px + (size_t)(yy - y - h->y0) * (size_t)h->w + (size_t)(x0 - x - h->x0);
            int xx = x0;
#ifdef __SSE2__
            const __m128i top = _mm_set1_epi32((int)0xFF000000u);
            for (; xx + 4 <= x1; xx += 4) {
                __m128i v = _mm_loadu_si128((const __m128i *)(const void *)(hp + (xx - x0)));
                __m128i d = _mm_loadu_si128((const __m128i *)(const void *)(row + xx));
                __m128i t = _mm_and_si128(v, top);
                d = _mm_andnot_si128(t, _mm_adds_epu8(d, _mm_andnot_si128(top, v)));
                _mm_storeu_si128((__m128i *)(void *)(row + xx), d);
            }
#endif
            for (; xx < x1; xx++) {
                uint32_t v = hp[xx - x0];
                if (!(v >> 24)) continue;
                uint32_t d = row[xx];
                unsigned r = ((d >> 16) & 255) + ((v >> 16) & 255), g = ((d >> 8) & 255) + ((v >> 8) & 255), b = (d & 255) + (v & 255);
                row[xx] = (r > 255 ? 255u : r) << 16 | (g > 255 ? 255u : g) << 8 | (b > 255 ? 255u : b);
            }
        }
    }
    return draw_text(f, font, x, y, utf8, rgb, 0, 256);
}

/* ------------------------------------------------------------------ */
/* Background                                                         */
/* ------------------------------------------------------------------ */

static uint32_t sky_at(float t)
{
    static const struct { float t; uint32_t c; } stops[] = {
        { 0.00f, 0x151A24 }, { 0.30f, 0x222833 }, { 0.46f, 0x30333A },
        { 0.52f, 0x3A3B3E }, { 0.70f, 0x2E2D2C }, { 1.00f, 0x1C1B1A },
    };
    for (size_t i = 1; i < sizeof stops / sizeof stops[0]; i++)
        if (t <= stops[i].t)
            return lerp_rgb(stops[i - 1].c, stops[i].c, (t - stops[i - 1].t) / (stops[i].t - stops[i - 1].t));
    return 0x1C1B1A;
}

void bl_background(BlFrame *f, BlBackground kind)
{
    if (!f || !f->pixels) return;
    int W = (int)f->width, H = (int)f->height;
    if (kind == BL_BG_BLACK) { bl_fill(f, 0, 0, W, H, 0); return; }
    for (int y = 0; y < H; y++) {
        uint32_t c = sky_at(H > 1 ? (float)y / (float)(H - 1) : 0.0f);
        uint32_t *row = row_ptr(f, y);
        for (int x = 0; x < W; x++) row[x] = c;
    }
    /* Warm street lights. */
    bl_glow_dot(f, W * 18 / 100, H * 30 / 100, W * 26 / 100, 0xFFBE6E, 46);
    bl_glow_dot(f, W * 86 / 100, H * 24 / 100, W * 22 / 100, 0xFFDCAA, 36);
    /* Tail-light trails, as behind the game's menus. */
    if (kind == BL_BG_START) {
        bl_trail(f, -W / 20, H * 47 / 100, W * 65 / 100, H * 47 / 100, 6, BL_RGB_TRAIL, 180, 0);
        bl_trail(f, W * 20 / 100, H * 50 / 100, W * 110 / 100, H * 50 / 100, 4, 0xFF3B2A, 140, 0);
        bl_trail(f, -W / 10, H * 53 / 100, W * 50 / 100, H * 53 / 100, 3, 0xFF2020, 128, 0);
        bl_trail(f, 0, H * 14 / 100, W * 60 / 100, H * 22 / 100, 2, 0xFFFFFF, 90, 0);
    } else {
        for (int i = 0; i < 4; i++) {
            int y0 = H * (30 + 15 * i) / 100;
            bl_trail(f, -W / 10, y0 + H / 3, W * 80 / 100, y0 - H / 3, 5 - (i & 1), BL_RGB_TRAIL, 135 - 10 * (i & 1), 0);
        }
    }
    /* Blue bokeh. */
    static const int bokeh[][2] = { { 62, 36 }, { 35, 78 }, { 18, 64 }, { 79, 70 } };
    for (size_t i = 0; i < sizeof bokeh / sizeof bokeh[0]; i++)
        bl_glow_dot(f, W * bokeh[i][0] / 100, H * bokeh[i][1] / 100, 9, 0x96BEFF, 120);
    /* Darken for legibility, add a vignette, and dither so the dark
     * gradients do not show 8-bit bands on a TV. */
    static const signed char bayer[4][4] = { { -8, 0, -6, 2 }, { 4, -4, 6, -2 }, { -5, 3, -7, 1 }, { 7, -1, 5, -3 } };
    float cx = (float)W * 0.5f, cy = (float)H * 0.5f;
    for (int y = 0; y < H; y++) {
        uint32_t *row = row_ptr(f, y);
        float ny = ((float)y - cy) / cy;
        for (int x = 0; x < W; x++) {
            float nx = ((float)x - cx) / cx;
            float v = clampf((sqrtf(nx * nx + ny * ny) / 1.3f - 0.55f) / 0.45f, 0.0f, 1.0f);
            v = v * v * (3.0f - 2.0f * v);
            uint32_t c = mix(row[x], 0x05080F, 90 + (int)(150.0f * v));
            int d = bayer[y & 3][x & 3];             /* -8..7, applied as -1..+1 */
            int r = clampi((int)((c >> 16) & 255) + (d + 8) / 6 - 1, 0, 255);
            int g = clampi((int)((c >> 8) & 255) + (d + 8) / 6 - 1, 0, 255);
            int b = clampi((int)(c & 255) + (d + 8) / 6 - 1, 0, 255);
            row[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
}
