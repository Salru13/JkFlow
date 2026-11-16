/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Screen redraw budget (owner 2026-11-15: "very laggy, like 1 fps"). Every
 * keypress and every frame of the carousel slide redraws the whole 1920x1080
 * frame on the CPU; the PS4 is about 3-5x slower than the host. Budgets on
 * one host core at -O2, without sanitizers:
 *   - a redraw of a screen already drawn once (still screen, focus moved,
 *     progress, every slide frame after the first): <= 8 ms;
 *   - a cold first draw (the screen's caches released): <= 40 ms.
 * Opening the launcher bakes the two backgrounds and draws Start cold once;
 * that is printed, not budgeted.
 * A busy host makes single timings swing 2-3x: each state is drawn several
 * times, the whole run is repeated, and the best time of each figure
 * counts (a keypress counts its slowest state). */
#define _POSIX_C_SOURCE 199309L
#include "bl_exe.h"
#include "bl_screens.h"
#include "bl_test.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WARM_MS 8.0
#define COLD_MS 40.0
#define ROUNDS 3

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

static BlFrame F;
static BlStartState START;
static BlStatus STATUS;
static BlPatchView PATCH;
static BlSettingsState SETTINGS;
static BlHelpState HELP;
static BlDialog DIALOG;

typedef void (*DrawFn)(void);
static void d_start(void) { bl_start_draw(&F, &START, &STATUS); }
static void d_patch(void) { bl_patch_draw(&F, &PATCH); }
static void d_settings(void) { bl_settings_draw(&F, &SETTINGS); }
static void d_help(void) { bl_help_draw(&F, &HELP); }
/* As the frontend draws a question over Start. */
static void d_dialog(void)
{
    if (bl_dialog_draw_cached(&F, &DIALOG, bl_start_key(&START, &STATUS))) return;
    bl_start_draw(&F, &START, &STATUS);
    bl_dialog_draw(&F, &DIALOG);
}
static void d_dialog_only(void) { bl_dialog_draw(&F, &DIALOG); }

static double best(DrawFn fn, int runs)
{
    double b = 1e9;
    for (int i = 0; i < runs; i++) {
        double a = now_ms();
        fn();
        double t = now_ms() - a;
        if (t < b) b = t;
    }
    return b;
}

/* Every figure keeps its best value over the rounds. */
enum { MAXF = 64 };
static struct { const char *name; double v; int budgeted; double limit; } FIG[MAXF];
static int nfig;
static void fig(int *i, const char *name, double v, double limit)
{
    int k = (*i)++;
    if (k >= nfig) { FIG[k].name = name; FIG[k].v = v; FIG[k].limit = limit; nfig = k + 1; }
    else if (v < FIG[k].v) FIG[k].v = v;
}

/* Cold: the screen's cached layers and card images released (the two
 * backgrounds are baked once when the launcher opens), then the first draw.
 * The question over Start: its own layer, Start drawn before. */
static double cold(DrawFn fn)
{
    double b = 1e9;
    for (int i = 0; i < 3; i++) {
        bl_screens_release_layers();
        if (fn == d_dialog) d_start();
        double a = now_ms();
        if (fn == d_dialog) d_dialog_only(); else fn();
        double t = now_ms() - a;
        if (t < b) b = t;
    }
    return b;
}

static void budget(int *id, const char *cold_name, const char *warm_name, DrawFn fn)
{
    fig(id, cold_name, cold(fn), fn == d_start ? 0 : COLD_MS);   /* Start: only when opening */
    fn();                                                          /* drawn once */
    fig(id, warm_name, best(fn, 15), WARM_MS);
}

/* Focus moves (a keypress): the redraw of each state it reaches. */
static void keypress(int *id, const char *name, DrawFn fn, void (*press)(int))
{
    fn();
    double worst = 0;
    for (int i = 0; i < 8; i++) {
        press(i);
        double t = best(fn, 9);
        if (t > worst) worst = t;
    }
    fig(id, name, worst, WARM_MS);
}

static void p_settings(int i) { BlField fl; bl_settings_input(&SETTINGS, i < 4 ? BL_PAD_DOWN : BL_PAD_UP, &fl); }
static void p_patch(int i) { bl_patch_input(&PATCH, (i & 1) ? BL_PAD_UP : BL_PAD_DOWN); }
static void p_progress(int i) { PATCH.done = (unsigned long long)i + 1; }
static void p_help(int i) { bl_help_input(&HELP, (i & 1) ? BL_PAD_UP : BL_PAD_DOWN); }
static void p_dialog(int i) { bl_dialog_input(&DIALOG, (i & 1) ? BL_PAD_LEFT : BL_PAD_RIGHT); }

/* The carousel slide: one card in 150 ms (eased) at 60 frames per second.
 * idle: the launcher had idle time on Start before the keypress (it builds
 * the card images then, a band of rows per idle step); else the press came
 * at once, right after the launcher opened: then a slide frame may build one
 * card image. */
static void slide(int *id, const char *name, uint32_t dir, int idle)
{
    d_start();
    double prep = 0;
    if (idle) {
        for (int n = 0; n < 1000; n++) {
            double a = now_ms();
            int more = bl_start_prepare(&START);
            double t = now_ms() - a;
            if (t > prep) prep = t;
            if (!more) break;
        }
    }
    bl_start_input(&START, dir, BL_READY_OK);
    double first = 0, worst = 0;
    int frames = 0;
    uint64_t clock_ns = 1000000000ull;
    bl_start_animate(&START, clock_ns);                           /* the press: the slide starts */
    while (bl_start_animate(&START, clock_ns += 16666667u)) {
        double a = now_ms();
        d_start();
        double t = now_ms() - a;
        if (frames > 0 || idle) { double again = best(d_start, 4); if (again < t) t = again; }   /* the same frame again */
        if (frames++ == 0) first = t; else if (t > worst) worst = t;
    }
    CHECK(frames > 3);
    static char names[MAXF][64];
    snprintf(names[*id], 64, "%s: first frame", name);
    snprintf(names[*id + 1], 64, "%s: other frames", name);
    snprintf(names[*id + 2], 64, "%s: still at the end", name);
    snprintf(names[*id + 3], 64, "%s: longest idle step", name);
    fig(id, names[*id], first, idle ? WARM_MS : COLD_MS);
    fig(id, names[*id], worst, idle ? WARM_MS : COLD_MS);
    fig(id, names[*id], best(d_start, 5), WARM_MS);
    fig(id, names[*id], prep, idle ? COLD_MS : 0);
}

static void round_once(void)
{
    int id = 0;
    memset(&STATUS, 0, sizeof STATUS);
    STATUS.readiness = BL_READY_OK; STATUS.net_up = 1;
    snprintf(STATUS.ipv4, sizeof STATUS.ipv4, "192.168.1.5");
    bl_start_init(&START);
    memset(&PATCH, 0, sizeof PATCH);
    PATCH.message = -1; PATCH.exe_verdict = BL_EXE_TESTED;
    bl_settings_init(&SETTINGS, NULL);
    bl_help_init(&HELP, BL_HELP_FOLDER);
    bl_dialog_init(&DIALOG, BL_S_PLAY_NO_ACCOUNT, BL_S_DIALOG_PLAY, BL_S_DIALOG_BACK, 2);

    bl_screens_release();
    double a = now_ms();
    d_start();
    d_help();                                                     /* the list background too */
    fig(&id, "open: bake backgrounds, Start", now_ms() - a, 0);
    budget(&id, "start cold (opening)", "start redraw", d_start);
    budget(&id, "patch cold", "patch redraw", d_patch);
    budget(&id, "settings cold", "settings redraw", d_settings);
    budget(&id, "help cold", "help redraw", d_help);
    budget(&id, "dialog over start cold", "dialog over start redraw", d_dialog);

    keypress(&id, "keypress: settings list", d_settings, p_settings);
    SETTINGS.in_panel = 1;
    keypress(&id, "keypress: settings panel", d_settings, p_settings);
    keypress(&id, "keypress: patch choice", d_patch, p_patch);
    PATCH.busy = 1; PATCH.total = 9;
    snprintf(PATCH.progress_name, sizeof PATCH.progress_name, "amax/dlls/amax_auth.asi");
    keypress(&id, "keypress: patch progress", d_patch, p_progress);
    keypress(&id, "keypress: help scroll", d_help, p_help);
    keypress(&id, "keypress: dialog focus", d_dialog, p_dialog);

    bl_screens_release_layers();
    /* Three cards (no About, finding 32): play -> settings -> play -> patch -> play -> settings. */
    slide(&id, "slide left at once", BL_PAD_LEFT, 0);
    slide(&id, "slide right after idle", BL_PAD_RIGHT, 1);
    slide(&id, "slide right at once", BL_PAD_RIGHT, 0);
    slide(&id, "slide left after idle", BL_PAD_LEFT, 1);
    slide(&id, "slide left after idle 2", BL_PAD_LEFT, 1);
}

int main(void)
{
    F = (BlFrame){ malloc(1920u * 1080u * 4u), 1920, 1080, 1920 * 4 };
    for (int r = 0; r < ROUNDS; r++) round_once();
    for (int i = 0; i < nfig; i++) {
        int ok = FIG[i].limit <= 0 || FIG[i].v <= FIG[i].limit;
        if (FIG[i].limit > 0) fprintf(stderr, "  %-40s %7.2f ms  (budget %.0f)%s\n", FIG[i].name, FIG[i].v, FIG[i].limit, ok ? "" : "  OVER");
        else fprintf(stderr, "  %-40s %7.2f ms  (not budgeted)\n", FIG[i].name, FIG[i].v);
        CHECK_TIMING(ok);
    }
    free(F.pixels);
    bl_screens_release();
    return bl_test_report("perf_screens");
}
