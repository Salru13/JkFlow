/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests for the front end: Start carousel, loader, start-failed screen
 * (plan Task 3, UI side) and the BLUR 1.00 Play flow (BLUR 1.00 plan Task L9). */
#define _DEFAULT_SOURCE
#include "bl_account.h"
#include "bl_clock.h"
#include "bl_events.h"
#include "bl_exe.h"
#include "bl_frontend.h"
#include "bl_fs.h"
#include "bl_loader.h"
#include "bl_native.h"
#include "bl_paths.h"
#include "bl_screens.h"
#include "bl_session.h"
#include "bl_strings.h"
#include "bl_sha256.h"
#include "bl_png.h"
#include "bl_test.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---------------- fake services ---------------- */

typedef struct Fake {
    uint64_t now;                 /* ns */
    int ticks;
    int no_pad_ticks;             /* pad() returns -1 for this many calls */
    const uint32_t *script;       /* pressed edges, one per pad() call after no_pad_ticks */
    int script_len, script_pos;
    int readiness;
    int quit_at;                  /* tick at which should_quit() returns 1, or -1 */
    int presents;
    char last_screen[32];
    char screens[48][16]; int nscreens;
    int max_ticks;
    int http_calls;
    long long server_offset;      /* server time = clock now - this */
    int server_answers;
    /* Frame pacing (owner finding 34): each present takes present_ns of
     * simulated time (the flip waits for a vblank: 60 Hz), sleeps add their
     * own; every present and sleep is recorded with the time it began. */
    uint64_t present_ns;
    int np, nsleep, npress;
    uint64_t pt[1024], st[1024], press_t[16];
    float ppos[1024];
    uint32_t phash[1024];
    char pscreen[1024][12];
    int unlink_at;                /* loop pass at which unlink_path is removed, or 0 */
    char unlink_path[600];
} Fake;
enum { REC = 1024 };

/* A few thousand pixels of the frame: enough to tell frames apart. */
static uint32_t sparse_hash(const BlFrame *f)
{
    uint32_t h = 2166136261u;
    size_t n = (size_t)f->stride * f->height / 4;
    const uint32_t *p = (const uint32_t *)(const void *)f->pixels;
    for (size_t i = 0; i < n; i += 997) h = (h ^ p[i]) * 16777619u;
    return h;
}

static int f_present(void *ctx, const BlFrame *frame)
{
    Fake *k = ctx;
    k->presents++;
    if (k->np < REC) {
        k->pt[k->np] = k->now;
        k->ppos[k->np] = bl_start_last_pos;
        k->phash[k->np] = sparse_hash(frame);
        snprintf(k->pscreen[k->np], sizeof k->pscreen[0], "%.11s", k->last_screen);
        k->np++;
    }
    k->now += k->present_ns;
    return 0;
}

static int f_pad(void *ctx, uint32_t *held, uint32_t *pressed)
{
    Fake *k = ctx;
    *held = 0; *pressed = 0;
    if (k->no_pad_ticks > 0) { k->no_pad_ticks--; return -1; }
    if (k->script_pos < k->script_len) *pressed = k->script[k->script_pos++];
    if (*pressed && k->npress < 16) k->press_t[k->npress++] = k->now;
    if (k->unlink_at && k->ticks == k->unlink_at) unlink(k->unlink_path);
    return 0;
}

static int f_quit(void *ctx)
{
    Fake *k = ctx;
    k->ticks++;
    return k->ticks == k->quit_at || k->ticks >= k->max_ticks;
}

static uint64_t f_now(void *ctx) { return ((Fake *)ctx)->now; }
static void f_sleep(void *ctx, uint32_t us)
{
    Fake *k = ctx;
    if (k->nsleep < REC) k->st[k->nsleep++] = k->now;
    k->now += (uint64_t)us * 1000u;
}
static int f_net(void *ctx, char *ip, size_t cap) { (void)ctx; snprintf(ip, cap, "192.168.0.124"); return 0; }
static int f_game(void *ctx) { return ((Fake *)ctx)->readiness; }
static void f_screen(void *ctx, const char *name)
{
    Fake *k = ctx;
    snprintf(k->last_screen, 32, "%s", name);
    if (k->nscreens < 48) snprintf(k->screens[k->nscreens++], 16, "%s", name);
}

static long long g_clock_now = 1794711845LL;               /* 2026-11-15 03:04:05 UTC */
static long long fake_clock(void) { return g_clock_now; }

static int f_http_date(void *ctx, const char *host, long long *epoch)
{
    Fake *k = ctx;
    k->http_calls++;
    if (strcmp(host, "amax-emu.com") || !k->server_answers) return -1;
    *epoch = g_clock_now - k->server_offset;
    return 0;
}

static BlServices services(Fake *k)
{
    BlServices s;
    memset(&s, 0, sizeof s);
    s.ctx = k; s.present = f_present; s.pad = f_pad; s.should_quit = f_quit; s.now_ns = f_now;
    s.sleep_us = f_sleep; s.net_status = f_net; s.game_state = f_game; s.screen = f_screen;
    return s;
}

static Fake fake(int readiness)
{
    Fake k;
    memset(&k, 0, sizeof k);
    k.readiness = readiness; k.quit_at = -1; k.max_ticks = 2000;
    k.present_ns = 16666667u;                      /* one vblank at 60 Hz */
    return k;
}

static int saw(const Fake *k, const char *screen)
{
    int n = 0;
    for (int i = 0; i < k->nscreens; i++) n += !strcmp(k->screens[i], screen);
    return n;
}

/* ---------------- (a) navigation and play ---------------- */

static void test_carousel_navigation(void)
{
    /* Owner finding 32: no About card; settings, play, patch. */
    CHECK_EQ(BL_CARD_COUNT, 3);
    BlStartState st;
    bl_start_init(&st);
    CHECK_EQ(st.focus, BL_CARD_PLAY);
    CHECK_EQ(bl_start_input(&st, BL_PAD_RIGHT, BL_READY_OK), BL_START_NONE);
    CHECK_EQ(st.focus, BL_CARD_PATCH);
    bl_start_input(&st, BL_PAD_RIGHT, BL_READY_OK);
    CHECK_EQ(st.focus, BL_CARD_PATCH);                /* stops at the end */
    bl_start_input(&st, BL_PAD_LEFT, BL_READY_OK);
    CHECK_EQ(st.focus, BL_CARD_PLAY);
    bl_start_input(&st, BL_PAD_LEFT, BL_READY_OK);
    CHECK_EQ(st.focus, BL_CARD_SETTINGS);
    bl_start_input(&st, BL_PAD_LEFT, BL_READY_OK);
    CHECK_EQ(st.focus, BL_CARD_SETTINGS);             /* stops at the other end */
    CHECK_EQ(st.band, BL_S_BAND_SETTINGS);
    bl_start_input(&st, BL_PAD_RIGHT, BL_READY_OK);
    CHECK_EQ(st.focus, BL_CARD_PLAY);
    CHECK_EQ(bl_start_input(&st, BL_PAD_CROSS, BL_READY_OK), BL_START_PLAY);
}

