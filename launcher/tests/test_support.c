/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests for the production review: the always-on event log ring, log
 * rotation and caps, session outcomes, save backups and the support bundle
 * (BLUR 1.00 plan Task L10: under /data/BLUR/support, never with account
 * data, logging through env only). */
#define _POSIX_C_SOURCE 200809L
#include "bl_account.h"
#include "bl_events.h"
#include "bl_exe.h"
#include "bl_fs.h"
#include "bl_frontend.h"
#include "bl_logs.h"
#include "bl_paths.h"
#include "bl_profile.h"
#include "bl_saves.h"
#include "bl_screens.h"
#include "bl_session.h"
#include "bl_sha256.h"
#include "bl_support.h"
#include "bl_test.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static char root[200], data[240], game[300], files[300], state[320];

static void put(const char *dir, const char *name, const char *text)
{
    char p[900]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "wb"); if (!f) return; fputs(text, f); fclose(f);
}

static void put_size(const char *dir, const char *name, long long size)
{
    char p[900]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "wb"); if (!f) return;
    if (size > 0) { fseek(f, size - 1, SEEK_SET); fputc('x', f); }
    fclose(f);
}

static char *slurp(const char *dir, const char *name)
{
    static char buf[65536];
    char p[900]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) { buf[0] = 0; return buf; }
    size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f);
    return buf;
}

static int has(const char *dir, const char *name)
{
    char p[900]; snprintf(p, sizeof p, "%s/%s", dir, name);
    return bl_fs_exists(p);
}

static long long size_of(const char *dir, const char *name)
{
    char p[900]; struct stat st; snprintf(p, sizeof p, "%s/%s", dir, name);
    return stat(p, &st) == 0 ? (long long)st.st_size : -1;
}

static const char *DLL = "our d3d9.dll", *OLD_DLL = "our older d3d9.dll", *CACHE = "DXVK seed cache with entries";
static char exe_sha[65];

static void setup(void)
{
    snprintf(root, sizeof root, "/tmp/bl_test_support_XXXXXX");
    if (!mkdtemp(root)) exit(1);
    snprintf(data, sizeof data, "%s/data", root); mkdir(data, 0755);
    snprintf(state, sizeof state, "%s/state", data); mkdir(state, 0755);
    snprintf(game, sizeof game, "%s/game", data); bl_fs_mkdirs(game);
    snprintf(files, sizeof files, "%s/files", root); mkdir(files, 0755);
    char hd[65], ho[65], hc[65], man[1024];
    bl_sha256_hex(DLL, strlen(DLL), hd);
    bl_sha256_hex(OLD_DLL, strlen(OLD_DLL), ho);
    bl_sha256_hex(CACHE, strlen(CACHE), hc);
    bl_sha256_hex("MZ tested", 9, exe_sha);
    put(files, "d3d9.dll", DLL);
    put(files, "dxvk-native.dxvk-cache", CACHE);
    snprintf(man, sizeof man, "file d3d9.dll %s %zu\nseed dxvk-native.dxvk-cache %s %zu\nknown %s\ntested %s\n",
             hd, strlen(DLL), hc, strlen(CACHE), ho, exe_sha);
    put(files, "manifest.txt", man);
}

/* ---------------- event log ring ---------------- */

static void test_events_ring(void)
{
    bl_events_open(state);
    CHECK(strstr(bl_events_path(), "/state/events.log") != NULL);
    bl_events_log(BL_E_START_FAILED, "first line %d", 1);
    CHECK(strstr(slurp(state, "events.log"), " E101 first line 1\n") != NULL);
    /* Control characters never break a line. */
    bl_events_log(BL_E_INFO, "path with\nnewline");
    CHECK(strstr(slurp(state, "events.log"), "path with newline\n") != NULL);
    /* The pair never holds more than 2 x BL_EVENTS_MAX. */
    char big[600]; memset(big, 'a', sizeof big - 1); big[sizeof big - 1] = 0;
    for (int i = 0; i < 400; i++) bl_events_log(BL_E_INFO, "%d %s", i, big);
    CHECK(size_of(state, "events.log") <= BL_EVENTS_MAX);
    CHECK(size_of(state, "events.log.1") > 0 && size_of(state, "events.log.1") <= BL_EVENTS_MAX);
    CHECK(strstr(slurp(state, "events.log"), " I 399 ") != NULL);     /* the newest is kept */
    bl_events_open(NULL);
    bl_events_log(BL_E_INFO, "nowhere");                                /* closed: no crash, nothing written */
    CHECK(strstr(slurp(state, "events.log"), "nowhere") == NULL);
    bl_events_open(state);
}

