/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests for the tested-exe gate (BLUR 1.00 plan Task L2). */
#define _POSIX_C_SOURCE 200809L
#include "bl_exe.h"
#include "bl_events.h"
#include "bl_fs.h"
#include "bl_strings.h"
#include "bl_test.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static char root[256];
static BlPaths P;

static void fresh(void)
{
    char cmd[400];
    if (root[0]) { snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); if (system(cmd)) {} }
    snprintf(root, sizeof root, "/tmp/bl-exe-XXXXXX");
    if (!mkdtemp(root)) exit(1);
    bl_paths_init(&P, root);
    bl_paths_make(&P);
    bl_fs_mkdirs(P.game);                                    /* the player's folder (never created by the launcher) */
    bl_exe_expected_sha = BL_TESTED_EXE_SHA;
}

/* A sparse file of the given size whose first bytes are tag. */
static void make_exe(const char *name, long long size, const char *tag)
{
    char p[400];
    snprintf(p, sizeof p, "%s/%s", P.game, name);
    int fd = open(p, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) exit(1);
    if (write(fd, tag, strlen(tag)) < 0 || ftruncate(fd, size) != 0) exit(1);
    close(fd);
}

static void set_mtime(const char *name, long sec)
{
    char p[400];
    snprintf(p, sizeof p, "%s/%s", P.game, name);
    struct timeval tv[2] = { { sec, 0 }, { sec, 0 } };
    utimes(p, tv);
}

static char g_sha[65];
static void use_fixture_sha(const char *name)
{
    char p[400];
    snprintf(p, sizeof p, "%s/%s", P.game, name);
    bl_fs_sha256(p, g_sha, NULL);
    bl_exe_expected_sha = g_sha;
}

static void test_missing(void)
{
    fresh();
    char name[64] = "x";
    CHECK_EQ(bl_exe_verify(&P, name), BL_EXE_MISSING);
    CHECK_EQ(name[0], 0);
}

static void test_wrong_size_no_hash(void)
{
    fresh();
    make_exe("BLUR.EXE", 5, "MZabc");
    char name[64];
    bl_exe_hash_count = 0;
    CHECK_EQ(bl_exe_verify(&P, name), BL_EXE_UNSUPPORTED);
    CHECK_EQ(bl_exe_hash_count, 0);
    CHECK(!strcmp(name, "BLUR.EXE"));
}

static void test_tested(void)
{
    fresh();
    make_exe("Blur.exe", (long long)BL_TESTED_EXE_SIZE, "MZ tested fixture");
    use_fixture_sha("Blur.exe");
    char name[64];
    bl_exe_hash_count = 0;
    CHECK_EQ(bl_exe_verify(&P, name), BL_EXE_TESTED);
    CHECK_EQ(bl_exe_hash_count, 1);
    CHECK(!strcmp(name, "Blur.exe"));
    /* the same size with another hash is not the tested exe */
    fresh();
    make_exe("blur.exe", (long long)BL_TESTED_EXE_SIZE, "MZ another build");
    CHECK_EQ(bl_exe_verify(&P, name), BL_EXE_UNSUPPORTED);
}

static void test_cache_hit(void)
{
    fresh();
    make_exe("Blur.exe", (long long)BL_TESTED_EXE_SIZE, "MZ tested fixture");
    use_fixture_sha("Blur.exe");
    set_mtime("Blur.exe", 1700000000);
    char name[64];
    bl_exe_hash_count = 0;
    CHECK_EQ(bl_exe_verify(&P, name), BL_EXE_TESTED);
    CHECK_EQ(bl_exe_verify(&P, name), BL_EXE_TESTED);
    CHECK_EQ(bl_exe_hash_count, 1);
    char p[400];
    snprintf(p, sizeof p, "%s/exe-check.txt", P.state);
    CHECK(bl_fs_exists(p));
}

static void test_cache_invalidated_on_mtime(void)
{
    fresh();
    make_exe("Blur.exe", (long long)BL_TESTED_EXE_SIZE, "MZ tested fixture");
    use_fixture_sha("Blur.exe");
    set_mtime("Blur.exe", 1700000000);
    char name[64];
    bl_exe_hash_count = 0;
    CHECK_EQ(bl_exe_verify(&P, name), BL_EXE_TESTED);
    /* replaced over FTP with another build of the same size */
    make_exe("Blur.exe", (long long)BL_TESTED_EXE_SIZE, "MZ another build");
    set_mtime("Blur.exe", 1700000500);
    CHECK_EQ(bl_exe_verify(&P, name), BL_EXE_UNSUPPORTED);
    CHECK_EQ(bl_exe_hash_count, 2);
}

static void test_strings_and_code(void)
{
    CHECK(!strcmp(BL_E_EXE_UNSUPPORTED, "E205"));
}

int main(void)
{
    RUN(test_missing);
    RUN(test_wrong_size_no_hash);
    RUN(test_tested);
    RUN(test_cache_hit);
    RUN(test_cache_invalidated_on_mtime);
    RUN(test_strings_and_code);
    char cmd[400]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); if (system(cmd)) {}
    return bl_test_report("test_exe");
}