static void test_play_through_run(void)
{
    static const uint32_t script[] = { 0, BL_PAD_LEFT, BL_PAD_RIGHT, 0, BL_PAD_CROSS };
    Fake k = fake(BL_READY_OK);
    k.script = script; k.script_len = 5;
    BlServices s = services(&k);
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_START_GAME);
    CHECK(k.presents > 0);
    CHECK(strcmp(k.last_screen, "start") == 0);
}

/* ---------------- (b) game missing ---------------- */

static void test_play_with_game_missing(void)
{
    BlStartState st;
    bl_start_init(&st);
    CHECK_EQ(bl_start_input(&st, BL_PAD_CROSS, BL_READY_NO_GAME), BL_START_NONE);
    CHECK_EQ(st.band, BL_S_GAME_NOT_FOUND);
    CHECK_EQ(st.focus, BL_CARD_PATCH);
    /* Audit 5: Help says where the folder goes ({square} is the Help key cap). */
    CHECK(!strcmp(bl_str(BL_S_GAME_NOT_FOUND), "Blur not found. Press {square} Help to see where it goes."));
    bl_start_init(&st);
    bl_start_input(&st, BL_PAD_CROSS, BL_READY_UNPATCHED);
    CHECK_EQ(st.band, BL_S_GAME_UNPATCHED);
    CHECK(!strcmp(bl_str(BL_S_GAME_UNPATCHED), "JkFlow files need to be installed. Press Patch."));
    bl_start_init(&st);
    CHECK_EQ(bl_start_input(&st, BL_PAD_CROSS, BL_READY_UNSUPPORTED), BL_START_NONE);
    CHECK_EQ(st.band, BL_S_GAME_UNSUPPORTED);

    /* Through the loop: Cross never starts; the run ends when the system asks. */
    static const uint32_t script[] = { BL_PAD_CROSS, BL_PAD_CROSS };
    Fake k = fake(BL_READY_NO_GAME);
    k.script = script; k.script_len = 2; k.quit_at = 50;
    BlServices s = services(&k);
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_LEAVE_HOME);
}

/* ---------------- (c) no controller yet ---------------- */

static void test_no_controller_then_cross(void)
{
    static const uint32_t script[] = { BL_PAD_CROSS };
    Fake k = fake(BL_READY_OK);
    k.no_pad_ticks = 100; k.script = script; k.script_len = 1;
    BlServices s = services(&k);
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_START_GAME);
    CHECK(k.ticks > 100);
}

/* ---------------- (e) golden Start frame ---------------- */

static void test_start_golden(void)
{
    BlFrame f = { malloc(1920u * 1080u * 4u), 1920, 1080, 1920 * 4 };
    BlStartState st;
    bl_start_init(&st);
    BlStatus status;
    memset(&status, 0, sizeof status);
    status.readiness = BL_READY_OK; status.net_up = 1;
    snprintf(status.ipv4, sizeof status.ipv4, "192.168.0.124");
    bl_start_draw(&f, &st, &status);
    char hex[65];
    bl_sha256_hex(f.pixels, (size_t)f.stride * f.height, hex);
    const char *golden_path = BL_ONLINE_UI ? BL_TEST_DIR "/golden/online-start.sha256" : BL_TEST_DIR "/golden/start.sha256";
    if (getenv("BL_UPDATE_GOLDEN")) {
        FILE *o = fopen(golden_path, "w"); fprintf(o, "%s\n", hex); fclose(o);
        bl_png_write(BL_TEST_DIR "/../build/golden-start.png", &f);
    }
    char want[80] = "";
    FILE *g = fopen(golden_path, "r");
    if (g) { if (!fgets(want, sizeof want, g)) want[0] = 0; fclose(g); }
    want[64] = 0;
    CHECK(strcmp(hex, want) == 0);
    if (strcmp(hex, want) != 0) {
        fprintf(stderr, "  start frame sha256 %s, golden %s (run with BL_UPDATE_GOLDEN=1 after checking build/golden-start.png)\n", hex, want);
        bl_png_write(BL_TEST_DIR "/../build/golden-start.png", &f);
    }
    free(f.pixels);
}

/* ---------------- (f) loader ---------------- */

static int luma_at(const BlFrame *f, int x, int y)
{
    uint32_t v; memcpy(&v, f->pixels + (size_t)y * f->stride + (size_t)x * 4, 4);
    return (int)((v >> 16) & 255) * 3 + (int)((v >> 8) & 255) * 6 + (int)(v & 255);
}

static void test_loader_deterministic_and_moving(void)
{
    size_t n = 1920u * 1080u * 4u;
    BlFrame a = { malloc(n), 1920, 1080, 1920 * 4 }, b = { malloc(n), 1920, 1080, 1920 * 4 };
    bl_loader_draw(&a, 1234);
    bl_loader_draw(&b, 1234);
    CHECK(memcmp(a.pixels, b.pixels, n) == 0);
    bl_loader_draw(&b, 1734);
    CHECK(memcmp(a.pixels, b.pixels, n) != 0);
    bl_loader_draw(&a, 0);
    int pts[64][2], np = 0;
    for (int y = 300; y < 650 && np < 64; y += 7)
        for (int x = 600; x < 1320 && np < 64; x += 13)
            if (luma_at(&a, x, y) > 2400) { pts[np][0] = x; pts[np][1] = y; np++; }
    CHECK(np >= 16);
    for (uint64_t t = 0; t < 8000; t += 650) {
        bl_loader_draw(&b, t);
        for (int i = 0; i < np; i++) CHECK(luma_at(&b, pts[i][0], pts[i][1]) > 2000);
    }
    CHECK(strstr(bl_str(BL_S_LOADER), "Starting Blur") != NULL);
    for (uint64_t t = 0; t < 4000; t += 33) {
        bl_loader_draw(&a, t);
        if (t % 495 == 0) {
            memset(b.pixels, 0x77, n);
            bl_loader_reset();
            bl_loader_draw(&b, t);
            CHECK(memcmp(a.pixels, b.pixels, n) == 0);
        }
    }
    bl_loader_draw(&a, 100);
    memset(a.pixels, 0x55, n);
    bl_loader_draw(&a, 133);
    bl_loader_reset();
    bl_loader_draw(&b, 133);
    CHECK(memcmp(a.pixels, b.pixels, n) == 0);
    free(a.pixels); free(b.pixels);
}

/* The title's first-start import progress (T1): the loader with a bar. */
static void test_loader_progress(void)
{
    size_t n = 1920u * 1080u * 4u;
    BlFrame a = { malloc(n), 1920, 1080, 1920 * 4 }, b = { malloc(n), 1920, 1080, 1920 * 4 };
    bl_loader_draw_progress(&a, 500, NULL, 10, 100, "Preparing BLUR");
    bl_loader_reset();
    bl_loader_draw_progress(&b, 500, NULL, 10, 100, "Preparing BLUR");
    CHECK(memcmp(a.pixels, b.pixels, n) == 0);
    bl_loader_draw_progress(&b, 500, NULL, 90, 100, "Preparing BLUR");
    CHECK(memcmp(a.pixels, b.pixels, n) != 0);              /* the bar moved */
    bl_loader_draw_progress(&b, 500, NULL, 200, 0, NULL);    /* nonsense counts do not crash */
    free(a.pixels); free(b.pixels);
}