/* ---------------- rotation and caps ---------------- */

static void test_logs_rotate(void)
{
    char logs[400]; snprintf(logs, sizeof logs, "%s/logs", data); mkdir(logs, 0755);
    put_size(logs, "dxn_server.txt", BL_LOG_ROTATE_BYTES + 10);
    put(logs, "dxn_server.txt.old", "older");
    put_size(game, "Blur_d3d9.log", 100);
    put_size(logs, "wowblur.log", BL_LOG_ROTATE_BYTES + 1);
    for (int i = 0; i < 20; i++) {
        char n[64]; snprintf(n, sizeof n, "session-%d.log", i);
        put(logs, n, "REC");
        char p[600]; snprintf(p, sizeof p, "%s/%s", logs, n);
        struct timeval tv[2] = { { 1000 + i, 0 }, { 1000 + i, 0 } };
        utimes(p, tv);
    }
    int n = bl_logs_rotate(game, data, "session-0.log");
    CHECK_EQ(size_of(logs, "dxn_server.txt"), -1);
    CHECK_EQ(size_of(logs, "dxn_server.txt.old"), BL_LOG_ROTATE_BYTES + 10);   /* the older .old went */
    CHECK_EQ(size_of(game, "Blur_d3d9.log"), 100);                              /* small: left alone */
    CHECK_EQ(size_of(logs, "wowblur.log.old"), BL_LOG_ROTATE_BYTES + 1);
    int sessions = 0;
    for (int i = 0; i < 20; i++) { char nm[64]; snprintf(nm, sizeof nm, "session-%d.log", i); sessions += has(logs, nm); }
    CHECK_EQ(sessions, BL_LOG_SESSION_KEEP);
    CHECK(has(logs, "session-0.log"));               /* this run's own, though oldest */
    CHECK(has(logs, "session-19.log") && !has(logs, "session-1.log"));
    CHECK_EQ(n, 2 + (20 - BL_LOG_SESSION_KEEP));
    CHECK_EQ(bl_logs_rotate(game, data, "session-0.log"), 0);   /* nothing left to do */
    long long bytes = 0;
    CHECK(bl_logs_summary(game, data, "session-0.log", &bytes) >= 3);
}

/* F1 request 1 (folder move, findings 4+5): the logs are where bl_paths
 * puts them. A root named System keeps them in the sibling Logs folder
 * (/data/JkFlow/System -> /data/JkFlow/Logs): rotation, the Settings count,
 * clearing and the support bundle all look there. */
static void test_logs_follow_bl_paths(void)
{
    char jk[300], sys[340], g[340];
    snprintf(jk, sizeof jk, "%s/JkFlow", root);
    snprintf(sys, sizeof sys, "%s/System", jk);
    snprintf(g, sizeof g, "%s/Blur", jk);
    BlPaths p; bl_paths_init(&p, sys);
    bl_paths_make(&p);
    bl_fs_mkdirs(g);
    CHECK(strstr(p.logs, "/JkFlow/Logs") != NULL);
    put_size(p.logs, "wowblur.log", BL_LOG_ROTATE_BYTES + 1);
    put(p.logs, "session-1.log", "REC");
    put(g, "hang260.txt", "a dev file the product never writes");     /* F1 request 4: not a log of ours */
    CHECK_EQ(bl_logs_rotate(g, sys, NULL), 1);
    CHECK_EQ(size_of(p.logs, "wowblur.log.old"), BL_LOG_ROTATE_BYTES + 1);
    long long bytes = 0;
    CHECK_EQ(bl_logs_summary(g, sys, NULL, &bytes), 2);
    BlSupportInfo in = { sys, g, files, "BLUR-1.00", NULL, "native", NULL };
    char dir[1200];
    CHECK_EQ(bl_support_bundle(&in, dir, sizeof dir), 0);
    CHECK(!strncmp(dir, p.support, strlen(p.support)));
    CHECK(has(dir, "logs_session-1.log"));
    CHECK_EQ(bl_logs_clear(g, sys, NULL), 2);
    CHECK(!has(p.logs, "session-1.log"));
}

