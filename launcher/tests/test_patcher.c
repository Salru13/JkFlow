/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests for the patcher (plan Task 5, Review Focus 1; BLUR 1.00 plan
 * Task L3: subfolders, parking, AMAX set, plugins; Review Focus 1 and 3). */
#define _DEFAULT_SOURCE
#include "bl_fs.h"
#include "bl_patcher.h"
#include "bl_sha256.h"
#include "bl_test.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char root[256], game[300], files[300], backup[300], files2[300], stage[300], files3[300], stage3[300], files4[300], stage4[300];

static void put(const char *dir, const char *name, const char *text)
{
    char p[700]; snprintf(p, sizeof p, "%s/%s", dir, name);
    char d[700]; snprintf(d, sizeof d, "%s", p);
    char *s = strrchr(d, '/'); if (s) { *s = 0; bl_fs_mkdirs(d); }
    FILE *f = fopen(p, "wb"); fputs(text, f); fclose(f);
}

static void hash_text(const char *text, char out[65]) { bl_sha256_hex(text, strlen(text), out); }

static void hash_of(const char *dir, const char *name, char out[65])
{
    char p[700]; snprintf(p, sizeof p, "%s/%s", dir, name);
    if (bl_fs_sha256(p, out, NULL) != 0) out[0] = 0;
}

static int exists(const char *dir, const char *name)
{
    char p[700]; snprintf(p, sizeof p, "%s/%s", dir, name);
    return access(p, F_OK) == 0;
}

static int has_text(const char *dir, const char *name, const char *text)
{
    char h[65], w[65];
    hash_of(dir, name, h); hash_text(text, w);
    return !strcmp(h, w);
}

/* 1 when some regular file under dir (any depth) holds text. */
static int tree_has(const char *dir, const char *text)
{
    char want[65];
    hash_text(text, want);
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[1200]; struct stat st;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (stat(p, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) found = tree_has(p, text);
        else { char h[65]; if (bl_fs_sha256(p, h, NULL) == 0 && !strcmp(h, want)) found = 1; }
    }
    closedir(d);
    return found;
}

static const char *EXE = "MZ fake Blur.exe for tests";
static const char *OURS_A = "our d3d9.dll v7";
static const char *OURS_B = "our d3d9_blur_dxvk.dll s7";
static const char *OURS_C = "our dxvk.conf\nd3d9.maxFrameRate = 60\n";
static const char *OLD_OURS_B = "our d3d9_blur_dxvk.dll s6";
static const char *FOREIGN_A = "somebody else's d3d9.dll";
static int pre_patch_state(void);

/* Second fixture (L3): our files, the AMAX set from a stage folder, parking. */
static const char *V7 = "our V7 amax/dxvk.dll";
static const char *S7 = "our S7 d3d9_blur_dxvk.dll";
static const char *OUR_PLUGINS = "return { require(\"account.account\") }\n";
static const char *AMAX_PLUGINS = "return { require(\"solo.solo\") }\n";
static const char *AMAX_PROXY = "AMAX d3d9.dll proxy";
static const char *AMAX_AUTH = "AMAX amax_auth.asi 1.2";
static const char *AMAX_HOOKS = "AMAX lua_hooks.asi 1.2";

/* Third fixture (owner spec 2026-11-15): offline set, and the online set from
 * the player's two zips (amax_client_files.zip, blur_12.zip) in a stage. */
static const char *EXE12 = "MZ Blur.exe 1.2 from blur_12.zip";
static const char *PAK12 = "gamedata.pak from blur_12.zip";
static const char *SIG12 = "gamedata.sig from blur_12.zip";
static const char *PLAYER_PAK = "the player's own gamedata.pak";

static void setup(void)
{
    snprintf(root, sizeof root, "/tmp/bl_test_patcher_XXXXXX");
    if (!mkdtemp(root)) { perror("mkdtemp"); exit(1); }
    snprintf(game, sizeof game, "%s/game", root);
    snprintf(files, sizeof files, "%s/files", root);
    snprintf(backup, sizeof backup, "%s/backup", root);
    snprintf(files2, sizeof files2, "%s/files2", root);
    snprintf(stage, sizeof stage, "%s/stage", root);
    mkdir(game, 0755); mkdir(files, 0755);
    put(files, "d3d9.dll", OURS_A);
    put(files, "d3d9_blur_dxvk.dll", OURS_B);
    put(files, "dxvk.conf", OURS_C);
    char ha[65], hb[65], hc[65], hob[65], man[2048];
    hash_text(OURS_A, ha); hash_text(OURS_B, hb); hash_text(OURS_C, hc); hash_text(OLD_OURS_B, hob);
    snprintf(man, sizeof man, "# blur launcher manifest 1\nfile d3d9.dll %s %zu\nfile d3d9_blur_dxvk.dll %s %zu\nfile dxvk.conf %s %zu\nknown %s\n",
             ha, strlen(OURS_A), hb, strlen(OURS_B), hc, strlen(OURS_C), hob);
    put(files, "manifest.txt", man);

    put(files2, "d3d9_blur_dxvk.dll", S7);
    put(files2, "amax-files/dxvk.dll", V7);
    put(files2, "amax-overlay/plugins/plugins.lua", OUR_PLUGINS);
    put(stage, "d3d9.dll", AMAX_PROXY);
    put(stage, "amax/dlls/amax_auth.asi", AMAX_AUTH);
    put(stage, "amax/dlls/lua_hooks.asi", AMAX_HOOKS);
    put(stage, "amax/plugins/plugins.lua", AMAX_PLUGINS);
    char h1[65], h2[65], h3[65], h4[65], h5[65], h6[65], h7[65], m2[4096];
    hash_text(S7, h1); hash_text(V7, h2); hash_text(OUR_PLUGINS, h3); hash_text(AMAX_PROXY, h4);
    hash_text(AMAX_AUTH, h5); hash_text(AMAX_HOOKS, h6); hash_text(AMAX_PLUGINS, h7);
    snprintf(m2, sizeof m2,
             "# blur launcher manifest 2\n"
             "file d3d9_blur_dxvk.dll %s %zu\n"
             "file amax/dxvk.dll %s %zu src=amax-files/dxvk.dll\n"
             "file amax/plugins/plugins.lua %s %zu src=amax-overlay/plugins/plugins.lua\n"
             "amax d3d9.dll %s %zu\n"
             "amax amax/dlls/amax_auth.asi %s %zu\n"
             "amax amax/dlls/lua_hooks.asi %s %zu\n"
             "known %s\n"
             "park dinput8.dll\npark dsound.dll\npark scripts\npark texmod\npark *.asi\npark d3d9.dll\npark lua5.1.dll\npark discord-rpc.dll\npark amax\n",
             h1, strlen(S7), h2, strlen(V7), h3, strlen(OUR_PLUGINS), h4, strlen(AMAX_PROXY),
             h5, strlen(AMAX_AUTH), h6, strlen(AMAX_HOOKS), h7);
    put(files2, "manifest.txt", m2);

    snprintf(files3, sizeof files3, "%s/files3", root);
    snprintf(stage3, sizeof stage3, "%s/stage3", root);
    put(files3, "d3d9.dll", V7);                       /* offline: V7 in the root, S7 beside it */
    put(files3, "d3d9_blur_dxvk.dll", S7);
    put(files3, "amax-dxvk/dxvk.dll", V7);             /* online: V7 behind the AMAX proxy */
    put(files3, "amax-overlay/plugins/plugins.lua", OUR_PLUGINS);
    put(stage3, "d3d9.dll", AMAX_PROXY);
    put(stage3, "amax/dlls/amax_auth.asi", AMAX_AUTH);
    put(stage3, "Blur.exe", EXE12);
    put(stage3, "cache/gamedata.pak", PAK12);
    put(stage3, "cache/gamedata.sig", SIG12);
    char e1[65], e2[65], e3[65], m3[4096];
    hash_text(EXE12, e1); hash_text(PAK12, e2); hash_text(SIG12, e3);
    snprintf(m3, sizeof m3,
             "# blur launcher manifest 3\n"
             "file d3d9.dll %s %zu\n"
             "file d3d9_blur_dxvk.dll %s %zu\n"
             "online amax/dxvk.dll %s %zu src=amax-dxvk/dxvk.dll\n"
             "online amax/plugins/plugins.lua %s %zu src=amax-overlay/plugins/plugins.lua\n"
             "amax d3d9.dll %s %zu\n"
             "amax amax/dlls/amax_auth.asi %s %zu\n"
             "blur12 Blur.exe %s %zu\n"
             "blur12 cache/gamedata.pak %s %zu\n"
             "blur12 cache/gamedata.sig %s %zu\n"
             "known %s\n"
             "park dinput8.dll\npark *.asi\npark amax\n",
             h2, strlen(V7), h1, strlen(S7), h2, strlen(V7), h3, strlen(OUR_PLUGINS), h4, strlen(AMAX_PROXY),
             h5, strlen(AMAX_AUTH), e1, strlen(EXE12), e2, strlen(PAK12), e3, strlen(SIG12), h7);
    put(files3, "manifest.txt", m3);
}

static void reset_game(void)
{
    char cmd[1200]; snprintf(cmd, sizeof cmd, "rm -rf '%s' '%s' && mkdir -p '%s'", game, backup, game);
    if (system(cmd) != 0) exit(1);
}

/* How many backups (folders) the backup root holds. */
static int backups(char *last, size_t cap)
{
    DIR *d = opendir(backup);
    int dirs = 0;
    struct dirent *e;
    while (d && (e = readdir(d))) if (e->d_name[0] != '.') { dirs++; if (last) snprintf(last, cap, "%.300s/%.200s", backup, e->d_name); }
    if (d) closedir(d);
    return dirs;
}