/* Owner finding 1 (2026-11-15): the first-start preparation says "Preparing
 * BLUR environment" in the big line and "First start only..." under it;
 * "Starting Blur..." only after Play. */
static void test_loader_big_line(void)
{
    CHECK(strcmp(bl_str(BL_S_LOADER_PREPARE), "Preparing BLUR environment") == 0);
    CHECK(strcmp(bl_str(BL_S_LOADER_FIRST_START), "First start only\xE2\x80\xA6") == 0);
    CHECK(strcmp(bl_str(BL_S_LOADER), "Starting Blur\xE2\x80\xA6") == 0);
    size_t n = 1920u * 1080u * 4u;
    BlFrame a = { malloc(n), 1920, 1080, 1920 * 4 }, b = { malloc(n), 1920, 1080, 1920 * 4 };
    /* The big line is the given text: Starting Blur and Preparing differ. */
    bl_loader_draw_progress(&a, 700, bl_str(BL_S_LOADER_PREPARE), 3, 10, bl_str(BL_S_LOADER_FIRST_START));
    bl_loader_reset();
    bl_loader_draw_progress(&b, 700, NULL, 3, 10, bl_str(BL_S_LOADER_FIRST_START));
    CHECK(memcmp(a.pixels, b.pixels, n) != 0);
    bl_loader_reset();
    bl_loader_draw_progress(&b, 700, bl_str(BL_S_LOADER), 3, 10, bl_str(BL_S_LOADER_FIRST_START));
    bl_loader_reset();
    bl_loader_draw_progress(&a, 700, NULL, 3, 10, bl_str(BL_S_LOADER_FIRST_START));
    CHECK(memcmp(a.pixels, b.pixels, n) == 0);              /* NULL: Starting Blur */
    /* The Play loader after the preparation in the same buffer: the longer
     * line is gone (same pixels as a fresh draw). */
    for (uint64_t t = 0; t < 400; t += 33) bl_loader_draw_progress(&a, t, bl_str(BL_S_LOADER_PREPARE), 5, 10, bl_str(BL_S_LOADER_FIRST_START));
    bl_loader_draw(&a, 433);
    bl_loader_draw(&a, 466);
    memset(b.pixels, 0x33, n);
    bl_loader_reset();
    bl_loader_draw(&b, 466);
    CHECK(memcmp(a.pixels, b.pixels, n) == 0);
    /* Big text drawn twice in a row in one buffer matches a fresh draw. */
    bl_loader_draw_text(&a, 500, bl_str(BL_S_LOADER_PREPARE));
    bl_loader_draw_text(&a, 533, bl_str(BL_S_LOADER_PREPARE));
    bl_loader_reset();
    bl_loader_draw_text(&b, 533, bl_str(BL_S_LOADER_PREPARE));
    CHECK(memcmp(a.pixels, b.pixels, n) == 0);
    free(a.pixels); free(b.pixels);
}

/* ---------------- (g) start failed ---------------- */

/* Review findings 1 and 3: the title stops before Blur and says why in one
 * sentence, drawn by the launcher (bl_notice_draw), wrapped on screen. */
static void test_title_stop_notices(void)
{
    char t[400];
    snprintf(t, sizeof t, bl_str(BL_S_MIGRATE_FAILED), "/data/BLUR", "/data/JkFlow/System");
    CHECK(!strcmp(t, "Your old JkFlow data in /data/BLUR could not be moved to /data/JkFlow/System. "
                     "Nothing was deleted. Free some space and start JkFlow again."));
    CHECK(!strcmp(bl_str(BL_S_TOO_MANY_SETTINGS),
                  "JkFlow could not start Blur: too many settings. Turn off Enable logs and try again."));
    CHECK(bl_str(BL_S_HINT_CLOSE_NOTICE)[0] != 0);
    BlFrame f = { malloc(1920u * 1080u * 4u), 1920, 1080, 1920 * 4 };
    memset(f.pixels, 0x55, (size_t)f.stride * f.height);
    int bottom = bl_notice_draw(&f, t, bl_str(BL_S_HINT_CLOSE_NOTICE));
    CHECK(bottom > 540 && bottom < 1080);
    int lit = 0;
    for (int y = 560; y < bottom; y += 3) for (int x = 200; x < 1720; x += 3) lit += luma_at(&f, x, y) > 1500;
    CHECK(lit > 100);
    /* Nothing outside the 1600-pixel text column: the long line wrapped. */
    int edge = 0;
    for (int y = 560; y < bottom; y++) for (int x = 0; x < 150; x++) edge += luma_at(&f, x, y) > 1500 || luma_at(&f, 1919 - x, y) > 1500;
    CHECK_EQ(edge, 0);
    free(f.pixels);
}

static void test_start_failed_screen(void)
{
    const BlStringId ids[] = { BL_S_ERROR_TITLE, BL_S_ERROR_BODY, BL_S_ERROR_LOG, BL_S_HINT_BACK_TO_START };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        CHECK(bl_str(ids[i])[0] != 0);
        CHECK(strstr(bl_str(ids[i]), "/data/") == NULL);
    }
    BlFrame f = { malloc(1920u * 1080u * 4u), 1920, 1080, 1920 * 4 };
    memset(f.pixels, 0x55, (size_t)f.stride * f.height);
    bl_error_draw(&f, NULL, "session-12.log", NULL);
    int lit = 0;
    for (int y = 500; y < 760; y += 3) for (int x = 300; x < 1600; x += 3) lit += luma_at(&f, x, y) > 1500;
    CHECK(lit > 100);
    free(f.pixels);

    /* Circle goes back to Start, where Cross plays. */
    static const uint32_t script[] = { BL_PAD_CIRCLE, 0, BL_PAD_CROSS };
    Fake k = fake(BL_READY_OK);
    k.script = script; k.script_len = 3;
    BlServices s = services(&k);
    s.start_failure = "exit code 1";
    s.start_failure_log = "session-12.log";
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_START_GAME);
    CHECK(strcmp(k.last_screen, "start") == 0);
    CHECK(k.script_pos == 3);
    /* F1 request 3 (finding 13): Cross on the failed-start screen starts
     * again at once, with the same Play checks. */
    static const uint32_t again[] = { BL_PAD_CROSS };
    Fake a = fake(BL_READY_OK);
    a.script = again; a.script_len = 1;
    s = services(&a);
    s.start_failure = "exit code 1";
    s.start_failure_log = "session-12.log";
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_START_GAME);
    CHECK_EQ(a.script_pos, 1);
    CHECK(strcmp(a.last_screen, "error") == 0);
    CHECK(!strcmp(bl_str(BL_S_ERROR_BODY), "The game stopped before its first frame. Cross: start again. Square: save a support bundle."));
    CHECK(!strcmp(bl_str(BL_S_HINT_START_AGAIN), "Start again"));
}

