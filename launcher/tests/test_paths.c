/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests for the JkFlow data layout (BLUR 1.00 plan Task L1; owner
 * findings 4+5, option B: /data/JkFlow/System and /data/JkFlow/Logs). */
#define _POSIX_C_SOURCE 200809L
#include "bl_paths.h"
#include "bl_saves.h"
#include "bl_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static int is_file(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISREG(st.st_mode); }

static void mk(const char *root, const char *rel)
{
    char cmd[1200]; snprintf(cmd, sizeof cmd, "mkdir -p '%s/%s'", root, rel);
    if (system(cmd) != 0) exit(1);
}

static void put(const char *root, const char *rel, const char *text)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    FILE *f = fopen(path, "w");
    if (!f) exit(1);
    fputs(text, f);
    fclose(f);
}

static int has(const char *root, const char *rel, const char *text)
{
    char path[1200], buf[256] = "";
    snprintf(path, sizeof path, "%s/%s", root, rel);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return !strcmp(buf, text);
}

static void rm_rf(const char *dir)
{
    char cmd[600]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (system(cmd) != 0) exit(1);
}

static void test_default_root(void)
{
    BlPaths p;
    bl_paths_init(&p, NULL);
    CHECK(!strcmp(p.root, "/data/JkFlow/System"));
    CHECK(!strcmp(p.game, "/data/JkFlow/System/game"));
    CHECK(!strcmp(p.backup, "/data/JkFlow/System/backup"));
    CHECK(!strcmp(p.prefix, "/data/JkFlow/System/prefix"));
    CHECK(!strcmp(p.cache, "/data/JkFlow/System/cache"));
    CHECK(!strcmp(p.logs, "/data/JkFlow/Logs"));                 /* logs and support: the Logs root */
    CHECK(!strcmp(p.support, "/data/JkFlow/Logs/support"));
    CHECK(!strcmp(p.state, "/data/JkFlow/System/state"));
    CHECK(!strcmp(p.import, "/data/JkFlow/System/import"));
    CHECK(!strcmp(p.saves, "/data/JkFlow/System/prefix/drive_c/users/BLUR/AppData/Roaming/Bizarre Creations/Blur"));
    CHECK(!strcmp(BL_WINE_GAME_DIR, "Z:\\data\\JkFlow\\Blur"));
    CHECK(!strcmp(BL_OLD_ROOT, "/data/BLUR"));
}

/* A System root always has its Logs beside it, in the same JkFlow spelling;
 * any other root (host tests) keeps logs/ and support/ inside. */
static void test_system_root_split(void)
{
    BlPaths p;
    bl_paths_init(&p, "/data/jkflow/System");
    CHECK(!strcmp(p.root, "/data/jkflow/System"));
    CHECK(!strcmp(p.state, "/data/jkflow/System/state"));
    CHECK(!strcmp(p.logs, "/data/jkflow/Logs"));
    CHECK(!strcmp(p.support, "/data/jkflow/Logs/support"));
    bl_paths_init(&p, "/tmp/x/notsystem");
    CHECK(!strcmp(p.logs, "/tmp/x/notsystem/logs"));
    CHECK(!strcmp(p.support, "/tmp/x/notsystem/support"));
    bl_paths_init(&p, "/System");                                  /* no parent: not a JkFlow layout */
    CHECK(!strcmp(p.logs, "/System/logs"));
}

/* The title sets the root it found; bl_paths_init(NULL) callers follow it. */
static void test_default_root_follows_title(void)
{
    BlPaths p;
    bl_paths_set_default_root("/data/JKFLOW/System");
    bl_paths_init(&p, NULL);
    CHECK(!strcmp(p.root, "/data/JKFLOW/System"));
    CHECK(!strcmp(p.logs, "/data/JKFLOW/Logs"));
    bl_paths_set_default_root(NULL);
    bl_paths_init(&p, NULL);
    CHECK(!strcmp(p.root, BL_DATA_ROOT));
    char big[300]; memset(big, 'a', sizeof big - 1); big[0] = '/'; big[sizeof big - 1] = 0;
    bl_paths_set_default_root(big);                                 /* too long: ignored */
    bl_paths_init(&p, NULL);
    CHECK(!strcmp(p.root, BL_DATA_ROOT));
}