static void test_missing_game(void)
{
    reset_game();
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files, NULL, &p), 0);
    CHECK_EQ(p.game, BL_GAME_MISSING);
    CHECK(bl_patch_apply(game, files, NULL, backup, &p, NULL, NULL) < 0);
}

static void test_plan_apply_restore(void)
{
    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "d3d9.dll", FOREIGN_A);           /* different, not ours: parked */
    put(game, "dxvk.conf", OURS_C);             /* already OK */
    /* d3d9_blur_dxvk.dll missing */
    char exe_before[65]; hash_of(game, "Blur.exe", exe_before);

    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files, NULL, &p), 0);
    CHECK_EQ(p.game, BL_GAME_FOUND);
    CHECK(strcmp(p.exe_name, "Blur.exe") == 0);
    CHECK_EQ(p.count, 3);
    CHECK_EQ(p.pending, 2);
    for (unsigned i = 0; i < p.count; i++) {
        if (!strcmp(p.items[i].name, "d3d9.dll")) { CHECK_EQ(p.items[i].state, BL_FILE_DIFFERENT); CHECK_EQ(p.items[i].foreign, 1); }
        if (!strcmp(p.items[i].name, "d3d9_blur_dxvk.dll")) { CHECK_EQ(p.items[i].state, BL_FILE_MISSING); CHECK_EQ(p.items[i].foreign, 0); }
        if (!strcmp(p.items[i].name, "dxvk.conf")) CHECK_EQ(p.items[i].state, BL_FILE_OK);
    }

    CHECK_EQ(bl_patch_apply(game, files, NULL, backup, &p, NULL, NULL), 0);
    BlPatchPlan q;
    CHECK_EQ(bl_patch_plan(game, files, NULL, &q), 0);
    CHECK_EQ(q.pending, 0);
    char h[65], want[65];
    hash_of(game, "d3d9.dll", h); hash_text(OURS_A, want); CHECK(strcmp(h, want) == 0);
    hash_of(game, "Blur.exe", h); CHECK(strcmp(h, exe_before) == 0);

    /* Exactly the foreign file went to the backup root, with its hash. */
    char last[1000] = "";
    CHECK_EQ(backups(last, sizeof last), 1);
    hash_of(last, "d3d9.dll", h); hash_text(FOREIGN_A, want); CHECK(strcmp(h, want) == 0);
    CHECK(!exists(last, "d3d9_blur_dxvk.dll"));
    CHECK(exists(last, "backup.txt"));
    CHECK(!exists(game, ".blur-launcher"));

    /* Second apply: nothing to do, nothing written. */
    struct stat s1, s2;
    char pa[600]; snprintf(pa, sizeof pa, "%s/d3d9.dll", game);
    stat(pa, &s1);
    sleep(1);
    CHECK_EQ(bl_patch_apply(game, files, NULL, backup, &q, NULL, NULL), 0);
    stat(pa, &s2);
    CHECK(s1.st_mtime == s2.st_mtime && s1.st_ino == s2.st_ino);

    /* Restore: foreign file back, added file removed, Blur.exe untouched. */
    CHECK_EQ(bl_patch_restore(game, backup), 0);
    hash_of(game, "d3d9.dll", h); hash_text(FOREIGN_A, want); CHECK(strcmp(h, want) == 0);
    CHECK(!exists(game, "d3d9_blur_dxvk.dll"));
    hash_of(game, "dxvk.conf", h); hash_text(OURS_C, want); CHECK(strcmp(h, want) == 0);
    hash_of(game, "Blur.exe", h); CHECK(strcmp(h, exe_before) == 0);
    CHECK_EQ(bl_patch_restore(game, backup), 1);        /* nothing left to restore */
}

static void test_our_older_version_is_not_foreign(void)
{
    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "d3d9_blur_dxvk.dll", OLD_OURS_B);
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files, NULL, &p), 0);
    for (unsigned i = 0; i < p.count; i++)
        if (!strcmp(p.items[i].name, "d3d9_blur_dxvk.dll")) { CHECK_EQ(p.items[i].state, BL_FILE_DIFFERENT); CHECK_EQ(p.items[i].foreign, 0); }
}

/* Blur.exe is found in any letter case; the patcher itself never reads it
 * (the tested-exe gate is bl_exe, L2). */
static void test_any_blur_is_accepted(void)
{
    const char *names[] = { "Blur.exe", "blur.exe", "BLUR.EXE" };
    for (int i = 0; i < 3; i++) {
        reset_game();
        put(game, names[i], "MZ some other Blur build");
        BlPatchPlan p;
        CHECK_EQ(bl_patch_plan(game, files, NULL, &p), 0);
        CHECK_EQ(p.game, BL_GAME_FOUND);
        CHECK(strcmp(p.exe_name, names[i]) == 0);
        CHECK_EQ(p.exe_size, strlen("MZ some other Blur build"));
        CHECK_EQ(bl_patch_apply(game, files, NULL, backup, &p, NULL, NULL), 0);
        BlPatchPlan q;
        bl_patch_plan(game, files, NULL, &q);
        CHECK_EQ(q.pending, 0);
        CHECK(has_text(game, names[i], "MZ some other Blur build"));
    }
    /* A folder called Blur.exe is not the game. */
    reset_game();
    char d[600]; snprintf(d, sizeof d, "%s/Blur.exe", game); mkdir(d, 0755);
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files, NULL, &p), 0);
    CHECK_EQ(p.game, BL_GAME_MISSING);
}

/* M13: backups are undone newest first by their counter, not by the clock. */
static void test_backup_order_ignores_clock(void)
{
    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "d3d9.dll", FOREIGN_A);
    BlPatchPlan p;
    bl_patch_plan(game, files, NULL, &p);
    CHECK_EQ(bl_patch_apply(game, files, NULL, backup, &p, NULL, NULL), 0);
    char last[64] = "";
    CHECK_EQ(bl_patch_last_backup(backup, last, sizeof last), 1);
    CHECK(last[0] != 0);
    /* Make the first backup look like it came from the future. */
    char from[900], to[900];
    snprintf(from, sizeof from, "%s/%s", backup, last);
    char *dash = strchr(last, '-');
    CHECK(dash != NULL);
    if (!dash) return;
    snprintf(to, sizeof to, "%s/%.*s-29991231T235959Z", backup, (int)(dash - last), last);
    CHECK_EQ(rename(from, to), 0);
    put(game, "d3d9.dll", "a second foreign d3d9.dll");
    bl_patch_plan(game, files, NULL, &p);
    CHECK_EQ(bl_patch_apply(game, files, NULL, backup, &p, NULL, NULL), 0);
    CHECK_EQ(bl_patch_restore(game, backup), 0);
    CHECK(pre_patch_state());                        /* the very first d3d9.dll is back */
    CHECK_EQ(bl_patch_last_backup(backup, last, sizeof last), 0);
    char label[64];
    bl_backup_label("000003-20261113T120501Z", label, sizeof label);
    CHECK(strcmp(label, "2026-11-13 12:05 UTC") == 0);
}

/* Fault injection: the Nth write fails (full disk), or the rename fails (power cut). */
static int g_writes, g_fail_at;
static long failing_write(int fd, const void *buf, size_t n)
{
    if (++g_writes == g_fail_at) return -1;
    return (long)write(fd, buf, n);
}
static int g_renames, g_rename_fail_at;
static int failing_rename(const char *a, const char *b)
{
    if (++g_renames == g_rename_fail_at) return -1;
    return rename(a, b);
}

static int pre_patch_state(void)
{
    char h[65], want[65];
    hash_of(game, "d3d9.dll", h); hash_text(FOREIGN_A, want);
    return !strcmp(h, want) && !exists(game, "d3d9_blur_dxvk.dll") && !exists(game, "dxvk.conf");
}

/* Review I2 + I1: after any fault, restore works; after a fault and a
 * finishing rerun, one restore returns the folder to its pre-patch state. */
static void test_restore_after_faults(void)
{
    for (int fail = 1; fail <= 8; fail++) {
        for (int kind = 0; kind < 2; kind++) {
            for (int rerun = 0; rerun < 2; rerun++) {
                reset_game();
                put(game, "Blur.exe", EXE);
                put(game, "d3d9.dll", FOREIGN_A);
                BlPatchPlan p;
                bl_patch_plan(game, files, NULL, &p);
                g_writes = g_renames = 0;
                if (kind == 0) { g_fail_at = fail; bl_fs_write = failing_write; }
                else { g_rename_fail_at = fail; bl_fs_rename = failing_rename; }
                bl_patch_apply(game, files, NULL, backup, &p, NULL, NULL);
                g_fail_at = g_rename_fail_at = 0;
                if (rerun) {
                    BlPatchPlan q;
                    bl_patch_plan(game, files, NULL, &q);
                    CHECK_EQ(bl_patch_apply(game, files, NULL, backup, &q, NULL, NULL), 0);
                }
                int r = bl_patch_restore(game, backup);
                CHECK(r == 0 || r == 1);
                CHECK(pre_patch_state());
                if (!pre_patch_state()) fprintf(stderr, "  fail=%d kind=%d rerun=%d restore=%d\n", fail, kind, rerun, r);
                CHECK_EQ(bl_patch_restore(game, backup), 1);
            }
        }
    }
    bl_fs_write = bl_fs_write_default;
    bl_fs_rename = rename;
}