static void test_bad_arguments(void)
{
    BlDecision d;
    CHECK(bl_frontend_run(NULL, &d) < 0);
    Fake k = fake(BL_READY_OK);
    BlServices s = services(&k);
    CHECK(bl_frontend_run(&s, NULL) < 0);
    s.present = NULL;
    CHECK(bl_frontend_run(&s, &d) < 0);
}

/* ---------------- BLUR 1.00 Play flow with real folders (L9) ---------------- */

static char root[256], files[400];
static BlPaths P;
static const char *S7 = "our S7 d3d9_blur_dxvk.dll";

static void sh(const char *cmd) { if (system(cmd) != 0) fprintf(stderr, "  command failed: %s\n", cmd); }

static void put(const char *dir, const char *name, const char *text)
{
    char p[900]; snprintf(p, sizeof p, "%s/%s", dir, name);
    char d[900]; snprintf(d, sizeof d, "%s", p);
    char *sl = strrchr(d, '/'); if (sl) { *sl = 0; bl_fs_mkdirs(d); }
    FILE *f = fopen(p, "wb"); fputs(text, f); fclose(f);
}

static int exists(const char *dir, const char *name)
{
    char p[900]; snprintf(p, sizeof p, "%s/%s", dir, name);
    return access(p, F_OK) == 0;
}

static char *slurp(const char *dir, const char *name)
{
    static char buf[8192];
    char p[900]; snprintf(p, sizeof p, "%s/%s", dir, name);
    if (bl_fs_read(p, buf, sizeof buf) < 0) buf[0] = 0;
    return buf;
}

/* A patched BLUR folder with the tested Blur.exe (by the test hook). */
static void world(int tested, int patched)
{
    char cmd[600];
    if (root[0]) { snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); sh(cmd); }
    snprintf(root, sizeof root, "/tmp/bl-fe-XXXXXX");
    if (!mkdtemp(root)) exit(1);
    bl_paths_init(&P, root);
    bl_paths_make(&P);
    bl_fs_mkdirs(P.game);                                    /* the player's folder (the launcher never creates it) */
    snprintf(files, sizeof files, "%s/blur-files", root);
    bl_fs_mkdirs(files);
    char p[700]; snprintf(p, sizeof p, "%s/Blur.exe", P.game);
    int fd = open(p, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0 || write(fd, "MZ", 2) != 2 || ftruncate(fd, tested ? (off_t)BL_TESTED_EXE_SIZE : 4096) != 0) exit(1);
    close(fd);
    static char sha[65];
    bl_fs_sha256(p, sha, NULL);
    bl_exe_expected_sha = sha;
    put(P.game, "cache/root.pak", "pak");
    char hs[65], man[400];
    bl_sha256_hex(S7, strlen(S7), hs);
    snprintf(man, sizeof man, "file d3d9_blur_dxvk.dll %s %zu\n", hs, strlen(S7));
    put(files, "manifest.txt", man);
    put(files, "d3d9_blur_dxvk.dll", S7);
    snprintf(cmd, sizeof cmd, "cp '%s/../assets/blur-files/model-base.ini' '%s/'", BL_TEST_DIR, files);
    sh(cmd);
    if (patched) put(P.game, "d3d9_blur_dxvk.dll", S7);
    bl_session_end_clean();
}

static BlServices world_services(Fake *k)
{
    BlServices s = services(k);
    s.game_state = NULL;
    s.data_root = root; s.game_dir = P.game; s.package_files_dir = files; s.build_id = "BLUR-1.00";
    return s;
}

static void save_account(void)
{
    BlAccount a; memset(&a, 0, sizeof a);
    snprintf(a.user, sizeof a.user, "racer"); snprintf(a.pass, sizeof a.pass, "pw");
    CHECK_EQ(bl_account_save(&P, &a), 0);
}

enum { X = BL_PAD_CROSS, O = BL_PAD_CIRCLE, L = BL_PAD_LEFT, R = BL_PAD_RIGHT };

/* The world patched for online play: the package's online set (one AMAX
 * file) is installed in the game folder, so bl_patch_online_state says ready. */
static void world_online(int tested)
{
    world(tested, 1);
    static const char *AMAX = "amax dxvk for online";
    char hs[65], hm[65], man[600];
    bl_sha256_hex(S7, strlen(S7), hs);
    bl_sha256_hex(AMAX, strlen(AMAX), hm);
    snprintf(man, sizeof man, "file d3d9_blur_dxvk.dll %s %zu\nonline amax/dxvk.dll %s %zu src=amax-dxvk/dxvk.dll\n",
             hs, strlen(S7), hm, strlen(AMAX));
    put(files, "manifest.txt", man);
    put(files, "amax-dxvk/dxvk.dll", AMAX);
    put(P.game, "amax/dxvk.dll", AMAX);
    CHECK_EQ(bl_patch_online_state(P.game, files), BL_ONLINE_READY);
}

static int count_of(const char *text, const char *what)
{
    int n = 0;
    for (const char *p = text; (p = strstr(p, what)) != NULL; p += strlen(what)) n++;
    return n;
}

/* Owner finding 7 (2026-11-15): another Blur.exe ran fine, so Play asks
 * nothing; the Patch screen line and the E205 logged at Patch stay. */
static void test_play_other_exe_no_question(void)
{
    world(0, 1);
    save_account();
    static const uint32_t script[] = { X };
    Fake k = fake(0); k.script = script; k.script_len = 1; k.max_ticks = 60;
    BlServices s = world_services(&k);
    BlDecision d;
    int e205 = count_of(slurp(P.state, BL_EVENTS_FILE), " E205 ");
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_START_GAME);
    bl_session_first_frame();
    bl_session_end_clean();
    CHECK_EQ(saw(&k, "dialog"), 0);
    CHECK_EQ(count_of(slurp(P.state, BL_EVENTS_FILE), " E205 "), e205);   /* nothing new at Play */
    CHECK(strstr(slurp(P.state, BL_EVENTS_FILE), "play: Blur.exe") == NULL);
}

/* sha256 of every file under dir with its name: a fingerprint of the folder. */
static void tree_sig(const char *dir, char *out, size_t cap)
{
    char cmd[800];
    snprintf(cmd, sizeof cmd, "cd '%s' && find . -print0 | sort -z | xargs -0 -I{} sh -c 'echo \"{}\"; [ -f \"{}\" ] && sha256sum < \"{}\"' | sha256sum", dir);
    FILE *p = popen(cmd, "r");
    out[0] = 0;
    if (p) { if (!fgets(out, (int)cap, p)) out[0] = 0; pclose(p); }
}

/* Owner rule: the launcher changes nothing in the game folder by itself.
 * Start, the status refresh and Play on an unpatched game (Cancel) leave it
 * byte for byte as it was. */
static void test_game_folder_untouched_until_patch(void)
{
    world(1, 0);
    save_account();
    char before[100], after[100];
    tree_sig(P.game, before, sizeof before);
    static const uint32_t cancel[] = { X, O, 0, 0, X, O };
    Fake k = fake(0); k.script = cancel; k.script_len = 6; k.max_ticks = 120;
    BlServices s = world_services(&k);
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_LEAVE_HOME);
    tree_sig(P.game, after, sizeof after);
    CHECK(before[0] && !strcmp(before, after));
}

