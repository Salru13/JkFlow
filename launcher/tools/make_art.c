/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * JkFlow's own artwork (owner decisions 2026-11-15: no official Blur logo,
 * key art or screenshots; no cars). Drawn from code only: the short mark
 * "JkFlow" (icon, launcher logo) and the same name (backgrounds) in
 * Nunito Black Italic (SIL OFL, assets/font), a small "PS4", a dark
 * background and neon speed streaks. Host tool, needs FreeType and the launcher's bl_ui:
 *
 *   make_art NUNITO_ITALIC_VF.ttf OUTDIR
 *
 * writes OUTDIR/icon0.png (512x512), OUTDIR/logo.png (512x512 on black: the
 * launcher logo source, assets/logo/blur-icon0.png, that bake_logo.py crops), OUTDIR/pic0.png and OUTDIR/pic1.png (1920x1080).
 * Then tools/png_recompress.py shrinks the files (pixels unchanged).
 */
#include "bl_png.h"
#include "bl_ui.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FT_Face g_face;

static void die(const char *what, int err) { fprintf(stderr, "make_art: %s (%d)\n", what, err); exit(1); }

/* ---- a float coverage mask the size of the frame ---- */
typedef struct Mask { float *a; int w, h; } Mask;

static Mask mask_new(int w, int h)
{
    Mask m = { calloc((size_t)w * h, sizeof(float)), w, h };
    if (!m.a) die("out of memory", 0);
    return m;
}

/* Text into the mask, centred on cx, baseline at by; letter spacing in px. */
static void text_mask(Mask *m, const char *s, int px, int weight, int cx, int by, int spacing)
{
    FT_MM_Var *mm = NULL;
    if (FT_Get_MM_Var(g_face, &mm)) die("FT_Get_MM_Var", 0);
    FT_Fixed coords[8] = { 0 };
    for (FT_UInt a = 0; a < mm->num_axis && a < 8; a++) {
        coords[a] = mm->axis[a].def;
        if (mm->axis[a].tag == FT_MAKE_TAG('w', 'g', 'h', 't')) coords[a] = (FT_Fixed)weight << 16;
    }
    FT_Set_Var_Design_Coordinates(g_face, mm->num_axis, coords);
    FT_Done_MM_Var(NULL, mm);
    FT_Set_Pixel_Sizes(g_face, 0, (FT_UInt)px);
    int width = 0;
    for (const char *p = s; *p; p++) {
        if (FT_Load_Char(g_face, (unsigned char)*p, FT_LOAD_DEFAULT)) die("FT_Load_Char", *p);
        width += (int)(g_face->glyph->advance.x >> 6) + (p[1] ? spacing : 0);
    }
    int pen = cx - width / 2;
    for (const char *p = s; *p; p++) {
        if (FT_Load_Char(g_face, (unsigned char)*p, FT_LOAD_RENDER)) die("FT_Load_Char", *p);
        FT_GlyphSlot g = g_face->glyph;
        for (unsigned r = 0; r < g->bitmap.rows; r++)
            for (unsigned c = 0; c < g->bitmap.width; c++) {
                int x = pen + g->bitmap_left + (int)c, y = by - g->bitmap_top + (int)r;
                if (x < 0 || y < 0 || x >= m->w || y >= m->h) continue;
                float v = g->bitmap.buffer[r * (unsigned)g->bitmap.pitch + c] / 255.0f;
                if (v > m->a[y * m->w + x]) m->a[y * m->w + x] = v;
            }
        pen += (int)(g->advance.x >> 6) + spacing;
    }
}