static void test_make_creates_all(void)
{
    char root[256];
    snprintf(root, sizeof root, "/tmp/bl-paths-XXXXXX");
    CHECK(mkdtemp(root) != NULL);
    BlPaths p;
    bl_paths_init(&p, root);
    CHECK(!strncmp(p.game, root, strlen(root)));
    CHECK_EQ(bl_paths_make(&p), 0);
    const char *dirs[] = { p.backup, p.prefix, p.cache, p.logs, p.support, p.state, p.import };
    for (unsigned i = 0; i < 7; i++) CHECK(is_dir(dirs[i]));
    CHECK(!is_dir(p.game));                     /* the game folder is the player's (/data/JkFlow/Blur) */
    CHECK(!is_dir(p.saves));
    char found[512];
    CHECK_EQ(bl_saves_find2(&p, found, sizeof found), -1);
    CHECK_EQ(bl_paths_make(&p), 0);
    char cmd[400]; snprintf(cmd, sizeof cmd, "mkdir -p '%s' && true", p.saves);
    CHECK_EQ(system(cmd), 0);
    CHECK_EQ(bl_saves_find2(&p, found, sizeof found), 0);
    CHECK(!strcmp(found, p.saves));
    rm_rf(root);
}

/* The JkFlow layout: make creates the System folders and Logs, Logs/support. */
static void test_make_jkflow_layout(void)
{
    char data[256], sys[512], logs[512];
    snprintf(data, sizeof data, "/tmp/bl-jk-XXXXXX");
    CHECK(mkdtemp(data) != NULL);
    snprintf(sys, sizeof sys, "%s/JkFlow/System", data);
    snprintf(logs, sizeof logs, "%s/JkFlow/Logs", data);
    BlPaths p;
    bl_paths_init(&p, sys);
    CHECK_EQ(bl_paths_make(&p), 0);
    CHECK(is_dir(p.state) && is_dir(p.prefix) && is_dir(p.cache) && is_dir(p.backup) && is_dir(p.import));
    CHECK(is_dir(logs));
    CHECK(!strcmp(p.logs, logs));
    CHECK(is_dir(p.support));
    rm_rf(data);
}

static void test_no_old_roots(void)
{
    BlPaths p;
    bl_paths_init(&p, NULL);
    const char *f[] = { p.root, p.game, p.backup, p.prefix, p.cache, p.logs, p.support, p.state, p.import, p.saves };
    for (unsigned i = 0; i < 10; i++) {
        char low[300];
        size_t n = strlen(f[i]);
        for (size_t k = 0; k <= n; k++) low[k] = (char)((f[i][k] >= 'A' && f[i][k] <= 'Z') ? f[i][k] + 32 : f[i][k]);
        CHECK(strstr(low, "prospero") == NULL);
        CHECK(strstr(f[i], "/data/Pc") == NULL);
        CHECK(strstr(f[i], "/data/BLUR") == NULL);
    }
}

static void test_long_root_refused(void)
{
    char root[200];
    memset(root, 'a', sizeof root - 1);
    root[0] = '/'; root[sizeof root - 1] = 0;
    BlPaths p;
    bl_paths_init(&p, root);          /* too long for the fields: falls back to empty, never truncated */
    CHECK_EQ(p.root[0], 0);
    CHECK(bl_paths_make(&p) < 0);
}

/* Owner decision 2026-11-15: the game is in /data/JkFlow/Blur, letter case
 * ignored at both levels; several spellings are an error naming them. */