/* Review I1: a package update (second apply) and then one restore. */
static void test_two_applies_one_restore(void)
{
    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "d3d9.dll", FOREIGN_A);
    BlPatchPlan p;
    bl_patch_plan(game, files, NULL, &p);
    CHECK_EQ(bl_patch_apply(game, files, NULL, backup, &p, NULL, NULL), 0);
    char mp[600]; snprintf(mp, sizeof mp, "%s/manifest.txt", files);
    char saved[2048]; FILE *f = fopen(mp, "rb"); size_t n = fread(saved, 1, sizeof saved - 1, f); saved[n] = 0; fclose(f);
    const char *v2 = "our d3d9.dll v8";
    char h1[65], h2[65], man[2600];
    hash_text(OURS_A, h1); hash_text(v2, h2);
    put(files, "d3d9.dll", v2);
    char *line = strstr(saved, "file d3d9.dll ");
    char *end = strchr(line, '\n');
    snprintf(man, sizeof man, "%.*sfile d3d9.dll %s %zu%sknown %s\n", (int)(line - saved), saved, h2, strlen(v2), end, h1);
    put(files, "manifest.txt", man);
    bl_patch_plan(game, files, NULL, &p);
    CHECK_EQ(p.pending, 1);
    CHECK_EQ(bl_patch_apply(game, files, NULL, backup, &p, NULL, NULL), 0);
    CHECK_EQ(bl_patch_restore(game, backup), 0);
    CHECK(pre_patch_state());
    CHECK_EQ(bl_patch_restore(game, backup), 1);
    put(files, "manifest.txt", saved);
    put(files, "d3d9.dll", OURS_A);
}

static void test_refuse_dotdot_and_leading_dot(void)
{
    const char *bad[] = { "../evil", "a/../b", "/abs.dll", "sub/.hidden", ".hidden", "amax/./x", "a\\b",
                          "Blur.exe", "BLUR.EXE", "sub/Blur.exe", "a//b", "trailing/", "" };
    char saved[4096];
    char mp[600]; snprintf(mp, sizeof mp, "%s/manifest.txt", files);
    FILE *f = fopen(mp, "rb"); size_t n = fread(saved, 1, sizeof saved - 1, f); saved[n] = 0; fclose(f);
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        const char *kinds[] = { "file", "amax" };
        for (int k = 0; k < 2; k++) {
            char m[4500];
            snprintf(m, sizeof m, "%s%s %s 0000000000000000000000000000000000000000000000000000000000000000 4\n", saved, kinds[k], bad[i]);
            put(files, "manifest.txt", m);
            reset_game();
            put(game, "Blur.exe", EXE);
            BlPatchPlan p;
            CHECK(bl_patch_plan(game, files, NULL, &p) < 0);
            if (bl_patch_plan(game, files, NULL, &p) >= 0) fprintf(stderr, "  accepted %s %s\n", kinds[k], bad[i]);
        }
    }
    /* a src= outside the package and a park glob that climbs out are refused too */
    const char *bad_lines[] = { "file x.dll 0000000000000000000000000000000000000000000000000000000000000000 4 src=../x\n",
                                "park ../evil\n", "park /data\n", "park a/b\n" };
    for (size_t i = 0; i < sizeof bad_lines / sizeof bad_lines[0]; i++) {
        char m[4500];
        snprintf(m, sizeof m, "%s%s", saved, bad_lines[i]);
        put(files, "manifest.txt", m);
        BlPatchPlan p;
        CHECK(bl_patch_plan(game, files, NULL, &p) < 0);
    }
    /* a subfolder name is fine */
    char m[4500];
    snprintf(m, sizeof m, "%sfile sub/dir.dll 0000000000000000000000000000000000000000000000000000000000000000 4\n", saved);
    put(files, "manifest.txt", m);
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files, NULL, &p), 0);
    put(files, "manifest.txt", saved);
}

typedef struct Seen { unsigned calls, last_done, total; } Seen;

static void cb(void *ctx, const char *name, unsigned done, unsigned total)
{
    Seen *s = ctx;
    (void)name;
    s->calls++; s->last_done = done; s->total = total;
}

static void test_progress_callback(void)
{
    reset_game();
    put(game, "Blur.exe", EXE);
    BlPatchPlan p;
    bl_patch_plan(game, files, NULL, &p);
    Seen seen = { 0, 0, 0 };
    CHECK_EQ(bl_patch_apply(game, files, NULL, backup, &p, cb, &seen), 0);
    CHECK_EQ(seen.total, 3);            /* all three files are missing */
    CHECK(seen.last_done == seen.total);
    CHECK(seen.calls >= seen.total);
}

/* The native shader cache is put in place only when missing or header-only,
 * never overwritten when the game has grown it, never backed up and never
 * removed by restore. */
static void test_seed_never_overwritten(void)
{
    static const char *SEED = "DXVK\x0a\x01\x02\x03\x04\x05\x06\x07 frozen 2663 entries";
    char seeds[400], man[1024], hs[65], ha[65];
    snprintf(seeds, sizeof seeds, "%s/files-seed", root);
    mkdir(seeds, 0755);
    put(seeds, "d3d9.dll", OURS_A);
    put(seeds, "dxvk-native.dxvk-cache", SEED);
    hash_text(SEED, hs); hash_text(OURS_A, ha);
    snprintf(man, sizeof man, "# blur launcher manifest 1\nfile d3d9.dll %s %zu\nseed dxvk-native.dxvk-cache %s %zu\n",
             ha, strlen(OURS_A), hs, strlen(SEED));
    put(seeds, "manifest.txt", man);

    reset_game();
    put(game, "Blur.exe", EXE);
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, seeds, NULL, &p), 0);
    CHECK_EQ(p.pending, 2);
    CHECK(p.items[1].seed && p.items[1].state == BL_FILE_MISSING);
    CHECK_EQ(bl_patch_apply(game, seeds, NULL, backup, &p, NULL, NULL), 0);
    char got[65]; hash_of(game, "dxvk-native.dxvk-cache", got);
    CHECK(!strcmp(got, hs));
    CHECK_EQ(bl_patch_plan(game, seeds, NULL, &p), 0);
    CHECK_EQ(p.pending, 0);
    CHECK_EQ(bl_patch_restore(game, backup), 0);
    CHECK(exists(game, "dxvk-native.dxvk-cache"));       /* restore keeps the cache */
    CHECK(!exists(game, "d3d9.dll"));                    /* but removes what patch added */

    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "dxvk-native.dxvk-cache", "DXVK\x0a\x01\x02\x03 grown by the game, 3100 entries");
    CHECK_EQ(bl_patch_plan(game, seeds, NULL, &p), 0);
    CHECK_EQ(p.pending, 1);
    CHECK_EQ(p.items[1].state, BL_FILE_OK);
    CHECK_EQ(bl_patch_apply(game, seeds, NULL, backup, &p, NULL, NULL), 0);
    CHECK(has_text(game, "dxvk-native.dxvk-cache", "DXVK\x0a\x01\x02\x03 grown by the game, 3100 entries"));

    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "dxvk-native.dxvk-cache", "DXVK\x0a\x01\x01\x01\x01\x01\x01\x01");
    CHECK_EQ(bl_patch_plan(game, seeds, NULL, &p), 0);
    CHECK_EQ(p.items[1].state, BL_FILE_MISSING);
    CHECK_EQ(bl_patch_apply(game, seeds, NULL, backup, &p, NULL, NULL), 0);
    hash_of(game, "dxvk-native.dxvk-cache", got);
    CHECK(!strcmp(got, hs));

    reset_game();
    put(game, "Blur.exe", EXE);
    CHECK_EQ(bl_patch_plan(game, seeds, NULL, &p), 0);
    put(game, "dxvk-native.dxvk-cache", "DXVK\x0a\x01\x02\x03 written by a race meanwhile");
    CHECK_EQ(bl_patch_apply(game, seeds, NULL, backup, &p, NULL, NULL), 0);
    CHECK(has_text(game, "dxvk-native.dxvk-cache", "DXVK\x0a\x01\x02\x03 written by a race meanwhile"));
}

/* "cache" lines (2026-11-15): the engine reads its state cache from
 * DXVK_STATE_CACHE_PATH = the launcher cache folder (bl_paths), so the seed goes there, never into
 * the game folder; same rule as seed (only when missing or header-only). */
static void test_cache_seed_goes_to_cache_dir(void)
{
    static const char *SEED = "DXVK\x0a\x01\x02\x03\x04\x05\x06\x07 frozen 2663 entries";
    char fdir[400], cdir[400], man[1024], hs[65], ha[65];
    snprintf(fdir, sizeof fdir, "%s/files-cache", root);
    snprintf(cdir, sizeof cdir, "%s/cachedir", root);
    mkdir(fdir, 0755);
    put(fdir, "d3d9.dll", OURS_A);
    put(fdir, "dxvk-native.dxvk-cache", SEED);
    hash_text(SEED, hs); hash_text(OURS_A, ha);
    snprintf(man, sizeof man, "# blur launcher manifest 1\nfile d3d9.dll %s %zu\ncache dxvk-native.dxvk-cache %s %zu\n",
             ha, strlen(OURS_A), hs, strlen(SEED));
    put(fdir, "manifest.txt", man);

    /* The plan and apply ignore cache lines: nothing lands in the game folder. */
    reset_game();
    put(game, "Blur.exe", EXE);
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, fdir, NULL, &p), 0);
    CHECK_EQ(p.count, 1);
    CHECK_EQ(bl_patch_apply(game, fdir, NULL, backup, &p, NULL, NULL), 0);
    CHECK(!exists(game, "dxvk-native.dxvk-cache"));

    /* Missing: installed (the folder is created), verified. */
    char cmd[900]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", cdir);
    if (system(cmd) != 0) exit(1);
    CHECK_EQ(bl_patch_seed_cache(fdir, cdir), 1);
    char got[65]; hash_of(cdir, "dxvk-native.dxvk-cache", got);
    CHECK(!strcmp(got, hs));
    /* Grown by the game: kept. */
    put(cdir, "dxvk-native.dxvk-cache", "DXVK\x0a\x01\x02\x03 grown by the game, 3100 entries");
    CHECK_EQ(bl_patch_seed_cache(fdir, cdir), 0);
    CHECK(has_text(cdir, "dxvk-native.dxvk-cache", "DXVK\x0a\x01\x02\x03 grown by the game, 3100 entries"));
    /* Header only (12 bytes): replaced by the seed. */
    put(cdir, "dxvk-native.dxvk-cache", "DXVK\x0a\x01\x01\x01\x01\x01\x01\x01");
    CHECK_EQ(bl_patch_seed_cache(fdir, cdir), 1);
    hash_of(cdir, "dxvk-native.dxvk-cache", got);
    CHECK(!strcmp(got, hs));
    /* A damaged package copy is refused and leaves the cache folder alone. */
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", cdir);
    if (system(cmd) != 0) exit(1);
    put(fdir, "dxvk-native.dxvk-cache", "DXVK damaged copy");
    CHECK(bl_patch_seed_cache(fdir, cdir) < 0);
    CHECK(!exists(cdir, "dxvk-native.dxvk-cache"));
    /* No cache lines: nothing to do. */
    CHECK_EQ(bl_patch_seed_cache(files, cdir), 0);
}

