/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests for the Patch, Settings and Help screens and the whole
 * front-end flow with real folders (plan Tasks 5-9; BLUR 1.00 plan Task L9). */
#define _DEFAULT_SOURCE
#include "bl_account.h"
#include "bl_amax.h"
#include "bl_clock.h"
#include "bl_config.h"
#include "bl_events.h"
#include "bl_exe.h"
#include "bl_frontend.h"
#include "bl_fs.h"
#include "bl_import.h"
#include "bl_logs.h"
#include "bl_paths.h"
#include "bl_patcher.h"
#include "bl_png.h"
#include "bl_screens.h"
#include "bl_session.h"
#include "bl_sha256.h"
#include "bl_strings.h"
#include "bl_test.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[256], blur_root[300], files[400];
static BlPaths P;

static void sh(const char *cmd) { if (system(cmd) != 0) fprintf(stderr, "  command failed: %s\n", cmd); }

static void put(const char *dir, const char *name, const char *text)
{
    char p[900]; snprintf(p, sizeof p, "%s/%s", dir, name);
    char d[900]; snprintf(d, sizeof d, "%s", p);
    char *sl = strrchr(d, '/'); if (sl) { *sl = 0; bl_fs_mkdirs(d); }
    FILE *f = fopen(p, "wb"); fputs(text, f); fclose(f);
}

static char *slurp(const char *dir, const char *name)
{
    static char buf[8192];
    char p[900]; snprintf(p, sizeof p, "%s/%s", dir, name);
    if (bl_fs_read(p, buf, sizeof buf) < 0) buf[0] = 0;
    return buf;
}

static int exists(const char *dir, const char *name)
{
    char p[900]; snprintf(p, sizeof p, "%s/%s", dir, name);
    return access(p, F_OK) == 0;
}

static const char *DLL = "our d3d9_blur_dxvk.dll";
static char exe_sha[65];

static void make_exe(const char *dir, int tested)
{
    char p[700]; snprintf(p, sizeof p, "%s/Blur.exe", dir);
    bl_fs_mkdirs(dir);
    int fd = open(p, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0 || write(fd, tested ? "MZ" : "MZ?", tested ? 2 : 3) < 0 || ftruncate(fd, (off_t)BL_TESTED_EXE_SIZE) != 0) exit(1);
    close(fd);
    put(dir, "cache/root.pak", "pak");
}

/* The engine's state cache seed: a "cache" line, installed into P.cache by Patch. */
static const char *CACHE_SEED = "DXVK\x0a\x01\x02\x03\x04\x05\x06\x07 seed with entries";

/* A fresh BLUR root; game 1: the player's Blur (tested exe) is in the game folder, 0: no game. */
static void setup(int game)
{
    char cmd[600];
    if (root[0]) { snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); sh(cmd); }
    snprintf(root, sizeof root, "/tmp/bl_test_screens_XXXXXX");
    if (!mkdtemp(root)) exit(1);
    snprintf(blur_root, sizeof blur_root, "%s/BLUR", root);
    bl_paths_init(&P, blur_root);
    bl_paths_make(&P);
    snprintf(files, sizeof files, "%s/blur-files", root);
    bl_fs_mkdirs(files);
    char hd[65], man[512];
    bl_sha256_hex(DLL, strlen(DLL), hd);
    char hc[65];
    bl_sha256_hex(CACHE_SEED, strlen(CACHE_SEED), hc);
    put(files, "d3d9_blur_dxvk.dll", DLL);
    put(files, "dxvk-native.dxvk-cache", CACHE_SEED);
    snprintf(man, sizeof man, "file d3d9_blur_dxvk.dll %s %zu\ncache dxvk-native.dxvk-cache %s %zu\n", hd, strlen(DLL), hc, strlen(CACHE_SEED));
    put(files, "manifest.txt", man);
    snprintf(cmd, sizeof cmd, "cp '%s/../assets/blur-files/model-base.ini' '%s/'", BL_TEST_DIR, files);
    sh(cmd);
    char p[700]; snprintf(p, sizeof p, "%s/Blur.exe", P.game);
    if (game) { make_exe(P.game, 1); bl_fs_sha256(p, exe_sha, NULL); }
    else snprintf(exe_sha, sizeof exe_sha, "%s", BL_TESTED_EXE_SHA);
    bl_exe_expected_sha = exe_sha;
    BlAccount a; memset(&a, 0, sizeof a);
    snprintf(a.user, sizeof a.user, "racer"); snprintf(a.pass, sizeof a.pass, "pw");
    bl_account_save(&P, &a);
    bl_session_end_clean();
}

/* ---------------- fake services ---------------- */

typedef struct Fake {
    uint64_t now;
    const uint32_t *script; int len, pos;
    int osk_calls;            /* the system keyboard: must never be called (finding 12) */
    char screens[32][16]; int nscreens;
    int ticks, max_ticks;
    int presents;
} Fake;

static int f_present(void *ctx, const BlFrame *f) { (void)f; ((Fake *)ctx)->presents++; return 0; }
static int f_pad(void *ctx, uint32_t *held, uint32_t *pressed)
{
    Fake *k = ctx; *held = 0;
    *pressed = k->pos < k->len ? k->script[k->pos++] : 0;
    return 0;
}
static int f_quit(void *ctx) { Fake *k = ctx; return ++k->ticks >= k->max_ticks; }
static uint64_t f_now(void *ctx) { return ((Fake *)ctx)->now; }
static void f_sleep(void *ctx, uint32_t us) { ((Fake *)ctx)->now += (uint64_t)us * 1000u; }
static int f_net(void *ctx, char *ip, size_t cap) { (void)ctx; snprintf(ip, cap, "192.168.0.124"); return 0; }
static int f_osk(void *ctx, const char *title, int secret, char *out, size_t cap)
{
    Fake *k = ctx; (void)title; (void)secret; (void)out; (void)cap;
    k->osk_calls++;
    return -1;
}
static void f_screen(void *ctx, const char *name)
{
    Fake *k = ctx;
    if (k->nscreens < 32) snprintf(k->screens[k->nscreens++], 16, "%s", name);
}

static BlServices services(Fake *k)
{
    BlServices s; memset(&s, 0, sizeof s);
    s.ctx = k; s.present = f_present; s.pad = f_pad; s.should_quit = f_quit; s.now_ns = f_now; s.sleep_us = f_sleep;
    s.net_status = f_net; s.osk = f_osk; s.screen = f_screen;
    s.data_root = blur_root; s.game_dir = P.game; s.package_files_dir = files; s.build_id = "BLUR-1.00";
    return s;
}

static int saw_screen(const Fake *k, const char *screen)
{
    int n = 0;
    for (int i = 0; i < k->nscreens; i++) n += !strcmp(k->screens[i], screen);
    return n;
}

static int run_clean(const BlServices *s, BlDecision *d)
{
    int r = bl_frontend_run(s, d);
    bl_session_first_frame();
    bl_session_end_clean();
    return r;
}

static const char *env_of(const BlDecision *d, const char *name)
{
    for (unsigned i = 0; i < d->env_count; i++) if (!strcmp(d->env[i].name, name)) return d->env[i].value;
    return NULL;
}

enum { X = BL_PAD_CROSS, O = BL_PAD_CIRCLE, L = BL_PAD_LEFT, R = BL_PAD_RIGHT, D = BL_PAD_DOWN, U = BL_PAD_UP };

/* ---------------- settings ---------------- */

static void test_settings_has_exactly_five_items(void)
{
    BlSettingsState st;
    bl_settings_init(&st, NULL);
    int listed[BL_SET_COUNT], n = 0;
    for (int i = 0; i < BL_SET_COUNT; i++) if (bl_settings_listed(&st, i)) listed[n++] = i;
    CHECK_EQ(n, 5);
    if (n == 5) {
        CHECK_EQ(listed[0], BL_SET_FRAMERATE);
        CHECK_EQ(listed[1], BL_SET_STATS);
        CHECK_EQ(listed[2], BL_SET_LOGGING);
        CHECK_EQ(listed[3], BL_SET_ACCOUNT);
        CHECK_EQ(listed[4], BL_SET_SUPPORT);
    }
    CHECK(!strcmp(bl_str(BL_S_SET_FRAMERATE), "Frame rate"));
    CHECK(!strcmp(bl_str(BL_S_SET_STATS), "Show FPS / ms"));
    CHECK(!strcmp(bl_str(BL_S_SET_LOGGING), "Enable logs"));
    CHECK(!strcmp(bl_str(BL_S_SET_ACCOUNT), "Online account"));
    CHECK(!strcmp(bl_str(BL_S_SET_SUPPORT), "Save support bundle"));
    CHECK_EQ(bl_settings_rows(&st, BL_SET_SUPPORT), 1);
    CHECK_EQ(st.item, BL_SET_FRAMERATE);
    /* Show FPS / ms is Off or On (DXVK_PS4_FPS_HUD fps). */
    BlField f;
    bl_settings_input(&st, D, &f);
    CHECK_EQ(st.item, BL_SET_STATS);
    bl_settings_input(&st, X, &f);
    bl_settings_input(&st, R, &f);
    CHECK_EQ(st.cfg.stats, BL_STATS_FPS);
    bl_settings_input(&st, R, &f);
    CHECK_EQ(st.cfg.stats, BL_STATS_FPS);                       /* only two options */
    bl_settings_input(&st, O, &f);
    for (int i = 0; i < 10; i++) bl_settings_input(&st, D, &f);
    CHECK_EQ(st.item, BL_SET_SUPPORT);
    bl_settings_input(&st, X, &f);
    CHECK_EQ(bl_settings_input(&st, X, &f), BL_SET_EV_BUNDLE);
}

/* Owner 2026-11-16: Frame rate, 30 FPS (default) or 60 FPS, with the help of
 * the value chosen under it. Not a verified Blur.exe: 30 FPS only. */
#define FPS30_HELP "Races run at a steady 30 FPS, menus at 60. Recommended for PS4 and PS4 Slim, and for a Blur.exe that is not verified."
#define FPS60_HELP "Races run at up to 60 FPS; split screen stays at 30. For PS4 Pro with Boost Mode on " \
                   "(PS4 Settings > System > Boost Mode). It may also work on other models, but races can be less stable."
#define FPS_UNTESTED "This Blur.exe is not a verified Blur, so JkFlow runs it at 30 FPS. Run Patch for online play and " \
                     "follow the steps; after that you can turn on 60 FPS on a PS4 Pro."
static void test_settings_frame_rate(void)
{
    CHECK(!strcmp(bl_str(BL_S_PANEL_FRAMERATE), "frame rate"));
    CHECK(!strcmp(bl_str(BL_S_OPT_FPS30), "30 FPS"));
    CHECK(!strcmp(bl_str(BL_S_OPT_FPS60), "60 FPS"));
    CHECK(!strcmp(bl_str(BL_S_NOTE_FPS30), FPS30_HELP));
    CHECK(!strcmp(bl_str(BL_S_NOTE_FPS60), FPS60_HELP));
    CHECK(!strcmp(bl_str(BL_S_FRAMERATE_UNTESTED), FPS_UNTESTED));
    BlSettingsState st;
    bl_settings_init(&st, NULL);
    BlField f;
    char note[400];
    CHECK_EQ(st.item, BL_SET_FRAMERATE);
    CHECK_EQ(bl_settings_rows(&st, BL_SET_FRAMERATE), 1);
    CHECK_EQ(st.cfg.frame_rate, BL_FPS_30);
    bl_settings_note(&st, BL_SET_FRAMERATE, note, sizeof note);
    CHECK(!strcmp(note, FPS30_HELP));
    bl_settings_input(&st, X, &f);
    CHECK(st.in_panel);
    bl_settings_input(&st, R, &f);
    CHECK_EQ(st.cfg.frame_rate, BL_FPS_60);
    bl_settings_input(&st, R, &f);
    CHECK_EQ(st.cfg.frame_rate, BL_FPS_60);                     /* two options */
    bl_settings_note(&st, BL_SET_FRAMERATE, note, sizeof note);
    CHECK(!strcmp(note, FPS60_HELP));
    bl_settings_input(&st, L, &f);
    CHECK_EQ(st.cfg.frame_rate, BL_FPS_30);
    bl_settings_input(&st, X, &f);
    CHECK_EQ(st.cfg.frame_rate, BL_FPS_60);                     /* Cross switches */
    /* Not a verified Blur.exe: 60 is refused with the reason, the note says it. */
    BlSettingsState u;
    bl_settings_init(&u, NULL);
    u.exe_untested = 1;
    bl_settings_note(&u, BL_SET_FRAMERATE, note, sizeof note);
    CHECK(!strcmp(note, FPS_UNTESTED));
    bl_settings_input(&u, X, &f);
    CHECK_EQ(u.message, -1);
    bl_settings_input(&u, R, &f);
    CHECK_EQ(u.cfg.frame_rate, BL_FPS_30);
    CHECK_EQ(u.message, BL_S_FRAMERATE_UNTESTED);
    u.message = -1;
    bl_settings_input(&u, X, &f);
    CHECK_EQ(u.cfg.frame_rate, BL_FPS_30);
    CHECK_EQ(u.message, BL_S_FRAMERATE_UNTESTED);
    /* A saved 60 is kept (the exe may be replaced later) but not shown as on. */
    BlConfig c; bl_config_defaults(&c); c.frame_rate = BL_FPS_60;
    bl_settings_init(&u, &c);
    u.exe_untested = 1;
    bl_settings_input(&u, X, &f);
    bl_settings_input(&u, L, &f);
    CHECK_EQ(u.cfg.frame_rate, BL_FPS_60);
    CHECK_EQ(u.message, -1);
    bl_settings_input(&u, R, &f);
    CHECK_EQ(u.message, BL_S_FRAMERATE_UNTESTED);
    /* Frame rate is never taken off the list. */
    bl_settings_hide(&st, BL_SET_FRAMERATE);
    CHECK(bl_settings_listed(&st, BL_SET_FRAMERATE));
}