/* Owner decision 2026-11-15: the game is <game_root>/JkFlow/Blur in any letter
 * case; Blur starts from the spelling on disk. Two spellings: no guess, Play
 * says which two. */
static void test_game_root_any_case(void)
{
    world(1, 1);
    save_account();
    char data[400], game[500], cmd[1200];
    snprintf(data, sizeof data, "%s/data", root);
    snprintf(game, sizeof game, "%s/jKfLoW/bLuR", data);
    snprintf(cmd, sizeof cmd, "mkdir -p '%s' && cp -a '%s/.' '%s/'", game, P.game, game);
    sh(cmd);
    static const uint32_t play[] = { X };
    Fake k = fake(0); k.script = play; k.script_len = 1; k.max_ticks = 60;
    BlServices s = world_services(&k);
    s.game_root = data; s.game_dir = NULL;
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_START_GAME);
    bl_session_first_frame();
    bl_session_end_clean();
    CHECK(strstr(d.profile_text, "\\jKfLoW\\bLuR\\Blur.exe\n") != NULL);
    CHECK(strstr(d.profile_text, "working_directory = Z:") != NULL);
    snprintf(cmd, sizeof cmd, "mkdir -p '%s/JKFLOW/BLUR'", data);
    sh(cmd);
    Fake m = fake(0); m.script = play; m.script_len = 1; m.max_ticks = 60;
    s = world_services(&m);
    s.game_root = data; s.game_dir = NULL;
    bl_start_last_band = -1;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_LEAVE_HOME);
    CHECK_EQ(bl_start_last_band, BL_S_GAME_SEVERAL);
    CHECK(strstr(bl_str(BL_S_GAME_SEVERAL), "/JKFLOW/BLUR") && strstr(bl_str(BL_S_GAME_SEVERAL), "/jKfLoW/bLuR"));
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", data);
    sh(cmd);
    /* No folder at all: the one place to copy it to. */
    Fake n = fake(0); n.script = play; n.script_len = 1; n.max_ticks = 60;
    s = world_services(&n);
    s.game_root = data; s.game_dir = NULL;
    bl_start_last_band = -1;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(bl_start_last_band, BL_S_GAME_NOT_FOUND);
    /* Square from there opens Help at "Your Blur folder". */
    static const uint32_t square[] = { BL_PAD_SQUARE };
    Fake q = fake(0); q.script = square; q.script_len = 1; q.max_ticks = 40;
    s = world_services(&q);
    s.game_root = data; s.game_dir = NULL;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(bl_help_last_drawn.scroll, bl_help_section(BL_HELP_FOLDER));
}

static void test_play_unpatched_dialog(void)
{
    /* Cancel: back to Start, nothing written. */
    world(1, 0);
    save_account();
    static const uint32_t cancel[] = { X, O };
    Fake k = fake(0); k.script = cancel; k.script_len = 2; k.max_ticks = 60;
    BlServices s = world_services(&k);
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_LEAVE_HOME);
    CHECK_EQ(saw(&k, "dialog"), 1);
    CHECK_EQ(bl_dialog_last_drawn.body, BL_S_PLAY_UPDATE);
    CHECK(!strcmp(bl_str(BL_S_PLAY_UPDATE), "JkFlow files need to be installed"));   /* audit 24: the band's word */
    CHECK(!exists(P.game, "d3d9_blur_dxvk.dll"));
    /* Patch now: the Patch screen runs, then Play starts. */
    static const uint32_t now[] = { X, X, 0, 0, O, X };
    Fake m = fake(0); m.script = now; m.script_len = 6; m.max_ticks = 200;
    s = world_services(&m);
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK(saw(&m, "patch") >= 1);
    CHECK(exists(P.game, "d3d9_blur_dxvk.dll"));
    CHECK_EQ(d.action, BL_START_GAME);
    CHECK(strstr(slurp(P.state, "patch.txt"), "build_id=BLUR-1.00") != NULL);
}

/* Owner finding 8 (2026-11-15): a game patched for offline play never asks
 * about the AMAX account. */
static void test_play_no_account_offline_never_asks(void)
{
    world(1, 1);
    CHECK_EQ(bl_patch_online_state(P.game, files), BL_ONLINE_NONE);
    static const uint32_t script[] = { X };
    Fake k = fake(0); k.script = script; k.script_len = 1; k.max_ticks = 60;
    BlServices s = world_services(&k);
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_START_GAME);
    CHECK_EQ(saw(&k, "dialog"), 0);
    bl_session_first_frame();
    bl_session_end_clean();
}

/* Patched for online play (the AMAX set installed): no account asks once. */
static void test_play_no_account_warns_once(void)
{
    world_online(1);
    /* Back from the warning, then Play again: asked again (not yet accepted). */
    static const uint32_t script[] = { X, O, 0, X, X };
    Fake k = fake(0); k.script = script; k.script_len = 5; k.max_ticks = 120;
    BlServices s = world_services(&k);
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_START_GAME);
    bl_session_first_frame();                                /* the game ran and ended normally */
    bl_session_end_clean();
    CHECK_EQ(saw(&k, "dialog"), 2);
    CHECK_EQ(bl_dialog_last_drawn.body, BL_S_PLAY_NO_ACCOUNT);
    CHECK(!strcmp(bl_str(BL_S_PLAY_NO_ACCOUNT), "No AMAX account set. Online will not log in. Play anyway?"));
    char lp[600]; snprintf(lp, sizeof lp, "%s/%s", P.game, BL_ACCOUNT_LUA);
    CHECK(!bl_fs_exists(lp));
    /* With an account: no question at all, and the pre-fill file is written. */
    save_account();
    static const uint32_t play[] = { X };
    Fake m = fake(0); m.script = play; m.script_len = 1; m.max_ticks = 60;
    s = world_services(&m);
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_START_GAME);
    CHECK_EQ(saw(&m, "dialog"), 0);
    CHECK(bl_fs_exists(lp));
    bl_session_first_frame();
    bl_session_end_clean();
}