/* ---------------- L3: subfolders, parking, the AMAX set ---------------- */

static void check_pinned_set(void)
{
    CHECK(has_text(game, "d3d9_blur_dxvk.dll", S7));
    CHECK(has_text(game, "amax/dxvk.dll", V7));
    CHECK(has_text(game, "amax/plugins/plugins.lua", OUR_PLUGINS));
    CHECK(has_text(game, "d3d9.dll", AMAX_PROXY));
    CHECK(has_text(game, "amax/dlls/amax_auth.asi", AMAX_AUTH));
    CHECK(has_text(game, "amax/dlls/lua_hooks.asi", AMAX_HOOKS));
}

static void test_subfolder_install(void)
{
    reset_game();
    put(game, "Blur.exe", EXE);
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files2, stage, &p), 0);
    CHECK_EQ(p.count, 6);
    int amax_items = 0;
    for (unsigned i = 0; i < p.count; i++) amax_items += p.items[i].amax;
    CHECK_EQ(amax_items, 3);
    CHECK_EQ(p.pending, 6);
    CHECK_EQ(bl_patch_apply(game, files2, stage, backup, &p, NULL, NULL), 0);
    check_pinned_set();
    CHECK_EQ(bl_patch_plan(game, files2, stage, &p), 0);
    CHECK_EQ(p.pending, 0);
    CHECK_EQ(p.park_count, 0);
    /* online without an AMAX source: the AMAX items cannot be installed */
    reset_game();
    put(game, "Blur.exe", EXE);
    CHECK_EQ(bl_patch_plan(game, files2, BL_PATCH_IN_PLACE, &p), 0);
    CHECK(bl_patch_apply(game, files2, BL_PATCH_IN_PLACE, backup, &p, NULL, NULL) < 0);
    CHECK(strstr(bl_patch_error, "amax") != NULL);
}

/* Review Focus 1: a copy that already has AMAX or mod files. */
static void test_park_foreign_amax_tree(void)
{
    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "amax/dlls/amax_auth.asi", "an older amax_auth.asi");
    put(game, "amax/dlls/old_extra.asi", "an asi AMAX 1.2 does not have");
    put(game, "amax/init.lua", "an older init.lua");
    put(game, "d3d9.dll", "a foreign d3d9.dll ASI loader");
    put(game, "dinput8.dll", "Ultimate ASI Loader");
    put(game, "scripts/x.asi", "a mod");
    put(game, "root.asi", "an asi in the root");
    put(game, "Blur.ini", "the game's own file: never touched");
    put(game, "amax/log/amax_auth.log", "a runtime log: not parked");
    put(game, "amax/plugins/account_cfg.lua", "return {} -- written by the launcher at Play");
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files2, stage, &p), 0);
    CHECK_EQ(p.park_count, 7);         /* auth, old_extra, init.lua, d3d9.dll, dinput8, scripts/x.asi, root.asi */
    CHECK(p.pending >= p.park_count);
    CHECK_EQ(bl_patch_apply(game, files2, stage, backup, &p, NULL, NULL), 0);
    check_pinned_set();
    const char *gone[] = { "amax/dlls/old_extra.asi", "amax/init.lua", "dinput8.dll", "scripts/x.asi", "root.asi" };
    for (unsigned i = 0; i < 5; i++) CHECK(!exists(game, gone[i]));
    CHECK(has_text(game, "Blur.ini", "the game's own file: never touched"));
    CHECK(has_text(game, "amax/log/amax_auth.log", "a runtime log: not parked"));
    CHECK(has_text(game, "amax/plugins/account_cfg.lua", "return {} -- written by the launcher at Play"));
    /* All of them are in one backup, with their relative paths kept. */
    char last[1000] = "";
    CHECK_EQ(backups(last, sizeof last), 1);
    CHECK(has_text(last, "amax/dlls/amax_auth.asi", "an older amax_auth.asi"));
    CHECK(has_text(last, "amax/dlls/old_extra.asi", "an asi AMAX 1.2 does not have"));
    CHECK(has_text(last, "amax/init.lua", "an older init.lua"));
    CHECK(has_text(last, "d3d9.dll", "a foreign d3d9.dll ASI loader"));
    CHECK(has_text(last, "dinput8.dll", "Ultimate ASI Loader"));
    CHECK(has_text(last, "scripts/x.asi", "a mod"));
    CHECK(has_text(last, "root.asi", "an asi in the root"));
    CHECK_EQ(bl_patch_plan(game, files2, stage, &p), 0);
    CHECK_EQ(p.pending, 0);
    CHECK_EQ(p.park_count, 0);
    /* Restore puts the player's mods back. */
    CHECK_EQ(bl_patch_restore(game, backup), 0);
    CHECK(has_text(game, "dinput8.dll", "Ultimate ASI Loader"));
    CHECK(has_text(game, "scripts/x.asi", "a mod"));
    CHECK(has_text(game, "amax/dlls/amax_auth.asi", "an older amax_auth.asi"));
    CHECK(!exists(game, "amax/dxvk.dll"));
}

/* Audit 21: up to BL_PATCH_MAX_PARK (4096) foreign files are parked and come
 * back byte for byte at Restore; one more and Patch refuses before any change. */
static void tree_sum(const char *dir, char *out, size_t cap)
{
    char cmd[1200];
    snprintf(cmd, sizeof cmd, "cd '%s' && find . -type f | LC_ALL=C sort | xargs -d '\\n' sha256sum | sha256sum", dir);
    out[0] = 0;
    FILE *p = popen(cmd, "r");
    if (p) { if (!fgets(out, (int)cap, p)) out[0] = 0; pclose(p); }
}

static void many_foreign(int n)
{
    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "Blur.ini", "the game's own file");
    char name[64], text[64];
    for (int i = 0; i < n; i++) {
        snprintf(name, sizeof name, "%s/mod%04d.asi", i % 2 ? "scripts" : "amax/dlls", i);
        snprintf(text, sizeof text, "mod %d of %d", i, n);
        put(game, name, text);
    }
}

static void test_park_many_foreign(void)
{
    BlPatchPlan *p = malloc(sizeof *p);
    char before[100], after[100];
    static const int ok_counts[] = { 300, 4096 };
    for (unsigned k = 0; k < 2; k++) {
        int n = ok_counts[k];
        many_foreign(n);
        tree_sum(game, before, sizeof before);
        CHECK_EQ(bl_patch_plan(game, files2, stage, p), 0);
        CHECK_EQ(p->park_total, (unsigned)n);
        CHECK_EQ(bl_patch_apply(game, files2, stage, backup, p, NULL, NULL), 0);
        CHECK(!exists(game, "scripts/mod0001.asi") && !exists(game, "amax/dlls/mod0000.asi"));
        CHECK_EQ(bl_patch_plan(game, files2, stage, p), 0);
        CHECK_EQ(p->pending, 0);
        CHECK_EQ(bl_patch_restore(game, backup), 0);
        CHECK(has_text(game, "scripts/mod0001.asi", n == 300 ? "mod 1 of 300" : "mod 1 of 4096"));
        tree_sum(game, after, sizeof after);
        CHECK(before[0] && !strcmp(before, after));
        if (strcmp(before, after)) fprintf(stderr, "  %d foreign files: not byte-identical after Restore\n", n);
    }
    /* 4097: refused, nothing changed, no backup made */
    many_foreign(BL_PATCH_MAX_PARK + 1);
    tree_sum(game, before, sizeof before);
    CHECK_EQ(bl_patch_plan(game, files2, stage, p), 0);
    CHECK_EQ(p->park_total, BL_PATCH_MAX_PARK + 1);
    CHECK(p->park_over[0] != 0);
    CHECK(bl_patch_apply(game, files2, stage, backup, p, NULL, NULL) < 0);
    CHECK(!strncmp(bl_patch_error, "too-many-files ", 15));
    tree_sum(game, after, sizeof after);
    CHECK(before[0] && !strcmp(before, after));
    CHECK_EQ(backups(NULL, 0), 0);
    free(p);
}

/* Audit 22: a file Patch cannot read would be moved but not listed in
 * backup.txt (Restore would never bring it back). Patch stops before moving
 * anything and names the file. */
