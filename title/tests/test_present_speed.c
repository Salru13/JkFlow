/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The title's present (prospero/src/pw_present.c, owner finding 34a): every
 * mode and placement gives the same pixels as the plain definition below
 * (rows outside the shown rectangle and the borders the background, inside
 * source row floor(i*h/shown_h) and column floor(i*w/shown_w), alpha 0xff),
 * and the launcher's case, a 1920x1080 frame onto the 1920x1080 scanout,
 * takes at most 2 ms on one host core at -O2 (it was 18.4 ms: a byte-wise
 * background fill of the whole target, then a byte-wise copy with a division
 * per pixel, into write-combined memory on the PS4).
 *   test_present_speed          correctness (run it under ASan/UBSan)
 *   test_present_speed bench    also the timing budget (build it -O2): a miss
 *                               warns; BL_PERF_STRICT=1 makes it fail */
#define _POSIX_C_SOURCE 199309L
#include "pw_present.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint32_t rd(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

/* The definition: what every target pixel must be. */
static void reference(const PwPresentFrame *fr, const PwPresentPlacement *pl, uint32_t bg, uint8_t *out,
                      uint32_t tw, uint32_t th, uint32_t ts)
{
    for (uint32_t y = 0; y < th; y++)
        for (uint32_t x = 0; x < tw; x++) {
            uint32_t v = bg;
            if (y >= pl->top && y < pl->top + pl->shown_height && x >= pl->left && x < pl->left + pl->shown_width) {
                uint64_t sy = (uint64_t)(y - pl->top) * fr->height / pl->shown_height;
                uint64_t sx = (uint64_t)(x - pl->left) * fr->width / pl->shown_width;
                v = rd(fr->pixels + sy * fr->stride + sx * 4);
            }
            v |= 0xff000000u;
            memcpy(out + (size_t)y * ts + (size_t)x * 4, &v, 4);
        }
}

static uint8_t *noise(size_t n, uint32_t seed)
{
    uint8_t *p = malloc(n);
    for (size_t i = 0; i < n; i++) { seed = seed * 1664525u + 1013904223u; p[i] = (uint8_t)(seed >> 24); }
    return p;
}

/* One case: frame fw x fh (stride padded by fpad pixels) onto tw x th
 * (stride padded by tpad), in mode. */
static void one(uint32_t fw, uint32_t fh, uint32_t fpad, uint32_t tw, uint32_t th, uint32_t tpad, int mode)
{
    uint32_t fs = (fw + fpad) * 4, ts = (tw + tpad) * 4, bg = 0x00336699u;
    uint8_t *src = noise((size_t)fs * fh, fw * 31u + fh);
    uint8_t *got = noise((size_t)ts * th, 7), *want = malloc((size_t)ts * th);
    memcpy(want, got, (size_t)ts * th);                          /* the stride padding stays as it was */
    PwPresentFrame fr = { src, fw, fh, fs, PW_PRESENT_BGRX8 };
    PwPresentTarget t = { got, tw, th, ts, (uint64_t)ts * th };
    PwPresentPlacement pl;
    CHECK(pw_present_scale(&fr, mode, bg, &t, &pl) == PW_OK);
    reference(&fr, &pl, bg, want, tw, th, ts);
    int same = memcmp(got, want, (size_t)ts * th) == 0;
    if (!same) fprintf(stderr, "  differs: %ux%u (+%u) -> %ux%u (+%u) mode %d\n", fw, fh, fpad, tw, th, tpad, mode);
    CHECK(same);
    free(src); free(got); free(want);
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
    int modes[] = { PW_PRESENT_SCALE_INTEGER, PW_PRESENT_SCALE_FIT, PW_PRESENT_SCALE_STRETCH };
    for (int m = 0; m < 3; m++) {
        one(1920, 1080, 0, 1920, 1080, 0, modes[m]);           /* the launcher: 1:1 */
        one(960, 540, 0, 1920, 1080, 0, modes[m]);             /* 2x */
        one(640, 360, 3, 1920, 1080, 0, modes[m]);             /* 3x, padded source rows */
        one(700, 300, 1, 1280, 720, 5, modes[m]);              /* borders on both axes, padded target */
        one(1, 1, 0, 7, 5, 1, modes[m]);
        one(5, 3, 0, 5, 3, 0, modes[m]);                        /* 1:1, rows shorter than a vector */
        one(17, 9, 2, 40, 21, 3, modes[m]);
    }
    /* The other entry points keep their contract. */
    {
        uint8_t *src = noise(64 * 64 * 4, 3), *dst = noise(100 * 80 * 4, 4), *want = malloc(100 * 80 * 4);
        memcpy(want, dst, 100 * 80 * 4);
        PwPresentFrame fr = { src, 64, 64, 256, PW_PRESENT_BGRX8 };
        PwPresentTarget t = { dst, 100, 80, 400, 100 * 80 * 4 };
        PwPresentPlacement pl;
        CHECK(pw_present_fit(&fr, 100, 80, 0, 1, &pl) == PW_OK);
        CHECK(pw_present_compose(&fr, &pl, 0x123456u, &t) == PW_OK);
        reference(&fr, &pl, 0x123456u, want, 100, 80, 400);
        CHECK(memcmp(dst, want, 100 * 80 * 4) == 0);
        PwPresentPlacement bad = pl;
        bad.shown_width++;
        CHECK(pw_present_compose(&fr, &bad, 0, &t) == PW_ERR_LIMIT);
        t.bytes--;
        CHECK(pw_present_compose(&fr, &pl, 0, &t) == PW_ERR_MALFORMED);
        free(src); free(dst); free(want);
    }
    if (argc > 1 && !strcmp(argv[1], "bench")) {
        uint8_t *src = noise(1920u * 1080u * 4u, 9), *dst = malloc(1920u * 1080u * 4u);
        PwPresentFrame fr = { src, 1920, 1080, 1920 * 4, PW_PRESENT_BGRX8 };
        PwPresentTarget t = { dst, 1920, 1080, 1920 * 4, 1920ull * 1080 * 4 };
        double best = 1e9;
        for (int i = 0; i < 30; i++) {
            double a = now_ms();
            CHECK(pw_present_scale(&fr, PW_PRESENT_SCALE_INTEGER, 0, &t, NULL) == PW_OK);
            double b = now_ms() - a;
            if (b < best) best = b;
        }
        /* Audit finding 20: a host timing miss is a warning (the host is not
         * the console and its load varies); BL_PERF_STRICT=1 makes it fail. */
        const char *strict = getenv("BL_PERF_STRICT");
        fprintf(stderr, "  present 1920x1080 1:1: %.2f ms (best of 30, budget 2.00)%s\n", best, best <= 2.0 ? "" : "  OVER");
        if (strict && !strcmp(strict, "1")) CHECK(best <= 2.0);
        else if (best > 2.0) fprintf(stderr, "  WARN timing: %.2f ms over the 2.00 ms budget (BL_PERF_STRICT=1 fails on it)\n", best);
        free(src); free(dst);
    }
    fprintf(stderr, "test_present_speed: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