static void test_play_clock_off_dialog(void)
{
    /* Audit 14: an offline-patched game asks nothing at Play: no HTTP
     * request, no clock question, even with the clock an hour off. */
    world(1, 1);
    bl_clock_now = fake_clock;
    static const uint32_t once[] = { X };
    Fake o = fake(0); o.script = once; o.script_len = 1; o.max_ticks = 60;
    o.server_answers = 1; o.server_offset = 3600;
    BlServices so = world_services(&o);
    so.http_date = f_http_date;
    BlDecision od;
    CHECK_EQ(bl_frontend_run(&so, &od), 0);
    CHECK_EQ(od.action, BL_START_GAME);
    CHECK_EQ(o.http_calls, 0);
    CHECK_EQ(saw(&o, "dialog"), 0);
    bl_session_first_frame();
    bl_session_end_clean();
    /* Online-patched: the check runs. */
    world_online(1);
    save_account();
    static const uint32_t back[] = { X, O };
    Fake k = fake(0); k.script = back; k.script_len = 2; k.max_ticks = 60;
    k.server_answers = 1; k.server_offset = 3600;
    BlServices s = world_services(&k);
    s.http_date = f_http_date;
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_LEAVE_HOME);
    CHECK_EQ(k.http_calls, 1);
    CHECK_EQ(bl_dialog_last_drawn.body, BL_S_CLOCK_OFF);
    CHECK(strstr(bl_dialog_last_drawn.body_text, "60 minutes off") != NULL);
    CHECK(strstr(slurp(P.state, BL_EVENTS_FILE), " I clock offset=3600") != NULL);
    /* Play anyway */
    static const uint32_t anyway[] = { X, X };
    Fake m = fake(0); m.script = anyway; m.script_len = 2; m.max_ticks = 60;
    m.server_answers = 1; m.server_offset = 3600;
    s = world_services(&m);
    s.http_date = f_http_date;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_START_GAME);
    bl_session_first_frame();
    bl_session_end_clean();
    /* No answer and a sane year: no question (Review Focus 5). */
    Fake q = fake(0); q.script = anyway; q.script_len = 1; q.max_ticks = 60;
    s = world_services(&q);
    s.http_date = f_http_date;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_START_GAME);
    CHECK_EQ(saw(&q, "dialog"), 0);
    bl_session_first_frame();
    bl_session_end_clean();
    bl_clock_now = NULL;
}

static void test_play_writes_arm_not_pending(void)
{
    world(1, 1);
    save_account();
    static const uint32_t play[] = { X };
    Fake k = fake(0); k.script = play; k.script_len = 1; k.max_ticks = 60;
    BlServices s = world_services(&k);
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_START_GAME);
    CHECK(exists(P.game, BL_NATIVE_ARM_FILE));
    CHECK(!exists(P.game, "dxn_pending.txt"));
    CHECK(exists(P.state, BL_SESSION_FILE));
    const char *native = NULL, *only = NULL;
    for (unsigned i = 0; i < d.env_count; i++) {
        if (!strcmp(d.env[i].name, "DXVK_PS4_NATIVE")) native = d.env[i].value;
        if (!strcmp(d.env[i].name, "DXVK_PS4_NATIVE_ONLY")) only = d.env[i].value;
    }
    CHECK(native && !strcmp(native, "menu"));
    CHECK(only && !strcmp(only, "1"));
    bl_session_end_clean();
    /* The arm file cannot be written: no start, E104 on the band. */
    world(1, 1);
    save_account();
    char armdir[700]; snprintf(armdir, sizeof armdir, "%s/%s", P.game, BL_NATIVE_ARM_FILE);
    mkdir(armdir, 0755);                                     /* a folder in the way */
    char inner[800]; snprintf(inner, sizeof inner, "%s/x", armdir); put(armdir, "x", "x");
    Fake m = fake(0); m.script = play; m.script_len = 1; m.max_ticks = 60;
    s = world_services(&m);
    bl_start_last_band = -1;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_LEAVE_HOME);
    CHECK_EQ(bl_start_last_band, BL_S_NATIVE_PREPARE_FAILED);
    CHECK(!strcmp(bl_str(BL_S_NATIVE_PREPARE_FAILED), "E104 JkFlow could not write its start file in the Blur folder. Free some space on the PS4, then press Play again."));
}

/* Owner 2026-11-16: Settings > Frame rate. The engine client (c988) reads
 * DXVK_PS4_RACE_LOCK: unset = 30 FPS races and 60 FPS menus, 60 = free races
 * (split screen 30), force = 30 everywhere. The tested exe follows the
 * setting; any other Blur.exe gets force from the first frame whatever the
 * setting says (owner finding 11), since the 30/60 modes read Blur's screen
 * state and are only safe on the tested exe. */
static BlDecision play_d;
static const char *play_env_with(int tested, int fps60, int logs, const char *name, char *events, size_t cap)
{
    BlDecision *d = &play_d;
    world(tested, 1);
    save_account();
    char ini[300];
    snprintf(ini, sizeof ini, "[game]\nframe_rate = %d\n[logging]\nenabled = %s\n", fps60 ? 60 : 30, logs ? "true" : "false");
    put(P.state, "launcher.ini", ini);
    static const uint32_t play[] = { X };
    Fake k = fake(0); k.script = play; k.script_len = 1; k.max_ticks = 60;
    BlServices s = world_services(&k);
    CHECK_EQ(bl_frontend_run(&s, d), 0);
    CHECK_EQ(d->action, BL_START_GAME);
    bl_session_first_frame();
    bl_session_end_clean();
    snprintf(events, cap, "%s", slurp(P.state, BL_EVENTS_FILE));
    for (unsigned i = 0; i < d->env_count; i++) if (!strcmp(d->env[i].name, name)) return d->env[i].value;
    return NULL;
}

static const char *play_env(int tested, const char *name, char *events, size_t cap)
{
    return play_env_with(tested, 0, 0, name, events, cap);
}

static void test_untested_exe_locks_30_from_frame_1(void)
{
    char events[8192];
    const char *lock = play_env(0, "DXVK_PS4_RACE_LOCK", events, sizeof events);
    CHECK(lock && !strcmp(lock, "force"));
    CHECK(strstr(events, "start: frame rate 30 (untested exe)") != NULL);
    /* 60 FPS saved in the settings does not unlock an untested exe. */
    lock = play_env_with(0, 1, 0, "DXVK_PS4_RACE_LOCK", events, sizeof events);
    CHECK(lock && !strcmp(lock, "force"));
    CHECK(strstr(events, "start: frame rate 30 (untested exe)") != NULL);
    CHECK(strstr(events, "start: frame rate 60") == NULL);
}

static void test_tested_exe_frame_rate_30(void)
{
    char events[8192];
    const char *lock = play_env_with(1, 0, 0, "DXVK_PS4_RACE_LOCK", events, sizeof events);
    CHECK(lock == NULL);                                     /* 30 mode: c988's default */
    CHECK(strstr(events, "start: frame rate 30\n") != NULL);
    CHECK(strstr(events, "untested") == NULL);
}

static void test_tested_exe_frame_rate_60(void)
{
    char events[8192];
    const char *lock = play_env_with(1, 1, 0, "DXVK_PS4_RACE_LOCK", events, sizeof events);
    CHECK(lock && !strcmp(lock, "60"));
    CHECK(strstr(events, "start: frame rate 60\n") != NULL);
}

/* Audit finding 7: the title's extra[] was full (27 of 27) in the worst case
 * before, logs on with an untested exe: 14 launcher lines. Each frame rate
 * mode with logs on stays at those 14 at most, so the title count does not
 * grow (the 60 line is only for the tested exe, which has no force line). */