static void test_game_dir_find(void)
{
    char data[256], out[512], other[512], want[600], cmd[900];
    snprintf(data, sizeof data, "/tmp/bl-gd-XXXXXX");
    CHECK(mkdtemp(data) != NULL);
    snprintf(want, sizeof want, "%s/JkFlow/Blur", data);
    CHECK_EQ(bl_game_dir_find(data, out, sizeof out, other, sizeof other), 0);   /* none: the default spelling */
    CHECK(!strcmp(out, want));
    mk(data, "jkflowX/Blur");                                /* other names are not it */
    mk(data, "Pc/Blur");
    mk(data, "JKFLOW/Blurry");
    CHECK_EQ(bl_game_dir_find(data, out, sizeof out, other, sizeof other), 0);
    mk(data, "JKFLOW/blur");
    CHECK_EQ(bl_game_dir_find(data, out, sizeof out, other, sizeof other), 1);
    snprintf(want, sizeof want, "%s/JKFLOW/blur", data);
    CHECK(!strcmp(out, want));
    mk(data, "JKFLOW/BLUR");                                 /* two spellings inside */
    CHECK_EQ(bl_game_dir_find(data, out, sizeof out, other, sizeof other), -1);
    CHECK(strstr(out, "/JKFLOW/BLUR") && strstr(other, "/JKFLOW/blur"));
    snprintf(cmd, sizeof cmd, "rm -rf '%s/JKFLOW' && mkdir -p '%s/jkflow/Blur' '%s/JkFlow/Blur'", data, data, data);
    CHECK(system(cmd) == 0);
    CHECK_EQ(bl_game_dir_find(data, out, sizeof out, other, sizeof other), -1);  /* two spellings outside */
    CHECK(strstr(out, "/JkFlow/Blur") && strstr(other, "/jkflow/Blur"));
    CHECK_EQ(bl_game_dir_find("/no/such/dir", out, sizeof out, other, sizeof other), 0);
    CHECK(!strcmp(out, "/no/such/dir/JkFlow/Blur"));
    rm_rf(data);
}

/* The JkFlow folder, found before anything is created: the spelling on
 * disk in any case; JkFlow is created only when there is none. */
static void test_jkflow_dir(void)
{
    char data[256], out[512], want[600];
    snprintf(data, sizeof data, "/tmp/bl-jd-XXXXXX");
    CHECK(mkdtemp(data) != NULL);
    snprintf(want, sizeof want, "%s/JkFlow", data);
    CHECK_EQ(bl_jkflow_dir(data, out, sizeof out, 0), 0);        /* none, not created */
    CHECK(!strcmp(out, want));
    CHECK(!is_dir(want));
    CHECK_EQ(bl_jkflow_dir(data, out, sizeof out, 1), 0);        /* none: created as JkFlow */
    CHECK(is_dir(want));
    CHECK_EQ(bl_jkflow_dir(data, out, sizeof out, 1), 1);        /* now it exists */
    CHECK(!strcmp(out, want));
    rm_rf(want);
    mk(data, "jkflow/Blur");                                     /* the player's spelling: used, no second folder */
    CHECK_EQ(bl_jkflow_dir(data, out, sizeof out, 1), 1);
    snprintf(want, sizeof want, "%s/jkflow", data);
    CHECK(!strcmp(out, want));
    snprintf(want, sizeof want, "%s/JkFlow", data);
    CHECK(!is_dir(want));
    mk(data, "JKFLOW");                                          /* two: the one holding the game wins */
    CHECK_EQ(bl_jkflow_dir(data, out, sizeof out, 1), 1);
    CHECK(strstr(out, "/jkflow") && !strstr(out, "/JKFLOW"));
    mk(data, "JKFLOW/blur");                                     /* both hold a game: exact JkFlow, else the first */
    CHECK_EQ(bl_jkflow_dir(data, out, sizeof out, 1), 1);
    CHECK(strstr(out, "/JKFLOW") != NULL);                       /* strcmp order: JKFLOW < jkflow */
    mk(data, "JkFlow");
    CHECK_EQ(bl_jkflow_dir(data, out, sizeof out, 1), 1);
    CHECK(strstr(out, "/JkFlow") != NULL);
    put(data, "jkFLOW", "a file, not a folder");
    CHECK_EQ(bl_jkflow_dir(data, out, sizeof out, 1), 1);
    CHECK(strstr(out, "/JkFlow") != NULL);
    rm_rf(data);
}

typedef struct { int lines; char last[400]; char all[4000]; } LogSink;
static void sink(void *ctx, const char *line)
{
    LogSink *s = ctx;
    s->lines++;
    snprintf(s->last, sizeof s->last, "%s", line);
    size_t n = strlen(s->all);
    snprintf(s->all + n, sizeof s->all - n, "%s\n", line);
}

