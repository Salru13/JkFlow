/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host test for n245_seed_import2 (BLUR 1.00 plan Task T1 item 4).
 * Build and run: title/tests/run_title_tests.sh TREE
 * Runs in a fresh temporary folder; prints PASS/FAIL per check. */
#include "native245_seed.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures, checks;
#define CHECK(cond, what) do { checks++; if (cond) printf("PASS %s\n", what); \
    else { failures++; printf("FAIL %s (line %d)\n", what, __LINE__); } } while (0)

static void hexsha(const void *data, size_t n, char out[65])
{
    N245Sha256 s;
    n245_sha256_init(&s);
    n245_sha256_update(&s, data, n);
    n245_sha256_hex(&s, out);
}

static void mkdirs(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0777); *p = '/'; }
    mkdir(tmp, 0777);
}

static void put(const char *path, const char *text)
{
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = 0; mkdirs(dir); }
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(2); }
    fputs(text, f);
    fclose(f);
}

static int equals(const char *path, const char *text)
{
    char buf[4096];
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return !strcmp(buf, text);
}

static int exists(const char *path) { struct stat st; return stat(path, &st) == 0; }

typedef struct { const char *rel, *text; } Entry;

/* Writes <app>/seed245/blur-native248/{seed.idx,objects/...}; *end = the END hash. */
static void make_seed(const char *app, const Entry *e, int n, char end[65])
{
    char path[1024], sha[65], body[8192] = "", line[512];
    const char *profile = "[application]\nid = seed\n";
    snprintf(path, sizeof path, "%s/seed245/blur-native248/objects", app);
    mkdirs(path);
    hexsha(profile, strlen(profile), sha);
    snprintf(path, sizeof path, "%s/seed245/blur-native248/objects/%s", app, sha);
    put(path, profile);
    strcat(body, "N245-SEED 1\nslug blur-native248\n");
    strcat(body, "manifest_end 0000000000000000000000000000000000000000000000000000000000000000\n");
    snprintf(line, sizeof line, "P\t%zu\t%s\n", strlen(profile), sha);
    strcat(body, line);
    strcat(body, "D\tdrive_c\nD\tdrive_c/users\n");
    for (int i = 0; i < n; i++) {
        hexsha(e[i].text, strlen(e[i].text), sha);
        snprintf(path, sizeof path, "%s/seed245/blur-native248/objects/%s", app, sha);
        put(path, e[i].text);
        snprintf(line, sizeof line, "F\t%zu\t%s\t%s\t-\n", strlen(e[i].text), sha, e[i].rel);
        strcat(body, line);
    }
    hexsha(body, strlen(body), end);
    snprintf(line, sizeof line, "END %s\n", end);
    strcat(body, line);
    snprintf(path, sizeof path, "%s/seed245/blur-native248/seed.idx", app);
    put(path, body);
}

static unsigned progress_calls, progress_last_done, progress_total;
static void progress(void *ctx, unsigned done, unsigned total)
{
    (void)ctx;
    progress_calls++;
    progress_last_done = done;
    progress_total = total;
}

static int fault_after = -1, fault_count;
static int fault(const char *op, const char *path)
{
    (void)path;
    if (fault_after < 0 || strcmp(op, "rename")) return 0;
    return fault_count++ == fault_after;
}

