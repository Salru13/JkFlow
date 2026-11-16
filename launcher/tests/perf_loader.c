/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Loader cost budget (plan Task 3 test f, Review Focus 6): 300 frames in
 * under 1.5 s on one core at -O2, without sanitizers. */
#define _POSIX_C_SOURCE 199309L
#include "bl_loader.h"
#include "bl_test.h"

#include <stdlib.h>
#include <time.h>

int main(void)
{
    BlFrame f = { malloc(1920u * 1080u * 4u), 1920, 1080, 1920 * 4 };
    bl_loader_draw(&f, 0);                         /* first call prepares the logo */
    /* The best of three rounds, so a busy host does not fail it. */
    double s = 1e9;
    for (int round = 0; round < 3; round++) {
        struct timespec a, b;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &a);
        for (int i = 0; i < 300; i++) bl_loader_draw(&f, (uint64_t)i * 33u);
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &b);
        double t = (double)(b.tv_sec - a.tv_sec) + (double)(b.tv_nsec - a.tv_nsec) / 1e9;
        if (t < s) s = t;
    }
    fprintf(stderr, "perf_loader: 300 frames in %.3f s CPU (%.2f ms/frame)\n", s, s * 1000.0 / 300.0);
    CHECK_TIMING(s < 0.6);                  /* 2 ms per frame on the host (review I4) */
    free(f.pixels);
    return bl_test_report("perf_loader");
}