static void test_settings_rules(void)
{
    BlSettingsState st;
    bl_settings_init(&st, NULL);
    BlField f;
    CHECK_EQ(bl_settings_input(&st, O, &f), BL_SET_EV_BACK);
    CHECK_EQ(bl_settings_input(&st, U, &f), BL_SET_EV_NONE);
    CHECK_EQ(st.item, BL_SET_FRAMERATE);
    bl_settings_input(&st, D, &f);
    bl_settings_input(&st, D, &f);
    CHECK_EQ(st.item, BL_SET_LOGGING);
    bl_settings_input(&st, X, &f);
    CHECK(st.in_panel);
    bl_settings_input(&st, L, &f);
    CHECK_EQ(st.cfg.logging, 0);                                /* clamps at Off */
    bl_settings_input(&st, D, &f);
    CHECK_EQ(bl_settings_input(&st, X, &f), BL_SET_EV_CLEAR_LOGS);
}

/* Audit 13: no text asks the player to send something to us (there is no contact address). */
static int asks_to_send_to_us(const char *s)
{
    if (strstr(s, "send us") || strstr(s, "Send us") || strstr(s, "send it to")) return 1;
    for (const char *p = strstr(s, "to us"); p; p = strstr(p + 1, "to us")) {
        char c = p[5];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return 1;
    }
    return 0;
}

static void test_strings(void)
{
    CHECK(asks_to_send_to_us("to send us") && asks_to_send_to_us("and send it to them") && asks_to_send_to_us("give it to us."));
    CHECK(!asks_to_send_to_us("used to use it"));
    CHECK(!strcmp(bl_str(BL_S_BAND_SUPPORT), "Collects the logs and your settings into one folder"));
    CHECK(!strcmp(bl_str(BL_S_BUNDLE_DONE), "Saved in %s. If needed, copy that folder to a PC over FTP."));
    /* Audit 19: one controller, one log file (not "1 controllers"). */
    {
        char t[80];
        snprintf(t, sizeof t, bl_str_n(BL_S_STATUS_PAD_ONE, BL_S_STATUS_PADS, 1), 1); CHECK(!strcmp(t, "1 controller"));
        snprintf(t, sizeof t, bl_str_n(BL_S_STATUS_PAD_ONE, BL_S_STATUS_PADS, 2), 2); CHECK(!strcmp(t, "2 controllers"));
        snprintf(t, sizeof t, bl_str_n(BL_S_STATUS_PAD_ONE, BL_S_STATUS_PADS, 0), 0); CHECK(!strcmp(t, "0 controllers"));
        snprintf(t, sizeof t, bl_str_n(BL_S_LOGS_CLEARED_ONE, BL_S_LOGS_CLEARED, 1), 1u); CHECK(!strcmp(t, "1 log file cleared"));
        snprintf(t, sizeof t, bl_str_n(BL_S_LOGS_CLEARED_ONE, BL_S_LOGS_CLEARED, 3), 3u); CHECK(!strcmp(t, "3 log files cleared"));
        snprintf(t, sizeof t, bl_str_n(BL_S_LOG_SUMMARY_ONE, BL_S_LOG_SUMMARY, 1), 1, 0.5); CHECK(!strcmp(t, "1 log file \xC2\xB7 0.5 MB"));
        snprintf(t, sizeof t, bl_str_n(BL_S_LOG_SUMMARY_ONE, BL_S_LOG_SUMMARY, 4), 4, 2.0); CHECK(!strcmp(t, "4 log files \xC2\xB7 2.0 MB"));
        /* Owner finding 40: under 1 MB the size is whole KB, rounded up (any byte is at least 1 KB; no logs is 0 KB). */
        bl_log_summary(t, sizeof t, 7, 41 * 1024); CHECK(!strcmp(t, "7 log files \xC2\xB7 41 KB"));
        bl_log_summary(t, sizeof t, 7, 41 * 1024 - 300); CHECK(!strcmp(t, "7 log files \xC2\xB7 41 KB"));
        bl_log_summary(t, sizeof t, 1, 10); CHECK(!strcmp(t, "1 log file \xC2\xB7 1 KB"));
        bl_log_summary(t, sizeof t, 1, 2048); CHECK(!strcmp(t, "1 log file \xC2\xB7 2 KB"));
        bl_log_summary(t, sizeof t, 0, 0); CHECK(!strcmp(t, "0 log files \xC2\xB7 0 KB"));
        bl_log_summary(t, sizeof t, 1, 1 << 19); CHECK(!strcmp(t, "1 log file \xC2\xB7 512 KB"));
        bl_log_summary(t, sizeof t, 1, 1 << 20); CHECK(!strcmp(t, "1 log file \xC2\xB7 1.0 MB"));
        bl_log_summary(t, sizeof t, 3, 3 << 20); CHECK(!strcmp(t, "3 log files \xC2\xB7 3.0 MB"));
        bl_log_summary(t, sizeof t, 9, 5LL * 1024 * 1024 + 300 * 1024); CHECK(!strcmp(t, "9 log files \xC2\xB7 5.3 MB"));
    }
    for (int i = 0; i < BL_S_COUNT; i++) {
        const char *s = bl_str((BlStringId)i);
        CHECK(s[0] != 0);
        if (!s[0]) fprintf(stderr, "  empty string id %d\n", i);
        CHECK(strstr(s, "\xC3\xA5") == NULL && strstr(s, "\xC3\xA4") == NULL && strstr(s, "\xC3\xB6") == NULL);
        CHECK(strstr(s, "\xC3\x85") == NULL && strstr(s, "\xC3\x84") == NULL && strstr(s, "\xC3\x96") == NULL);
        /* The only console paths in a string: /data/JkFlow/Blur (the game) and /data/JkFlow (the zips);
         * our own folders come from bl_paths at run time (F1 folder move). */
        CHECK(strstr(s, "/data/") == NULL || strstr(s, "/data/JkFlow") != NULL);
        CHECK(strstr(s, "/data/BLUR") == NULL);
        CHECK(strstr(s, "50+") == NULL);                            /* owner 2026-11-16: no 50+ FPS claim */
        CHECK(!asks_to_send_to_us(s));
        if (asks_to_send_to_us(s)) fprintf(stderr, "  string %d asks to send to us: %s\n", i, s);
        CHECK(strstr(s, "/data/Pc") == NULL && strstr(s, "rospero") == NULL && strstr(s, "ROSPERO") == NULL);
        if (strstr(s, "/data/Pc")) fprintf(stderr, "  string %d: %s\n", i, s);
    }
}

static void golden(const char *name, const BlFrame *f)
{
    char hex[65], path[512], want[80] = "";
    bl_sha256_hex(f->pixels, (size_t)f->stride * f->height, hex);
    snprintf(path, sizeof path, BL_TEST_DIR "/golden/%s.sha256", name);
    char png[512]; snprintf(png, sizeof png, BL_TEST_DIR "/../build/golden-%s.png", name);
    if (getenv("BL_UPDATE_GOLDEN")) { FILE *o = fopen(path, "w"); fprintf(o, "%s\n", hex); fclose(o); }
    FILE *g = fopen(path, "r");
    if (g) { if (!fgets(want, sizeof want, g)) want[0] = 0; fclose(g); }
    want[64] = 0;
    CHECK(strcmp(hex, want) == 0);
    if (strcmp(hex, want) || getenv("BL_UPDATE_GOLDEN")) {
        bl_png_write(png, f);
        if (strcmp(hex, want)) fprintf(stderr, "  %s sha256 %s, golden %s\n", name, hex, want);
    }
}

/* ---------------- the whole flow ---------------- */

/* Patch installs our files in place in the player's folder; Settings turn
 * logs on; Play starts Blur from that folder with them. */
static void test_full_flow_patch_settings_play(void)
{
    setup(1);
    static const uint32_t script[] = {
        R,              /* play -> patch */
        X,              /* open Patch */
        X,              /* patch now: check the folder, install, record */
        O,              /* "Patch is done" dialog: back to start (finding 38) */
        L, L, X,        /* play -> settings, open */
        D, D,           /* Enable logs (below Frame rate, Show FPS / ms) */
        X, R, O,        /* into panel, On, out of panel */
        O,              /* save and back */
        R, X,           /* settings -> play, start */
    };
    Fake k; memset(&k, 0, sizeof k);
    k.script = script; k.len = (int)(sizeof script / sizeof script[0]); k.max_ticks = 500;
    BlServices s = services(&k);
    BlDecision d;
    CHECK_EQ(run_clean(&s, &d), 0);
    CHECK_EQ(d.action, BL_START_GAME);
    CHECK(!strcmp(slurp(P.game, "d3d9_blur_dxvk.dll"), DLL));
    CHECK(exists(P.game, "cache/root.pak"));
    CHECK(!strcmp(slurp(P.cache, "dxvk-native.dxvk-cache"), CACHE_SEED));   /* seed in /data/BLUR/cache */
    CHECK(!exists(P.game, "dxvk-native.dxvk-cache"));                       /* never in the game folder */
    char exe_line[700], wdir[600];                           /* Blur starts from the player's folder */
    snprintf(wdir, sizeof wdir, "Z:%s", P.game);
    for (char *c = wdir; *c; c++) if (*c == '/') *c = '\\';
    snprintf(exe_line, sizeof exe_line, "executable = %s\\Blur.exe\n", wdir);
    CHECK(strstr(d.profile_text, exe_line) != NULL);
    CHECK(strstr(slurp(P.state, BL_EVENTS_FILE), "patch: game folder ") != NULL);
    CHECK(strstr(slurp(P.state, "launcher.ini"), "enabled = true") != NULL);
    CHECK(env_of(&d, "BLUR_LOGS") && !strcmp(env_of(&d, "BLUR_LOGS"), "1"));
    CHECK(strstr(d.dxvk_conf, "d3d9.maxFrameRate = 60") != NULL);
    char rec[400]; snprintf(rec, sizeof rec, "%s", slurp(P.state, "patch.txt"));
    CHECK(strstr(rec, "build_id=BLUR-1.00\n") && strstr(rec, "manifest_sha=") && strstr(rec, "stamp="));
    const char *want[] = { "start", "patch", "dialog", "start", "settings", "start" };
    CHECK_EQ(k.nscreens, 6);
    for (int i = 0; i < 6 && i < k.nscreens; i++) CHECK(strcmp(k.screens[i], want[i]) == 0);
}

/* Another Blur.exe (ruling 2026-11-15, like Sahin): Patch logs E205 as a
 * warning and goes on; the player's own exe is never replaced. */
static void test_patch_other_exe_continues(void)
{
    setup(1);
    make_exe(P.game, 0);                                     /* another build of the right size */
    char exe[700], before[65], after[65];
    snprintf(exe, sizeof exe, "%s/Blur.exe", P.game);
    CHECK_EQ(bl_fs_sha256(exe, before, NULL), 0);
    static const uint32_t script[] = { R, X, X };
    Fake k; memset(&k, 0, sizeof k);
    k.script = script; k.len = 3; k.max_ticks = 200;
    BlServices s = services(&k);
    BlDecision d;
    CHECK_EQ(run_clean(&s, &d), 0);
    CHECK_EQ(d.action, BL_LEAVE_HOME);
    CHECK(bl_fs_sha256(exe, after, NULL) == 0 && !strcmp(before, after));   /* the player's own exe, unchanged */
    CHECK(exists(P.game, "cache/root.pak"));
    CHECK(!strcmp(slurp(P.game, "d3d9_blur_dxvk.dll"), DLL));
    CHECK(strstr(slurp(P.state, BL_EVENTS_FILE), " E205 ") != NULL);
    CHECK_EQ(bl_patch_last_drawn.steps[1], BL_STEP_DONE);
    CHECK_EQ(bl_patch_last_drawn.exe_verdict, BL_EXE_UNSUPPORTED);
}

/* Review Focus 4 through the screen: a conflict asks, Keep is the default. */
static void test_patch_import_conflict_keeps(void)
{
    setup(1);
    char cur[700]; snprintf(cur, sizeof cur, "%s/save", P.saves);
    static const unsigned char hdr[12] = { 0x37, 0xE3, 0xCD, 0xAB, 1, 0, 0, 0, 2, 0, 0, 0 };
    char saved[40], imported[40];
    memcpy(saved, hdr, 12); memcpy(saved + 12, "mine", 5);
    memcpy(imported, hdr, 12); memcpy(imported + 12, "theirs", 7);
    bl_fs_mkdirs(cur);
    char p[800]; snprintf(p, sizeof p, "%s/blursave", cur);
    bl_fs_write_atomic(p, saved, 17, 0644);
    snprintf(p, sizeof p, "%s/blursave", P.import);
    bl_fs_write_atomic(p, imported, 19, 0644);
    static const uint32_t script[] = { R, X, X, X };          /* Patch, patch now, Cross on the dialog: Keep (focused) */
    Fake k; memset(&k, 0, sizeof k);
    k.script = script; k.len = 4; k.max_ticks = 80;
    BlServices s = services(&k);
    BlDecision d;
    CHECK_EQ(run_clean(&s, &d), 0);
    CHECK(strstr(slurp(P.state, BL_EVENTS_FILE), "patch: save import kept the current save") != NULL);   /* asked, Keep */
    CHECK_EQ(bl_dialog_last_drawn.body, BL_S_PATCH_DONE_ASK);   /* then Patch finished (finding 38) */
    char got[40] = "";
    snprintf(p, sizeof p, "%s/blursave", cur);
    CHECK_EQ(bl_fs_read(p, got, sizeof got), 17);
    CHECK(!memcmp(got + 12, "mine", 5));
    CHECK_EQ(bl_import_check(&P), BL_IMPORT_NONE);              /* not asked again */
    CHECK(bl_patch_last_drawn.steps[8] == BL_STEP_DONE);
}

/* Owner decision 2026-11-15: no search and no copy-in. Without Blur.exe in
 * the game folder Patch stops at step 1 with the one place to copy it to,
 * and writes nothing into the game folder. */
static void online_world(void);
static void place_zip(const char *zip, const char *as);
static char amax_zip[600], blur_zip[600];

