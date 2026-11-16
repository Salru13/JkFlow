/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests for the optional save import (BLUR 1.00 plan Task L6, Review
 * Focus 4, decision D20). */
#define _POSIX_C_SOURCE 200809L
#include "bl_fs.h"
#include "bl_import.h"
#include "bl_sha256.h"
#include "bl_test.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char root[256], cur_dir[400];
static BlPaths P;

static void sh(const char *cmd) { if (system(cmd) != 0) fprintf(stderr, "  command failed: %s\n", cmd); }

/* A save: magic 0xABCDE337 (LE), then sizes and a body. */
static void put_save(const char *dir, const char *name, const char *body)
{
    char p[800]; snprintf(p, sizeof p, "%s/%s", dir, name);
    bl_fs_mkdirs(dir);
    FILE *f = fopen(p, "wb");
    unsigned char hdr[12] = { 0x37, 0xE3, 0xCD, 0xAB, 1, 0, 0, 0, 2, 0, 0, 0 };
    fwrite(hdr, 1, 12, f);
    fputs(body, f);
    fclose(f);
}

static void put(const char *dir, const char *name, const char *text)
{
    char p[800]; snprintf(p, sizeof p, "%s/%s", dir, name);
    bl_fs_mkdirs(dir);
    FILE *f = fopen(p, "wb"); fputs(text, f); fclose(f);
}

static void hash(const char *dir, const char *name, char out[65])
{
    char p[800]; snprintf(p, sizeof p, "%s/%s", dir, name);
    if (bl_fs_sha256(p, out, NULL) != 0) out[0] = 0;
}

static int exists(const char *dir, const char *name)
{
    char p[800]; snprintf(p, sizeof p, "%s/%s", dir, name);
    return access(p, F_OK) == 0;
}

/* The one entry of dir whose name starts with prefix, or "". */
static void entry_with(const char *dir, const char *prefix, char *out, size_t cap)
{
    out[0] = 0;
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d))) if (!strncmp(e->d_name, prefix, strlen(prefix))) snprintf(out, cap, "%s", e->d_name);
    if (d) closedir(d);
}

static void fresh(void)
{
    char cmd[400];
    if (root[0]) { snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); sh(cmd); }
    snprintf(root, sizeof root, "/tmp/bl-import-XXXXXX");
    if (!mkdtemp(root)) exit(1);
    bl_paths_init(&P, root);
    bl_paths_make(&P);
    snprintf(cur_dir, sizeof cur_dir, "%s/save", P.saves);
}

static void test_none(void)
{
    fresh();
    CHECK_EQ(bl_import_check(&P), BL_IMPORT_NONE);
    char b[256];
    CHECK_EQ(bl_import_apply(&P, 0, b, sizeof b), 1);
}

static void test_ready_applies_and_renames(void)
{
    fresh();
    put_save(P.import, "blursave", "imported progress");
    CHECK_EQ(bl_import_check(&P), BL_IMPORT_READY);
    char want[65], got[65], b[256] = "x";
    hash(P.import, "blursave", want);
    CHECK_EQ(bl_import_apply(&P, 0, b, sizeof b), 0);       /* READY applies without asking */
    hash(cur_dir, "blursave", got);
    CHECK(!strcmp(want, got));
    CHECK(!exists(P.import, "blursave"));
    char name[128];
    entry_with(P.import, "blursave.imported-", name, sizeof name);
    CHECK(name[0] != 0);
    CHECK_EQ(b[0], 0);                                       /* nothing was backed up */
}

static void test_conflict_default_keep(void)
{
    fresh();
    put_save(cur_dir, "blursave", "current progress");
    put_save(P.import, "blursave", "imported progress");
    CHECK_EQ(bl_import_check(&P), BL_IMPORT_CONFLICT);
    char before[65], after[65], b[256];
    hash(cur_dir, "blursave", before);
    CHECK_EQ(bl_import_apply(&P, 0, b, sizeof b), 0);
    hash(cur_dir, "blursave", after);
    CHECK(!strcmp(before, after));                           /* byte-equal */
    char name[128];
    entry_with(P.import, "blursave.kept-", name, sizeof name);
    CHECK(name[0] != 0);
    CHECK(!exists(P.import, "blursave"));
    CHECK_EQ(bl_import_check(&P), BL_IMPORT_NONE);
}