/* Three box blurs (about a Gaussian), radius r, in place. */
static void blur(Mask *m, int r)
{
    float *t = malloc(sizeof(float) * (size_t)m->w * m->h);
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < m->h; y++) {
            float acc = 0; float *row = m->a + (size_t)y * m->w;
            for (int x = -r; x <= r; x++) acc += row[x < 0 ? 0 : x >= m->w ? m->w - 1 : x];
            for (int x = 0; x < m->w; x++) {
                t[(size_t)y * m->w + x] = acc / (2 * r + 1);
                int a = x - r, b = x + r + 1;
                acc += row[b >= m->w ? m->w - 1 : b] - row[a < 0 ? 0 : a];
            }
        }
        for (int x = 0; x < m->w; x++) {
            float acc = 0;
            for (int y = -r; y <= r; y++) acc += t[(size_t)(y < 0 ? 0 : y >= m->h ? m->h - 1 : y) * m->w + x];
            for (int y = 0; y < m->h; y++) {
                m->a[(size_t)y * m->w + x] = acc / (2 * r + 1);
                int a = y - r, b = y + r + 1;
                acc += t[(size_t)(b >= m->h ? m->h - 1 : b) * m->w + x] - t[(size_t)(a < 0 ? 0 : a) * m->w + x];
            }
        }
    }
    free(t);
}

static uint32_t *px(BlFrame *f, int x, int y) { return (uint32_t *)(void *)(f->pixels + (size_t)y * f->stride) + x; }

/* Add (glow) or mix (solid) colour rgb at strength a into one pixel. */
static void put(BlFrame *f, int x, int y, uint32_t rgb, float a, int add)
{
    if (x < 0 || y < 0 || x >= (int)f->width || y >= (int)f->height || a <= 0) return;
    if (a > 1) a = 1;
    uint32_t *d = px(f, x, y);
    int out[3];
    for (int k = 0; k < 3; k++) {
        int sh = 16 - 8 * k, dc = (int)((*d >> sh) & 255), sc = (int)((rgb >> sh) & 255);
        int v = add ? dc + (int)(sc * a) : (int)(dc + (sc - dc) * a);
        out[k] = v > 255 ? 255 : v;
    }
    *d = (uint32_t)out[0] << 16 | (uint32_t)out[1] << 8 | (uint32_t)out[2];
}

static uint32_t lerp_rgb(uint32_t a, uint32_t b, float t)
{
    uint32_t o = 0;
    for (int k = 0; k < 3; k++) {
        int sh = 16 - 8 * k, ca = (int)((a >> sh) & 255), cb = (int)((b >> sh) & 255);
        o |= (uint32_t)(ca + (int)((cb - ca) * t)) << sh;
    }
    return o;
}

/* Our palette: cyan -> violet -> magenta across the word. */
static uint32_t ramp(float t)
{
    return t < 0.5f ? lerp_rgb(0x22D3EE, 0x8B5CF6, t * 2) : lerp_rgb(0x8B5CF6, 0xEC4899, (t - 0.5f) * 2);
}

/* The wordmark: glow in the ramp colours, then the letters in white. */
static void wordmark(BlFrame *f, const char *name, int cx, int by, int size, int ps4_size)
{
    Mask t = mask_new((int)f->width, (int)f->height);
    text_mask(&t, name, size, 900, cx, by, size / 40);
    Mask g = mask_new(t.w, t.h);
    memcpy(g.a, t.a, sizeof(float) * (size_t)t.w * t.h);
    blur(&g, size / 9 > 2 ? size / 9 : 2);
    int half = (int)strlen(name) * size / 3;
    int x0 = cx - half, x1 = cx + half;
    for (int y = 0; y < t.h; y++)
        for (int x = 0; x < t.w; x++) {
            float tt = (float)(x - x0) / (float)(x1 - x0);
            tt = tt < 0 ? 0 : tt > 1 ? 1 : tt;
            put(f, x, y, ramp(tt), g.a[(size_t)y * t.w + x] * 1.6f, 1);
            put(f, x, y, 0xFFFFFF, t.a[(size_t)y * t.w + x], 0);
        }
    memset(t.a, 0, sizeof(float) * (size_t)t.w * t.h);
    text_mask(&t, "PS4", ps4_size, 700, cx, by + ps4_size * 2, ps4_size / 2);
    for (int i = 0; i < t.w * t.h; i++) if (t.a[i] > 0) put(f, i % t.w, i / t.w, 0xC8D2E6, t.a[i], 0);
    free(t.a); free(g.a);
}