static void test_frame_rate_env_fits_worst_case(void)
{
    char events[8192];
    const struct { int tested, fps60; unsigned want; } cases[] = { { 1, 0, 13 }, { 1, 1, 14 }, { 0, 0, 14 }, { 0, 1, 14 } };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        play_env_with(cases[i].tested, cases[i].fps60, 1, "DXVK_PS4_RACE_LOCK", events, sizeof events);
        fprintf(stderr, "  logs on, %s exe, %d FPS: %u launcher env lines (limit 14, headroom %d)\n",
                cases[i].tested ? "tested" : "untested", cases[i].fps60 ? 60 : 30, play_d.env_count, 14 - (int)play_d.env_count);
        CHECK_EQ(play_d.env_count, cases[i].want);
        CHECK(play_d.env_count <= 14);
        int logs = 0;
        for (unsigned j = 0; j < play_d.env_count; j++) logs |= !strcmp(play_d.env[j].name, "BLUR_LOGS") && !strcmp(play_d.env[j].value, "1");
        CHECK(logs);
    }
}

/* Finding 19 L6: the native client's dxn_stage.txt in the game folder only
 * grows; every start begins it again (the bundle still gets the one of a
 * failed start, written during that start). dxn_arm.txt stays. */
static void test_play_resets_dxn_stage(void)
{
    world(1, 1);
    save_account();
    put(P.game, "dxn_stage.txt", "stage create 3\nstage create 3\n");
    static const uint32_t play[] = { X };
    Fake k = fake(0); k.script = play; k.script_len = 1; k.max_ticks = 60;
    BlServices s = world_services(&k);
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK_EQ(d.action, BL_START_GAME);
    bl_session_first_frame();
    bl_session_end_clean();
    CHECK(!exists(P.game, "dxn_stage.txt"));
    CHECK(exists(P.game, BL_NATIVE_ARM_FILE));
}

/* Owner finding 27: the AMAX password must not stay in plain text in the
 * game folder. The plugin deletes account_cfg.lua once it read it; when it
 * could not, the launcher deletes it at its next start. account.cfg in the
 * state folder stays. */
static void test_launcher_start_removes_prefill(void)
{
    world(1, 1);
    save_account();
    put(P.game, "amax/plugins/account_cfg.lua", "return { username = \"racer\", password = \"pw\" }\n");
    Fake k = fake(0); k.max_ticks = 3;
    BlServices s = world_services(&k);
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK(!exists(P.game, "amax/plugins/account_cfg.lua"));
    BlAccount a;
    CHECK_EQ(bl_account_load(&P, &a), 0);
    CHECK(!strcmp(a.user, "racer"));
}

static void test_error_screen_only_no_first_frame(void)
{
    world(1, 1);
    save_account();
    CHECK_EQ(bl_session_begin(P.state), 0);              /* a start that never reached its first frame */
    Fake k = fake(0); k.max_ticks = 30;
    BlServices s = world_services(&k);
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK(!strcmp(k.screens[0], "error"));
    CHECK(strstr(slurp(P.state, BL_EVENTS_FILE), " E101 ") != NULL);
    /* Square on the error screen saves a support bundle. */
    CHECK_EQ(bl_session_begin(P.state), 0);
    static const uint32_t sq[] = { BL_PAD_SQUARE };
    Fake m = fake(0); m.script = sq; m.script_len = 1; m.max_ticks = 30;
    s = world_services(&m);
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK(strstr(slurp(P.state, BL_EVENTS_FILE), "support bundle: ") != NULL);
}

static void test_abnormal_end_no_screen(void)
{
    world(1, 1);
    save_account();
    CHECK_EQ(bl_session_begin(P.state), 0);
    bl_session_first_frame();                                /* reached the game, then never ended cleanly */
    Fake k = fake(0); k.max_ticks = 30;
    BlServices s = world_services(&k);
    BlDecision d;
    bl_start_last_band = -1;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK(!strcmp(k.screens[0], "start"));
    CHECK_EQ(saw(&k, "error"), 0);
    CHECK(strstr(slurp(P.state, BL_EVENTS_FILE), " E102 ") != NULL);
}

/* ---------------- (g) fluid navigation (owner finding 34) ---------------- */

/* The real loop at 60 Hz (each present waits one vblank), with presses at
 * the given loop passes; the run ends at pass `end`. */
static Fake K;
static uint32_t SCRIPT[1200];
static void run_presses(const int *at, const uint32_t *keys, int n, int end)
{
    memset(SCRIPT, 0, sizeof SCRIPT);
    for (int i = 0; i < n; i++) SCRIPT[at[i]] = keys[i];
    K = fake(BL_READY_OK);
    K.script = SCRIPT; K.script_len = end; K.quit_at = end;
    BlServices s = services(&K);
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK(K.np < REC && K.nsleep < REC);
}

static int present_at(uint64_t t)
{
    int i = 0;
    while (i < K.np && K.pt[i] < t) i++;
    return i;
}

static int sleeps_in(uint64_t a, uint64_t b)
{
    int n = 0;
    for (int i = 0; i < K.nsleep; i++) n += K.st[i] >= a && K.st[i] < b;
    return n;
}

static uint64_t max_gap(int from, int to)
{
    uint64_t g = 0;
    for (int i = from + 1; i <= to && i < K.np; i++) if (K.pt[i] - K.pt[i - 1] > g) g = K.pt[i] - K.pt[i - 1];
    return g;
}

/* One card to the right: at least 8 frames between the two cards, eased
 * (each step no larger than the one before), ending exactly on the card,
 * no sleep while it moves, every frame within 17 ms of the one before. */
static void test_slide_is_fluid(void)
{
    static const int at[] = { 300 };
    static const uint32_t keys[] = { R };
    run_presses(at, keys, 1, 420);
    CHECK(sleeps_in(0, K.press_t[0]) > 0);                     /* idle before the press: the loop sleeps */
    int p0 = present_at(K.press_t[0]), end = p0, between = 0;
    while (end < K.np && K.ppos[end] != 2.0f) end++;
    CHECK(end < K.np);
    for (int i = p0; i <= end && i < K.np; i++) between += K.ppos[i] > 1.0f && K.ppos[i] < 2.0f;
    fprintf(stderr, "  slide: %d frames between the cards, %d presents to the end\n", between, end - p0 + 1);
    CHECK(between >= 8);
    for (int i = p0 + 1; i <= end && i < K.np; i++) {
        CHECK(K.ppos[i] >= K.ppos[i - 1]);                    /* monotonic */
        if (i >= p0 + 2) CHECK(K.ppos[i] - K.ppos[i - 1] <= K.ppos[i - 1] - K.ppos[i - 2] + 1e-6f);   /* eased out */
        if (K.ppos[i] > 1.0f) CHECK(K.phash[i] != K.phash[i - 1]);   /* every frame shows a new picture */
    }
    for (int i = end; i < K.np; i++) CHECK(K.ppos[i] == 2.0f);
    /* Eased out: it starts fast and lands softly (a linear slide has equal steps). */
    if (end >= p0 + 2 && end < K.np) CHECK(K.ppos[p0 + 1] - K.ppos[p0] > 2.0f * (K.ppos[end] - K.ppos[end - 1]));
    CHECK_EQ(sleeps_in(K.press_t[0], K.pt[end < K.np ? end : K.np - 1]), 0);
    CHECK(max_gap(p0, end) <= 17000000u);
}