int main(void)
{
    char base[256], app1[300], app2[300], app3[300], prefix[300], path[1024];
    char end1[65], end2[65], end3[65], marker[200];
    N245Result r;
    N245Status st;
    snprintf(base, sizeof base, "/tmp/test_seed_import2.%d", (int)getpid());
    mkdirs(base);
    snprintf(app1, sizeof app1, "%s/app1", base);
    snprintf(app2, sizeof app2, "%s/app2", base);
    snprintf(app3, sizeof app3, "%s/app3", base);
    snprintf(prefix, sizeof prefix, "%s/data/BLUR/prefix", base);

    const Entry v1[] = { { "system.reg", "reg v1\n" }, { "drive_c/windows/a.dll", "dll v1\n" },
                         { "drive_c/users/BLUR/AppData/save.txt", "seed save v1\n" } };
    const Entry v2[] = { { "system.reg", "reg v2\n" }, { "drive_c/windows/a.dll", "dll v2\n" },
                         { "drive_c/users/BLUR/AppData/save.txt", "seed save v2\n" },
                         { "drive_c/windows/new.dll", "new in v2\n" } };
    const Entry v3[] = { { "system.reg", "reg v3\n" }, { "drive_c/windows/a.dll", "dll v3\n" },
                         { "drive_c/users/BLUR/AppData/save.txt", "seed save v3\n" },
                         { "drive_c/windows/new.dll", "new in v3\n" } };
    make_seed(app1, v1, 3, end1);
    make_seed(app2, v2, 4, end2);
    make_seed(app3, v3, 4, end3);
    const char *roots1[] = { "/nonexistent", app1 }, *roots2[] = { app2 }, *roots3[] = { app3 };

    /* Fresh import. */
    st = n245_seed_import2(roots1, 2, prefix, end1, &r, progress, NULL, 1);
    CHECK(st == N245_IMPORTED, "fresh import -> imported");
    CHECK(r.copied == 3 && r.files_total == 3, "fresh import copied 3 of 3");
    snprintf(path, sizeof path, "%s/drive_c/windows/a.dll", prefix);
    CHECK(equals(path, "dll v1\n"), "fresh file content");
    snprintf(path, sizeof path, "%s/.n245-complete", prefix);
    CHECK(exists(path), "marker in prefix");
    snprintf(path, sizeof path, "%s.n245-staging", prefix);
    CHECK(!exists(path), "staging renamed away");
    CHECK(progress_calls >= 4 && progress_last_done == 3 && progress_total == 3, "progress reached total");
    snprintf(path, sizeof path, "%s/data/BLUR/profiles", base);
    CHECK(!exists(path), "no profiles folder written");
    snprintf(path, sizeof path, "%s/data/BLUR/prefixes", base);
    CHECK(!exists(path), "no prefixes folder written");

    /* Second start: fast path. */
    st = n245_seed_import2(roots1, 2, prefix, end1, &r, NULL, NULL, 1);
    CHECK(st == N245_PRESENT, "second start -> present");

    /* The player changes the save; a new seed arrives. */
    snprintf(path, sizeof path, "%s/drive_c/users/BLUR/AppData/save.txt", prefix);
    put(path, "PLAYER SAVE\n");
    st = n245_seed_import2(roots2, 1, prefix, end2, &r, NULL, NULL, 0);
    CHECK(st == N245_REFUSED_FOREIGN_PREFIX, "new seed without update mode -> refused");
    snprintf(path, sizeof path, "%s/drive_c/windows/a.dll", prefix);
    CHECK(equals(path, "dll v1\n"), "refusal changed nothing");
    st = n245_seed_import2(roots2, 1, prefix, end2, &r, NULL, NULL, 1);
    CHECK(st == N245_UPDATED, "new seed in update mode -> updated");
    CHECK(equals(path, "dll v2\n"), "update replaced a file outside users/");
    snprintf(path, sizeof path, "%s/system.reg", prefix);
    CHECK(equals(path, "reg v2\n"), "update replaced the registry hive");
    snprintf(path, sizeof path, "%s/drive_c/windows/new.dll", prefix);
    CHECK(equals(path, "new in v2\n"), "update added a new file");
    snprintf(path, sizeof path, "%s/drive_c/users/BLUR/AppData/save.txt", prefix);
    CHECK(equals(path, "PLAYER SAVE\n"), "update never touched the player's save");
    snprintf(marker, sizeof marker, "N245-COMPLETE %s\n", end2);
    snprintf(path, sizeof path, "%s/.n245-complete", prefix);
    CHECK(equals(path, marker), "marker names the new seed");
    st = n245_seed_import2(roots2, 1, prefix, end2, &r, NULL, NULL, 1);
    CHECK(st == N245_PRESENT, "after update -> present");

    /* A missing file under users/ is created by an update. */
    snprintf(path, sizeof path, "%s/drive_c/users/BLUR/AppData/save.txt", prefix);
    unlink(path);
    snprintf(path, sizeof path, "%s/.n245-complete", prefix);
    unlink(path);
    st = n245_seed_import2(roots2, 1, prefix, end2, &r, NULL, NULL, 1);
    snprintf(path, sizeof path, "%s/drive_c/users/BLUR/AppData/save.txt", prefix);
    CHECK(st == N245_UPDATED && equals(path, "seed save v2\n"), "update creates a missing users/ file");

    /* Interrupted update: the old marker stays, the next start finishes it. */
    n245_test_skip_fsync = 1;
    fault_after = 1; fault_count = 0;
    n245_test_fault = fault;
    st = n245_seed_import2(roots3, 1, prefix, end3, &r, NULL, NULL, 1);
    n245_test_fault = NULL;
    CHECK(st == N245_ERR_IO, "update interrupted at the second rename -> err-io");
    snprintf(path, sizeof path, "%s/.n245-complete", prefix);
    CHECK(equals(path, marker), "interrupted update keeps the old marker");
    st = n245_seed_import2(roots3, 1, prefix, end3, &r, NULL, NULL, 1);
    CHECK(st == N245_UPDATED && r.kept >= 1, "rerun finishes the update");
    snprintf(path, sizeof path, "%s/drive_c/windows/new.dll", prefix);
    CHECK(equals(path, "new in v3\n"), "rerun content");

    /* Interrupted fresh import resumes from staging (rename 0 is the owner
     * stamp, 1 the first file: fail at the second file). */
    char prefix2[300];
    snprintf(prefix2, sizeof prefix2, "%s/data/BLUR2/prefix", base);
    fault_after = 2; fault_count = 0;
    n245_test_fault = fault;
    st = n245_seed_import2(roots1, 2, prefix2, end1, &r, NULL, NULL, 1);
    n245_test_fault = NULL;
    CHECK(st == N245_ERR_IO, "fresh import interrupted -> err-io");
    CHECK(!exists(prefix2), "no prefix after an interrupted fresh import");
    st = n245_seed_import2(roots1, 2, prefix2, end1, &r, NULL, NULL, 0);
    CHECK(st == N245_IMPORTED && r.kept >= 1 && r.copied >= 1, "fresh import resumes from staging");

    /* Staging left by another seed is adopted (every file is verified). */
    char prefix3[300];
    snprintf(prefix3, sizeof prefix3, "%s/data/BLUR3/prefix", base);
    fault_after = 0; fault_count = 0;
    n245_test_fault = fault;
    (void)n245_seed_import2(roots1, 2, prefix3, end1, &r, NULL, NULL, 1);
    n245_test_fault = NULL;
    st = n245_seed_import2(roots2, 1, prefix3, end2, &r, NULL, NULL, 1);
    snprintf(path, sizeof path, "%s/drive_c/windows/a.dll", prefix3);
    CHECK(st == N245_IMPORTED && equals(path, "dll v2\n"), "staging of another seed adopted and verified");

    /* Wrong pin, no seed, bad arguments. */
    st = n245_seed_import2(roots1, 2, prefix, end2, &r, NULL, NULL, 1);
    CHECK(st == N245_ERR_SEED, "unpinned index -> err-seed");
    const char *none[] = { "/nonexistent" };
    st = n245_seed_import2(none, 1, prefix, end1, &r, NULL, NULL, 1);
    CHECK(st == N245_NO_SEED, "no seed in the package -> no-seed");
    st = n245_seed_import2(roots1, 2, "relative/prefix", end1, &r, NULL, NULL, 1);
    CHECK(st == N245_ERR_ARGS, "relative prefix -> err-args");
    st = n245_seed_import2(roots1, 2, prefix, NULL, &r, NULL, NULL, 1);
    CHECK(st == N245_DISABLED, "no pin -> disabled");

    char cmd[400];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", base);
    if (system(cmd) != 0) printf("note: could not remove %s\n", base);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