static void test_patch_game_missing_stops(void)
{
    setup(0);
    static const uint32_t script[] = { R, X, X };
    Fake k; memset(&k, 0, sizeof k);
    k.script = script; k.len = 3; k.max_ticks = 80;
    BlServices s = services(&k);
    BlDecision d;
    CHECK_EQ(run_clean(&s, &d), 0);
    CHECK_EQ(d.action, BL_LEAVE_HOME);
    CHECK_EQ(bl_patch_last_drawn.steps[0], BL_STEP_FAIL);
    for (int i = 0; i < k.nscreens; i++) CHECK(strcmp(k.screens[i], "dialog") != 0);   /* finding 38: no "done" on a failure */
    CHECK(strstr(bl_patch_last_drawn.message_text, "Blur.exe was not found in ") != NULL && strstr(bl_patch_last_drawn.message_text, " Nothing was changed. ") != NULL);
    CHECK(!exists(P.game, "d3d9_blur_dxvk.dll"));
    CHECK(!exists(P.game, "Blur.exe"));
    /* Patch for online play without a game: the same stop, nothing written. */
    online_world();
    char p[900]; snprintf(p, sizeof p, "%s/Blur.exe", P.game);
    unlink(p);
    place_zip(amax_zip, "amax_client_files.zip");
    place_zip(blur_zip, "blur_12.zip");
    static const uint32_t online[] = { R, X, D, X };
    Fake m; memset(&m, 0, sizeof m);
    m.script = online; m.len = 4; m.max_ticks = 80;
    s = services(&m);
    CHECK_EQ(run_clean(&s, &d), 0);
    CHECK_EQ(bl_patch_last_drawn.steps[0], BL_STEP_FAIL);
    CHECK(!exists(P.game, "Blur.exe") && !exists(P.game, "d3d9.dll") && !exists(P.game, "amax"));
}

/* ---------------- offline and online Patch (owner spec 2026-11-15 v3) ---------------- */

static const char *V7 = "our V7 d3d9.dll", *PROXY = "AMAX proxy d3d9.dll", *AUTH = "AMAX amax_auth.asi";
static const char *EXE12 = "MZ Blur.exe 1.2 from blur_12.zip", *PAK12 = "gamedata.pak 1.2", *SIG12 = "gamedata.sig 1.2";
static char zip_dir[400], zsha[2][65], exe12_sha[65];

/* The package lists the offline and the online set; the fake zips are
 * built in <root>/zips (not yet placed in zip_dir, the parent of the game). */
static void online_world(void)
{
    setup(1);
    snprintf(zip_dir, sizeof zip_dir, "%s", P.game);
    *strrchr(zip_dir, '/') = 0;
    char zips[500], cmd[4000];
    snprintf(zips, sizeof zips, "%s/zips", root);
    bl_fs_mkdirs(zips);
    snprintf(amax_zip, sizeof amax_zip, "%s/amax_client_files.zip", zips);
    snprintf(blur_zip, sizeof blur_zip, "%s/blur_12.zip", zips);
    snprintf(cmd, sizeof cmd,
             "python3 -I -c 'import sys,zipfile\n"
             "z=zipfile.ZipFile(sys.argv[1],\"w\",zipfile.ZIP_DEFLATED); z.writestr(\"d3d9.dll\",sys.argv[3]); z.writestr(\"amax/dlls/amax_auth.asi\",sys.argv[4]); z.close()\n"
             "import hashlib; e=sys.argv[5].encode(); e+=bytes(13738496-len(e)); open(sys.argv[8],\"w\").write(hashlib.sha256(e).hexdigest())\n"
             "z=zipfile.ZipFile(sys.argv[2],\"w\",zipfile.ZIP_DEFLATED); z.writestr(\"Blur.exe\",e); z.writestr(\"cache/gamedata.pak\",sys.argv[6]); z.writestr(\"cache/gamedata.sig\",sys.argv[7]); z.close()'"
             " '%s' '%s' '%s' '%s' '%s' '%s' '%s' '%s/exe12.sha'", amax_zip, blur_zip, PROXY, AUTH, EXE12, PAK12, SIG12, zips);
    sh(cmd);
    char shaf[600]; snprintf(shaf, sizeof shaf, "%s/exe12.sha", zips);
    CHECK_EQ(bl_fs_read(shaf, exe12_sha, sizeof exe12_sha), 64);
    uint64_t size = 0;
    bl_fs_sha256(amax_zip, zsha[0], &size); bl_amax_zip_sha = zsha[0]; bl_amax_zip_size = (long long)size;
    bl_fs_sha256(blur_zip, zsha[1], &size); bl_blur12_zip_sha = zsha[1]; bl_blur12_zip_size = (long long)size;
    char h[7][65], hc[65], man[2000];
    const char *t[7] = { DLL, V7, PROXY, AUTH, EXE12, PAK12, SIG12 };
    for (int i = 0; i < 7; i++) bl_sha256_hex(t[i], strlen(t[i]), h[i]);
    bl_sha256_hex(CACHE_SEED, strlen(CACHE_SEED), hc);
    put(files, "d3d9.dll", V7);
    put(files, "amax-dxvk/dxvk.dll", V7);
    snprintf(man, sizeof man,
             "file d3d9_blur_dxvk.dll %s %zu\nfile d3d9.dll %s %zu\nonline amax/dxvk.dll %s %zu src=amax-dxvk/dxvk.dll\n"
             "amax d3d9.dll %s %zu\namax amax/dlls/amax_auth.asi %s %zu\nblur12 Blur.exe %s %zu\nblur12 cache/gamedata.pak %s %zu\n"
             "blur12 cache/gamedata.sig %s %zu\ncache dxvk-native.dxvk-cache %s %zu\npark amax\n",
             h[0], strlen(DLL), h[1], strlen(V7), h[1], strlen(V7), h[2], strlen(PROXY), h[3], strlen(AUTH),
             exe12_sha, (size_t)BL_TESTED_EXE_SIZE, h[5], strlen(PAK12), h[6], strlen(SIG12), hc, strlen(CACHE_SEED));
    put(files, "manifest.txt", man);
    put(P.game, "cache/gamedata.pak", "the player's own gamedata.pak");
}

static int exe_is12(void)
{
    char p[900], h[65] = "";
    snprintf(p, sizeof p, "%s/Blur.exe", P.game);
    return bl_fs_sha256(p, h, NULL) == 0 && !strcmp(h, exe12_sha);
}

static void place_zip(const char *zip, const char *as)
{
    char cmd[1500]; snprintf(cmd, sizeof cmd, "cp '%s' '%s/%s'", zip, zip_dir, as);
    sh(cmd);
}

/* sha256 of every file under dir with its name: a fingerprint of the folder. */
static void tree_sig(const char *dir, char *out, size_t cap)
{
    char cmd[800];
    snprintf(cmd, sizeof cmd, "cd '%s' && find . -type f -print0 | sort -z | xargs -0 -I{} sh -c 'echo \"{}\"; sha256sum < \"{}\"' | sha256sum", dir);
    FILE *p = popen(cmd, "r");
    out[0] = 0;
    if (p) { if (!fgets(out, (int)cap, p)) out[0] = 0; pclose(p); }
}

static int run_script(const uint32_t *script, int len, int max_ticks, BlDecision *d, Fake *k)
{
    memset(k, 0, sizeof *k);
    k->script = script; k->len = len; k->max_ticks = max_ticks;
    BlServices s = services(k);
    return run_clean(&s, d);
}

/* Game only: Patch for offline play (the default), no zip, no internet. */
static void test_patch_offline_game_only(void)
{
    online_world();
    static const uint32_t script[] = { R, X, X, O };
    Fake k; BlDecision d;
    CHECK_EQ(run_script(script, 4, 80, &d, &k), 0);
    CHECK_EQ(bl_patch_last_drawn.steps[8], BL_STEP_DONE);
    CHECK_EQ(bl_patch_last_drawn.message, BL_S_PATCH_ALL_DONE);
    CHECK(!strcmp(slurp(P.game, "d3d9.dll"), V7));
    CHECK(!exists(P.game, "amax"));
    CHECK(!strcmp(slurp(P.game, "cache/gamedata.pak"), "the player's own gamedata.pak"));
    CHECK_EQ(bl_start_last_status.readiness, BL_READY_OK);
    CHECK_EQ(bl_start_last_status.online, BL_ONLINE_NONE);
    CHECK(!strcmp(bl_str(BL_S_STATUS_PATCHED), "Patched for offline play"));     /* finding 28 */
}

/* Owner finding 38: a successful Patch also opens a one-button dialog;
 * Cross or Circle goes to Start (not back to Patch). The Patch message stays. */
static const char *last_screen(const Fake *k) { return k->nscreens ? k->screens[k->nscreens - 1] : ""; }

static void test_patch_done_dialog(void)
{
    CHECK(!strcmp(bl_str(BL_S_PATCH_DONE_ASK), "Patch is done! Blur is ready to play. Press Cross to go back to Start, then press Play."));
    CHECK(!strcmp(bl_str(BL_S_PATCH_DONE_ASK_ONLINE),
                  "Patch is done! Blur is ready for online and offline play. Press Cross to go back to Start, then press Play."));
    CHECK(!strcmp(bl_str(BL_S_DIALOG_BACK_TO_START), "Back to Start"));
    Fake k; BlDecision d;
    online_world();
    memset(&bl_dialog_last_drawn, 0, sizeof bl_dialog_last_drawn);
    static const uint32_t open[] = { R, X, X };
    CHECK_EQ(run_script(open, 3, 80, &d, &k), 0);
    CHECK_EQ(bl_patch_last_drawn.message, BL_S_PATCH_ALL_DONE);
    CHECK(!strcmp(last_screen(&k), "dialog"));
    CHECK_EQ(bl_dialog_last_drawn.body, BL_S_PATCH_DONE_ASK);
    CHECK_EQ(bl_dialog_last_drawn.first, BL_S_DIALOG_BACK_TO_START);
    CHECK_EQ(bl_dialog_last_drawn.second, -1);                   /* one button */
    static const uint32_t cross[] = { R, X, X, X };
    CHECK_EQ(run_script(cross, 4, 80, &d, &k), 0);
    CHECK(!strcmp(last_screen(&k), "start"));
    CHECK_EQ(bl_patch_last_drawn.message, BL_S_PATCH_ALL_DONE);
    static const uint32_t circle[] = { R, X, X, O };
    CHECK_EQ(run_script(circle, 4, 80, &d, &k), 0);
    CHECK(!strcmp(last_screen(&k), "start"));
    static const uint32_t right[] = { R, X, X, R, X };           /* Right does not leave the one button */
    CHECK_EQ(run_script(right, 5, 80, &d, &k), 0);
    CHECK(!strcmp(last_screen(&k), "start"));
}

/* Owner finding 31: nothing placed and no network. Patch for online play
 * stays on the Patch screen; step 4 says the network is missing and what to
 * do (the real zip folder), then "Press Square Help" stands out; no download
 * is tried and nothing in the game folder changes. */
static int f_net_down(void *ctx, char *ip, size_t cap) { (void)ctx; if (cap) ip[0] = 0; return -1; }
static int g_dl_fail, g_dl_calls;
static BlPatchView g_dl_seen[2];    /* audit 17: the Patch panel as each download starts */
static int fake_https(void *ctx, const char *url, const char *dest, long long max_bytes,
                      void (*prog)(void *, long long, long long), void *pctx);

static void no_network_want(char *want, size_t cap)
{
    snprintf(want, cap, "No network. Nothing was changed. Connect the PS4 to the internet and press Patch again, or put "
                        "amax_client_files.zip and blur_12.zip (still zipped) in %s.", zip_dir);
}

static void test_patch_online_no_network_stays(void)
{
    online_world();
    char before[100], after[100], want[1000];
    tree_sig(P.game, before, sizeof before);
    static const uint32_t script[] = { R, X, D, X };
    Fake k; memset(&k, 0, sizeof k);
    k.script = script; k.len = 4; k.max_ticks = 80;
    BlServices s = services(&k);
    s.net_status = f_net_down;
    s.https_get = fake_https;
    g_dl_fail = 0; g_dl_calls = 0;
    BlDecision d;
    CHECK_EQ(run_clean(&s, &d), 0);
    CHECK_EQ(g_dl_calls, 0);                                   /* no network: no download tried */
    CHECK(!strcmp(k.screens[k.nscreens - 1], "patch"));
    CHECK_EQ(saw_screen(&k, "help"), 0);
    CHECK_EQ(bl_patch_last_drawn.steps[3], BL_STEP_FAIL);       /* amber, as the step-4 failure */
    no_network_want(want, sizeof want);
    CHECK(!strcmp(bl_patch_last_drawn.message_text, want));
    if (strcmp(bl_patch_last_drawn.message_text, want)) fprintf(stderr, "  got: %s\n", bl_patch_last_drawn.message_text);
    CHECK(bl_patch_last_drawn.help_line);
    tree_sig(P.game, after, sizeof after);
    CHECK(before[0] && !strcmp(before, after));
    /* Square then opens Help at the online section, the cause on top. */
    static const uint32_t square[] = { R, X, D, X, BL_PAD_SQUARE };
    memset(&k, 0, sizeof k);
    k.script = square; k.len = 5; k.max_ticks = 80;
    s = services(&k);
    s.net_status = f_net_down;
    s.https_get = fake_https;
    CHECK_EQ(run_clean(&s, &d), 0);
    CHECK(!strcmp(k.screens[k.nscreens - 1], "help"));
    CHECK_EQ(bl_help_last_drawn.scroll, bl_help_section(BL_HELP_ONLINE));
    CHECK(!strcmp(bl_help_last_drawn.note, want));
    CHECK_EQ(g_dl_calls, 0);
}

/* The title's https_get (fake): the zips are downloaded into /data/BLUR,
 * with progress on the Patch screen; a failure stays on Patch with the cause
 * and the Help line (owner finding 31). */
static int fake_https(void *ctx, const char *url, const char *dest, long long max_bytes,
                      void (*prog)(void *, long long, long long), void *pctx)
{
    (void)ctx;
    if (g_dl_calls < 2) g_dl_seen[g_dl_calls] = bl_patch_last_drawn;     /* the panel while connecting */
    g_dl_calls++;
    CHECK(strstr(dest, zip_dir) == dest && !strstr(dest, ".part"));   /* the zip folder, the final name */
    if (prog) prog(pctx, 1, max_bytes);
    if (g_dl_fail) return g_dl_fail < 0 ? g_dl_fail : -1;          /* <0: that result code */
    char cmd[1500]; snprintf(cmd, sizeof cmd, "cp '%s' '%s.part' && mv '%s.part' '%s'", strstr(url, "blur_12") ? blur_zip : amax_zip, dest, dest, dest);
    sh(cmd);
    return 0;
}

/* Audits 9 (owner 2026-11-16): the secure download needs a right clock. A
 * clock that is off stops an online Patch before any download, and a
 * certificate failure from the title (-6) says the same; Help is one key away. */