static void test_park_unreadable_stops(void)
{
    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "scripts/a.asi", "a readable mod");
    put(game, "scripts/b.asi", "a mod Patch cannot read");
    char before[100], after[100], p[700];
    tree_sum(game, before, sizeof before);
    snprintf(p, sizeof p, "%s/scripts/b.asi", game);
    chmod(p, 0);
    BlPatchPlan *plan = malloc(sizeof *plan);
    CHECK_EQ(bl_patch_plan(game, files2, stage, plan), 0);
    CHECK(bl_patch_apply(game, files2, stage, backup, plan, NULL, NULL) < 0);
    CHECK(!strncmp(bl_patch_error, "unreadable ", 11));
    CHECK(!strcmp(bl_patch_error_name, "scripts/b.asi"));
    CHECK_EQ(backups(NULL, 0), 0);
    chmod(p, 0644);
    tree_sum(game, after, sizeof after);
    CHECK(before[0] && !strcmp(before, after));
    free(plan);
}

/* Letter case does not hide a foreign loader from parking (Wine ignores case). */
static void test_park_ignores_case(void)
{
    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "DINPUT8.DLL", "loader in upper case");
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files2, stage, &p), 0);
    CHECK_EQ(p.park_count, 1);
    CHECK_EQ(bl_patch_apply(game, files2, stage, backup, &p, NULL, NULL), 0);
    CHECK(!exists(game, "DINPUT8.DLL"));
}

static void test_ours_not_parked(void)
{
    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "d3d9_blur_dxvk.dll", OURS_B);        /* not ours in files2 ... */
    char mp[600]; snprintf(mp, sizeof mp, "%s/manifest.txt", files2);
    char saved[4096]; FILE *f = fopen(mp, "rb"); size_t n = fread(saved, 1, sizeof saved - 1, f); saved[n] = 0; fclose(f);
    char hk[65], m[4300]; hash_text(OURS_B, hk);
    snprintf(m, sizeof m, "%sknown %s\n", saved, hk);  /* ... until a known line says it is an older one of ours */
    put(files2, "manifest.txt", m);
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files2, stage, &p), 0);
    CHECK_EQ(p.park_count, 0);
    CHECK_EQ(bl_patch_apply(game, files2, stage, backup, &p, NULL, NULL), 0);
    CHECK(has_text(game, "d3d9_blur_dxvk.dll", S7));
    char last[1000] = "";
    backups(last, sizeof last);
    CHECK(!tree_has(backup, OURS_B));                  /* replaced without a backup */
    put(files2, "manifest.txt", saved);
}

static void test_plugins_lua_replaced(void)
{
    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "amax/plugins/plugins.lua", AMAX_PLUGINS);   /* AMAX's own list (known hash) */
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files2, stage, &p), 0);
    for (unsigned i = 0; i < p.count; i++)
        if (!strcmp(p.items[i].name, "amax/plugins/plugins.lua")) { CHECK_EQ(p.items[i].state, BL_FILE_DIFFERENT); CHECK_EQ(p.items[i].foreign, 0); }
    CHECK_EQ(bl_patch_apply(game, files2, stage, backup, &p, NULL, NULL), 0);
    CHECK(has_text(game, "amax/plugins/plugins.lua", OUR_PLUGINS));
    CHECK(!tree_has(backup, AMAX_PLUGINS));
}

/* Owner finding 25 (2026-11-15): JkFlow ships no AMAX file. AMAX's own
 * plugins.lua (from the player's zip) gets our account plugin added and
 * "solo" and "fps" commented out at Patch time; the manifest pins both the
 * file in the zip and the edited result ("edit=<sha256>"). */
static const char *AMAX_LIST =
    "return {\r\n\trequire(\"default.resprays\"),\r\n\trequire(\"default.laps\"),\r\n\trequire(\"default.block_popups\"),\r\n"
    "\t-- require(\"hello.hello\"),\r\n\t-- require(\"foo.foo\"),\r\n\trequire(\"solo.solo\"),\r\n\trequire(\"fps.fps\"),\r\n"
    "\trequire(\"revs.revs\"),\r\n}\r\n";
static const char *AMAX_LIST_EDITED =
    "return {\r\n\trequire(\"default.resprays\"),\r\n\trequire(\"default.laps\"),\r\n\trequire(\"default.block_popups\"),\r\n"
    "\t-- require(\"hello.hello\"),\r\n\t-- require(\"foo.foo\"),\r\n\t-- require(\"solo.solo\"),\r\n\t-- require(\"fps.fps\"),\r\n"
    "\trequire(\"revs.revs\"),\r\n\trequire(\"account.account\"),\r\n}\r\n";
static const char *ACCOUNT_PLUGIN = "-- our account plugin";

static void edit_fixture(void)
{
    snprintf(files4, sizeof files4, "%s/files4", root);
    snprintf(stage4, sizeof stage4, "%s/stage4", root);
    put(files4, "amax-overlay/plugins/account/account.lua", ACCOUNT_PLUGIN);
    put(stage4, "d3d9.dll", AMAX_PROXY);
    put(stage4, "amax/plugins/plugins.lua", AMAX_LIST);
    char h1[65], h2[65], h3[65], h4[65], m[2048];
    hash_text(ACCOUNT_PLUGIN, h1); hash_text(AMAX_PROXY, h2); hash_text(AMAX_LIST, h3); hash_text(AMAX_LIST_EDITED, h4);
    snprintf(m, sizeof m,
             "# blur launcher manifest 2\n"
             "online amax/plugins/account/account.lua %s %zu src=amax-overlay/plugins/account/account.lua\n"
             "amax d3d9.dll %s %zu\n"
             "amax amax/plugins/plugins.lua %s %zu edit=%s\n"
             "park amax\n",
             h1, strlen(ACCOUNT_PLUGIN), h2, strlen(AMAX_PROXY), h3, strlen(AMAX_LIST), h4);
    put(files4, "manifest.txt", m);
}

static void test_plugins_edit_text(void)
{
    char out[2048];
    long n = bl_patch_plugins_edit(AMAX_LIST, strlen(AMAX_LIST), out, sizeof out);
    CHECK_EQ(n, (long)strlen(AMAX_LIST_EDITED));
    CHECK(n > 0 && !memcmp(out, AMAX_LIST_EDITED, (size_t)n));
    if (n > 0 && memcmp(out, AMAX_LIST_EDITED, (size_t)n)) fprintf(stderr, "  got: %.*s\n", (int)n, out);
    /* Already edited: unchanged (a second Patch changes nothing). */
    char again[2048];
    CHECK_EQ(bl_patch_plugins_edit(out, (size_t)n, again, sizeof again), n);
    CHECK(!memcmp(again, out, (size_t)n));
    /* Unix line ends stay Unix; the list without a closing brace is refused. */
    const char *lf = "return {\n  require(\"solo.solo\"),\n  require(\"x.x\"),\n}\n";
    n = bl_patch_plugins_edit(lf, strlen(lf), out, sizeof out);
    const char *lf_want = "return {\n  -- require(\"solo.solo\"),\n  require(\"x.x\"),\n  require(\"account.account\"),\n}\n";
    CHECK(n == (long)strlen(lf_want) && !memcmp(out, lf_want, strlen(lf_want)));
    CHECK_EQ(bl_patch_plugins_edit("return { require(\"x.x\")", 23, out, sizeof out), -1);
    CHECK_EQ(bl_patch_plugins_edit(AMAX_LIST, strlen(AMAX_LIST), out, 20), -1);         /* too small */
}

static void test_plugins_lua_edited_at_patch(void)
{
    edit_fixture();
    reset_game();
    put(game, "Blur.exe", EXE);
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files4, stage4, &p), 0);
    CHECK_EQ(bl_patch_apply(game, files4, stage4, backup, &p, NULL, NULL), 0);
    CHECK(has_text(game, "amax/plugins/plugins.lua", AMAX_LIST_EDITED));
    CHECK(has_text(game, "amax/plugins/account/account.lua", ACCOUNT_PLUGIN));
    CHECK_EQ(bl_patch_plan(game, files4, BL_PATCH_IN_PLACE, &p), 0);
    CHECK_EQ(p.pending, 0);                                   /* the edited list counts as in place */
    CHECK_EQ(bl_patch_online_state(game, files4), BL_ONLINE_READY);
    CHECK_EQ(bl_patch_restore(game, backup), 0);             /* added: Restore removes it */
    CHECK(!exists(game, "amax/plugins/plugins.lua"));
    /* AMAX's own list already in the folder: parked (backup), Restore puts it back. */
    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "amax/plugins/plugins.lua", AMAX_LIST);
    CHECK_EQ(bl_patch_plan(game, files4, stage4, &p), 0);
    for (unsigned i = 0; i < p.count; i++)
        if (!strcmp(p.items[i].name, "amax/plugins/plugins.lua")) { CHECK_EQ(p.items[i].state, BL_FILE_DIFFERENT); CHECK_EQ(p.items[i].foreign, 1); }
    CHECK_EQ(bl_patch_apply(game, files4, stage4, backup, &p, NULL, NULL), 0);
    CHECK(has_text(game, "amax/plugins/plugins.lua", AMAX_LIST_EDITED));
    CHECK(tree_has(backup, AMAX_LIST));
    CHECK_EQ(bl_patch_restore(game, backup), 0);
    CHECK(has_text(game, "amax/plugins/plugins.lua", AMAX_LIST));
    /* A stage list that is not AMAX's (another zip) is refused, nothing half-done. */
    reset_game();
    put(game, "Blur.exe", EXE);
    put(stage4, "amax/plugins/plugins.lua", "return { }\n");
    CHECK_EQ(bl_patch_plan(game, files4, stage4, &p), 0);
    CHECK(bl_patch_apply(game, files4, stage4, backup, &p, NULL, NULL) < 0);
    CHECK(!exists(game, "amax/plugins/plugins.lua"));
    put(stage4, "amax/plugins/plugins.lua", AMAX_LIST);
}