/* One-time move of an old /data/BLUR into System and Logs. */
static void test_migrate_moves_everything(void)
{
    char data[256], old[512], sys[600], jk[512];
    snprintf(data, sizeof data, "/tmp/bl-mg-XXXXXX");
    CHECK(mkdtemp(data) != NULL);
    snprintf(old, sizeof old, "%s/BLUR", data);
    snprintf(jk, sizeof jk, "%s/JkFlow", data);
    snprintf(sys, sizeof sys, "%s/System", jk);
    const char *subs[] = { "prefix", "cache", "state", "backup", "import", "logs", "support", "player-names", "game" };
    for (unsigned i = 0; i < 9; i++) mk(old, subs[i]);
    put(old, "prefix/system.reg", "reg");
    put(old, "cache/dxvk.conf", "conf");
    put(old, "state/launcher.ini", "ini");
    put(old, "logs/session-0.log", "s0");
    put(old, "support/bundle.txt", "b");
    put(old, "player-names/1", "name");
    put(old, "loose.txt", "loose");
    BlPaths p;
    bl_paths_init(&p, sys);
    LogSink s = { 0 };
    CHECK_EQ(bl_paths_migrate(old, &p, sink, &s), 10);
    CHECK(!is_dir(old));                                         /* the empty /data/BLUR is removed */
    CHECK(has(sys, "prefix/system.reg", "reg"));
    CHECK(has(sys, "cache/dxvk.conf", "conf"));
    CHECK(has(sys, "state/launcher.ini", "ini"));
    CHECK(is_dir(p.backup) && is_dir(p.import));
    CHECK(has(jk, "Logs/session-0.log", "s0"));
    CHECK(has(jk, "Logs/support/bundle.txt", "b"));
    CHECK(has(sys, "player-names/1", "name"));                   /* anything else goes to System */
    CHECK(has(sys, "loose.txt", "loose"));
    CHECK(is_dir(p.game));
    CHECK(strstr(s.all, "prefix") != NULL);
    CHECK(strstr(s.all, "removed") != NULL);
    /* Safe to repeat: nothing left to move, nothing touched. */
    LogSink s2 = { 0 };
    CHECK_EQ(bl_paths_migrate(old, &p, sink, &s2), 0);
    CHECK_EQ(s2.lines, 0);
    CHECK(has(sys, "state/launcher.ini", "ini"));
    rm_rf(data);
}

/* Audit 5 (owner 2026-11-16): an old /data/BLUR holding Blur.exe directly
 * (any letter case) is the player's GAME, not old launcher data: it becomes
 * <JkFlow>/BLUR in one rename when no game is there (missing or an empty
 * folder, any letter case) and stays put when one is. Never merged into
 * System. Blur.exe only deeper down: the System migration as before. */