#define CLOCK_TLS "The PS4 date and time are wrong, so the secure download cannot start. Nothing was changed. Sync the time in GoldHEN, " \
                  "or set it by hand in PS4 Settings > Date and Time. Or place the two zip files by hand: press {square} Help."
static long long g_srv_offset;
static int g_date_calls;
static long long fixed_clock(void) { return 1794711845LL; }
static int fake_date(void *ctx, const char *host, long long *epoch)
{
    (void)ctx; (void)host;
    g_date_calls++;
    *epoch = 1794711845LL - g_srv_offset;
    return 0;
}

static void test_patch_online_clock_wrong(void)
{
    CHECK(!strcmp(bl_str(BL_S_CLOCK_GOLDHEN), "Sync the time in GoldHEN"));
    char want[600];
    snprintf(want, sizeof want, bl_str(BL_S_CLOCK_TLS), bl_str(BL_S_CLOCK_GOLDHEN));
    CHECK(!strcmp(want, CLOCK_TLS));
    bl_clock_now = fixed_clock;
    /* The clock 2 hours off: no download, step 4 stops with the text. */
    online_world();
    char before[100], after[100];
    tree_sig(P.game, before, sizeof before);
    static const uint32_t script[] = { R, X, D, X };
    Fake k; memset(&k, 0, sizeof k);
    k.script = script; k.len = 4; k.max_ticks = 80;
    BlServices s = services(&k);
    s.https_get = fake_https;
    s.http_date = fake_date;
    BlDecision d;
    g_dl_fail = 0; g_dl_calls = 0; g_date_calls = 0; g_srv_offset = 7200;
    CHECK_EQ(run_clean(&s, &d), 0);
    CHECK_EQ(g_date_calls, 1);
    CHECK_EQ(g_dl_calls, 0);
    CHECK_EQ(bl_patch_last_drawn.steps[3], BL_STEP_FAIL);
    CHECK(!strcmp(bl_patch_last_drawn.message_text, CLOCK_TLS));
    tree_sig(P.game, after, sizeof after);
    CHECK(!strcmp(before, after));
    /* A right clock: the download goes ahead. */
    online_world();
    Fake m; memset(&m, 0, sizeof m);
    m.script = script; m.len = 4; m.max_ticks = 200;
    s = services(&m);
    s.https_get = fake_https;
    s.http_date = fake_date;
    g_dl_calls = 0; g_srv_offset = 0;
    CHECK_EQ(run_clean(&s, &d), 0);
    CHECK_EQ(g_dl_calls, 2);
    /* The title's certificate failure (-6): the same text. */
    online_world();
    Fake c; memset(&c, 0, sizeof c);
    c.script = script; c.len = 4; c.max_ticks = 80;
    s = services(&c);
    s.https_get = fake_https;
    g_dl_fail = -6; g_dl_calls = 0;
    CHECK_EQ(run_clean(&s, &d), 0);
    g_dl_fail = 0;
    CHECK_EQ(g_dl_calls, 1);
    CHECK(!strcmp(bl_patch_last_drawn.message_text, CLOCK_TLS));
    bl_clock_now = NULL;
}

static void test_patch_online_download(void)
{
    online_world();
    static const uint32_t script[] = { R, X, D, X, O };
    Fake k; memset(&k, 0, sizeof k);
    k.script = script; k.len = 5; k.max_ticks = 200;
    BlServices s = services(&k);
    s.https_get = fake_https;
    BlDecision d;
    g_dl_fail = 0; g_dl_calls = 0;
    CHECK_EQ(run_clean(&s, &d), 0);
    CHECK_EQ(g_dl_calls, 2);
    CHECK(exe_is12());
    CHECK_EQ(bl_start_last_status.online, BL_ONLINE_READY);
    /* Audit 17: before the first byte of each zip the panel says it is
     * connecting (not "Installing" with no name), with an empty bar: the
     * first zip's line and full bar are gone when the second starts. */
    for (int i = 0; i < 2; i++) {
        char line[200];
        bl_patch_status_line(&g_dl_seen[i], line, sizeof line);
        CHECK(!strcmp(line, "Connecting to amax-emu.com\xE2\x80\xA6"));
        if (strcmp(line, "Connecting to amax-emu.com\xE2\x80\xA6")) fprintf(stderr, "  zip %d: got: %s\n", i + 1, line);
        CHECK_EQ(g_dl_seen[i].done, 0);
        CHECK_EQ(g_dl_seen[i].total, 0);
    }
    char p[900]; snprintf(p, sizeof p, "%s/blur_12.zip.part", P.state);
    CHECK(!bl_fs_exists(p));
    /* a failed download: Help at the online section with the cause; nothing changed */
    online_world();
    char before[100], after[100];
    tree_sig(P.game, before, sizeof before);
    static const uint32_t fail[] = { R, X, D, X };
    Fake m; memset(&m, 0, sizeof m);
    m.script = fail; m.len = 4; m.max_ticks = 80;
    s = services(&m);
    s.https_get = fake_https;
    g_dl_fail = 1;
    CHECK_EQ(run_clean(&s, &d), 0);
    g_dl_fail = 0;
    CHECK(!strcmp(m.screens[m.nscreens - 1], "patch"));
    CHECK_EQ(saw_screen(&m, "help"), 0);
    CHECK_EQ(bl_patch_last_drawn.steps[3], BL_STEP_FAIL);
    char want[1000];
    snprintf(want, sizeof want, "Download of amax_client_files.zip failed. Nothing was changed. Press Patch again, or put the zips in %s.", zip_dir);
    CHECK(!strcmp(bl_patch_last_drawn.message_text, want));
    if (strcmp(bl_patch_last_drawn.message_text, want)) fprintf(stderr, "  got: %s\n", bl_patch_last_drawn.message_text);
    CHECK(bl_patch_last_drawn.help_line);
    tree_sig(P.game, after, sizeof after);
    CHECK(before[0] && !strcmp(before, after));
}

/* Audit 16: an online Patch needs its room plus 1 GB free; with less it stops
 * before any download and says how much to free (whole GB, rounded up). An
 * unknown free space (<0) changes nothing. */
static long long g_free;
static long long fake_free(void *ctx, const char *path) { (void)ctx; (void)path; return g_free; }

static int online_patch_with_free(long long free_b, Fake *k)
{
    online_world();
    static const uint32_t script[] = { R, X, D, X, O };
    memset(k, 0, sizeof *k);
    k->script = script; k->len = 5; k->max_ticks = 200;
    BlServices s = services(k);
    s.https_get = fake_https;
    s.free_bytes = fake_free;
    g_free = free_b;
    g_dl_fail = 0; g_dl_calls = 0;
    BlDecision d;
    return run_clean(&s, &d);
}

static void test_patch_online_free_space(void)
{
    const long long MB = 1024LL * 1024, GB = 1024 * MB;
    Fake k;
    /* enough: downloads and installs */
    CHECK_EQ(online_patch_with_free(3 * GB, &k), 0);
    CHECK_EQ(g_dl_calls, 2);
    CHECK(exe_is12());
    CHECK_EQ(bl_patch_last_drawn.steps[8], BL_STEP_DONE);
    /* unknown: today's behaviour (downloads and installs) */
    CHECK_EQ(online_patch_with_free(-1, &k), 0);
    CHECK_EQ(g_dl_calls, 2);
    CHECK(exe_is12());
    /* not enough: stops at the space step, nothing downloaded or changed */
    char before[100], after[100];
    online_world();
    tree_sig(P.game, before, sizeof before);
    CHECK_EQ(online_patch_with_free(100 * MB, &k), 0);
    CHECK_EQ(g_dl_calls, 0);
    CHECK_EQ(bl_patch_last_drawn.steps[2], BL_STEP_FAIL);
    CHECK(!strcmp(bl_patch_last_drawn.message_text, "Not enough free space on the PS4. Free at least 2 GB, then try again."));
    if (strcmp(bl_patch_last_drawn.message_text, "Not enough free space on the PS4. Free at least 2 GB, then try again."))
        fprintf(stderr, "  got: %s\n", bl_patch_last_drawn.message_text);
    tree_sig(P.game, after, sizeof after);
    CHECK(before[0] && !strcmp(before, after));
    /* just under room + 1 GB: free at least 1 GB */
    CHECK_EQ(online_patch_with_free(GB + 100 * MB, &k), 0);
    CHECK_EQ(g_dl_calls, 0);
    CHECK(!strcmp(bl_patch_last_drawn.message_text, "Not enough free space on the PS4. Free at least 1 GB, then try again."));
    g_free = 0;
}

/* Review blocker 1 through the screens: an online Patch stops after the
 * player's gamedata.pak was parked (the new one cannot be written). Start is
 * not Ready; the next Patch (offline) gives the player's files back byte for
 * byte. */
static char g_block[900];
static int block_rename(const char *from, const char *to)
{
    if (!strcmp(to, g_block)) { errno = EIO; return -1; }
    return rename(from, to);
}

static void test_patch_online_interrupted(void)
{
    online_world();
    make_exe(P.game, 0);
    put(P.game, "cache/gamedata.pak", "the player's own gamedata.pak");
    char exe_before[65], pak_before[65], p[900], h[65];
    snprintf(p, sizeof p, "%s/Blur.exe", P.game); bl_fs_sha256(p, exe_before, NULL);
    snprintf(p, sizeof p, "%s/cache/gamedata.pak", P.game); bl_fs_sha256(p, pak_before, NULL);
    place_zip(amax_zip, "amax_client_files.zip");
    place_zip(blur_zip, "blur_12.zip");
    snprintf(g_block, sizeof g_block, "%s/cache/gamedata.pak", P.game);
    bl_fs_rename = block_rename;
    static const uint32_t online[] = { R, X, D, X, O };
    Fake k; BlDecision d;
    CHECK_EQ(run_script(online, 5, 200, &d, &k), 0);
    bl_fs_rename = rename;
    CHECK_EQ(bl_patch_last_drawn.steps[4], BL_STEP_FAIL);
    CHECK(!exists(P.game, "cache/gamedata.pak"));        /* parked: the folder is incomplete */
    CHECK(bl_start_last_status.readiness != BL_READY_OK);
    CHECK_EQ(bl_start_last_status.online, BL_ONLINE_PARTIAL);
    /* Play does not start Blur */
    static const uint32_t play[] = { X, O };
    CHECK_EQ(run_script(play, 2, 40, &d, &k), 0);
    CHECK(d.action != BL_START_GAME);
    /* the next Patch, offline: everything back */
    static const uint32_t offline[] = { R, X, U, X, O };
    CHECK_EQ(run_script(offline, 5, 200, &d, &k), 0);
    CHECK_EQ(bl_patch_last_drawn.steps[8], BL_STEP_DONE);
    snprintf(p, sizeof p, "%s/Blur.exe", P.game); bl_fs_sha256(p, h, NULL); CHECK(!strcmp(h, exe_before));
    snprintf(p, sizeof p, "%s/cache/gamedata.pak", P.game); bl_fs_sha256(p, h, NULL); CHECK(!strcmp(h, pak_before));
    CHECK(!strcmp(slurp(P.game, "d3d9.dll"), V7));
    CHECK_EQ(bl_start_last_status.readiness, BL_READY_OK);
    CHECK_EQ(bl_start_last_status.online, BL_ONLINE_NONE);
}

/* Spec 5: a failure is one plain sentence with the next action; codes and
 * errno stay in events.log. */
static int g_enospc_at, g_writes_seen;
static long enospc_write(int fd, const void *buf, size_t n)
{
    if (++g_writes_seen == g_enospc_at) { errno = ENOSPC; return -1; }
    return bl_fs_write_default(fd, buf, n);
}

static void test_patch_failure_is_one_sentence(void)
{
    online_world();
    g_writes_seen = 0; g_enospc_at = 3;
    bl_fs_write = enospc_write;
    static const uint32_t offline[] = { R, X, X };
    Fake k; BlDecision d;
    CHECK_EQ(run_script(offline, 3, 80, &d, &k), 0);
    bl_fs_write = bl_fs_write_default;
    const char *m = bl_patch_last_drawn.message_text;
    CHECK(!strcmp(m, "Not enough free space on the PS4. Free some space, then press Patch again."));
    if (strcmp(m, "Not enough free space on the PS4. Free some space, then press Patch again.")) fprintf(stderr, "  got: %s\n", m);
    CHECK(strstr(m, "errno") == NULL && strstr(m, "(E") == NULL);
    CHECK(strstr(slurp(P.state, BL_EVENTS_FILE), "errno=28") != NULL);
    /* any other failure: the file and what to do, still one sentence */
    online_world();
    g_writes_seen = 0; g_enospc_at = 3;
    bl_fs_rename = block_rename;
    snprintf(g_block, sizeof g_block, "%s/d3d9.dll", P.game);
    CHECK_EQ(run_script(offline, 3, 80, &d, &k), 0);
    bl_fs_rename = rename;
    m = bl_patch_last_drawn.message_text;
    CHECK(!strcmp(m, "d3d9.dll could not be written: press Patch again, and save a support bundle in Settings if it keeps failing."));
    if (strstr(m, "errno")) fprintf(stderr, "  got: %s\n", m);
    CHECK(strstr(m, "errno") == NULL && strstr(m, "(E") == NULL);
}

/* Audit 21: more other files than Patch can park (4096): it stops before any
 * change, names the folder, and says what to do. */