/* Review finding 2: PKG 286 (JkFlow before round 2) shipped its own
 * amax/plugins/plugins.lua. The owner's Slim was online-patched with it. */
static const char *PKG286_PLUGINS =
    "-- BLUR: the AMAX client's plugin list, installed by Patch over AMAX's own\n"
    "-- amax/plugins/plugins.lua. Changes from AMAX's list: \"solo\" and \"fps\" are\n"
    "-- off (the bench-tested state), and the BLUR account plugin is added.\n"
    "return {\n\trequire(\"default.resprays\"),\n\trequire(\"default.laps\"),\n\trequire(\"default.block_popups\"),\n"
    "\t-- require(\"hello.hello\"),\n\t-- require(\"foo.foo\"),\n\t-- require(\"solo.solo\"),\n\t-- require(\"fps.fps\"),\n"
    "\trequire(\"revs.revs\"),\n\trequire(\"account.account\"),\n}\n";
#define PKG286_PLUGINS_SHA "425c88668242071199db76e7a18d671d26a0ad7b607e84c1facfb96b3148342a"

/* 1 when some file under dir requires account.account (needs the plugin). */
static int tree_requires_account(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[1200], buf[8192]; struct stat st;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (stat(p, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { found = tree_requires_account(p); continue; }
        long n = bl_fs_read(p, buf, sizeof buf - 1);
        if (n > 0) { buf[n] = 0; found = strstr(buf, "require(\"account.account\")") != NULL; }
    }
    closedir(d);
    return found;
}

static void test_pkg286_plugins_repatch_restore(void)
{
    char h[65];
    hash_text(PKG286_PLUGINS, h);
    CHECK(!strcmp(h, PKG286_PLUGINS_SHA));                     /* the bytes PKG 286 installed */
    /* The package's known list (make_package_inputs.sh) must hold it. */
    char cmd[1200], known[4096] = "", line[128];
    snprintf(cmd, sizeof cmd, "awk '/^printf .known /{on=1} on{print} /> \"\\$known\"/{exit}' '%s' | grep -oE '[0-9a-f]{64}'",
             BL_TEST_DIR "/../../title/make_package_inputs.sh");
    FILE *pp = popen(cmd, "r");
    size_t used = 0;
    while (pp && fgets(line, sizeof line, pp)) used += (size_t)snprintf(known + used, sizeof known - used, "known %s", line);
    if (pp) pclose(pp);
    CHECK(strstr(known, "known " PKG286_PLUGINS_SHA "\n") != NULL);
    /* The online set as round 2 builds it, with that known list. */
    edit_fixture();
    char files5[400], man[8192], m4[2048] = "";
    snprintf(files5, sizeof files5, "%s/files5", root);
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && cp -r '%s' '%s'", files5, files4, files5);
    CHECK(system(cmd) == 0);
    CHECK(bl_fs_read(strcat(strcpy(man, files4), "/manifest.txt"), m4, sizeof m4 - 1) > 0);
    snprintf(man, sizeof man, "%s%s", m4, known);
    put(files5, "manifest.txt", man);
    /* A folder patched online by 286 (AMAX already there: 286 overwrote
     * AMAX's list, its backup does not hold it). */
    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "d3d9.dll", AMAX_PROXY);
    put(game, "amax/plugins/plugins.lua", PKG286_PLUGINS);
    put(game, "amax/plugins/account/account.lua", ACCOUNT_PLUGIN);
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files5, stage4, &p), 0);
    for (unsigned i = 0; i < p.count; i++)
        if (!strcmp(p.items[i].name, "amax/plugins/plugins.lua")) { CHECK_EQ(p.items[i].state, BL_FILE_DIFFERENT); CHECK_EQ(p.items[i].foreign, 0); }
    CHECK_EQ(p.park_count, 0);                                  /* ours: not parked as the player's */
    CHECK_EQ(bl_patch_apply(game, files5, stage4, backup, &p, NULL, NULL), 0);
    CHECK(has_text(game, "amax/plugins/plugins.lua", AMAX_LIST_EDITED));
    CHECK(!tree_has(backup, PKG286_PLUGINS));
    /* Restore: AMAX's own list comes back, byte for byte. */
    CHECK_EQ(bl_patch_restore(game, backup), 0);
    CHECK(has_text(game, "amax/plugins/plugins.lua", AMAX_LIST));
    CHECK(!tree_requires_account(game) || exists(game, "amax/plugins/account/account.lua"));
    /* The same folder whose 286 run added the list: Restore goes back to no list, never to one that
     * requires a plugin that is gone. */
    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "d3d9.dll", AMAX_PROXY);
    put(game, "amax/plugins/plugins.lua", PKG286_PLUGINS);
    put(game, "amax/plugins/account/account.lua", ACCOUNT_PLUGIN);
    put(backup, "000001-20261114T120000Z/backup.txt",
        "run online\nadded d3d9.dll\nadded amax/plugins/plugins.lua\nadded amax/plugins/account/account.lua\n");
    put(backup, "000001-20261114T120000Z/done", "done\n");
    CHECK_EQ(bl_patch_plan(game, files5, stage4, &p), 0);
    CHECK_EQ(bl_patch_apply(game, files5, stage4, backup, &p, NULL, NULL), 0);
    CHECK_EQ(bl_patch_restore(game, backup), 0);
    CHECK(!exists(game, "amax/plugins/plugins.lua"));
    CHECK(!tree_requires_account(game) || exists(game, "amax/plugins/account/account.lua"));
}

/* make_manifest.py computes the same edited hash as the launcher (one rule,
 * two implementations: the package build and the console). */
static void test_plugins_edit_same_as_make_manifest(void)
{
    char dir[400], cmd[2400], line[512] = "", want[65];
    snprintf(dir, sizeof dir, "%s/mm", root);
    put(dir, "files/x.dll", "x");
    put(dir, "amax/amax/plugins/plugins.lua", AMAX_LIST);
    snprintf(cmd, sizeof cmd, "python3 -I '" BL_TEST_DIR "/../tools/make_manifest.py' '%s/files' x.dll --amax '%s/amax' "
             "--edit-plugins amax/plugins/plugins.lua && grep '^amax amax/plugins/plugins.lua ' '%s/files/manifest.txt'", dir, dir, dir);
    FILE *pp = popen(cmd, "r");
    if (pp) { if (!fgets(line, sizeof line, pp)) line[0] = 0; pclose(pp); }
    char out[2048];
    long n = bl_patch_plugins_edit(AMAX_LIST, strlen(AMAX_LIST), out, sizeof out);
    CHECK(n > 0);
    bl_sha256_hex(out, n > 0 ? (size_t)n : 0, want);
    const char *e = strstr(line, " edit=");
    CHECK(e && !strncmp(e + 6, want, 64));
    if (!e || strncmp(e + 6, want, 64)) fprintf(stderr, "  manifest line: %s  C edit sha: %s\n", line, want);
}

/* Review Focus 3: a rename (power cut) or a write (full disk) fails at each
 * step in turn; a rerun finishes the job and no file is ever lost. */
static const char *const ORIG_REL[] = { "amax/dlls/amax_auth.asi", "d3d9.dll", "dinput8.dll", "scripts/x.asi", "amax/old.lua" };
static const char *const ORIG_TEXT[] = { "old auth", "foreign proxy", "loader", "a mod", "old lua" };

static int nothing_lost(void)
{
    for (unsigned i = 0; i < 5; i++)
        if (!has_text(game, ORIG_REL[i], ORIG_TEXT[i]) && !tree_has(backup, ORIG_TEXT[i])) {
            fprintf(stderr, "  lost %s\n", ORIG_REL[i]);
            return 0;
        }
    return has_text(game, "Blur.exe", EXE);
}

static void test_interrupt_each_step_then_rerun(void)
{
    for (int kind = 0; kind < 2; kind++) {
        int clean_run = 0;
        for (int k = 1; k < 200 && !clean_run; k++) {
            reset_game();
            put(game, "Blur.exe", EXE);
            for (unsigned i = 0; i < 5; i++) put(game, ORIG_REL[i], ORIG_TEXT[i]);
            BlPatchPlan p;
            CHECK_EQ(bl_patch_plan(game, files2, stage, &p), 0);
            g_writes = g_renames = 0;
            if (kind == 0) { g_rename_fail_at = k; bl_fs_rename = failing_rename; }
            else { g_fail_at = k; bl_fs_write = failing_write; }
            int r = bl_patch_apply(game, files2, stage, backup, &p, NULL, NULL);
            int calls = kind == 0 ? g_renames : g_writes;
            g_fail_at = g_rename_fail_at = 0;
            bl_fs_write = bl_fs_write_default;
            bl_fs_rename = rename;
            if (calls < k) { clean_run = 1; CHECK_EQ(r, 0); }
            CHECK(nothing_lost());
            if (!nothing_lost()) fprintf(stderr, "  kind=%d k=%d after the fault\n", kind, k);
            /* no temp files left next to the game's files */
            CHECK(!exists(game, "d3d9.dll.tmp") && !exists(game, "amax/dlls/amax_auth.asi.tmp"));
            BlPatchPlan q;
            CHECK_EQ(bl_patch_plan(game, files2, stage, &q), 0);
            CHECK_EQ(bl_patch_apply(game, files2, stage, backup, &q, NULL, NULL), 0);
            CHECK_EQ(bl_patch_plan(game, files2, stage, &q), 0);
            CHECK_EQ(q.pending, 0);
            CHECK_EQ(q.park_count, 0);
            check_pinned_set();
            CHECK(nothing_lost());
            if (!nothing_lost()) fprintf(stderr, "  kind=%d k=%d after the rerun\n", kind, k);
        }
        CHECK(clean_run);
    }
}