/* ---------------- session outcomes ---------------- */

static void test_session(void)
{
    int ab = -1;
    char when[40];
    CHECK_EQ(bl_session_check(state, &ab, when, sizeof when), BL_SESSION_CLEAN);
    CHECK_EQ(ab, 0);
    /* Started, never reached the first frame. */
    CHECK_EQ(bl_session_begin(state), 0);
    CHECK_EQ(bl_session_check(state, &ab, when, sizeof when), BL_SESSION_NO_FIRST_FRAME);
    CHECK(strlen(when) == 16);
    CHECK_EQ(ab, 0);
    /* Two sessions that ran and did not end normally. */
    for (int i = 1; i <= 2; i++) {
        bl_session_begin(state);
        bl_session_first_frame();
        CHECK_EQ(bl_session_check(state, &ab, NULL, 0), BL_SESSION_ABNORMAL);
        CHECK_EQ(ab, i);
    }
    /* A failed start neither adds nor clears. */
    bl_session_begin(state);
    CHECK_EQ(bl_session_check(state, &ab, NULL, 0), BL_SESSION_NO_FIRST_FRAME);
    CHECK_EQ(ab, 2);
    /* A clean end clears the count. */
    bl_session_begin(state);
    bl_session_first_frame();
    bl_session_end_clean();
    bl_session_first_frame();                       /* after the end: no-op */
    CHECK(!has(state, BL_SESSION_FILE));
    CHECK_EQ(bl_session_check(state, &ab, NULL, 0), BL_SESSION_CLEAN);
    CHECK_EQ(ab, 0);
    CHECK(!has(state, BL_SESSION_COUNT_FILE));
    CHECK_EQ(bl_session_check(NULL, &ab, NULL, 0), BL_SESSION_CLEAN);
}

/* ---------------- saves ---------------- */

static void test_saves(void)
{
    BlPaths paths;
    bl_paths_init(&paths, data);
    const BlPaths *P = &paths;
    char saves[700];
    snprintf(saves, sizeof saves, "%s/sub", paths.saves);
    CHECK_EQ(bl_saves_backup(P), 1);                                  /* no saves yet */
    bl_fs_mkdirs(saves);
    char top[800]; snprintf(top, sizeof top, "%.700s/..", saves);
    put(top, "profile.sav", "progress 1");
    put(saves, "options.cfg", "opts");
    char found[1400];
    CHECK_EQ(bl_saves_find2(P, found, sizeof found), 0);
    CHECK(!strcmp(found, paths.saves));
    CHECK_EQ(bl_saves_backup(P), 0);
    CHECK_EQ(bl_saves_backup(P), 1);                         /* the same: not again */
    CHECK_EQ(bl_saves_count(P, NULL, 0), 1);
    put(top, "profile.sav", "progress 2");
    put(top, "extra.tmp", "made after the backup");
    CHECK_EQ(bl_saves_backup(P), 0);
    char newest[64];
    CHECK_EQ(bl_saves_count(P, newest, sizeof newest), 2);
    CHECK(!strncmp(newest, "000002-", 7));
    /* Damage, then restore the newest backup. */
    put(top, "profile.sav", "corrupt");
    CHECK_EQ(bl_saves_restore(P), 0);
    CHECK(strcmp(slurp(top, "profile.sav"), "progress 2") == 0);
    CHECK(strcmp(slurp(saves, "options.cfg"), "opts") == 0);
    CHECK_EQ(bl_saves_count(P, NULL, 0), 3);                       /* the damaged state was kept too */
    /* The newest is now the damaged one: restoring again brings it back,
     * and files the backup lacks go. */
    put(top, "stray.tmp", "x");
    CHECK_EQ(bl_saves_restore(P), 0);
    CHECK(!has(top, "stray.tmp"));
    /* Only the newest BL_SAVES_KEEP stay. */
    for (int i = 0; i < BL_SAVES_KEEP + 3; i++) {
        char t[32]; snprintf(t, sizeof t, "state %d", i);
        put(top, "profile.sav", t);
        CHECK_EQ(bl_saves_backup(P), 0);
    }
    CHECK_EQ(bl_saves_count(P, NULL, 0), BL_SAVES_KEEP);
    CHECK(bl_saves_bytes(P) > 0);
}