static void test_patch_too_many_other_files(void)
{
    online_world();
    char cmd[1200], before[100], after[100], want[1000];
    snprintf(cmd, sizeof cmd, "mkdir -p '%s/amax/mods' && cd '%s/amax/mods' && seq -f 'm%%04g.txt' 1 %d | xargs touch",
             P.game, P.game, BL_PATCH_MAX_PARK + 1);
    sh(cmd);
    tree_sig(P.game, before, sizeof before);
    static const uint32_t offline[] = { R, X, X };
    Fake k; BlDecision d;
    CHECK_EQ(run_script(offline, 3, 80, &d, &k), 0);
    snprintf(want, sizeof want, "Too many other files in %s/amax/mods for Patch (more than 4096). Nothing was changed. "
                                "Move some out, then try again.", P.game);
    CHECK(!strcmp(bl_patch_last_drawn.message_text, want));
    if (strcmp(bl_patch_last_drawn.message_text, want)) fprintf(stderr, "  got: %s\n", bl_patch_last_drawn.message_text);
    CHECK_EQ(bl_patch_last_drawn.steps[4], BL_STEP_FAIL);
    tree_sig(P.game, after, sizeof after);
    CHECK(before[0] && !strcmp(before, after));
    /* Audit 22: a file Patch cannot read: the same, with the file named. */
    online_world();
    put(P.game, "amax/mods/locked.txt", "a file Patch cannot read");
    char locked[900]; snprintf(locked, sizeof locked, "%s/amax/mods/locked.txt", P.game);
    chmod(locked, 0);
    CHECK_EQ(run_script(offline, 3, 80, &d, &k), 0);
    chmod(locked, 0644);
    CHECK(!strcmp(bl_patch_last_drawn.message_text, "Patch could not read amax/mods/locked.txt. Nothing was changed. Copy your Blur "
                                                    "folder from your PC again (or remove that file), then try again."));
    if (!strstr(bl_patch_last_drawn.message_text, "locked")) fprintf(stderr, "  got: %s\n", bl_patch_last_drawn.message_text);
    CHECK(!strcmp(slurp(P.game, "amax/mods/locked.txt"), "a file Patch cannot read"));
    CHECK(!exists(P.game, "d3d9_blur_dxvk.dll"));
}

/* Game + one zip: the exact cause and the next action; nothing changed. */
static void test_patch_online_one_zip(void)
{
    online_world();
    place_zip(amax_zip, "amax_client_files.zip");
    char before[100], after[100], want[1000];
    tree_sig(P.game, before, sizeof before);
    static const uint32_t script[] = { R, X, D, X };
    Fake k; BlDecision d;
    CHECK_EQ(run_script(script, 4, 80, &d, &k), 0);
    /* not a failure: Help at the online section, the cause on top */
    CHECK(!strcmp(k.screens[k.nscreens - 1], "help"));
    CHECK_EQ(bl_help_last_drawn.scroll, bl_help_section(BL_HELP_ONLINE));
    for (int i = 0; i < BL_PATCH_STEPS; i++) CHECK(bl_patch_last_drawn.steps[i] != BL_STEP_FAIL);
    snprintf(want, sizeof want, "Only amax_client_files.zip is in %s. Nothing was changed. Put blur_12.zip there too, then press Patch for online play.", zip_dir);
    CHECK(!strcmp(bl_help_last_drawn.note, want));
    if (strcmp(bl_help_last_drawn.note, want)) fprintf(stderr, "  got: %s\n", bl_help_last_drawn.note);
    tree_sig(P.game, after, sizeof after);
    CHECK(before[0] && !strcmp(before, after));
}

/* A bad zip: named, with the next action; nothing changed. */
static void test_patch_online_bad_zip(void)
{
    online_world();
    place_zip(amax_zip, "amax_client_files.zip");
    put(zip_dir, "Blur_12.ZIP", "PK a damaged download");
    char before[100], after[100];
    tree_sig(P.game, before, sizeof before);
    static const uint32_t script[] = { R, X, D, X };
    Fake k; BlDecision d;
    CHECK_EQ(run_script(script, 4, 80, &d, &k), 0);
    CHECK(!strcmp(k.screens[k.nscreens - 1], "help"));
    char want[1000];
    snprintf(want, sizeof want, "blur_12.zip in %s is damaged. Nothing was changed. Download it again from amax-emu.com, then press Patch again.", zip_dir);
    CHECK(!strcmp(bl_help_last_drawn.note, want));
    if (strcmp(bl_help_last_drawn.note, want)) fprintf(stderr, "  got: %s\n", bl_help_last_drawn.note);
    tree_sig(P.game, after, sizeof after);
    CHECK(before[0] && !strcmp(before, after));
}

/* Game + both zips: online patch, then online -> offline -> online again; the
 * player's own Blur.exe and gamedata.pak come back byte for byte. */
static void test_patch_round_trip(void)
{
    online_world();
    make_exe(P.game, 0);                               /* the player's own (untested) exe */
    put(P.game, "cache/gamedata.pak", "the player's own gamedata.pak");
    char exe_before[65], pak_before[65], p[900], h[65];
    snprintf(p, sizeof p, "%s/Blur.exe", P.game); bl_fs_sha256(p, exe_before, NULL);
    snprintf(p, sizeof p, "%s/cache/gamedata.pak", P.game); bl_fs_sha256(p, pak_before, NULL);
    place_zip(amax_zip, "amax_client_files.zip");
    place_zip(blur_zip, "blur_12.zip");
    /* 1. offline first */
    static const uint32_t offline[] = { R, X, X, O };
    Fake k; BlDecision d;
    CHECK_EQ(run_script(offline, 4, 80, &d, &k), 0);
    CHECK(!strcmp(slurp(P.game, "d3d9.dll"), V7));
    /* 2. online: the tested exe (the test hook pins the fake one) */
    bl_exe_expected_sha = exe12_sha;
    static const uint32_t online[] = { R, X, D, X, O, L, X };
    CHECK_EQ(run_script(online, 7, 200, &d, &k), 0);
    CHECK_EQ(bl_patch_last_drawn.steps[8], BL_STEP_DONE);
    CHECK_EQ(bl_patch_last_drawn.message, BL_S_PATCH_DONE_ONLINE);
    CHECK_EQ(bl_dialog_last_drawn.body, BL_S_PATCH_DONE_ASK_ONLINE);     /* finding 38: Circle on it went to Start */
    CHECK(exe_is12());
    CHECK(!strcmp(slurp(P.game, "cache/gamedata.pak"), PAK12));
    CHECK(!strcmp(slurp(P.game, "d3d9.dll"), PROXY));
    CHECK(!strcmp(slurp(P.game, "amax/dxvk.dll"), V7));
    CHECK(!strcmp(slurp(P.game, "amax/dlls/amax_auth.asi"), AUTH));
    CHECK_EQ(bl_start_last_status.online, BL_ONLINE_READY);
    CHECK(!strcmp(bl_str(BL_S_STATUS_READY_ONLINE), "Ready \xE2\x80\x94 online and offline"));
    CHECK_EQ(d.action, BL_START_GAME);                  /* Play starts, no exe warning: the tested exe */
    CHECK(strstr(slurp(P.state, BL_EVENTS_FILE), "play: Blur.exe is not the tested") == NULL);
    snprintf(p, sizeof p, "%s/%s", P.state, BL_ONLINE_STAGE);
    CHECK(!bl_fs_exists(p));                             /* the stage is gone after Patch */
    /* 3. back to offline: the default choice is online now, so Up first */
    static const uint32_t back[] = { R, X, U, X, O };
    CHECK_EQ(run_script(back, 5, 120, &d, &k), 0);
    CHECK_EQ(bl_patch_last_drawn.steps[8], BL_STEP_DONE);
    snprintf(p, sizeof p, "%s/Blur.exe", P.game); bl_fs_sha256(p, h, NULL); CHECK(!strcmp(h, exe_before));
    snprintf(p, sizeof p, "%s/cache/gamedata.pak", P.game); bl_fs_sha256(p, h, NULL); CHECK(!strcmp(h, pak_before));
    CHECK(!strcmp(slurp(P.game, "d3d9.dll"), V7));
    CHECK(!exists(P.game, "amax/dxvk.dll") && !exists(P.game, "amax/dlls/amax_auth.asi"));
    CHECK(!exists(P.game, "cache/gamedata.sig"));
    CHECK_EQ(bl_start_last_status.online, BL_ONLINE_NONE);
    CHECK_EQ(bl_start_last_status.readiness, BL_READY_OK);
    /* 4. and online again */
    static const uint32_t again[] = { R, X, D, X, O };
    CHECK_EQ(run_script(again, 5, 200, &d, &k), 0);
    CHECK(exe_is12());
    CHECK_EQ(bl_start_last_status.online, BL_ONLINE_READY);
}

/* Online files damaged later: the status says so and where to look. */
static void test_status_online_missing(void)
{
    online_world();
    place_zip(amax_zip, "amax_client_files.zip");
    place_zip(blur_zip, "blur_12.zip");
    static const uint32_t online[] = { R, X, D, X, O };
    Fake k; BlDecision d;
    CHECK_EQ(run_script(online, 5, 200, &d, &k), 0);
    char p[900]; snprintf(p, sizeof p, "%s/amax/dlls/amax_auth.asi", P.game);
    unlink(p);
    CHECK_EQ(run_script(NULL, 0, 5, &d, &k), 0);
    CHECK_EQ(bl_start_last_status.online, BL_ONLINE_PARTIAL);
    CHECK_EQ(bl_start_last_status.readiness, BL_READY_UNPATCHED);
    CHECK(!strcmp(bl_str(BL_S_STATUS_ONLINE_MISSING), "Online files missing \xE2\x80\x94 see Help"));
}

/* Help: from Start (Square), scrolls, Circle goes back. */
static void test_help_from_start(void)
{
    setup(1);
    static const uint32_t script[] = { BL_PAD_SQUARE, D, D, O };
    Fake k; BlDecision d;
    CHECK_EQ(run_script(script, 4, 30, &d, &k), 0);
    CHECK_EQ(saw_screen(&k, "help"), 1);
    CHECK(!strcmp(k.screens[k.nscreens - 1], "start"));
    CHECK_EQ(bl_help_last_drawn.scroll, 2);
    BlHelpState h;
    bl_help_init(&h, BL_HELP_TROUBLE);
    CHECK_EQ(h.scroll, bl_help_section(BL_HELP_TROUBLE));
    for (int i = 0; i < 100; i++) bl_help_input(&h, D);
    CHECK(h.scroll < bl_help_count());
    CHECK_EQ(bl_help_input(&h, O), 1);
}

static void test_corrupt_ini_is_reported(void)
{
    setup(1);
    put(P.state, "launcher.ini", "[overlay]\nstats = sideways\n");
    Fake k; memset(&k, 0, sizeof k);
    k.max_ticks = 5;
    BlServices s = services(&k);
    BlDecision d;
    bl_start_last_band = -1;
    CHECK_EQ(run_clean(&s, &d), 0);
    CHECK_EQ(bl_start_last_band, BL_S_SETTINGS_WARNING);
}

static void test_caches_released(void)
{
    setup(1);
    Fake k; memset(&k, 0, sizeof k);
    k.max_ticks = 3;
    BlServices s = services(&k);
    BlDecision d;
    CHECK_EQ(run_clean(&s, &d), 0);
    CHECK_EQ(bl_screens_cache_bytes(), 0);
}

static void test_no_data_root_says_not_saved(void)
{
    setup(1);
    static const uint32_t script[] = { L, X, O };
    Fake k; memset(&k, 0, sizeof k);
    k.script = script; k.len = 3; k.max_ticks = 40;
    BlServices s = services(&k);
    s.data_root = NULL;
    BlDecision d;
    CHECK_EQ(run_clean(&s, &d), 0);
    CHECK_EQ(bl_start_last_band, BL_S_SETTINGS_SAVE_FAILED);
}

/* The account page through the frontend with the launcher's own keyboard
 * (owner finding 12: the system keyboard opened top left and could not be
 * used). The system keyboard service is offered but never called. */
enum { OPT = BL_PAD_OPTIONS, R2 = 0x200u };
static void test_account_page_saves(void)
{
    setup(1);
    bl_account_clear(&P);
    static const uint32_t script[] = {
        L, X, D, D, D, X,           /* settings > Online account, into the panel */
        X, D, X, D, X, OPT,         /* user name: keyboard, "q" "a", Options = OK */
        D, X, X, R2,                /* password: keyboard, "1", R2 = OK */
        D, X, O, O                  /* Save, back, back */
    };
    Fake k; memset(&k, 0, sizeof k);
    k.script = script; k.len = (int)(sizeof script / sizeof script[0]); k.max_ticks = 120;
    BlServices s = services(&k);
    BlDecision d;
    CHECK_EQ(run_clean(&s, &d), 0);
    CHECK_EQ(k.osk_calls, 0);
    BlAccount a;
    CHECK_EQ(bl_account_load(&P, &a), 0);
    CHECK(!strcmp(a.user, "qa") && !strcmp(a.pass, "1"));
    if (strcmp(a.user, "qa") || strcmp(a.pass, "1")) fprintf(stderr, "  got user '%s'\n", a.user);
    CHECK_EQ(saw_screen(&k, "keyboard"), 2);
    /* Circle cancels: the name stays as it was. */
    static const uint32_t cancel[] = { L, X, D, D, D, X, X, D, X, O, D, D, X, O, O };
    Fake c; memset(&c, 0, sizeof c);
    c.script = cancel; c.len = (int)(sizeof cancel / sizeof cancel[0]); c.max_ticks = 120;
    s = services(&c);
    CHECK_EQ(run_clean(&s, &d), 0);
    CHECK_EQ(bl_account_load(&P, &a), 0);
    CHECK(!strcmp(a.user, "qa") && !strcmp(a.pass, "1"));
    CHECK_EQ(c.osk_calls, 0);
    /* The account item is listed without a system keyboard service too. */
    static const uint32_t to_account[] = { L, X, D, D, D };
    Fake n; memset(&n, 0, sizeof n);
    n.script = to_account; n.len = 5; n.max_ticks = 20;
    s = services(&n);
    s.osk = NULL;
    /* Audit 29a: the Start idle prefetch and Settings show the user name
     * without loading the password; opening the account panel loads it. */
    int loads = bl_account_load_count;
    CHECK_EQ(run_clean(&s, &d), 0);
    CHECK_EQ(bl_settings_last_drawn.item, BL_SET_ACCOUNT);
    CHECK(bl_settings_listed(&bl_settings_last_drawn, BL_SET_ACCOUNT));
    CHECK(!strcmp(bl_settings_last_drawn.account_user, "qa") && bl_settings_last_drawn.account_pass_set);
    CHECK_EQ(bl_account_load_count, loads);
    static const uint32_t open_account[] = { L, X, D, D, D, X, D, D, X };   /* into the panel, Save */
    memset(&n, 0, sizeof n);
    n.script = open_account; n.len = 9; n.max_ticks = 40;
    s = services(&n);
    CHECK_EQ(run_clean(&s, &d), 0);
    CHECK_EQ(bl_account_load_count, loads + 1);
    BlAccount kept;
    CHECK_EQ(bl_account_load(&P, &kept), 0);                    /* Save kept the loaded password */
    CHECK(!strcmp(kept.user, "qa") && !strcmp(kept.pass, "1"));
}