static void test_migrate_game_folder(void)
{
    char data[256], old[512], sys[600], jk[512], want[700];
    snprintf(data, sizeof data, "/tmp/bl-mgg-XXXXXX");
    CHECK(mkdtemp(data) != NULL);
    snprintf(old, sizeof old, "%s/BLUR", data);
    snprintf(jk, sizeof jk, "%s/JkFlow", data);
    snprintf(sys, sizeof sys, "%s/System", jk);
    BlPaths p;
    bl_paths_init(&p, sys);

    /* 1. A new player's game in /data/BLUR: renamed whole to JkFlow/BLUR. */
    mk(old, "data");
    put(old, "blur.EXE", "exe");
    put(old, "data/gamedata.pak", "pak");
    LogSink s = { 0 };
    CHECK_EQ(bl_paths_migrate(old, &p, sink, &s), 1);
    CHECK(has(jk, "BLUR/blur.EXE", "exe"));
    CHECK(has(jk, "BLUR/data/gamedata.pak", "pak"));
    CHECK(!is_dir(old));
    snprintf(want, sizeof want, "%s/blur.EXE", sys);
    CHECK(!is_file(want));                                        /* nothing in System */
    snprintf(want, sizeof want, "migrate: %s is a game folder -> JkFlow/BLUR", old);
    CHECK(strstr(s.all, want) != NULL);
    char game[600];
    CHECK_EQ(bl_game_dir_find(data, game, sizeof game, NULL, 0), 1);   /* Play finds it */
    rm_rf(data);

    /* 2. An empty jkflow/blur (another letter case) is taken, no second folder. */
    CHECK(mkdtemp(strcpy(data, "/tmp/bl-mgg-XXXXXX")) != NULL);
    snprintf(old, sizeof old, "%s/BLUR", data);
    snprintf(jk, sizeof jk, "%s/JkFlow", data);
    snprintf(sys, sizeof sys, "%s/System", jk);
    bl_paths_init(&p, sys);
    mk(jk, "blur");
    mk(old, "");
    put(old, "Blur.exe", "exe");
    LogSink s2 = { 0 };
    CHECK_EQ(bl_paths_migrate(old, &p, sink, &s2), 1);
    CHECK(has(jk, "blur/Blur.exe", "exe"));
    snprintf(want, sizeof want, "%s/BLUR", jk);
    CHECK(!is_dir(want));
    CHECK(!is_dir(old));
    CHECK(strstr(s2.all, "is a game folder -> JkFlow/BLUR") != NULL);
    rm_rf(data);

    /* 3. A game already in JkFlow/Blur: nothing moves, nothing merged, logged. */
    CHECK(mkdtemp(strcpy(data, "/tmp/bl-mgg-XXXXXX")) != NULL);
    snprintf(old, sizeof old, "%s/BLUR", data);
    snprintf(jk, sizeof jk, "%s/JkFlow", data);
    snprintf(sys, sizeof sys, "%s/System", jk);
    bl_paths_init(&p, sys);
    mk(jk, "Blur");
    put(jk, "Blur/Blur.exe", "present");
    mk(old, "prefix");
    put(old, "BLUR.EXE", "new");
    LogSink s3 = { 0 };
    CHECK_EQ(bl_paths_migrate(old, &p, sink, &s3), 0);
    CHECK(has(old, "BLUR.EXE", "new"));
    CHECK(is_dir(old));
    CHECK(has(jk, "Blur/Blur.exe", "present"));
    snprintf(want, sizeof want, "%s/prefix", sys);
    CHECK(!is_dir(want));
    snprintf(want, sizeof want, "migrate: %s kept (game already present)", old);
    CHECK(strstr(s3.all, want) != NULL);
    CHECK(strstr(s3.all, "is a game folder") == NULL);
    rm_rf(data);

    /* 4. Blur.exe only in a folder inside: old data, the System migration. */
    CHECK(mkdtemp(strcpy(data, "/tmp/bl-mgg-XXXXXX")) != NULL);
    snprintf(old, sizeof old, "%s/BLUR", data);
    snprintf(jk, sizeof jk, "%s/JkFlow", data);
    snprintf(sys, sizeof sys, "%s/System", jk);
    bl_paths_init(&p, sys);
    mk(old, "games");
    put(old, "games/Blur.exe", "exe");
    LogSink s4 = { 0 };
    CHECK_EQ(bl_paths_migrate(old, &p, sink, &s4), 1);
    CHECK(has(sys, "games/Blur.exe", "exe"));
    snprintf(want, sizeof want, "%s/BLUR", jk);
    CHECK(!is_dir(want));
    CHECK(strstr(s4.all, "game folder") == NULL && strstr(s4.all, "migrate: moved") != NULL);
    rm_rf(data);
}

/* Never overwrite: a target that exists keeps its content and the old one
 * is kept beside it, and the log says so. */
static void test_migrate_keeps_both(void)
{
    char data[256], old[512], sys[600], jk[512];
    snprintf(data, sizeof data, "/tmp/bl-mk-XXXXXX");
    CHECK(mkdtemp(data) != NULL);
    snprintf(old, sizeof old, "%s/BLUR", data);
    snprintf(jk, sizeof jk, "%s/JkFlow", data);
    snprintf(sys, sizeof sys, "%s/System", jk);
    mk(old, "state");
    put(old, "state/launcher.ini", "old");
    mk(old, "logs");
    put(old, "logs/session-0.log", "old-log");
    mk(sys, "state");
    put(sys, "state/launcher.ini", "new");
    mk(jk, "Logs");
    put(jk, "Logs/session-0.log", "new-log");
    mk(sys, "state.old-BLUR");                                   /* the first "both" name is taken too */
    BlPaths p;
    bl_paths_init(&p, sys);
    LogSink s = { 0 };
    CHECK_EQ(bl_paths_migrate(old, &p, sink, &s), 2);
    CHECK(has(sys, "state/launcher.ini", "new"));
    CHECK(has(sys, "state.old-BLUR2/launcher.ini", "old"));
    CHECK(has(jk, "Logs/session-0.log", "new-log"));
    CHECK(has(jk, "Logs.old-BLUR/session-0.log", "old-log"));
    CHECK(strstr(s.all, "kept both") != NULL);
    CHECK(!is_dir(old));
    rm_rf(data);
}