/* ---------------- exe info, cache reset, removal, resets ---------------- */

static long long g_free = -1;
static long long fake_free(const char *path) { (void)path; return g_free; }

static void test_exe_info_and_space(void)
{
    BlExeInfo e;
    CHECK_EQ(bl_exe_info(game, files, &e), 0);
    CHECK(!e.found);
    put(game, "BLUR.EXE", "MZ tested");
    bl_exe_info(game, files, &e);
    CHECK(e.found && e.tested && !strcmp(e.sha, exe_sha) && e.size == 9);
    put(game, "BLUR.EXE", "MZ another copy");
    bl_exe_info(game, files, &e);
    CHECK(e.found && !e.tested);

    /* Free space: unknown is not checked. */
    bl_free_bytes = fake_free; g_free = 10;
    CHECK(!bl_space_ok(game, 1));
    g_free = -1;
    CHECK(bl_space_ok(game, 1LL << 40));
    bl_free_bytes = NULL;
}

/* ---------------- support bundle (L10) ---------------- */

static void bundle_world(void)
{
    char logs[400]; snprintf(logs, sizeof logs, "%s/logs", data); bl_fs_mkdirs(logs);
    put(game, "dxn_stage.txt", "stage create 3\n");
    put(game, "Blur_d3d9.log", "d3d9 errors\n");
    put(state, "launcher.ini", "[logging]\nenabled = false\n");
    put(state, "patch.txt", "build_id=BLUR-1.00\n");
    put(logs, "session-3.log", "REC seq=1 exit\n");
    put(logs, "radv.log", "radv errors\n");
    char amaxlog[400]; snprintf(amaxlog, sizeof amaxlog, "%s/amax/log", game); bl_fs_mkdirs(amaxlog);
    put(amaxlog, "amax_auth.log", "auth: connected\n");
    put(amaxlog, "notes.txt", "not a log");
}

static void test_bundle_location(void)
{
    bundle_world();
    BlSupportInfo in = { data, game, files, "BLUR-1.00", NULL, "native", NULL };
    char dir[1200], want[600];
    CHECK_EQ(bl_support_bundle(&in, dir, sizeof dir), 0);
    snprintf(want, sizeof want, "%s/support/", data);
    CHECK(!strncmp(dir, want, strlen(want)));
    CHECK(strstr(dir, "Blur-support") == NULL);
    char *man = slurp(dir, "MANIFEST.txt");
    CHECK(strstr(man, "build BLUR-1.00\n") != NULL);
    CHECK(strstr(man, "blur.exe ") != NULL);
    CHECK(strstr(man, "package manifest.txt ") != NULL);
    CHECK(strstr(man, "installed file d3d9.dll") != NULL);
    const char *in_set[] = { "events.log", "launcher.ini", "patch.txt", "dxn_stage.txt", "Blur_d3d9.log", "logs_session-3.log", "logs_radv.log" };
    for (unsigned i = 0; i < sizeof in_set / sizeof in_set[0]; i++) {
        CHECK(has(dir, in_set[i]));
        if (!has(dir, in_set[i])) fprintf(stderr, "  missing %s\n", in_set[i]);
    }
    CHECK(strcmp(slurp(dir, "dxn_stage.txt"), "stage create 3\n") == 0);
    CHECK(strstr(slurp(dir, "README.txt"), "E101") != NULL);
    bl_free_bytes = fake_free; g_free = 0;
    CHECK_EQ(bl_support_bundle(&in, dir, sizeof dir), -2);
    bl_free_bytes = NULL;
    BlSupportInfo none = { NULL, game, files, "BLUR-1.00", NULL, "native", NULL };
    CHECK(bl_support_bundle(&none, dir, sizeof dir) < 0);           /* no data folder: nowhere to put it */
}