/* Final review I2: mods often have spaces, non-ASCII or dot names; they are
 * parked (and restored) like any other, never a reason to stop Patch. */
static void test_park_odd_names(void)
{
    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "scripts/My Mod.asi", "a mod with a space");
    put(game, "Widescreen Fix.asi", "a root asi with a space");
    put(game, "scripts/._foo.asi", "AppleDouble");
    put(game, "scripts/ç%25.asi", "non-ASCII and percent");
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files2, stage, &p), 0);
    CHECK_EQ(p.park_count, 4);
    CHECK_EQ(bl_patch_apply(game, files2, stage, backup, &p, NULL, NULL), 0);
    CHECK(!exists(game, "scripts/My Mod.asi") && !exists(game, "Widescreen Fix.asi"));
    CHECK(!exists(game, "scripts/._foo.asi") && !exists(game, "scripts/ç%25.asi"));
    CHECK(tree_has(backup, "a mod with a space") && tree_has(backup, "non-ASCII and percent"));
    check_pinned_set();
    CHECK_EQ(bl_patch_restore(game, backup), 0);
    CHECK(has_text(game, "scripts/My Mod.asi", "a mod with a space"));
    CHECK(has_text(game, "Widescreen Fix.asi", "a root asi with a space"));
    CHECK(has_text(game, "scripts/._foo.asi", "AppleDouble"));
    CHECK(has_text(game, "scripts/ç%25.asi", "non-ASCII and percent"));
}

/* ---------------- offline and online sets (owner spec 2026-11-15) ---------------- */

static int item_index(const BlPatchPlan *p, const char *name)
{
    for (unsigned i = 0; i < p->count; i++) if (!strcmp(p->items[i].name, name)) return (int)i;
    return -1;
}

static void player_copy(void)
{
    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "cache/gamedata.pak", PLAYER_PAK);
}

/* Offline (no source): only file/seed lines; the player's exe and pak stay,
 * no AMAX file appears, and the copy is complete afterwards. */
static void test_offline_skips_online_lines(void)
{
    player_copy();
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files3, NULL, &p), 0);
    CHECK_EQ(p.count, 2);
    CHECK(item_index(&p, "d3d9.dll") >= 0 && item_index(&p, "d3d9_blur_dxvk.dll") >= 0);
    for (unsigned i = 0; i < p.count; i++) { CHECK_EQ(p.items[i].amax, 0); CHECK_EQ(p.items[i].online, 0); }
    CHECK_EQ(bl_patch_apply(game, files3, NULL, backup, &p, NULL, NULL), 0);
    CHECK(has_text(game, "d3d9.dll", V7));
    CHECK(has_text(game, "d3d9_blur_dxvk.dll", S7));
    CHECK(!exists(game, "amax"));
    CHECK(has_text(game, "Blur.exe", EXE));
    CHECK(has_text(game, "cache/gamedata.pak", PLAYER_PAK));
    CHECK_EQ(bl_patch_plan(game, files3, NULL, &p), 0);
    CHECK_EQ(p.pending, 0);
    CHECK_EQ(bl_patch_online_in_place(game, files3), 0);
}

/* Online: the AMAX set, our AMAX-only files and the blur_12 members; the
 * player's originals are parked first and Restore brings them back. */
static void test_online_installs_and_restores(void)
{
    player_copy();
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files3, stage3, &p), 0);
    CHECK_EQ(p.count, 8);                               /* the root d3d9.dll once: the AMAX proxy */
    int d = item_index(&p, "d3d9.dll");
    CHECK(d >= 0 && p.items[d].amax == 1);
    int e = item_index(&p, "Blur.exe");
    CHECK(e >= 0 && p.items[e].amax == 1 && p.items[e].online == 1 && p.items[e].foreign == 1);
    CHECK_EQ(bl_patch_apply(game, files3, stage3, backup, &p, NULL, NULL), 0);
    CHECK(has_text(game, "d3d9.dll", AMAX_PROXY));
    CHECK(has_text(game, "d3d9_blur_dxvk.dll", S7));
    CHECK(has_text(game, "amax/dxvk.dll", V7));
    CHECK(has_text(game, "amax/plugins/plugins.lua", OUR_PLUGINS));
    CHECK(has_text(game, "amax/dlls/amax_auth.asi", AMAX_AUTH));
    CHECK(has_text(game, "Blur.exe", EXE12));
    CHECK(has_text(game, "cache/gamedata.pak", PAK12));
    CHECK(has_text(game, "cache/gamedata.sig", SIG12));
    CHECK(tree_has(backup, EXE) && tree_has(backup, PLAYER_PAK));
    CHECK_EQ(bl_patch_plan(game, files3, stage3, &p), 0);
    CHECK_EQ(p.pending, 0);
    /* Afterwards no source is needed: the set is in place. */
    CHECK_EQ(bl_patch_online_in_place(game, files3), 1);
    CHECK_EQ(bl_patch_plan(game, files3, BL_PATCH_IN_PLACE, &p), 0);
    CHECK_EQ(p.pending, 0);
    /* Restore: the player's own exe and pak, no AMAX proxy. */
    CHECK_EQ(bl_patch_restore(game, backup), 0);
    CHECK(has_text(game, "Blur.exe", EXE));
    CHECK(has_text(game, "cache/gamedata.pak", PLAYER_PAK));
    CHECK(!exists(game, "d3d9.dll"));
    CHECK(!exists(game, "cache/gamedata.sig"));
    CHECK(!exists(game, "amax/dxvk.dll"));
}

/* Offline, then online: V7 in the root is ours (replaced, not parked); going
 * back offline parks only the AMAX proxy. */
static void test_offline_then_online_then_offline(void)
{
    player_copy();
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files3, NULL, &p), 0);
    CHECK_EQ(bl_patch_apply(game, files3, NULL, backup, &p, NULL, NULL), 0);
    CHECK_EQ(bl_patch_plan(game, files3, stage3, &p), 0);
    int d = item_index(&p, "d3d9.dll");
    CHECK(d >= 0 && p.items[d].state == BL_FILE_DIFFERENT && p.items[d].foreign == 0);
    CHECK_EQ(bl_patch_apply(game, files3, stage3, backup, &p, NULL, NULL), 0);
    CHECK(!tree_has(backup, V7));
    CHECK_EQ(bl_patch_plan(game, files3, NULL, &p), 0);
    d = item_index(&p, "d3d9.dll");
    CHECK(d >= 0 && p.items[d].foreign == 1);
    CHECK_EQ(p.park_count, 1);                          /* only the proxy: the AMAX tree is left alone */
    CHECK_EQ(bl_patch_apply(game, files3, NULL, backup, &p, NULL, NULL), 0);
    CHECK(has_text(game, "d3d9.dll", V7));
    CHECK(has_text(game, "amax/dlls/amax_auth.asi", AMAX_AUTH));
    CHECK(tree_has(backup, AMAX_PROXY));
}

/* The player's exe and pak in another letter case are replaced in place:
 * never a second Blur.exe next to BLUR.EXE. */
static void test_online_keeps_letter_case(void)
{
    reset_game();
    put(game, "BLUR.EXE", EXE);
    put(game, "Cache/GameData.pak", PLAYER_PAK);
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files3, stage3, &p), 0);
    CHECK(item_index(&p, "BLUR.EXE") >= 0 && item_index(&p, "Cache/GameData.pak") >= 0);
    CHECK_EQ(bl_patch_apply(game, files3, stage3, backup, &p, NULL, NULL), 0);
    CHECK(has_text(game, "BLUR.EXE", EXE12));
    CHECK(!exists(game, "Blur.exe"));
    CHECK(has_text(game, "Cache/GameData.pak", PAK12));
    CHECK(!exists(game, "cache"));
    CHECK(has_text(game, "Cache/gamedata.sig", SIG12));
    CHECK_EQ(bl_patch_plan(game, files3, stage3, &p), 0);
    CHECK_EQ(p.pending, 0);
    CHECK_EQ(bl_patch_restore(game, backup), 0);
    CHECK(has_text(game, "BLUR.EXE", EXE));
    CHECK(has_text(game, "Cache/GameData.pak", PLAYER_PAK));
}

/* A fault at any rename or write never leaves the folder without Blur.exe,
 * and a rerun finishes the job. */
static void test_online_exe_never_missing(void)
{
    for (int kind = 0; kind < 2; kind++) {
        int clean_run = 0;
        for (int k = 1; k < 100 && !clean_run; k++) {
            player_copy();
            BlPatchPlan p;
            CHECK_EQ(bl_patch_plan(game, files3, stage3, &p), 0);
            g_writes = g_renames = 0;
            if (kind == 0) { g_rename_fail_at = k; bl_fs_rename = failing_rename; }
            else { g_fail_at = k; bl_fs_write = failing_write; }
            int r = bl_patch_apply(game, files3, stage3, backup, &p, NULL, NULL);
            int calls = kind == 0 ? g_renames : g_writes;
            g_fail_at = g_rename_fail_at = 0;
            bl_fs_write = bl_fs_write_default;
            bl_fs_rename = rename;
            if (calls < k) { clean_run = 1; CHECK_EQ(r, 0); }
            CHECK(has_text(game, "Blur.exe", EXE) || has_text(game, "Blur.exe", EXE12));
            CHECK(has_text(game, "cache/gamedata.pak", PLAYER_PAK) || tree_has(backup, PLAYER_PAK));
            BlPatchPlan q;
            CHECK_EQ(bl_patch_plan(game, files3, stage3, &q), 0);
            CHECK_EQ(bl_patch_apply(game, files3, stage3, backup, &q, NULL, NULL), 0);
            CHECK(has_text(game, "Blur.exe", EXE12));
            CHECK(tree_has(backup, EXE) && tree_has(backup, PLAYER_PAK));
        }
        CHECK(clean_run);
    }
}

