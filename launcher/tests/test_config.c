/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests for launcher.ini and the password file (plan Task 4, Review Focus 2). */
#define _DEFAULT_SOURCE
#include "bl_config.h"
#include "bl_fs.h"
#include "bl_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char dir[256], ini[300], secret[300];

static void setup(void)
{
    snprintf(dir, sizeof dir, "/tmp/bl_test_config_XXXXXX");
    if (!mkdtemp(dir)) { perror("mkdtemp"); exit(1); }
    snprintf(ini, sizeof ini, "%s/launcher.ini", dir);
    snprintf(secret, sizeof secret, "%s/online.secret", dir);
}

static void put(const char *path, const char *text, size_t n)
{
    FILE *f = fopen(path, "wb"); fwrite(text, 1, n, f); fclose(f);
}

static char *slurp(const char *path)
{
    static char buf[1 << 16];
    FILE *f = fopen(path, "rb");
    if (!f) { buf[0] = 0; return buf; }
    size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f);
    return buf;
}

/* M1: after the rename the folder itself is flushed, or a power cut can
 * still lose the new name. */
static char g_synced[8][400];
static int g_nsynced;
static int record_sync(const char *d)
{
    if (g_nsynced < 8) snprintf(g_synced[g_nsynced++], 400, "%s", d);
    return 0;
}

static void test_folder_is_flushed(void)
{
    g_nsynced = 0;
    bl_fs_sync_dir = record_sync;
    char p[400], q[400];
    snprintf(p, sizeof p, "%s/flush.txt", dir);
    CHECK_EQ(bl_fs_write_atomic(p, "x", 1, 0644), 0);
    CHECK_EQ(g_nsynced, 1);
    CHECK(g_nsynced >= 1 && strcmp(g_synced[0], dir) == 0);
    snprintf(q, sizeof q, "%s/flush-copy.txt", dir);
    CHECK_EQ(bl_fs_copy(p, q, 0644), 0);
    CHECK_EQ(g_nsynced, 2);
    bl_fs_sync_dir = bl_fs_sync_dir_default;
    unlink(p); unlink(q);
}

/* M2: a planted "<file>.tmp" link must not redirect the write elsewhere. */
static void test_tmp_link_is_not_followed(void)
{
    char victim[400], target[400], link[400];
    snprintf(victim, sizeof victim, "%s/victim.txt", dir);
    snprintf(target, sizeof target, "%s/target.txt", dir);
    snprintf(link, sizeof link, "%s/target.txt.tmp", dir);
    put(victim, "keep me", 7);
    CHECK_EQ(symlink(victim, link), 0);
    CHECK_EQ(bl_fs_write_atomic(target, "new", 3, 0644), 0);
    CHECK(strcmp(slurp(victim), "keep me") == 0);
    CHECK(strcmp(slurp(target), "new") == 0);
    char from[400]; snprintf(from, sizeof from, "%s/from.txt", dir);
    put(from, "copied", 6);
    unlink(link);
    CHECK_EQ(symlink(victim, link), 0);
    CHECK_EQ(bl_fs_copy(from, target, 0644), 0);
    CHECK(strcmp(slurp(victim), "keep me") == 0);
    CHECK(strcmp(slurp(target), "copied") == 0);
    unlink(victim); unlink(target); unlink(from); unlink(link);
}

/* M12: an unknown line longer than 1 KB survives a save whole. */
static void test_long_unknown_line_survives(void)
{
    static char text[5000], line[3100];
    memset(line, 'a', 3000);
    line[3000] = 0;
    snprintf(text, sizeof text, "[mods]\nlist = %s\n[logging]\nenabled = true\n", line);
    put(ini, text, strlen(text));
    BlConfig c;
    char w[256];
    bl_config_load(ini, &c, w, sizeof w);
    CHECK_EQ(c.logging, 1);
    CHECK_EQ(bl_config_save(ini, &c), 0);
    char want[3200];
    snprintf(want, sizeof want, "list = %s\n", line);
    CHECK(strstr(slurp(ini), want) != NULL);
    unlink(ini);
}

static void test_defaults(void)
{
    BlConfig c;
    bl_config_defaults(&c);
    CHECK_EQ(c.schema, 1);
    CHECK_EQ(c.model, BL_MODEL_AUTO);
    CHECK_EQ(c.online_mode, BL_ONLINE_OFFICIAL);
    CHECK_EQ(c.port, 0);
    CHECK_EQ(c.login_account, 0);
    CHECK_EQ(c.logging, 0);
    CHECK_EQ(c.renderer, BL_RENDERER_NATIVE);
    CHECK_EQ(c.stats, BL_STATS_OFF);
    CHECK(c.host[0] == 0 && c.username[0] == 0);
    /* A missing file is not an error. */
    char w[256];
    CHECK_EQ(bl_config_load("/tmp/does/not/exist.ini", &c, w, sizeof w), 0);
}