static void test_note_long_line(void)
{
    BlFrame f = { malloc(1920u * 1080u * 4u), 1920, 1080, 1920 * 4 };
    static char text[1600];
    for (int i = 0; i < 1590; i += 3) memcpy(text + i, "ab ", 3);
    text[1590] = 0;
    int lh = bl_font_line_height(BL_FONT_NOTE);
    int y = bl_note(&f, 0, 0, 1 << 30, text, BL_RGB_WHITE, BL_FONT_NOTE);
    CHECK(y >= 2 * lh && y <= 5 * lh);
    free(f.pixels);
}

static void test_logs_uncapped_and_live_log_kept(void)
{
    setup(1);
    char p[600];
    for (int i = 0; i < 100; i++) { char n[64]; snprintf(n, sizeof n, "session-%03d.log", i); put(P.logs, n, "line\n"); }
    long long bytes = 0;
    CHECK_EQ(bl_logs_summary(P.game, blur_root, NULL, &bytes), 100);
    CHECK_EQ(bytes, 500);
    CHECK_EQ(bl_logs_clear(P.game, blur_root, "session-042.log"), 99);
    snprintf(p, sizeof p, "%s/session-042.log", P.logs);
    CHECK(access(p, F_OK) == 0);
    CHECK_EQ(bl_logs_summary(P.game, blur_root, "session-042.log", &bytes), 0);
}

static void test_golden_screens(void)
{
    BlFrame f = { malloc(1920u * 1080u * 4u), 1920, 1080, 1920 * 4 };
    /* Patch: halfway, the AMAX step running. */
    BlPatchView v; memset(&v, 0, sizeof v);
    v.message = -1;
    for (int i = 0; i < 3; i++) v.steps[i] = BL_STEP_DONE;
    v.steps[3] = BL_STEP_RUN;
    v.step = 3; v.busy = 1; v.done = 2; v.total = 5;
    snprintf(v.progress_name, sizeof v.progress_name, "amax/dlls/amax_auth.asi");
    v.exe_verdict = BL_EXE_TESTED;
    bl_patch_draw(&f, &v);
    golden("patch", &f);
    /* Patch failed at a later step; another Blur.exe is an amber line, never
     * a failed step (owner findings 21 and 28). */
    memset(&v, 0, sizeof v);
    v.message = -1; v.exe_verdict = BL_EXE_UNSUPPORTED;
    for (int i = 0; i < 4; i++) v.steps[i] = BL_STEP_DONE;
    v.steps[4] = BL_STEP_FAIL; v.step = 4;
    snprintf(v.message_text, sizeof v.message_text, bl_str(BL_S_PATCH_FILE_FAILED), "d3d9.dll");
    bl_patch_draw(&f, &v);
    golden("patch-failed", &f);
    /* Patch: the two choices, online focused. */
    memset(&v, 0, sizeof v);
    v.message = -1; v.choice = 1; v.exe_verdict = BL_EXE_TESTED;
    bl_patch_draw(&f, &v);
    golden("patch-choice", &f);
    /* Patch for online play without network and zips (owner finding 31):
     * step 4 amber with the cause, then the Help line. */
    memset(&v, 0, sizeof v);
    v.message = -1; v.choice = 1; v.exe_verdict = BL_EXE_TESTED;
    for (int i = 0; i < 3; i++) v.steps[i] = BL_STEP_DONE;
    v.steps[3] = BL_STEP_FAIL; v.step = 3; v.help_line = 1;
    snprintf(v.message_text, sizeof v.message_text, bl_str(BL_S_ONLINE_NO_NETWORK), "/data/JkFlow");
    bl_patch_draw(&f, &v);
    golden("patch-online-no-network", &f);
    /* Audit 8: an interrupted download, the key caps where {cross} and {square} stand. */
    memset(&v, 0, sizeof v);
    v.message = -1; v.choice = 1; v.exe_verdict = BL_EXE_TESTED;
    for (int i = 0; i < 3; i++) v.steps[i] = BL_STEP_DONE;
    v.steps[3] = BL_STEP_FAIL; v.step = 3; v.help_line = 1;
    snprintf(v.message_text, sizeof v.message_text, "%s", bl_str(BL_S_DL_INTERRUPTED));
    bl_patch_draw(&f, &v);
    golden("patch-interrupted", &f);
    /* Help: from the top, and opened at the online section. */
    BlHelpState hs;
    bl_help_init(&hs, BL_HELP_BEFORE);
    bl_help_draw(&f, &hs);
    golden("help", &f);
    bl_help_init(&hs, BL_HELP_ONLINE);
    bl_help_draw(&f, &hs);
    golden("help-online", &f);
    bl_help_init(&hs, BL_HELP_TROUBLE);
    bl_help_draw(&f, &hs);
    golden("help-trouble", &f);

    BlSettingsState st;
    bl_settings_init(&st, NULL);
    st.item = BL_SET_STATS; st.cfg.stats = BL_STATS_FPS; st.in_panel = 1;
    bl_settings_draw(&f, &st);
    golden("settings-stats", &f);
    st.item = BL_SET_LOGGING; st.cfg.logging = 1; st.log_count = 3; st.log_bytes = 3 << 20;
    bl_settings_draw(&f, &st);
    golden("settings-logs", &f);
    st.item = BL_SET_ACCOUNT; st.in_panel = 1; st.row = 1;
    snprintf(st.account_user, sizeof st.account_user, "XxXJoker13XxX");
    st.account_pass_set = 1;
    bl_settings_draw(&f, &st);
    golden("settings-account", &f);
    st.item = BL_SET_SUPPORT; st.in_panel = 0;
    bl_settings_draw(&f, &st);
    golden("settings-support", &f);
    /* Frame rate: 30 (the default), 60 with its help, and a Blur.exe that is
     * not verified: 60 greyed, the reason under it (owner 2026-11-16). */
    bl_settings_init(&st, NULL);
    st.in_panel = 1;
    bl_settings_draw(&f, &st);
    golden("settings-framerate-30", &f);
    st.cfg.frame_rate = BL_FPS_60;
    bl_settings_draw(&f, &st);
    golden("settings-framerate-60", &f);
    st.exe_untested = 1; st.message = BL_S_FRAMERATE_UNTESTED;
    bl_settings_draw(&f, &st);
    golden("settings-framerate-untested", &f);

    /* The Play dialog over Start. */
    BlDialog dg;
    bl_dialog_init(&dg, BL_S_PLAY_NO_ACCOUNT, BL_S_DIALOG_PLAY, BL_S_DIALOG_BACK, 2);
    BlStartState ss; bl_start_init(&ss);
    BlStatus status; memset(&status, 0, sizeof status); status.readiness = BL_READY_OK;
    bl_start_draw(&f, &ss, &status);
    bl_dialog_draw(&f, &dg);
    golden("dialog", &f);
    /* Owner finding 38: Patch done, one button over the finished Patch screen. */
    {
        BlPatchView pv; memset(&pv, 0, sizeof pv);
        for (int i = 0; i < 9; i++) pv.steps[i] = BL_STEP_DONE;
        pv.step = 8; pv.message = BL_S_PATCH_ALL_DONE; pv.exe_verdict = BL_EXE_TESTED;
        BlDialog one;
        bl_dialog_init(&one, BL_S_PATCH_DONE_ASK, BL_S_DIALOG_BACK_TO_START, -1, 1);
        bl_patch_draw(&f, &pv);
        bl_dialog_draw(&f, &one);
        golden("dialog-patch-done", &f);
        CHECK_EQ(bl_dialog_input(&one, R), 0);
        CHECK_EQ(one.focus, 0);                                 /* no second button to go to */
        CHECK_EQ(bl_dialog_input(&one, X), 1);
        CHECK_EQ(bl_dialog_input(&one, O), 1);
    }
    /* Audit 5: no game found, the band points to Help (Square key cap). */
    bl_start_init(&ss);
    ss.focus = ss.target = BL_CARD_PATCH; ss.pos = ss.from = (float)BL_CARD_PATCH;
    ss.band = BL_S_GAME_NOT_FOUND;
    status.readiness = BL_READY_NO_GAME;
    bl_start_draw(&f, &ss, &status);
    golden("start-not-found", &f);
    bl_help_init(&hs, BL_HELP_FOLDER);
    bl_help_draw(&f, &hs);
    golden("help-folder", &f);
    CHECK_EQ(bl_dialog_input(&dg, R), 0);
    CHECK_EQ(dg.focus, 1);
    CHECK_EQ(bl_dialog_input(&dg, X), 2);
    CHECK_EQ(bl_dialog_input(&dg, O), 2);                       /* Circle: the given choice */
    free(f.pixels);
}

static void help_text_of(const BlHelpState *st, BlStringId id, char *out, size_t cap);

static char *read_text(const char *path)
{
    static char buf[16384];
    buf[0] = 0;
    FILE *f = fopen(path, "rb");
    if (f) { size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f); }
    return buf;
}

/* Owner finding 32 (2026-11-15): no About screen; credits only in the
 * GitHub README; one legal line at the end of Help's "Before you start";
 * the licence texts stay in the package. */
/* Audit 22: every message that stops Patch, Restore or Play says what
 * happened, "Nothing was changed." when that is true, and one concrete next
 * step (or Help); at most three short sentences besides "Nothing was changed.". */
static int has_next_step(const char *s)
{
    static const char *const STEP[] = { "press", "Press", "Put ", "put ", "Download", "Copy", "Free", "free some", "Reinstall",
                                        "Turn off", "Keep only", "Check", "Move", "Connect", "try again", "Try again",
                                        "Restart", "start JkFlow again", "start again", "Sync", "Place", "Help" };
    for (size_t i = 0; i < sizeof STEP / sizeof STEP[0]; i++) if (strstr(s, STEP[i])) return 1;
    return 0;
}

static int sentences(const char *s)
{
    int n = 0;
    for (const char *p = s; *p; p++) if (*p == '.' && (p[1] == ' ' || p[1] == 0)) n++;
    if (strstr(s, "Nothing was changed.")) n--;
    return n;
}

/* Audit 24: the smaller text items. */
static void test_audit24_texts(void)
{
    /* (1) the folder on disk, not a fixed "Blur" */
    BlPatchView v; memset(&v, 0, sizeof v);
    v.message = -1; v.exe_verdict = BL_EXE_MISSING;
    snprintf(v.game_dir, sizeof v.game_dir, "/data/JkFlow/BLUR");
    BlFrame f = { malloc(1920u * 1080u * 4u), 1920, 1080, 1920 * 4 };
    bl_patch_draw(&f, &v);
    char t[300];
    snprintf(t, sizeof t, bl_str(BL_S_PATCH_EXE_NONE), bl_patch_last_drawn.game_dir);
    CHECK(!strcmp(t, "No Blur.exe in /data/JkFlow/BLUR yet"));
    free(f.pixels);
    /* (2) the same word as the band */
    CHECK(strstr(bl_str(BL_S_PLAY_UPDATE), "installed") && strstr(bl_str(BL_S_GAME_UNPATCHED), "installed"));
    /* (6) the in-game close, in Help's "If something goes wrong" */
    int close_at = -1;
    for (int i = 0; i < bl_help_count(); i++) if (bl_help_para_id(i) == BL_S_HELP_E_CLOSE) close_at = i;
    CHECK(close_at > bl_help_section(BL_HELP_TROUBLE));
    CHECK(!strcmp(bl_str(BL_S_HELP_E_CLOSE), "To close Blur, quit from Blur's own main menu; you come back to JkFlow."));
    /* (8)/(10) no unused strings left: no Compatible, no "test files" */
    for (int i = 0; i < BL_S_COUNT; i++) {
        const char *x = bl_str((BlStringId)i);
        CHECK(strstr(x, "Compatible") == NULL && strstr(x, "test files") == NULL && strstr(x, "Native (recommended)") == NULL);
    }
}

static void test_stop_messages(void)
{
    static const struct { BlStringId id; int unchanged; } STOP[] = {
        { BL_S_GAME_MISSING, 1 }, { BL_S_GAME_SEVERAL_FMT, 0 }, { BL_S_GAME_NOT_FOUND, 0 }, { BL_S_GAME_UNPATCHED, 0 },
        { BL_S_PATCH_PACKAGE_BROKEN, 1 }, { BL_S_PATCH_NO_SPACE, 0 }, { BL_S_PATCH_NO_SPACE_BEFORE, 1 },
        { BL_S_PATCH_NO_SPACE_GB, 0 }, { BL_S_PATCH_TOO_MANY, 1 }, { BL_S_PATCH_UNREADABLE, 1 },
        { BL_S_PATCH_FILE_FAILED, 0 }, { BL_S_PATCH_STILL_WRONG, 0 }, { BL_S_PATCH_IMPORT_FAILED, 0 },
        { BL_S_PATCH_RESTORE_FAILED, 0 },
        { BL_S_ONLINE_NO_ZIPS, 1 }, { BL_S_ONLINE_ONLY_ONE, 1 }, { BL_S_ONLINE_ZIP_BAD, 1 }, { BL_S_ONLINE_DOWNLOAD_FAILED, 1 },
        { BL_S_ONLINE_UNPACK_FAILED, 1 }, { BL_S_ONLINE_MEMBER_BAD, 1 }, { BL_S_ONLINE_NO_NETWORK, 1 },
        { BL_S_ONLINE_ZIP_NEWER, 1 }, { BL_S_ONLINE_UNPACK_MEMORY, 1 }, { BL_S_DL_NO_CONNECTION, 1 },
        { BL_S_DL_INTERRUPTED, 1 }, { BL_S_DL_SERVER_ERROR, 1 }, { BL_S_DL_MISMATCH, 1 }, { BL_S_CLOCK_TLS, 1 },
        { BL_S_NATIVE_PREPARE_FAILED, 0 }, { BL_S_PLAY_FILES_BROKEN, 0 }, { BL_S_TOO_MANY_SETTINGS, 0 },
        { BL_S_MIGRATE_FAILED, 0 }, { BL_S_ERROR_BODY, 0 },
    };
    for (size_t i = 0; i < sizeof STOP / sizeof STOP[0]; i++) {
        const char *t = bl_str(STOP[i].id);
        int ok = has_next_step(t) && sentences(t) >= 1 && sentences(t) <= 3
                 && (!STOP[i].unchanged || strstr(t, "Nothing was changed.") != NULL)
                 && (strstr(t, "Reinstall") == NULL || STOP[i].id == BL_S_PATCH_PACKAGE_BROKEN || STOP[i].id == BL_S_PLAY_FILES_BROKEN);
        CHECK(ok);
        if (!ok) fprintf(stderr, "  stop message %d: %s\n", (int)STOP[i].id, t);
    }
    /* Play no longer says "reinstall" when the profile or the env failed (finding 24). */
    CHECK(strstr(bl_str(BL_S_PATCH_PACKAGE_BROKEN), "launcher") == NULL);
}