/* A press during a slide turns it around from where it is: no jump. */
static void test_slide_retarget(void)
{
    static const int at[] = { 300, 304 };
    static const uint32_t keys[] = { R, L };
    run_presses(at, keys, 2, 420);
    int p0 = present_at(K.press_t[0]), end = present_at(K.press_t[1]);
    float top = 1.0f, worst = 0.0f;
    while (end < K.np && K.ppos[end] != 1.0f) end++;
    CHECK(end < K.np);
    for (int i = p0 + 1; i <= end && i < K.np; i++) {
        float d = fabsf(K.ppos[i] - K.ppos[i - 1]);
        if (d > worst) worst = d;
        if (K.ppos[i] > top) top = K.ppos[i];
    }
    fprintf(stderr, "  retarget: reached %.3f, largest step %.3f\n", (double)top, (double)worst);
    CHECK(top > 1.2f && top < 2.0f);
    CHECK(worst <= 0.35f);
    for (int i = end; i < K.np; i++) CHECK(K.ppos[i] == 1.0f);
    CHECK_EQ(sleeps_in(K.press_t[0], K.pt[end < K.np ? end : K.np - 1]), 0);
}

/* Presents from a press up to the first sleep after it: the transition. */
static int transition_frames(uint64_t press, const char *screen, int *changes)
{
    int p0 = present_at(press), n = 0;
    uint64_t sleep_t = ~0ull;
    for (int i = 0; i < K.nsleep; i++) if (K.st[i] >= press) { sleep_t = K.st[i]; break; }
    *changes = 0;
    for (int i = p0; i < K.np && K.pt[i] < sleep_t; i++) {
        n++;
        CHECK(!strcmp(K.pscreen[i], screen));
        if (i > p0 && K.phash[i] != K.phash[i - 1]) ++*changes;
    }
    CHECK(max_gap(p0, p0 + n - 1) <= 17000000u);
    return n;
}

/* Screen changes are a short cross-fade (120-150 ms, at least 8 frames)
 * without sleeping, both ways. No About screen anywhere (finding 32). */
static void test_screen_change_fades(void)
{
    static const int at[] = { 300, 340, 420, 460, 470 };
    static const uint32_t keys[] = { L, X, O, L, X };     /* settings, open, back, (stays on settings), open */
    run_presses(at, keys, 5, 560);
    int c1, c2, c3;
    int n1 = transition_frames(K.press_t[1], "settings", &c1);
    int n2 = transition_frames(K.press_t[2], "start", &c2);
    int n3 = transition_frames(K.press_t[4], "settings", &c3);
    fprintf(stderr, "  fades: open %d frames (%d changes), back %d (%d), again %d (%d)\n", n1, c1, n2, c2, n3, c3);
    CHECK(c1 >= 8 && c2 >= 8 && c3 >= 8);
    CHECK(n1 <= 12 && n2 <= 12 && n3 <= 12);                /* 120-150 ms at 60 Hz, then idle */
    CHECK_EQ(saw(&K, "about"), 0);
}

/* The Settings file reads (log scan, account) happen in Start idle time:
 * opening Settings reads nothing (the account file removed meanwhile still
 * shows the account read before). */
static void test_settings_reads_in_idle(void)
{
    world(1, 1);
    save_account();
    static const int at[] = { 300, 310 };
    static const uint32_t keys[] = { L, X };
    memset(SCRIPT, 0, sizeof SCRIPT);
    SCRIPT[at[0]] = keys[0]; SCRIPT[at[1]] = keys[1];
    K = fake(0);
    K.script = SCRIPT; K.script_len = 400; K.quit_at = 400; K.unlink_at = 305;
    snprintf(K.unlink_path, sizeof K.unlink_path, "%s/%s", P.state, BL_ACCOUNT_FILE);
    BlServices s = world_services(&K);
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK(access(K.unlink_path, F_OK) != 0);
    CHECK(!strcmp(bl_settings_last_drawn.account_user, "racer"));
}

/* Owner 2026-11-16: Settings > Frame rate from Start. Not a verified
 * Blur.exe: 60 refused with the reason; the tested exe: 60 saved. */
static void settings_frame_rate_run(int tested)
{
    world(tested, 1);
    save_account();
    static const int at[] = { 300, 330, 360, 390, 420, 450 };
    static const uint32_t keys[] = { L, X, X, R, O, O };
    memset(SCRIPT, 0, sizeof SCRIPT);
    for (int i = 0; i < 6; i++) SCRIPT[at[i]] = keys[i];
    K = fake(0);
    K.script = SCRIPT; K.script_len = 600; K.quit_at = 600;
    BlServices s = world_services(&K);
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
}

static void test_settings_frame_rate_flow(void)
{
    settings_frame_rate_run(0);
    CHECK_EQ(bl_settings_last_drawn.exe_untested, 1);
    CHECK_EQ(bl_settings_last_drawn.cfg.frame_rate, BL_FPS_30);
    CHECK(strstr(slurp(P.state, "launcher.ini"), "frame_rate = 30") != NULL);
    settings_frame_rate_run(1);
    CHECK_EQ(bl_settings_last_drawn.exe_untested, 0);
    CHECK_EQ(bl_settings_last_drawn.cfg.frame_rate, BL_FPS_60);
    CHECK(strstr(slurp(P.state, "launcher.ini"), "frame_rate = 60") != NULL);
    CHECK(strstr(slurp(P.state, BL_EVENTS_FILE), "frame_rate=60") != NULL);
}

int main(void)
{
    RUN(test_carousel_navigation);
    RUN(test_slide_is_fluid);
    RUN(test_slide_retarget);
    RUN(test_screen_change_fades);
    RUN(test_settings_reads_in_idle);
    RUN(test_play_through_run);
    RUN(test_play_with_game_missing);
    RUN(test_no_controller_then_cross);
    RUN(test_start_golden);
    RUN(test_loader_deterministic_and_moving);
    RUN(test_loader_progress);
    RUN(test_loader_big_line);
    RUN(test_start_failed_screen);
    RUN(test_title_stop_notices);
    RUN(test_bad_arguments);
    RUN(test_play_other_exe_no_question);
    RUN(test_play_no_account_offline_never_asks);
    RUN(test_game_folder_untouched_until_patch);
    RUN(test_game_root_any_case);
    RUN(test_play_unpatched_dialog);
    RUN(test_play_no_account_warns_once);
    RUN(test_play_clock_off_dialog);
    RUN(test_play_writes_arm_not_pending);
    RUN(test_error_screen_only_no_first_frame);
    RUN(test_abnormal_end_no_screen);
    RUN(test_untested_exe_locks_30_from_frame_1);
    RUN(test_tested_exe_frame_rate_30);
    RUN(test_tested_exe_frame_rate_60);
    RUN(test_frame_rate_env_fits_worst_case);
    RUN(test_settings_frame_rate_flow);
    RUN(test_play_resets_dxn_stage);
    RUN(test_launcher_start_removes_prefill);
    char cmd[400]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); sh(cmd);
    bl_exe_expected_sha = BL_TESTED_EXE_SHA;
    return bl_test_report("test_frontend");
}