/* ---- background: dark navy centre to black edges, neon speed streaks ---- */
static void background(BlFrame *f)
{
    float cx = f->width / 2.0f, cy = f->height * 0.45f, r = f->width * 0.75f;
    for (uint32_t y = 0; y < f->height; y++)
        for (uint32_t x = 0; x < f->width; x++) {
            float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy)) / r;
            if (d > 1) d = 1;
            /* dithered (fixed hash noise, reproducible): no rings in the dark gradient */
            uint32_t h = (x * 73856093u) ^ (y * 19349663u);
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            float n = (float)(h & 1023) / 1023.0f;
            static const float A[3] = { 0x16, 0x1C, 0x34 }, B[3] = { 0x03, 0x04, 0x09 };
            uint32_t o = 0;
            for (int k = 0; k < 3; k++) o |= (uint32_t)(A[k] + (B[k] - A[k]) * d + n) << (16 - 8 * k);
            *px(f, (int)x, (int)y) = o;
        }
}

/* Streaks rise slightly to the right, like motion; s scales them to the frame. */
static void streaks(BlFrame *f, float s)
{
    static const struct { float x0, y0, x1, y1; int th; uint32_t rgb; int a; } S[] = {
        { -0.10f, 0.30f, 0.62f, 0.22f, 3, 0x22D3EE, 200 },
        {  0.30f, 0.74f, 1.10f, 0.64f, 4, 0xEC4899, 210 },
        { -0.05f, 0.80f, 0.45f, 0.75f, 2, 0x8B5CF6, 170 },
        {  0.55f, 0.16f, 1.05f, 0.11f, 2, 0xEC4899, 150 },
        {  0.62f, 0.86f, 1.10f, 0.81f, 2, 0x22D3EE, 150 },
    };
    for (unsigned i = 0; i < sizeof S / sizeof S[0]; i++) {
        int th = (int)(S[i].th * s); if (th < 1) th = 1;
        bl_trail(f, (int)(S[i].x0 * f->width), (int)(S[i].y0 * f->height), (int)(S[i].x1 * f->width),
                 (int)(S[i].y1 * f->height), th, S[i].rgb, S[i].a, 1);
    }
}

static BlFrame frame(int w, int h)
{
    BlFrame f = { calloc((size_t)w * h, 4), (uint32_t)w, (uint32_t)h, (uint32_t)w * 4 };
    if (!f.pixels) die("out of memory", 0);
    return f;
}

static void save(const BlFrame *f, const char *dir, const char *name)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (bl_png_write(path, f) != 0) die("cannot write", 0);
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: make_art NUNITO_ITALIC_VF.ttf OUTDIR\n"); return 2; }
    FT_Library lib;
    if (FT_Init_FreeType(&lib)) die("FT_Init_FreeType", 0);
    if (FT_New_Face(lib, argv[1], 0, &g_face)) die("FT_New_Face", 0);

    /* icon0: the home-screen icon. */
    BlFrame ic = frame(512, 512);
    background(&ic);
    streaks(&ic, 1.0f);
    wordmark(&ic, "JkFlow", 256, 292, 128, 26);
    save(&ic, argv[2], "icon0.png");

    /* logo: the same mark on pure black (the launcher adds it onto its screens), inside
     * bake_logo.py's crop (x 50..462, y 150..362). */
    BlFrame lg = frame(512, 512);
    wordmark(&lg, "JkFlow", 256, 285, 100, 22);
    save(&lg, argv[2], "logo.png");
    free(lg.pixels);

    /* pic1: home-screen background, pic0: the same with fewer streaks (loader look). */
    for (int k = 0; k < 2; k++) {
        BlFrame f = frame(1920, 1080);
        background(&f);
        if (k) streaks(&f, 2.0f);
        wordmark(&f, "JkFlow", 960, 560, 230, 52);
        save(&f, argv[2], k ? "pic1.png" : "pic0.png");
        free(f.pixels);
    }
    free(ic.pixels);
    FT_Done_Face(g_face);
    FT_Done_FreeType(lib);
    return 0;
}