static void test_bundle_never_contains_account(void)
{
    bundle_world();
    BlPaths p;
    bl_paths_init(&p, data);
    BlAccount a; memset(&a, 0, sizeof a);
    snprintf(a.user, sizeof a.user, "SecretUserX"); snprintf(a.pass, sizeof a.pass, "SecretPassY");
    CHECK_EQ(bl_account_save(&p, &a), 0);
    snprintf(p.game, sizeof p.game, "%.90s", game);
    CHECK_EQ(bl_account_apply(&p), 0);                              /* game/amax/plugins/account_cfg.lua */
    put(state, "online.secret", "SecretPassY");
    put(p.import, "blursave", "save with SecretPassY");
    char sv[600]; snprintf(sv, sizeof sv, "%s/save", p.saves); bl_fs_mkdirs(sv);
    put(sv, "blursave", "save with SecretPassY");
    BlSupportInfo in = { data, game, files, "BLUR-1.00", NULL, "native", NULL };
    char dir[1200];
    CHECK_EQ(bl_support_bundle(&in, dir, sizeof dir), 0);
    char cmd[3200];
    snprintf(cmd, sizeof cmd, "test -z \"$(grep -rl -e SecretPassY -e SecretUserX '%s')\" && test -z \"$(find '%s' -name 'account*' -o -name '*blursave*' -o -name 'online.secret')\"", dir, dir);
    CHECK_EQ(system(cmd), 0);
    char *man = slurp(dir, "MANIFEST.txt");
    CHECK(strstr(man, "account.cfg") == NULL && strstr(man, "account_cfg.lua") == NULL);
    CHECK(strstr(man, "blursave") == NULL && strstr(man, "online.secret") == NULL);
    bl_account_clear(&p);
}

/* Owner finding 27: every copied file has the AMAX user name and the LAN
 * addresses masked (a public server address stays: it helps diagnosis);
 * the README names the product JkFlow. */
static void test_bundle_masks_user_and_lan_ips(void)
{
    bundle_world();
    BlPaths p;
    bl_paths_init(&p, data);
    BlAccount a; memset(&a, 0, sizeof a);
    snprintf(a.user, sizeof a.user, "RacerJoe"); snprintf(a.pass, sizeof a.pass, "pw");
    CHECK_EQ(bl_account_save(&p, &a), 0);
    char logs[400]; snprintf(logs, sizeof logs, "%s/logs", data);
    put(logs, "session-3.log", "login RacerJoe from 192.168.0.124 to 203.0.113.5\nlan 10.1.2.3 172.20.1.1 172.32.0.1 1.10.1.2.3\n");
    put(game, "amax/log/amax_auth.log", "auth: RacerJoe@192.168.1.20 ok\n");
    BlSupportInfo in = { data, game, files, "BLUR-1.00", NULL, "native", NULL };
    char dir[1200];
    CHECK_EQ(bl_support_bundle(&in, dir, sizeof dir), 0);
    char *s = slurp(dir, "logs_session-3.log");
    CHECK(strstr(s, "RacerJoe") == NULL && strstr(s, "192.168.0.124") == NULL);
    CHECK(strstr(s, "10.1.2.3 ") == NULL && strstr(s, "172.20.1.1") == NULL);
    CHECK(strstr(s, "203.0.113.5") != NULL && strstr(s, "172.32.0.1") != NULL);    /* public: kept */
    CHECK(strstr(s, "login <user> from <lan-ip> to 203.0.113.5\n") != NULL);
    if (!strstr(s, "<user>")) fprintf(stderr, "  got: %s", s);
    s = slurp(dir, "amax_log_amax_auth.log");
    CHECK(!strcmp(s, "auth: <user>@<lan-ip> ok\n"));
    char cmd[1600];
    snprintf(cmd, sizeof cmd, "test -z \"$(grep -rl -e RacerJoe -e 192.168. '%s')\"", dir);
    CHECK_EQ(system(cmd), 0);
    CHECK(strstr(slurp(dir, "README.txt"), "JkFlow support bundle\n=====================\n") != NULL);
    bl_account_clear(&p);
}