static void test_round_trip(void)
{
    BlConfig a, b;
    bl_config_defaults(&a);
    a.model = BL_MODEL_PRO; a.online_mode = BL_ONLINE_PRIVATE;
    snprintf(a.host, sizeof a.host, "blur.example.net");
    snprintf(a.auth_host, sizeof a.auth_host, "10.0.0.5");
    snprintf(a.lobby_host, sizeof a.lobby_host, "10.0.0.6");
    a.port = 3074; a.login_account = 1;
    snprintf(a.username, sizeof a.username, "XxXJoker13XxX");
    a.logging = 1; a.stats = BL_STATS_FULL;
    a.renderer = BL_RENDERER_COMPATIBLE;
    a.frame_rate = BL_FPS_60;
    CHECK_EQ(bl_config_save(ini, &a), 0);
    char w[256] = "";
    CHECK_EQ(bl_config_load(ini, &b, w, sizeof w), 0);
    CHECK(memcmp(&a, &b, sizeof a) == 0);
    /* Saving again keeps a backup of the previous file. */
    b.stats = BL_STATS_FPS;
    CHECK_EQ(bl_config_save(ini, &b), 0);
    char bak[320]; snprintf(bak, sizeof bak, "%s.bak", ini);
    CHECK(strstr(slurp(bak), "stats = full") != NULL);
    CHECK(strstr(slurp(ini), "stats = fps") != NULL);
    CHECK(strstr(slurp(ini), "[renderer]\nmode = compatible") != NULL);
}

static void test_unknown_keys_survive(void)
{
    const char *text = "; hand-written\r\n[launcher]\r\nschema = 1\r\ncolour = pink\r\n[future]\r\nthing = 42\r\n"
                       "[online]\r\nmode = private   ; my server\r\nhost = 192.168.0.9\r\n";
    put(ini, text, strlen(text));
    BlConfig c; char w[256] = "";
    CHECK_EQ(bl_config_load(ini, &c, w, sizeof w), 0);          /* CRLF is fine */
    CHECK_EQ(c.online_mode, BL_ONLINE_PRIVATE);
    CHECK(strcmp(c.host, "192.168.0.9") == 0);
    c.port = 1234;
    CHECK_EQ(bl_config_save(ini, &c), 0);
    char *out = slurp(ini);
    CHECK(strstr(out, "; hand-written") != NULL);
    CHECK(strstr(out, "colour = pink") != NULL);
    CHECK(strstr(out, "[future]") != NULL && strstr(out, "thing = 42") != NULL);
    CHECK(strstr(out, "port = 1234") != NULL);
    BlConfig d; CHECK_EQ(bl_config_load(ini, &d, w, sizeof w), 0);
    CHECK_EQ(d.port, 1234);
    CHECK(strcmp(d.host, "192.168.0.9") == 0);
}

static void test_junk_and_bad_values(void)
{
    /* 1 MB of random bytes: defaults, a warning, no crash. */
    size_t n = 1u << 20;
    char *junk = malloc(n);
    srand(7);
    for (size_t i = 0; i < n; i++) junk[i] = (char)(rand() & 255);
    put(ini, junk, n);
    free(junk);
    BlConfig c, d; char w[256] = "";
    bl_config_defaults(&d);
    CHECK_EQ(bl_config_load(ini, &c, w, sizeof w), 1);
    CHECK(w[0] != 0);
    CHECK(memcmp(&c, &d, sizeof c) == 0);
    /* Out-of-range port and unknown model fall back with a warning. */
    const char *bad = "[online]\nport = 70000\n[console]\nmodel = banana\n[overlay]\nstats = huge\n[logging]\nenabled = maybe\n[renderer]\nmode = vulkan\n";
    put(ini, bad, strlen(bad));
    w[0] = 0;
    CHECK_EQ(bl_config_load(ini, &c, w, sizeof w), 1);
    CHECK_EQ(c.port, 0);
    CHECK_EQ(c.model, BL_MODEL_AUTO);
    CHECK_EQ(c.stats, BL_STATS_OFF);
    CHECK_EQ(c.renderer, BL_RENDERER_NATIVE);
    CHECK(strstr(w, "renderer") != NULL);
    CHECK_EQ(c.logging, 0);
    CHECK(strstr(w, "port") != NULL && strstr(w, "model") != NULL);
    /* Over-long values are rejected, not truncated into something else. */
    char longhost[400] = "[online]\nhost = ";
    memset(longhost + strlen(longhost), 'a', 300);
    put(ini, longhost, strlen(longhost));
    CHECK_EQ(bl_config_load(ini, &c, w, sizeof w), 1);
    CHECK_EQ(c.host[0], 0);
    /* Saving after junk writes a clean file. */
    put(ini, "\x01\x02\x00garbage", 10);
    CHECK_EQ(bl_config_load(ini, &c, w, sizeof w), 1);
    CHECK_EQ(bl_config_save(ini, &c), 0);
    CHECK_EQ(bl_config_load(ini, &c, w, sizeof w), 0);
}

