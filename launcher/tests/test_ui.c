/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests for bl_ui (plan Task 2). */
#include "bl_ui.h"
#include "bl_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 1920
#define H 1080
#define PAD_ROWS 8
#define PAD_COLS 16                 /* extra pixels per row beyond width */
#define STRIDE ((W + PAD_COLS) * 4)
#define CANARY 0xA5u

static uint8_t *g_buf;
static size_t g_size;

/* A frame inside a larger buffer: canary rows above and below, canary
 * columns after each row. */
static BlFrame guarded(void)
{
    g_size = (size_t)STRIDE * (H + 2 * PAD_ROWS);
    g_buf = malloc(g_size);
    memset(g_buf, CANARY, g_size);
    BlFrame f = { g_buf + (size_t)STRIDE * PAD_ROWS, W, H, STRIDE };
    for (int y = 0; y < H; y++) memset(f.pixels + (size_t)y * STRIDE, 0, W * 4);
    return f;
}

static int canaries_intact(void)
{
    for (size_t i = 0; i < (size_t)STRIDE * PAD_ROWS; i++)
        if (g_buf[i] != CANARY) return 0;
    for (size_t i = g_size - (size_t)STRIDE * PAD_ROWS; i < g_size; i++)
        if (g_buf[i] != CANARY) return 0;
    for (int y = 0; y < H; y++) {
        const uint8_t *row = g_buf + (size_t)STRIDE * (PAD_ROWS + y);
        for (int i = W * 4; i < STRIDE; i++) if (row[i] != CANARY) return 0;
    }
    return 1;
}

static uint32_t px(const BlFrame *f, int x, int y)
{
    uint32_t v; memcpy(&v, f->pixels + (size_t)y * f->stride + (size_t)x * 4, 4);
    return v & 0xFFFFFF;
}

static int luma(uint32_t c) { return ((c >> 16) & 255) * 3 + ((c >> 8) & 255) * 6 + (c & 255); }

static void test_clipping_keeps_canaries(void)
{
    BlFrame f = guarded();
    const int rects[][4] = {
        { -100, -100, 300, 300 }, { 1800, 1000, 400, 400 }, { 0, 0, 0, 0 }, { 5, 5, -10, -10 },
        { -5000, -5000, 20000, 20000 }, { 1919, 1079, 1, 1 }, { 1920, 1080, 10, 10 }, { 0, -1, W, 1 },
    };
    for (size_t i = 0; i < sizeof rects / sizeof rects[0]; i++) {
        const int *r = rects[i];
        bl_fill(&f, r[0], r[1], r[2], r[3], 0xFFFFFF);
        bl_fill_alpha(&f, r[0], r[1], r[2], r[3], 0x123456, 128);
        bl_gradient_v(&f, r[0], r[1], r[2], r[3], 0x111111, 0xEEEEEE);
        bl_glass(&f, r[0], r[1], r[2], r[3], 24, (int)(i & 1));
        bl_glass_tilted(&f, r[0], r[1], r[2], r[3], 24, (i & 1) ? 0.8f : -0.8f);
        bl_pill(&f, r[0], r[1], r[2], r[3], (int)(i & 1));
        bl_trail(&f, r[0], r[1], r[0] + r[2], r[1] + r[3], 6, BL_RGB_TRAIL, 200, 1);
        bl_glow_dot(&f, r[0], r[1], r[2] > 0 ? r[2] : 30, 0xFFC080, 120);
        bl_blit_add(&f, bl_logo_rgb, bl_logo_w, bl_logo_h, r[0], r[1], 300);
        bl_text(&f, BL_FONT_TITLE, r[0], r[1], "play blur", BL_RGB_CREAM);
        bl_text_glow(&f, BL_FONT_HEAD, r[0], r[1], "settings", BL_RGB_WHITE, BL_RGB_RIM);
    }
    bl_glass_tilted(&f, 100, 100, 300, 300, 24, 5.0f);   /* out-of-range turn */
    bl_glass(&f, 100, 100, 40, 40, 500, 1);                 /* radius larger than box */
    bl_trail(&f, 500, 500, 500, 500, 4, BL_RGB_TRAIL, 255, 1); /* zero length */
    bl_trail(&f, 0, 0, 100, 100, 0, BL_RGB_TRAIL, 255, 1);     /* zero thickness */
    bl_text(&f, BL_FONT_BODY, 0, 0, NULL, 0xFFFFFF);
    CHECK(canaries_intact());
    bl_background(&f, BL_BG_START);
    bl_background(&f, BL_BG_LIST);
    bl_background(&f, BL_BG_BLACK);
    CHECK(canaries_intact());
    free(g_buf);
}