static void test_bundle_includes_amax_logs(void)
{
    bundle_world();
    BlSupportInfo in = { data, game, files, "BLUR-1.00", NULL, "native", NULL };
    char dir[1200];
    CHECK_EQ(bl_support_bundle(&in, dir, sizeof dir), 0);
    CHECK(has(dir, "amax_log_amax_auth.log"));
    CHECK(strcmp(slurp(dir, "amax_log_amax_auth.log"), "auth: connected\n") == 0);
    CHECK(!has(dir, "amax_log_notes.txt"));                         /* *.log only */
    CHECK(strstr(slurp(dir, "MANIFEST.txt"), "file amax_log_amax_auth.log ") != NULL);
}

/* Audit 26: the AMAX user name is masked at any length (a 1- or 2-character
 * name as a whole word, so other words keep their letters), and the PS4 local
 * user name too; AMAX's own logs stay in the bundle. */
static void test_bundle_masks_any_user_length(void)
{
    static const struct { const char *user, *log, *want; } C[] = {
        { "q", "login q ok; quick query\n", "login <user> ok; quick query\n" },
        { "Jo", "user=Jo, Joe and jo\n", "user=<user>, Joe and <user>\n" },
        { "SixteenCharsName", "hi SixteenCharsName!\n", "hi <user>!\n" },
    };
    BlPaths p;
    for (unsigned i = 0; i < sizeof C / sizeof C[0]; i++) {
        bundle_world();
        bl_paths_init(&p, data);
        BlAccount a; memset(&a, 0, sizeof a);
        snprintf(a.user, sizeof a.user, "%s", C[i].user); snprintf(a.pass, sizeof a.pass, "pw");
        CHECK_EQ(bl_account_save(&p, &a), 0);
        put(game, "amax/log/amax_auth.log", C[i].log);
        BlSupportInfo in = { data, game, files, "BLUR-1.00", NULL, "native", NULL };
        char dir[1200];
        CHECK_EQ(bl_support_bundle(&in, dir, sizeof dir), 0);
        const char *s = slurp(dir, "amax_log_amax_auth.log");
        CHECK(!strcmp(s, C[i].want));
        if (strcmp(s, C[i].want)) fprintf(stderr, "  user %s: got: %s", C[i].user, s);
        bl_account_clear(&p);
    }
    /* the PS4 local user name */
    bundle_world();
    char logs[400]; snprintf(logs, sizeof logs, "%s/logs", data);
    put(logs, "session-4.log", "player John Smith joined\n");
    BlSupportInfo in = { data, game, files, "BLUR-1.00", NULL, "native", NULL };
    char dir[1200];
    bl_support_local_user = "John Smith";
    CHECK_EQ(bl_support_bundle(&in, dir, sizeof dir), 0);
    bl_support_local_user = NULL;
    CHECK(!strcmp(slurp(dir, "logs_session-4.log"), "player <ps4-user> joined\n"));
    /* no dxn_stage.txt (the product engine does not write it): normal */
    char st[700]; snprintf(st, sizeof st, "%s/dxn_stage.txt", game); unlink(st);
    CHECK_EQ(bl_support_bundle(&in, dir, sizeof dir), 0);
    CHECK(strstr(slurp(dir, "MANIFEST.txt"), "dxn_stage") == NULL);
}

/* Audit 26: a copy that cannot be written whole fails the bundle (no silent
 * half file). The file size limit makes the big log's copy fail; the small
 * README and MANIFEST still fit. */
static void test_bundle_write_error_fails(void)
{
    bundle_world();
    char logs[400]; snprintf(logs, sizeof logs, "%s/logs", data);
    static char big[96 * 1024];
    memset(big, 'x', sizeof big - 1);
    put(logs, "write-error-big.txt", big);
    BlSupportInfo in = { data, game, files, "BLUR-1.00", NULL, "native", NULL };
    char dir[1200];
    struct rlimit old, lim;
    getrlimit(RLIMIT_FSIZE, &old);
    lim = old; lim.rlim_cur = 32 * 1024;
    signal(SIGXFSZ, SIG_IGN);
    setrlimit(RLIMIT_FSIZE, &lim);
    int r = bl_support_bundle(&in, dir, sizeof dir);
    setrlimit(RLIMIT_FSIZE, &old);
    signal(SIGXFSZ, SIG_DFL);
    CHECK(r < 0);
    char cmd[800];
    snprintf(cmd, sizeof cmd, "test -z \"$(find '%s/support' -name logs_write-error-big.txt 2>/dev/null)\"", data);
    CHECK_EQ(system(cmd), 0);                                        /* no half copy left */
    char bigp[500]; snprintf(bigp, sizeof bigp, "%s/write-error-big.txt", logs); unlink(bigp);
}