/* Nothing old: no work, no folders made, no log. A file named BLUR is
 * not a root. */
static void test_migrate_nothing(void)
{
    char data[256], old[512], sys[512];
    snprintf(data, sizeof data, "/tmp/bl-mn-XXXXXX");
    CHECK(mkdtemp(data) != NULL);
    snprintf(old, sizeof old, "%s/BLUR", data);
    snprintf(sys, sizeof sys, "%s/JkFlow/System", data);
    BlPaths p;
    bl_paths_init(&p, sys);
    LogSink s = { 0 };
    CHECK_EQ(bl_paths_migrate(old, &p, sink, &s), 0);
    CHECK_EQ(s.lines, 0);
    CHECK(!is_dir(sys));
    put(data, "BLUR", "file");
    CHECK_EQ(bl_paths_migrate(old, &p, sink, &s), 0);
    CHECK(is_file(old));
    CHECK_EQ(bl_paths_migrate(NULL, &p, sink, &s), 0);
    CHECK_EQ(bl_paths_migrate(old, NULL, NULL, NULL), -1);
    rm_rf(data);
}

/* A move that fails leaves the old root in place (not removed) and says so;
 * the next start tries again. */
static void test_migrate_failure_keeps_old(void)
{
    char data[256], old[512], sys[600], jk[512];
    snprintf(data, sizeof data, "/tmp/bl-mf-XXXXXX");
    CHECK(mkdtemp(data) != NULL);
    snprintf(old, sizeof old, "%s/BLUR", data);
    snprintf(jk, sizeof jk, "%s/JkFlow", data);
    snprintf(sys, sizeof sys, "%s/System", jk);
    mk(old, "state");
    mk(old, "cache");
    put(data, "JkFlow", "a file where the folder should be");  /* System cannot be made */
    BlPaths p;
    bl_paths_init(&p, sys);
    LogSink s = { 0 };
    CHECK_EQ(bl_paths_migrate(old, &p, sink, &s), -1);
    CHECK(is_dir(old));
    CHECK(strstr(s.all, "not removed") != NULL);
    rm_rf(data);
}

/* Review finding 1: any number of entries moves (no 64-entry cap). */
static void test_migrate_any_number(void)
{
    char data[256], old[512], sys[600], name[64];
    snprintf(data, sizeof data, "/tmp/bl-ma-XXXXXX");
    CHECK(mkdtemp(data) != NULL);
    snprintf(old, sizeof old, "%s/BLUR", data);
    snprintf(sys, sizeof sys, "%s/JkFlow/System", data);
    mk(old, "prefix");
    for (int i = 0; i < 150; i++) { snprintf(name, sizeof name, "f%03d.txt", i); put(old, name, "x"); }
    BlPaths p;
    bl_paths_init(&p, sys);
    LogSink s = { 0 };
    CHECK_EQ(bl_paths_migrate(old, &p, sink, &s), 151);
    CHECK(!is_dir(old));
    CHECK(is_dir(p.prefix));
    CHECK(has(sys, "f149.txt", "x"));
    rm_rf(data);
}

/* An empty folder made by an earlier build (after a failed move) is not
 * the player's data: the old one takes its place, never ".old-BLUR". */