static void test_text_width_matches_advance(void)
{
    BlFrame f = guarded();
    for (int font = 0; font < BL_FONT_COUNT; font++) {
        int w = bl_text_width((BlFont)font, "play");
        CHECK(w > 0);
        CHECK_EQ(w, bl_text(&f, (BlFont)font, 100, 100, "play", 0xFFFFFF));
        CHECK(bl_font_line_height((BlFont)font) > bl_font_ascent((BlFont)font));
        CHECK(bl_font_ascent((BlFont)font) > 0);
    }
    /* Bigger fonts are wider. */
    CHECK(bl_text_width(BL_FONT_TITLE, "play") > bl_text_width(BL_FONT_NOTE, "play"));
    /* Text actually puts pixels down. */
    int lit = 0;
    for (int y = 100; y < 160; y++) for (int x = 100; x < 400; x++) lit += px(&f, x, y) != 0;
    CHECK(lit > 50);
    CHECK(canaries_intact());
    free(g_buf);
}

static void test_text_hostile_input(void)
{
    BlFrame f = guarded();
    const char bad[] = { (char)0xFF, 'a', (char)0xC3, (char)0x28, (char)0xE2, (char)0x82, 'b',
                         (char)0xF0, (char)0x9F, (char)0x98, 0 };
    int w = bl_text(&f, BL_FONT_BODY, 10, 10, bad, 0xFFFFFF);
    CHECK_EQ(w, bl_text_width(BL_FONT_BODY, bad));
    CHECK(w == bl_text_width(BL_FONT_BODY, "a(b"));   /* only the valid ASCII is drawn */
    char *long_text = malloc(10001);
    memset(long_text, 'W', 10000); long_text[10000] = 0;
    w = bl_text(&f, BL_FONT_TITLE, 1800, 1050, long_text, 0xFFFFFF);
    CHECK(w > W);
    bl_text_glow(&f, BL_FONT_TITLE, -500, -40, long_text, 0xFFFFFF, BL_RGB_RIM);
    free(long_text);
    CHECK(canaries_intact());
    free(g_buf);
}

static void test_focused_glass_is_brighter(void)
{
    BlFrame f = guarded();
    bl_fill(&f, 0, 0, W, H, 0x101418);
    bl_glass(&f, 100, 100, 400, 450, 26, 0);
    bl_glass(&f, 700, 100, 400, 450, 26, 1);
    /* Rim pixel in the middle of the left edge. */
    CHECK(luma(px(&f, 701, 325)) > luma(px(&f, 101, 325)));
    /* Inside is blue: blue channel dominates. */
    uint32_t c = px(&f, 300, 300);
    CHECK((c & 255) > ((c >> 16) & 255));
    CHECK(canaries_intact());
    free(g_buf);
}

static void test_black_blit_is_invisible(void)
{
    BlFrame f = guarded();
    bl_background(&f, BL_BG_START);
    uint8_t *before = malloc(g_size);
    memcpy(before, g_buf, g_size);
    static uint8_t black[64 * 32 * 3];
    bl_blit_add(&f, black, 64, 32, 500, 500, 512);
    CHECK(memcmp(before, g_buf, g_size) == 0);
    /* The real logo adds light. */
    bl_blit_add(&f, bl_logo_rgb, bl_logo_w, bl_logo_h, 700, 300, 256);
    CHECK(memcmp(before, g_buf, g_size) != 0);
    free(before);
    free(g_buf);
}

static void test_background_is_static(void)
{
    for (int kind = 0; kind < 3; kind++) {
        BlFrame f = guarded();
        bl_background(&f, (BlBackground)kind);
        uint8_t *first = malloc(g_size);
        memcpy(first, g_buf, g_size);
        bl_fill(&f, 0, 0, W, H, 0x777777);
        bl_background(&f, (BlBackground)kind);
        CHECK(memcmp(first, g_buf, g_size) == 0);
        free(first);
        free(g_buf);
    }
}

static void test_image_scale(void)
{
    const uint8_t src[2 * 1 * 3] = { 0, 0, 0, 200, 100, 50 };
    uint8_t dst[4 * 2 * 3];
    bl_image_scale(src, 2, 1, dst, 4, 2);
    CHECK_EQ(dst[0], 0);                          /* left edge stays black */
    CHECK_EQ(dst[3 * 3 + 0], 200);                /* right edge keeps the colour */
    CHECK(dst[1 * 3] > 0 && dst[1 * 3] < 200);    /* in between is interpolated */
}

int main(void)
{
    RUN(test_clipping_keeps_canaries);
    RUN(test_text_width_matches_advance);
    RUN(test_text_hostile_input);
    RUN(test_focused_glass_is_brighter);
    RUN(test_black_blit_is_invisible);
    RUN(test_background_is_static);
    RUN(test_image_scale);
    return bl_test_report("test_ui");
}