/* Logging is passed through env only: no switch files (BLUR 1.00 design 7). */
typedef struct Fake { uint64_t now; int ticks, max_ticks; const uint32_t *script; int len, pos; } Fake;
static int f_present(void *ctx, const BlFrame *f) { (void)ctx; (void)f; return 0; }
static int f_pad(void *ctx, uint32_t *held, uint32_t *pressed)
{
    Fake *k = ctx; *held = 0;
    *pressed = k->pos < k->len ? k->script[k->pos++] : 0;
    return 0;
}
static int f_quit(void *ctx) { Fake *k = ctx; return ++k->ticks >= k->max_ticks; }
static uint64_t f_now(void *ctx) { return ((Fake *)ctx)->now; }
static void f_sleep(void *ctx, uint32_t us) { ((Fake *)ctx)->now += (uint64_t)us * 1000u; }

static void test_no_switch_files_written(void)
{
    /* A playable game folder: the tested exe (by the hook) and our file. */
    char exe[600]; snprintf(exe, sizeof exe, "%s/Blur.exe", game);
    int fd = open(exe, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd >= 0) { if (ftruncate(fd, (off_t)BL_TESTED_EXE_SIZE) != 0) {} close(fd); }
    static char sha[65];
    bl_fs_sha256(exe, sha, NULL);
    bl_exe_expected_sha = sha;
    put(game, "d3d9.dll", DLL);
    put(game, "dxvk-native.dxvk-cache", CACHE);
    char base[700]; snprintf(base, sizeof base, "%s/model-base.ini", files);
    FILE *in = fopen(BL_TEST_DIR "/../assets/blur-files/model-base.ini", "rb"), *out = fopen(base, "wb");
    int ch; while (in && (ch = fgetc(in)) != EOF) fputc(ch, out);
    if (in) fclose(in);
    fclose(out);
    put(state, "launcher.ini", "[logging]\nenabled = true\n");
    BlPaths p; bl_paths_init(&p, data);
    BlAccount a; memset(&a, 0, sizeof a);
    snprintf(a.user, sizeof a.user, "u"); snprintf(a.pass, sizeof a.pass, "p");
    bl_account_save(&p, &a);
    static const uint32_t play[] = { BL_PAD_CROSS };
    Fake k; memset(&k, 0, sizeof k);
    k.script = play; k.len = 1; k.max_ticks = 50;
    BlServices s; memset(&s, 0, sizeof s);
    s.ctx = &k; s.present = f_present; s.pad = f_pad; s.should_quit = f_quit; s.now_ns = f_now; s.sleep_us = f_sleep;
    s.data_root = data; s.game_dir = game; s.package_files_dir = files; s.build_id = "BLUR-1.00";
    s.local_user = "John Smith";                                    /* audit 26: masked in a bundle */
    BlDecision d;
    CHECK_EQ(bl_frontend_run(&s, &d), 0);
    CHECK(bl_support_local_user && !strcmp(bl_support_local_user, "John Smith"));
    bl_support_local_user = NULL;
    CHECK_EQ(d.action, BL_START_GAME);
    CHECK(!has(game, "blur_logs.on"));
    CHECK(!has(data, "radv.env"));
    int logs_on = 0;
    for (unsigned i = 0; i < d.env_count; i++) logs_on |= !strcmp(d.env[i].name, "BLUR_LOGS") && !strcmp(d.env[i].value, "1");
    CHECK(logs_on);
    bl_session_first_frame();
    bl_session_end_clean();
    bl_exe_expected_sha = BL_TESTED_EXE_SHA;
    bl_account_clear(&p);
    remove(base);
}

/* ---------------- settings: confirm and hidden items ---------------- */