/* Review blocker 1: an online Patch that stops anywhere (a failed rename or
 * write at each step in turn) is seen as unfinished whenever an original is
 * displaced; restoring and patching offline then gives the player's files
 * back byte for byte. A finished run is not "unfinished". */
static void test_interrupted_online_then_offline(void)
{
    for (int kind = 0; kind < 2; kind++) {
        int clean_run = 0;
        for (int k = 1; k < 100 && !clean_run; k++) {
            player_copy();
            BlPatchPlan p;
            CHECK_EQ(bl_patch_plan(game, files3, stage3, &p), 0);
            g_writes = g_renames = 0;
            if (kind == 0) { g_rename_fail_at = k; bl_fs_rename = failing_rename; }
            else { g_fail_at = k; bl_fs_write = failing_write; }
            int r = bl_patch_apply(game, files3, stage3, backup, &p, NULL, NULL);
            int calls = kind == 0 ? g_renames : g_writes;
            g_fail_at = g_rename_fail_at = 0;
            bl_fs_write = bl_fs_write_default;
            bl_fs_rename = rename;
            if (calls < k) { clean_run = 1; CHECK_EQ(r, 0); CHECK_EQ(bl_patch_unfinished(game, backup), 0); continue; }
            int intact = has_text(game, "Blur.exe", EXE) && has_text(game, "cache/gamedata.pak", PLAYER_PAK);
            int unfinished = bl_patch_unfinished(game, backup);
            CHECK(intact || unfinished == 1);
            if (!intact && unfinished != 1) fprintf(stderr, "  kind=%d k=%d: original displaced, run not seen as unfinished\n", kind, k);
            /* what every Patch does first, then the offline set */
            if (unfinished) CHECK_EQ(bl_patch_restore(game, backup), 0);
            CHECK_EQ(bl_patch_unfinished(game, backup), 0);
            BlPatchPlan q;
            CHECK_EQ(bl_patch_plan(game, files3, NULL, &q), 0);
            CHECK_EQ(bl_patch_apply(game, files3, NULL, backup, &q, NULL, NULL), 0);
            CHECK(has_text(game, "Blur.exe", EXE));
            CHECK(has_text(game, "cache/gamedata.pak", PLAYER_PAK));
            CHECK(!exists(game, "cache/gamedata.sig"));
            CHECK(has_text(game, "d3d9.dll", V7));
        }
        CHECK(clean_run);
    }
}

/* Review 8: a folder AMAX/ in another letter case is the same folder (Wine
 * ignores case): its files are recognised, ours go into it, nothing is
 * parked or written twice. */
static void test_folder_case_ignored(void)
{
    reset_game();
    put(game, "Blur.exe", EXE);
    put(game, "AMAX/dlls/amax_auth.asi", AMAX_AUTH);
    put(game, "AMAX/Plugins/plugins.lua", AMAX_PLUGINS);
    BlPatchPlan p;
    CHECK_EQ(bl_patch_plan(game, files3, stage3, &p), 0);
    CHECK_EQ(p.park_count, 1);                          /* only the player's own exe: nothing in AMAX/ */
    CHECK(p.park_count == 1 && !strcmp(p.park[0], "Blur.exe"));
    int a = item_index(&p, "AMAX/dlls/amax_auth.asi");
    CHECK(a >= 0 && p.items[a].state == BL_FILE_OK);
    CHECK_EQ(bl_patch_apply(game, files3, stage3, backup, &p, NULL, NULL), 0);
    CHECK(!exists(game, "amax"));
    CHECK(has_text(game, "AMAX/dxvk.dll", V7));
    CHECK(has_text(game, "AMAX/Plugins/plugins.lua", OUR_PLUGINS));
    CHECK(has_text(game, "AMAX/dlls/amax_auth.asi", AMAX_AUTH));
    CHECK_EQ(bl_patch_plan(game, files3, stage3, &p), 0);
    CHECK_EQ(p.pending, 0);
    CHECK_EQ(bl_patch_online_state(game, files3), BL_ONLINE_READY);
}

/* blur12 may name Blur.exe (top level only); other kinds never may. */
static void test_blur12_exe_line_rules(void)
{
    char mp[600], saved[4096];
    snprintf(mp, sizeof mp, "%s/manifest.txt", files3);
    FILE *f = fopen(mp, "rb"); size_t n = fread(saved, 1, sizeof saved - 1, f); saved[n] = 0; fclose(f);
    const char *bad[] = { "blur12 sub/Blur.exe", "online Blur.exe", "file Blur.exe", "amax Blur.exe", "blur12 ../x" };
    player_copy();
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char m[4600];
        snprintf(m, sizeof m, "%s%s 0000000000000000000000000000000000000000000000000000000000000000 4\n", saved, bad[i]);
        put(files3, "manifest.txt", m);
        BlPatchPlan p;
        CHECK(bl_patch_plan(game, files3, stage3, &p) < 0);
        if (bl_patch_plan(game, files3, stage3, &p) >= 0) fprintf(stderr, "  accepted %s\n", bad[i]);
    }
    put(files3, "manifest.txt", saved);
}

/* Big files are hashed once per size and change time (the console takes
 * seconds for Blur.exe and gamedata.pak). */
static void test_hash_cache(void)
{
    player_copy();
    char cache[400], big[700];
    snprintf(cache, sizeof cache, "%s/hash-cache.txt", root);
    snprintf(big, sizeof big, "%s/cache/gamedata.pak", game);
    FILE *f = fopen(big, "wb");
    for (int i = 0; i < 300000; i++) fputs("0123456789", f);
    fclose(f);
    char a[65], b[65];
    unsigned before = bl_fs_hash_count;
    CHECK_EQ(bl_fs_sha256_cached(cache, big, a, NULL), 0);
    CHECK_EQ(bl_fs_hash_count, before + 1);
    struct stat c1, c2;
    CHECK_EQ(stat(cache, &c1), 0);
    CHECK_EQ(bl_fs_sha256_cached(cache, big, b, NULL), 0);
    CHECK_EQ(bl_fs_hash_count, before + 1);             /* from the cache */
    CHECK(!strcmp(a, b));
    CHECK_EQ(stat(cache, &c2), 0);                      /* a hit does not rewrite the cache */
    CHECK(c1.st_ino == c2.st_ino && c1.st_mtim.tv_sec == c2.st_mtim.tv_sec && c1.st_mtim.tv_nsec == c2.st_mtim.tv_nsec);
    /* a change of the inode's status (ctime) is a miss */
    chmod(big, 0600);
    CHECK_EQ(bl_fs_sha256_cached(cache, big, b, NULL), 0);
    CHECK_EQ(bl_fs_hash_count, before + 2);
    before++;
    hash_of(game, "cache/gamedata.pak", b);
    CHECK(!strcmp(a, b));
    f = fopen(big, "r+b"); fputs("X", f); fclose(f);    /* same size, new content */
    struct timespec ts[2] = { { 0, UTIME_OMIT }, { 1000000000, 123 } };
    utimensat(AT_FDCWD, big, ts, 0);
    CHECK_EQ(bl_fs_sha256_cached(cache, big, b, NULL), 0);
    CHECK_EQ(bl_fs_hash_count, before + 3);             /* a miss (and the direct hash above) */
    CHECK(strcmp(a, b) != 0);
    /* a small file is always hashed, never cached */
    put(game, "small.txt", "small");
    snprintf(big, sizeof big, "%s/small.txt", game);
    CHECK_EQ(bl_fs_sha256_cached(cache, big, a, NULL), 0);
    CHECK_EQ(bl_fs_sha256_cached(cache, big, a, NULL), 0);
    CHECK_EQ(bl_fs_hash_count, before + 5);
    CHECK_EQ(bl_fs_sha256_cached(NULL, big, a, NULL), 0);
}

int main(void)
{
    setup();
    RUN(test_missing_game);
    RUN(test_plan_apply_restore);
    RUN(test_our_older_version_is_not_foreign);
    RUN(test_any_blur_is_accepted);
    RUN(test_restore_after_faults);
    RUN(test_two_applies_one_restore);
    RUN(test_backup_order_ignores_clock);
    RUN(test_refuse_dotdot_and_leading_dot);
    RUN(test_progress_callback);
    RUN(test_seed_never_overwritten);
    RUN(test_cache_seed_goes_to_cache_dir);
    RUN(test_subfolder_install);
    RUN(test_park_foreign_amax_tree);
    RUN(test_park_many_foreign);
    RUN(test_park_unreadable_stops);
    RUN(test_park_ignores_case);
    RUN(test_ours_not_parked);
    RUN(test_plugins_lua_replaced);
    RUN(test_plugins_edit_text);
    RUN(test_plugins_lua_edited_at_patch);
    RUN(test_pkg286_plugins_repatch_restore);
    RUN(test_plugins_edit_same_as_make_manifest);
    RUN(test_interrupt_each_step_then_rerun);
    RUN(test_park_odd_names);
    RUN(test_offline_skips_online_lines);
    RUN(test_online_installs_and_restores);
    RUN(test_offline_then_online_then_offline);
    RUN(test_online_keeps_letter_case);
    RUN(test_online_exe_never_missing);
    RUN(test_blur12_exe_line_rules);
    RUN(test_interrupted_online_then_offline);
    RUN(test_folder_case_ignored);
    RUN(test_hash_cache);
    char cmd[400]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); if (system(cmd)) {}
    return bl_test_report("test_patcher");
}