static void test_no_about_legal_line(void)
{
    BlHelpState h;
    bl_help_init(&h, BL_HELP_BEFORE);
    char t[600];
    help_text_of(&h, BL_S_HELP_0_LEGAL, t, sizeof t);
    CHECK(!strcmp(t, "Unofficial fan project. Licences and source: github.com/Salru13/JkFlow. JkFlow is not affiliated with Activision "
                     "or Sony. Blur is a trademark of Activision; PS4 is a trademark of Sony Interactive Entertainment."));
    int legal = -1, data = -1;
    for (int i = 0; i < bl_help_count(); i++) {
        if (bl_help_para_id(i) == BL_S_HELP_0_LEGAL) legal = i;
        if (bl_help_para_id(i) == BL_S_HELP_0_DATA) data = i;
    }
    CHECK(legal >= 0 && legal == data + 1 && legal + 1 == bl_help_section(BL_HELP_FOLDER));   /* the section's last line */
    for (int i = 0; i < BL_S_COUNT; i++) {
        const char *x = bl_str((BlStringId)i);
        CHECK(strstr(x, "plastikman") == NULL && strstr(x, "orbis-win") == NULL && strstr(x, "Thanks to") == NULL);
        CHECK(strstr(x, "see about") == NULL);
    }
    /* The licence texts stay files; the native renderer is listed; the
     * About list file is gone (nothing reads it). */
    const char *tp = read_text(BL_TEST_DIR "/../../THIRD_PARTY.md");
    CHECK(strstr(tp, "DXVK 1.10.3, the base of our native renderer (`dxvknative.prx`) and its client (`d3d9.c988.dll`) | ") != NULL);
    CHECK(strstr(tp, "| zlib/libpng |") != NULL);
    CHECK(strstr(tp, "Thanks to plastikman for orbis-win and his PS4 Wine port (github.com/plastikman).") != NULL);
    CHECK(access(BL_TEST_DIR "/../assets/blur-files/about_components.txt", F_OK) != 0);
}

/* Owner finding 33: the badge never overlaps the JkFlow logo, nor the logo
 * the status bar, on any screen (they share bl_top_bar). */
static void ink_columns(BlTopPart part, int *x0, int *x1)
{
    BlFrame f = { calloc(1920u * 1080u, 4), 1920, 1080, 1920 * 4 };
    bl_top_bar_part(&f, part, BL_ICON_GEAR);
    *x0 = 1920; *x1 = -1;
    for (int y = 0; y < 1080; y++)
        for (int x = 0; x < 1920; x++) {
            uint32_t p;
            memcpy(&p, f.pixels + (size_t)y * f.stride + (size_t)x * 4, 4);
            if (p & 0xFFFFFF) { if (x < *x0) *x0 = x; if (x > *x1) *x1 = x; }
        }
    free(f.pixels);
}

static void test_top_bar_no_overlap(void)
{
    int b0, b1, l0, l1, r0, r1;
    ink_columns(BL_TOP_BADGE, &b0, &b1);                       /* disc, its glow and the pictogram */
    ink_columns(BL_TOP_LOGO, &l0, &l1);
    ink_columns(BL_TOP_BAR, &r0, &r1);                         /* the thin bar and its glow */
    fprintf(stderr, "  top bar columns: badge %d-%d, logo %d-%d, bar %d-%d\n", b0, b1, l0, l1, r0, r1);
    CHECK(b0 <= b1 && l0 <= l1 && r0 <= r1);
    CHECK(b1 < l0);
    CHECK(l1 < r0);
}

/* Owner finding 6 (2026-11-15): no build label on any screen ("BLUR-1.00"
 * top right). The build id stays in events.log and the support bundle. */
static void test_no_build_label(void)
{
    BlFrame a = { malloc(1920u * 1080u * 4u), 1920, 1080, 1920 * 4 }, b = a;
    b.pixels = malloc(1920u * 1080u * 4u);
    BlStartState st; bl_start_init(&st);
    BlStatus s; memset(&s, 0, sizeof s); s.readiness = BL_READY_OK;
    bl_start_draw(&a, &st, &s);
    snprintf(s.build, sizeof s.build, "BLUR-1.00");
    bl_start_draw(&b, &st, &s);
    CHECK(memcmp(a.pixels, b.pixels, (size_t)a.stride * a.height) == 0);
    free(a.pixels); free(b.pixels);
}

/* The cached redraw (owner 2026-11-15: smooth launcher) must give exactly
 * the frames the direct drawing gives: every still state of every screen,
 * drawn after other states (the caches warm) and right after a release. */
static BlFrame CF, DF;
static int frames_same(const char *what)
{
    int same = memcmp(CF.pixels, DF.pixels, (size_t)CF.stride * CF.height) == 0;
    if (!same) {
        long worst = 0, count = 0;
        for (size_t i = 0; i < (size_t)CF.stride * CF.height; i++) {
            long d = labs((long)CF.pixels[i] - (long)DF.pixels[i]);
            if (d) count++;
            if (d > worst) worst = d;
        }
        fprintf(stderr, "  cached %s differs: %ld bytes, max %ld\n", what, count, worst);
    }
    return same;
}

#define BOTH(what, call_c, call_d) do { bl_screens_set_cache(1); { BlFrame *f = &CF; call_c; } \
    bl_screens_set_cache(0); { BlFrame *f = &DF; call_d; } bl_screens_set_cache(1); CHECK(frames_same(what)); } while (0)
#define SAME(what, call) BOTH(what, call, call)

static void test_cache_matches_direct(void)
{
    CF = (BlFrame){ malloc(1920u * 1080u * 4u), 1920, 1080, 1920 * 4 };
    DF = CF; DF.pixels = malloc(1920u * 1080u * 4u);
    /* Start: every focus, after slides (sprites built at other focuses), with each status. */
    BlStartState st; bl_start_init(&st);
    BlStatus s; memset(&s, 0, sizeof s);
    s.readiness = BL_READY_OK; s.net_up = 1; snprintf(s.ipv4, sizeof s.ipv4, "10.0.0.7");
    for (int pass = 0; pass < 2; pass++)
        for (int focus = 0; focus < BL_CARD_COUNT; focus++) {
            st.focus = focus; st.pos = (float)focus; st.band = BL_S_BAND_PLAY + 0;
            SAME("start", bl_start_draw(f, &st, &s));
            bl_screens_set_cache(1);
            BlStartState mid = st; mid.pos = (float)focus + (focus < BL_CARD_COUNT - 1 ? 0.5f : -0.5f);
            bl_start_draw(&CF, &mid, &s);                         /* a slide frame between */
        }
    /* The card images built in idle time (bands of rows) give the same frames. */
    bl_screens_release_layers();
    st.focus = 1; st.pos = 1.0f;
    bl_start_draw(&CF, &st, &s);
    int steps = 0;
    while (bl_start_prepare(&st) && steps < 1000) steps++;
    CHECK(steps > 9);                                             /* in bands: more steps than images (9) */
    for (int focus = 0; focus < BL_CARD_COUNT; focus++) {
        st.focus = focus; st.pos = (float)focus;
        SAME("start idle-built", bl_start_draw(f, &st, &s));
    }
    s.logging = 1; s.pads_known = 1; s.pads = 2; s.online = BL_ONLINE_PARTIAL;
    SAME("start status", bl_start_draw(f, &st, &s));
    s.readiness = BL_READY_NO_GAME; s.net_up = 0;
    st.band = BL_S_GAME_MISSING;
    SAME("start long band", bl_start_draw(f, &st, &s));
    bl_screens_release_layers();
    SAME("start cold", bl_start_draw(f, &st, &s));

    /* Patch: idle, both choices, busy with progress, failed. */
    BlPatchView v; memset(&v, 0, sizeof v);
    v.message = -1; v.exe_verdict = BL_EXE_TESTED;
    SAME("patch", bl_patch_draw(f, &v));
    v.choice = 1; v.online = BL_ONLINE_READY;
    SAME("patch online", bl_patch_draw(f, &v));
    v.busy = 1; v.total = 9; v.done = 4; v.steps[0] = BL_STEP_DONE; v.steps[1] = BL_STEP_RUN;
    snprintf(v.progress_name, sizeof v.progress_name, "amax/dlls/amax_auth.asi");
    SAME("patch busy", bl_patch_draw(f, &v));
    memset(&v, 0, sizeof v);
    v.message = -1; v.steps[1] = BL_STEP_FAIL; v.exe_verdict = BL_EXE_UNSUPPORTED;
    snprintf(v.message_text, sizeof v.message_text, "%s", bl_str(BL_S_GAME_UNSUPPORTED));
    SAME("patch failed", bl_patch_draw(f, &v));

    /* Settings: every item, list and panel, messages, a long account name. */
    BlSettingsState ss;
    bl_settings_init(&ss, NULL);
    for (int item = 0; item < BL_SET_COUNT; item++) {
        if (!bl_settings_listed(&ss, item)) continue;
        ss.item = item;
        for (int in = 0; in < 2; in++) {
            ss.in_panel = in; ss.row = in;
            SAME("settings", bl_settings_draw(f, &ss));
        }
    }
    ss.cfg.logging = 1; ss.message = BL_S_BUNDLE_FAILED; ss.item = BL_SET_SUPPORT; ss.in_panel = 1; ss.row = 0;
    SAME("settings message", bl_settings_draw(f, &ss));
    ss.item = BL_SET_ACCOUNT; ss.message = -1;
    snprintf(ss.account_user, sizeof ss.account_user, "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW");
    SAME("settings long name", bl_settings_draw(f, &ss));

    /* Help, scrolled; the question over Start. */
    BlHelpState hs;
    for (int at = 0; at <= BL_HELP_TROUBLE; at++) {
        bl_help_init(&hs, (BlHelpSection)at);
        SAME("help", bl_help_draw(f, &hs));
    }
    snprintf(hs.note, sizeof hs.note, "The download did not finish.");
    SAME("help note", bl_help_draw(f, &hs));
    BlDialog dg;
    bl_dialog_init(&dg, BL_S_PLAY_NO_ACCOUNT, BL_S_DIALOG_PLAY, BL_S_DIALOG_BACK, 2);
    s.readiness = BL_READY_OK; st.band = BL_S_BAND_PLAY;
    for (int focus = 0; focus < 2; focus++) {
        dg.focus = focus;
        SAME("dialog", (bl_start_draw(f, &st, &s), bl_dialog_draw(f, &dg)));
    }
    /* The frontend's way: from the cache without drawing Start first. */
    BOTH("dialog cached", (void)(bl_dialog_draw_cached(f, &dg, bl_start_key(&st, &s)) || (bl_start_draw(f, &st, &s), bl_dialog_draw(f, &dg), 1)),
         (bl_start_draw(f, &st, &s), bl_dialog_draw(f, &dg)));
    s.net_up = 1;                                                 /* the screen under it changed */
    SAME("dialog over changed start", (bl_start_draw(f, &st, &s), bl_dialog_draw(f, &dg)));
    SAME("dialog over patch", (bl_patch_draw(f, &v), bl_dialog_draw(f, &dg)));

    /* No cache may grow past the cap; a release frees everything. */
    CHECK(bl_screens_cache_bytes() <= ((size_t)48 << 20) + 100000);
    bl_screens_release();
    CHECK_EQ(bl_screens_cache_bytes(), 0);
    free(CF.pixels); free(DF.pixels);
}

/* Owner finding 3 (2026-11-15): "Enable logs" and "Save support bundle"
 * say what they do and where the files go; the folders come from bl_paths. */
static void test_logs_texts(void)
{
    BlPaths d; bl_paths_init(&d, NULL);
    BlSettingsState st; bl_settings_init(&st, NULL);
    CHECK(!strcmp(st.logs_dir, d.logs));
    CHECK(!strcmp(st.support_dir, d.support));
    char note[400];
    bl_settings_note(&st, BL_SET_LOGGING, note, sizeof note);
    char want[400];
    snprintf(want, sizeof want, "Writes game logs to %s while you play. Turn on only when something goes wrong.", d.logs);
    CHECK(!strcmp(note, want));
    bl_settings_note(&st, BL_SET_SUPPORT, note, sizeof note);
    snprintf(want, sizeof want, "Collects the logs and your settings into one folder: %s/<date-time>. If needed, copy it to a PC over FTP.", d.support);
    CHECK(!strcmp(note, want));
    snprintf(st.logs_dir, sizeof st.logs_dir, "/data/Other/logs");      /* the layout may move: the text follows */
    bl_settings_note(&st, BL_SET_LOGGING, note, sizeof note);
    CHECK(strstr(note, "/data/Other/logs while you play") != NULL);
    CHECK(strstr(bl_str(BL_S_NOTE_LOGGING), "/data/") == NULL);           /* no root in the strings */
    CHECK(strstr(bl_str(BL_S_NOTE_SUPPORT), "/data/") == NULL);

    /* The frontend passes its own folders; saving names the exact folder. */
    setup(1);
    static const uint32_t script[] = { L, X, D, D, D, D, X, X, O, O };       /* Frame rate first */
    Fake k; memset(&k, 0, sizeof k);
    k.script = script; k.len = 10; k.max_ticks = 60;
    BlServices s = services(&k);
    BlDecision dd;
    CHECK_EQ(run_clean(&s, &dd), 0);
    CHECK(!strcmp(bl_settings_last_drawn.logs_dir, P.logs));
    CHECK(!strcmp(bl_settings_last_drawn.support_dir, P.support));
    const char *m = bl_settings_last_drawn.message_text;
    char prefix[300]; snprintf(prefix, sizeof prefix, "Saved in %s/", P.support);
    CHECK(!strncmp(m, prefix, strlen(prefix)));
    const char *fp = m + strlen("Saved in ");
    char folder[512];
    size_t fl = strlen(fp) < sizeof folder ? strlen(fp) : sizeof folder - 1;
    memcpy(folder, fp, fl); folder[fl] = 0;
    char *end = strchr(folder, '.'); if (end) *end = 0;
    struct stat sb;
    CHECK(stat(folder, &sb) == 0 && S_ISDIR(sb.st_mode));
}