static void test_settings_confirm_and_hide(void)
{
    BlSettingsState st;
    bl_settings_init(&st, NULL);
    BlField f;
    bl_settings_hide(&st, BL_SET_ONLINE);
    bl_settings_hide(&st, BL_SET_ACCOUNT);
    bl_settings_hide(&st, BL_SET_STATS);                          /* never hidden */
    CHECK(bl_settings_listed(&st, BL_SET_STATS));
    CHECK(!bl_settings_listed(&st, BL_SET_ACCOUNT));
    CHECK_EQ(st.item, BL_SET_FRAMERATE);
    bl_settings_input(&st, BL_PAD_DOWN, &f);
    CHECK_EQ(st.item, BL_SET_STATS);
    bl_settings_input(&st, BL_PAD_DOWN, &f);
    CHECK_EQ(st.item, BL_SET_LOGGING);
    bl_settings_input(&st, BL_PAD_DOWN, &f);
    CHECK_EQ(st.item, BL_SET_SUPPORT);                            /* the account skipped */
    bl_settings_input(&st, BL_PAD_CROSS, &f);
    CHECK_EQ(bl_settings_input(&st, BL_PAD_CROSS, &f), BL_SET_EV_BUNDLE);      /* acts at once */
    /* Remove account asks first; any other button cancels. */
    BlSettingsState a;
    bl_settings_init(&a, NULL);
    a.item = BL_SET_ACCOUNT; a.in_panel = 1; a.row = 3;
    CHECK_EQ(bl_settings_input(&a, BL_PAD_CROSS, &f), BL_SET_EV_NONE);
    CHECK_EQ(a.message, BL_S_CONFIRM_ACCOUNT_REMOVE);
    CHECK_EQ(bl_settings_input(&a, BL_PAD_CIRCLE, &f), BL_SET_EV_NONE);      /* cancels, stays in the panel */
    CHECK(a.in_panel);
    CHECK_EQ(a.confirm_row, -1);
    bl_settings_input(&a, BL_PAD_CROSS, &f);
    CHECK_EQ(bl_settings_input(&a, BL_PAD_CROSS, &f), BL_SET_EV_ACCOUNT_REMOVE);
}

static void test_profile_logging_on(void)
{
    char base[700]; snprintf(base, sizeof base, "%s/model-base.ini", files);
    FILE *in = fopen(BL_TEST_DIR "/../assets/blur-files/model-base.ini", "rb"), *out = fopen(base, "wb");
    int ch; while (in && (ch = fgetc(in)) != EOF) fputc(ch, out);
    if (in) fclose(in);
    fclose(out);
    BlConfig c; bl_config_defaults(&c);
    c.logging = 1; c.stats = BL_STATS_FPS;
    BlDecision d; memset(&d, 0, sizeof d);
    CHECK_EQ(bl_profile_compose(&c, 0, files, &d), 0);
    const char *want[][2] = { { "WINEDEBUG", "+tid,err+all,+loaddll" }, { "DXVK_LOG_LEVEL", "info" }, { "BLUR_LOGS", "1" },
                              { "DXVK_PS4_FRAME_LOG", "1" }, { "DXVK_PS4_FPS_HUD", "fps" } };
    for (unsigned w = 0; w < sizeof want / sizeof want[0]; w++) {
        int ok = 0;
        for (unsigned i = 0; i < d.env_count; i++) if (!strcmp(d.env[i].name, want[w][0]) && !strcmp(d.env[i].value, want[w][1])) ok = 1;
        CHECK(ok);
        if (!ok) fprintf(stderr, "  missing %s=%s\n", want[w][0], want[w][1]);
    }
    CHECK(d.env_count <= 13);       /* fits the title's extra[] room (BLUR 1.00 plan Task L9, T1 asserts it) */
}

int main(void)
{
    setup();
    RUN(test_events_ring);
    RUN(test_logs_rotate);
    RUN(test_logs_follow_bl_paths);
    RUN(test_session);
    RUN(test_saves);
    RUN(test_exe_info_and_space);
    RUN(test_bundle_location);
    RUN(test_bundle_never_contains_account);
    RUN(test_bundle_includes_amax_logs);
    RUN(test_bundle_masks_any_user_length);
    RUN(test_bundle_write_error_fails);
    RUN(test_bundle_masks_user_and_lan_ips);
    RUN(test_no_switch_files_written);
    RUN(test_settings_confirm_and_hide);
    RUN(test_profile_logging_on);
    char cmd[400]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd) != 0) fprintf(stderr, "cleanup failed\n");
    return bl_test_report("test_support");
}