static void test_replace_backs_up_first(void)
{
    fresh();
    put_save(cur_dir, "blursave", "current progress");
    put_save(P.import, "blursave", "imported progress");
    char old[65], imp[65], got[65], b[256] = "";
    hash(cur_dir, "blursave", old);
    hash(P.import, "blursave", imp);
    CHECK_EQ(bl_import_apply(&P, 1, b, sizeof b), 0);
    hash(cur_dir, "blursave", got);
    CHECK(!strcmp(got, imp));
    CHECK(b[0] != 0 && !strncmp(b, P.backup, strlen(P.backup)));
    char bsave[400]; snprintf(bsave, sizeof bsave, "%s/save", b);
    hash(bsave, "blursave", got);
    CHECK(!strcmp(got, old));
    char name[128];
    entry_with(P.import, "blursave.imported-", name, sizeof name);
    CHECK(name[0] != 0);
}

/* The backup must be complete before the current save changes. */
static int g_renames, g_fail_at;
static int failing_rename(const char *a, const char *b2)
{
    if (++g_renames == g_fail_at) return -1;
    return rename(a, b2);
}

static void test_replace_failure_keeps_current(void)
{
    for (int k = 1; k <= 3; k++) {
        fresh();
        put_save(cur_dir, "blursave", "current progress");
        put_save(P.import, "blursave", "imported progress");
        char old[65], imp[65], got[65], b[256];
        hash(cur_dir, "blursave", old);
        hash(P.import, "blursave", imp);
        g_renames = 0; g_fail_at = k;
        bl_fs_rename = failing_rename;
        int r = bl_import_apply(&P, 1, b, sizeof b);
        bl_fs_rename = rename;
        hash(cur_dir, "blursave", got);
        CHECK(!strcmp(got, old) || !strcmp(got, imp));      /* never a partial save */
        if (r != 0) CHECK(exists(P.import, "blursave"));   /* not marked as imported */
    }
}

static void test_invalid_magic_refused(void)
{
    fresh();
    put(P.import, "blursave", "not a Blur save at all");
    CHECK_EQ(bl_import_check(&P), BL_IMPORT_INVALID);
    char b[256];
    CHECK(bl_import_apply(&P, 1, b, sizeof b) < 0);
    CHECK(!exists(cur_dir, "blursave"));
    fresh();
    put(P.import, "blursave", "\x37\xE3\xCD");               /* shorter than the header */
    CHECK_EQ(bl_import_check(&P), BL_IMPORT_INVALID);
}

static void test_never_applied_twice(void)
{
    fresh();
    put_save(P.import, "blursave", "imported progress");
    char b[256];
    CHECK_EQ(bl_import_apply(&P, 0, b, sizeof b), 0);
    CHECK_EQ(bl_import_check(&P), BL_IMPORT_NONE);
    put_save(cur_dir, "blursave", "played on since");
    char before[65], after[65];
    hash(cur_dir, "blursave", before);
    CHECK_EQ(bl_import_apply(&P, 1, b, sizeof b), 1);
    hash(cur_dir, "blursave", after);
    CHECK(!strcmp(before, after));
}

static void test_settings_xml_ignored(void)
{
    fresh();
    put(P.saves, "settings.xml", "<current/>");
    put(P.import, "settings.xml", "<imported/>");
    put_save(P.import, "blursave", "imported progress");
    char b[256], h[65], w[65];
    CHECK_EQ(bl_import_apply(&P, 0, b, sizeof b), 0);
    hash(P.saves, "settings.xml", h);
    bl_sha256_hex("<current/>", 10, w);
    CHECK(!strcmp(h, w));
    CHECK(exists(P.import, "settings.xml"));
    CHECK(!exists(cur_dir, "settings.xml"));
}

int main(void)
{
    RUN(test_none);
    RUN(test_ready_applies_and_renames);
    RUN(test_conflict_default_keep);
    RUN(test_replace_backs_up_first);
    RUN(test_replace_failure_keeps_current);
    RUN(test_invalid_magic_refused);
    RUN(test_never_applied_twice);
    RUN(test_settings_xml_ignored);
    char cmd[400]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); sh(cmd);
    return bl_test_report("test_import");
}
