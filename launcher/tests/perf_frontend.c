/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The real launcher loop against the host clock (owner finding 34: fluid
 * navigation, 60 FPS target, 30 at least). The fake host's clock is the
 * host's monotonic clock plus the time the loop asked to sleep (a sleep costs
 * no real time here); its present copies the frame to a scanout buffer with
 * 32-bit stores, as the title's 1:1 present does. So the gap between two
 * presents is the loop's own host cost per frame: draw, cross-fade, present.
 * Budget at -O2 on one host core: every frame of a slide or a screen change
 * within 17 ms of the one before; a slide shows at least 8 frames between
 * two cards. The PS4 is about 3-5x slower than the host. A busy host makes
 * single timings swing: the run is repeated and each figure keeps its best. */
#define _POSIX_C_SOURCE 199309L
#include "bl_frontend.h"
#include "bl_screens.h"
#include "bl_test.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { REC = 4096, PASSES = 1100, ROUNDS = 3 };

typedef struct Host {
    uint64_t slept;                       /* ns the loop asked to sleep */
    int pass, np, npress;
    uint64_t pt[REC], press_t[16];
    float ppos[REC];
    char pscreen[REC][12], screen[12];
    uint32_t *scanout;
} Host;

static Host H;
static uint32_t SCRIPT[PASSES];

static uint64_t real_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static uint64_t h_now(void *ctx) { (void)ctx; return real_ns() + H.slept; }
static void h_sleep(void *ctx, uint32_t us) { (void)ctx; H.slept += (uint64_t)us * 1000u; }
static int h_quit(void *ctx) { (void)ctx; return ++H.pass >= PASSES; }
static void h_screen(void *ctx, const char *name) { (void)ctx; snprintf(H.screen, sizeof H.screen, "%.11s", name); }

static int h_pad(void *ctx, uint32_t *held, uint32_t *pressed)
{
    *held = 0;
    *pressed = H.pass - 1 < PASSES ? SCRIPT[H.pass - 1] : 0;
    if (*pressed && H.npress < 16) H.press_t[H.npress++] = h_now(ctx);
    return 0;
}

static int h_present(void *ctx, const BlFrame *f)
{
    const uint32_t *src = (const uint32_t *)(const void *)f->pixels;
    uint32_t *dst = H.scanout;
    for (size_t i = 0, n = (size_t)f->width * f->height; i < n; i++) dst[i] = src[i] | 0xff000000u;
    if (H.np < REC) {
        H.pt[H.np] = h_now(ctx);
        H.ppos[H.np] = bl_start_last_pos;
        snprintf(H.pscreen[H.np], sizeof H.pscreen[0], "%.11s", H.screen);
        H.np++;
    }
    return 0;
}

static int present_at(uint64_t t)
{
    int i = 0;
    while (i < H.np && H.pt[i] < t) i++;
    return i;
}

/* Presents from press `k` up to the next press (or the end): frames that
 * moved between two cards, and the longest gap while it animated (until the
 * last frame that differs in position or screen from the end state). */
static void segment(int k, float from, float to, int *between, double *gap_ms, int *frames)
{
    int a = present_at(H.press_t[k]), b = k + 1 < H.npress ? present_at(H.press_t[k + 1]) : H.np;
    int end = a;
    *between = 0;
    for (int i = a; i < b; i++) {
        float lo = from < to ? from : to, hi = from < to ? to : from;
        if (H.ppos[i] > lo && H.ppos[i] < hi) ++*between;
        if (i > a && (H.pt[i] - H.pt[i - 1]) < 100000000ull) end = i;    /* animated frames come back to back */
    }
    uint64_t g = 0;
    for (int i = a + 1; i <= end; i++) if (H.pt[i] - H.pt[i - 1] > g) g = H.pt[i] - H.pt[i - 1];
    *gap_ms = (double)g / 1e6;
    *frames = end - a + 1;
}

int main(void)
{
    BlServices s;
    memset(&s, 0, sizeof s);
    s.present = h_present; s.pad = h_pad; s.should_quit = h_quit; s.now_ns = h_now; s.sleep_us = h_sleep;
    s.screen = h_screen; s.build_id = "BLUR-1.00";
    H.scanout = malloc(1920u * 1080u * 4u);
    /* Idle first (card images built), then each press with time to finish. */
    static const struct { int at; uint32_t key; float from, to; const char *name; } P[] = {
        { 300, BL_PAD_RIGHT, 1, 2, "slide play->patch" }, { 360, BL_PAD_LEFT, 2, 1, "slide patch->play" },
        { 420, BL_PAD_LEFT, 1, 0, "slide play->settings" }, { 480, BL_PAD_CROSS, 0, 0, "open settings (fade)" },
        { 560, BL_PAD_CIRCLE, 0, 0, "settings back (fade)" }, { 640, BL_PAD_RIGHT, 0, 1, "slide settings->play" },
        { 760, BL_PAD_SQUARE, 1, 1, "open help (fade)" }, { 860, BL_PAD_CIRCLE, 1, 1, "help back (fade)" },
    };
    enum { NP = sizeof P / sizeof P[0] };
    double best_gap[NP];
    int best_between[NP], frames[NP];
    for (int i = 0; i < NP; i++) { best_gap[i] = 1e9; best_between[i] = 0; frames[i] = 0; }
    for (int r = 0; r < ROUNDS; r++) {
        uint32_t *keep = H.scanout;
        memset(&H, 0, sizeof H);
        H.scanout = keep;
        memset(SCRIPT, 0, sizeof SCRIPT);
        for (int i = 0; i < NP; i++) if (P[i].key) SCRIPT[P[i].at] = P[i].key;
        BlDecision d;
        CHECK_EQ(bl_frontend_run(&s, &d), 0);
        int k = 0;
        for (int i = 0; i < NP; i++) {
            if (!P[i].key) continue;
            int between, fr;
            double g;
            segment(k++, P[i].from, P[i].to, &between, &g, &fr);
            if (g < best_gap[i]) best_gap[i] = g;
            if (between > best_between[i]) best_between[i] = between;
            if (fr > frames[i]) frames[i] = fr;
        }
    }
    for (int i = 0; i < NP; i++) {
        if (!P[i].key) continue;
        int slide = P[i].from != P[i].to;
        int ok = best_gap[i] <= 17.0 && (!slide || best_between[i] >= 8) && frames[i] >= 8;
        fprintf(stderr, "  %-24s %3d frames, %3d between cards, longest frame gap %6.2f ms (budget 17)%s\n", P[i].name,
                frames[i], best_between[i], best_gap[i], ok ? "" : "  OVER");
        CHECK_TIMING(ok);
    }
    free(H.scanout);
    return bl_test_report("perf_frontend");
}