static int failing_rename(const char *a, const char *b) { (void)a; (void)b; return -1; }

static void test_atomic_save(void)
{
    BlConfig c;
    bl_config_defaults(&c);
    c.port = 1111;
    CHECK_EQ(bl_config_save(ini, &c), 0);
    char before[4096]; snprintf(before, sizeof before, "%s", slurp(ini));
    c.port = 2222;
    bl_fs_rename = failing_rename;                  /* power cut before the rename */
    CHECK(bl_config_save(ini, &c) < 0);
    bl_fs_rename = rename;
    CHECK(strcmp(before, slurp(ini)) == 0);         /* original intact */
    char tmp[320]; snprintf(tmp, sizeof tmp, "%s.tmp", ini);
    CHECK(access(tmp, F_OK) != 0);                  /* no temp file left behind */
}

static void test_secret(void)
{
    BlConfig c;
    bl_config_defaults(&c);
    c.login_account = 1;
    snprintf(c.username, sizeof c.username, "joker");
    CHECK_EQ(bl_secret_save(secret, "hunter2-very-secret"), 0);
    CHECK_EQ(bl_config_save(ini, &c), 0);
    struct stat st;
    CHECK_EQ(stat(secret, &st), 0);
    CHECK_EQ(st.st_mode & 0777, 0600);
    char pw[64] = "";
    CHECK_EQ(bl_secret_load(secret, pw, sizeof pw), 0);
    CHECK(strcmp(pw, "hunter2-very-secret") == 0);
    CHECK(strstr(slurp(ini), "hunter2") == NULL);
    /* Empty password deletes the file. */
    CHECK_EQ(bl_secret_save(secret, ""), 0);
    CHECK(access(secret, F_OK) != 0);
    CHECK_EQ(bl_secret_load(secret, pw, sizeof pw), 1);
    CHECK_EQ(pw[0], 0);
}

/* Owner 2026-11-16: Frame rate 30 FPS (default) or 60 FPS, [game] frame_rate.
 * A launcher.ini from before has no such key and loads as 30. */
static void test_frame_rate(void)
{
    BlConfig c;
    bl_config_defaults(&c);
    CHECK_EQ(c.frame_rate, BL_FPS_30);
    const char *old = "[launcher]\nschema = 1\n[logging]\nenabled = true\n[overlay]\nstats = fps\n";
    put(ini, old, strlen(old));
    char w[256] = "";
    CHECK_EQ(bl_config_load(ini, &c, w, sizeof w), 0);
    CHECK_EQ(c.frame_rate, BL_FPS_30);
    CHECK_EQ(c.logging, 1);
    c.frame_rate = BL_FPS_60;
    CHECK_EQ(bl_config_save(ini, &c), 0);
    CHECK(strstr(slurp(ini), "[game]\nframe_rate = 60\n") != NULL);
    BlConfig d;
    CHECK_EQ(bl_config_load(ini, &d, w, sizeof w), 0);
    CHECK_EQ(d.frame_rate, BL_FPS_60);
    d.frame_rate = BL_FPS_30;
    CHECK_EQ(bl_config_save(ini, &d), 0);
    CHECK(strstr(slurp(ini), "frame_rate = 30\n") != NULL);
    CHECK(strstr(slurp(ini), "frame_rate = 60") == NULL);
    /* Anything else: 30, with a warning. */
    const char *bad = "[game]\nframe_rate = 120\n";
    put(ini, bad, strlen(bad));
    w[0] = 0;
    CHECK_EQ(bl_config_load(ini, &c, w, sizeof w), 1);
    CHECK_EQ(c.frame_rate, BL_FPS_30);
    CHECK(strstr(w, "frame rate") != NULL);
    unlink(ini);
}

int main(void)
{
    setup();
    RUN(test_frame_rate);
    RUN(test_defaults);
    RUN(test_round_trip);
    RUN(test_unknown_keys_survive);
    RUN(test_junk_and_bad_values);
    RUN(test_atomic_save);
    RUN(test_secret);
    RUN(test_folder_is_flushed);
    RUN(test_tmp_link_is_not_followed);
    RUN(test_long_unknown_line_survives);
    return bl_test_report("test_config");
}