/* Owner findings 17 and 18 (2026-11-15): the online choice says what it
 * does and points to Help for the files; Help names the real folders, and a
 * Saves section says where saves come from and go. */
static void help_text_of(const BlHelpState *st, BlStringId id, char *out, size_t cap)
{
    out[0] = 0;
    for (int i = 0; i < bl_help_count(); i++)
        if (bl_help_para_id(i) == (int)id) { bl_help_text(st, i, out, cap); return; }
}

/* Owner findings 21, 26, 28 and 29 (2026-11-15): the texts the player reads. */
static void test_findings_texts(void)
{
    /* 26: before you start, first in Help. */
    BlHelpState h;
    bl_help_init(&h, BL_HELP_BEFORE);
    CHECK_EQ(bl_help_section(BL_HELP_BEFORE), 0);
    CHECK_EQ(h.scroll, 0);
    CHECK(bl_help_section(BL_HELP_FOLDER) > 0);
    snprintf(h.zip_dir, sizeof h.zip_dir, "/data/Elsewhere");
    char t[600];
    help_text_of(&h, BL_S_HELP_0_HEAD, t, sizeof t);
    CHECK(!strcmp(t, "1  Before you start"));
    /* Owner finding 37: "Before you start" is the data line and the legal line only. */
    CHECK_EQ(bl_help_para_id(1), (int)BL_S_HELP_0_DATA);
    CHECK_EQ(bl_help_para_id(2), (int)BL_S_HELP_0_LEGAL);
    CHECK_EQ(bl_help_para_id(3), (int)BL_S_HELP_A_HEAD);
    help_text_of(&h, BL_S_HELP_0_DATA, t, sizeof t);
    CHECK(strstr(t, "/data/Elsewhere") && strstr(t, "Uninstalling, reinstalling or updating") && strstr(t, "deletes your saves, your account and settings"));
    help_text_of(&h, BL_S_HELP_A_BODY, t, sizeof t);
    CHECK(strstr(t, "USB") == NULL && strstr(t, "FTP") != NULL);
    help_text_of(&h, BL_S_HELP_A_HEAD, t, sizeof t);
    CHECK(!strcmp(t, "2  Your Blur folder"));
    /* Owner finding 37: the folder as full paths (no ASCII tree), {game} as Help and Start use it. */
    {
        snprintf(h.game_dir, sizeof h.game_dir, "/data/Elsewhere/Blur");
        const BlStringId ids[] = { BL_S_HELP_A_BODY, BL_S_HELP_A_DIR, BL_S_HELP_A_LOOK, BL_S_HELP_A_EXE,
                                   BL_S_HELP_A_BINK, BL_S_HELP_A_REST, BL_S_HELP_A_AFTER };
        const char *want[] = { "Copy all your Blur files over FTP into this folder:", "/data/Elsewhere/Blur",
                               "It should look like this:", "/data/Elsewhere/Blur/Blur.exe", "/data/Elsewhere/Blur/binkw32.dll",
                               "/data/Elsewhere/Blur/\xE2\x80\xA6  (all other Blur files and folders)",
                               "Blur.exe must be directly in this folder, not in a folder inside it. "
                               "Upper or lower case does not matter. Only use a copy of Blur you own." };
        int at = bl_help_section(BL_HELP_FOLDER);
        for (int i = 0; i < 7; i++) {
            help_text_of(&h, ids[i], t, sizeof t);
            CHECK(!strcmp(t, want[i]));
            if (strcmp(t, want[i])) fprintf(stderr, "  folder %d: \"%s\"\n", i, t);
            CHECK_EQ(bl_help_para_id(at + 1 + i), (int)ids[i]);
            CHECK(strstr(t, "`--") == NULL && strstr(t, "|--") == NULL);
        }
        CHECK_EQ(bl_help_para_id(at + 8), (int)BL_S_HELP_B_HEAD);
        /* The paths keep the code-style look; the Start hint names the same folder. */
        help_text_of(&h, BL_S_HELP_E_FOLDER, t, sizeof t);
        CHECK(strstr(t, "/data/Elsewhere/Blur") != NULL);
    }
    for (int i = 0; i < BL_S_COUNT; i++) {
        const char *x = bl_str((BlStringId)i);
        CHECK(strstr(x, "USB drive") == NULL || strstr(x, "USB drives are not supported") != NULL);
        CHECK(strstr(x, "Create an account") == NULL);            /* 29: refer, never explain */
    }
    /* 29: the account texts refer to the official AMAX page only. */
    const char *account = "Online play uses the AMAX community servers and needs an AMAX account. How to get one: "
                          "amax-emu.com/how_to_play (official AMAX page). Then enter it in Settings > Online account.";
    CHECK(!strcmp(bl_str(BL_S_HELP_D_BODY), account));
    CHECK(!strcmp(bl_str(BL_S_NOTE_ACCOUNT_CREATE), account));
    /* 28: the status next to the IP, and saves back to a PC. */
    CHECK(!strcmp(bl_str(BL_S_STATUS_PATCHED), "Patched for offline play"));
    snprintf(h.saves_dir, sizeof h.saves_dir, "/data/Elsewhere/saves");
    help_text_of(&h, BL_S_HELP_S_BACK, t, sizeof t);
    CHECK(strstr(t, "Back to a PC") && strstr(t, "/data/Elsewhere/saves/save/" BL_IMPORT_FILE) && strstr(t, "FTP"));
    /* 21/28: another Blur.exe is an amber line, not a failure. */
    CHECK(!strcmp(bl_str(BL_S_PATCH_EXE_OTHER),
                  "Blur.exe: not a verified Blur, so it runs at 30 FPS. Patch for online play installs the verified one."));
    CHECK(!strcmp(bl_str(BL_S_GAME_UNSUPPORTED),
                  "This Blur.exe is not a verified Blur, so JkFlow runs it at 30 FPS. Patch for online play installs the verified one."));
    help_text_of(&h, BL_S_HELP_E_EXE, t, sizeof t);
    CHECK(!strcmp(t, "Blur.exe not verified (also a retail or Steam one): JkFlow runs it at 30 FPS. Patch for online play "
                     "puts in the verified one; then 60 FPS can be turned on in Settings > Frame rate on a PS4 Pro."));
    /* The account note names the real state folder. */
    BlSettingsState st;
    bl_settings_init(&st, NULL);
    snprintf(st.state_dir, sizeof st.state_dir, "/data/Elsewhere/System/state");
    bl_settings_note(&st, BL_SET_ACCOUNT, t, sizeof t);
    CHECK(!strcmp(t, "Your account is stored on this console in /data/Elsewhere/System/state and sent only to the AMAX servers. "
                     "The password is saved on the PS4 in plain text, because AMAX needs it that way; Remove account deletes it."));
    CHECK(strstr(bl_str(BL_S_NOTE_ACCOUNT_STORED), "/data/") == NULL);
    CHECK(strstr(bl_str(BL_S_IMPORT_CONFLICT), "/data/") == NULL && strstr(bl_str(BL_S_PATCH_IMPORT_FAILED), "/data/") == NULL);
}

static void test_online_and_saves_texts(void)
{
    CHECK(!strcmp(bl_str(BL_S_PATCH_ONLINE_NOTE), "Downloads amax_client_files.zip and blur_12.zip from amax-emu.com and installs them."));
    CHECK(!strcmp(bl_str(BL_S_PATCH_ONLINE_HELP),
                  "No internet, or the download failed? Press Square for Help: it shows where to put the two .zip files (do not unzip them)."));
    CHECK(!strcmp(bl_str(BL_S_HINT_HELP_ONLINE), "Help: online files"));
    CHECK(!strcmp(bl_str(BL_S_HINT_HELP_FOLDER), "Help: Blur folder"));     /* audit 24: what Square opens */
    char msg[400];
    snprintf(msg, sizeof msg, bl_str(BL_S_ONLINE_NO_ZIPS), "/data/X");
    CHECK(!strcmp(msg, "Online files not found. Nothing was changed. Put amax_client_files.zip and blur_12.zip (still zipped) in /data/X, then press Patch again."));
    snprintf(msg, sizeof msg, bl_str(BL_S_ONLINE_NO_NETWORK), "/data/X");
    CHECK(!strcmp(msg, "No network. Nothing was changed. Connect the PS4 to the internet and press Patch again, or put amax_client_files.zip "
                       "and blur_12.zip (still zipped) in /data/X."));
    /* The key cap is drawn where {square} stands (the font has no square glyph). */
    CHECK(!strcmp(bl_str(BL_S_PATCH_HELP_MORE), "Press {square} Help for step-by-step instructions."));

    /* Help: the default folders, then the ones the frontend sets. */
    BlHelpState h;
    bl_help_init(&h, BL_HELP_FOLDER);
    BlPaths d; bl_paths_init(&d, NULL);
    CHECK(!strcmp(h.zip_dir, "/data/JkFlow"));
    CHECK(!strcmp(h.import_dir, d.import));
    CHECK(!strcmp(h.saves_dir, d.saves));
    char t[600];
    help_text_of(&h, BL_S_HELP_C_BODY, t, sizeof t);
    CHECK(!strcmp(t, "Patch for online play downloads the files for you. Only without internet: on a computer, download "
                     "amax_client_files.zip and blur_12.zip from amax-emu.com (How to play). "
                     "Do NOT unzip them. Put both .zip files in /data/JkFlow, next to the Blur folder:"));
    snprintf(h.zip_dir, sizeof h.zip_dir, "/data/Elsewhere");
    snprintf(h.import_dir, sizeof h.import_dir, "/data/Elsewhere/import");
    snprintf(h.saves_dir, sizeof h.saves_dir, "/data/Elsewhere/saves");
    help_text_of(&h, BL_S_HELP_C_ZIP1, t, sizeof t);
    CHECK(!strcmp(t, "/data/Elsewhere/amax_client_files.zip"));
    help_text_of(&h, BL_S_HELP_C_ZIP2, t, sizeof t);
    CHECK(!strcmp(t, "/data/Elsewhere/blur_12.zip"));
    /* Saves: where on the PC, which file, where on the console, then Patch. */
    CHECK(bl_help_section(BL_HELP_SAVES) > bl_help_section(BL_HELP_ACCOUNT));
    CHECK(bl_help_section(BL_HELP_TROUBLE) > bl_help_section(BL_HELP_SAVES));
    help_text_of(&h, BL_S_HELP_S_HEAD, t, sizeof t);
    CHECK(!strcmp(t, "6  Saves"));
    help_text_of(&h, BL_S_HELP_S_BODY, t, sizeof t);
    CHECK(strstr(t, "%APPDATA%\\Bizarre Creations\\Blur\\save") != NULL && strstr(t, BL_IMPORT_FILE) != NULL);
    help_text_of(&h, BL_S_HELP_S_TO, t, sizeof t);
    CHECK(!strcmp(t, "/data/Elsewhere/import/" BL_IMPORT_FILE));
    help_text_of(&h, BL_S_HELP_S_THEN, t, sizeof t);
    CHECK(strstr(t, "step 7") != NULL && strstr(t, "backed up first") != NULL);
    help_text_of(&h, BL_S_HELP_S_BACK, t, sizeof t);
    CHECK(strstr(t, "/data/Elsewhere/saves/save/" BL_IMPORT_FILE) != NULL);
    help_text_of(&h, BL_S_HELP_E_HEAD, t, sizeof t);
    CHECK(!strcmp(t, "7  If something goes wrong"));
    /* No folder is written into the strings themselves. */
    CHECK(strstr(bl_str(BL_S_HELP_C_ZIP1), "/data/") == NULL && strstr(bl_str(BL_S_HELP_S_TO), "/data/") == NULL);

    /* The frontend fills in its own folders. */
    setup(1);
    static const uint32_t script[] = { BL_PAD_SQUARE, O };
    Fake k; memset(&k, 0, sizeof k);
    k.script = script; k.len = 2; k.max_ticks = 30;
    BlServices s = services(&k);
    BlDecision dd;
    CHECK_EQ(run_clean(&s, &dd), 0);
    char parent[300]; snprintf(parent, sizeof parent, "%s", P.game);
    char *sl = strrchr(parent, '/'); if (sl) *sl = 0;
    CHECK(!strcmp(bl_help_last_drawn.zip_dir, parent));
    CHECK(!strcmp(bl_help_last_drawn.import_dir, P.import));
    CHECK(!strcmp(bl_help_last_drawn.saves_dir, P.saves));
}

int main(void)
{
    RUN(test_online_and_saves_texts);
    RUN(test_findings_texts);
    RUN(test_logs_texts);
    RUN(test_no_build_label);
    RUN(test_cache_matches_direct);
    RUN(test_settings_has_exactly_five_items);
    RUN(test_settings_frame_rate);
    RUN(test_settings_rules);
    RUN(test_strings);
    RUN(test_full_flow_patch_settings_play);
    RUN(test_patch_other_exe_continues);
    RUN(test_patch_import_conflict_keeps);
    RUN(test_patch_game_missing_stops);
    RUN(test_patch_offline_game_only);
    RUN(test_patch_done_dialog);
    RUN(test_patch_online_no_network_stays);
    RUN(test_patch_online_one_zip);
    RUN(test_patch_online_interrupted);
    RUN(test_patch_failure_is_one_sentence);
    RUN(test_patch_too_many_other_files);
    RUN(test_patch_online_download);
    RUN(test_patch_online_free_space);
    RUN(test_patch_online_clock_wrong);
    RUN(test_patch_online_bad_zip);
    RUN(test_patch_round_trip);
    RUN(test_status_online_missing);
    RUN(test_help_from_start);
    RUN(test_golden_screens);
    RUN(test_corrupt_ini_is_reported);
    RUN(test_caches_released);
    RUN(test_no_data_root_says_not_saved);
    RUN(test_account_page_saves);
    RUN(test_note_long_line);
    RUN(test_logs_uncapped_and_live_log_kept);
    RUN(test_no_about_legal_line);
    RUN(test_stop_messages);
    RUN(test_audit24_texts);
    RUN(test_top_bar_no_overlap);
    char cmd[400]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); sh(cmd);
    return bl_test_report("test_screens");
}