static void test_migrate_empty_target_is_not_preexisting(void)
{
    char data[256], old[512], sys[600], jk[512], alt[700];
    snprintf(data, sizeof data, "/tmp/bl-me-XXXXXX");
    CHECK(mkdtemp(data) != NULL);
    snprintf(old, sizeof old, "%s/BLUR", data);
    snprintf(jk, sizeof jk, "%s/JkFlow", data);
    snprintf(sys, sizeof sys, "%s/System", jk);
    mk(old, "prefix");
    put(old, "prefix/system.reg", "old-reg");
    mk(old, "logs");
    put(old, "logs/session-0.log", "old-log");
    mk(sys, "prefix");                                           /* empty, made fresh */
    mk(jk, "Logs");                                              /* empty, made fresh */
    BlPaths p;
    bl_paths_init(&p, sys);
    LogSink s = { 0 };
    CHECK_EQ(bl_paths_migrate(old, &p, sink, &s), 2);
    CHECK(has(sys, "prefix/system.reg", "old-reg"));
    snprintf(alt, sizeof alt, "%s/prefix.old-BLUR", sys);
    CHECK(!is_dir(alt));
    CHECK(has(jk, "Logs/session-0.log", "old-log"));
    snprintf(alt, sizeof alt, "%s/Logs.old-BLUR", jk);
    CHECK(!is_dir(alt));
    CHECK(!is_dir(old));
    rm_rf(data);
}

/* logs that could not move keeps support in the old root too: support
 * moving into a fresh Logs would make the retry park the old logs beside
 * fresh data. */
static void test_migrate_logs_failure_keeps_support(void)
{
    char data[256], old[512], sys[600], jk[512], alt[700];
    snprintf(data, sizeof data, "/tmp/bl-ml-XXXXXX");
    CHECK(mkdtemp(data) != NULL);
    snprintf(old, sizeof old, "%s/BLUR", data);
    snprintf(jk, sizeof jk, "%s/JkFlow", data);
    snprintf(sys, sizeof sys, "%s/System", jk);
    mk(old, "logs");
    put(old, "logs/session-0.log", "old-log");
    mk(old, "support");
    put(old, "support/bundle.txt", "b");
    mk(jk, ".");
    put(jk, "Logs", "a file where Logs should be");              /* logs cannot move */
    for (int k = 1; k <= 9; k++) {
        if (k == 1) snprintf(alt, sizeof alt, "Logs.old-BLUR"); else snprintf(alt, sizeof alt, "Logs.old-BLUR%d", k);
        put(jk, alt, "taken");
    }
    BlPaths p;
    bl_paths_init(&p, sys);
    LogSink s = { 0 };
    CHECK_EQ(bl_paths_migrate(old, &p, sink, &s), -1);
    CHECK(has(old, "logs/session-0.log", "old-log"));
    CHECK(has(old, "support/bundle.txt", "b"));
    CHECK_EQ(bl_paths_pending(old), 1);
    rm_rf(data);
}

/* bl_paths_pending: the old root still holds something. */
static void test_paths_pending(void)
{
    char data[256], old[512];
    snprintf(data, sizeof data, "/tmp/bl-mp-XXXXXX");
    CHECK(mkdtemp(data) != NULL);
    snprintf(old, sizeof old, "%s/BLUR", data);
    CHECK_EQ(bl_paths_pending(old), 0);                          /* none */
    CHECK_EQ(bl_paths_pending(NULL), 0);
    mk(old, ".");
    CHECK_EQ(bl_paths_pending(old), 0);                          /* empty */
    put(old, ".hidden", "x");
    CHECK_EQ(bl_paths_pending(old), 1);                          /* any entry */
    rm_rf(old);
    put(data, "BLUR", "a file");
    CHECK_EQ(bl_paths_pending(old), 0);                          /* a file is not a root */
    rm_rf(data);
}

int main(void)
{
    RUN(test_game_dir_find);
    RUN(test_default_root);
    RUN(test_system_root_split);
    RUN(test_default_root_follows_title);
    RUN(test_make_creates_all);
    RUN(test_make_jkflow_layout);
    RUN(test_no_old_roots);
    RUN(test_long_root_refused);
    RUN(test_jkflow_dir);
    RUN(test_migrate_moves_everything);
    RUN(test_migrate_keeps_both);
    RUN(test_migrate_nothing);
    RUN(test_migrate_failure_keeps_old);
    RUN(test_migrate_any_number);
    RUN(test_migrate_empty_target_is_not_preexisting);
    RUN(test_migrate_logs_failure_keeps_support);
    RUN(test_paths_pending);
    RUN(test_migrate_game_folder);
    return bl_test_report("test_paths");
}
