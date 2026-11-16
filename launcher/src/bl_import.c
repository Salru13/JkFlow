/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#define _POSIX_C_SOURCE 200809L
#include "bl_import.h"
#include "bl_fs.h"
#include "bl_patcher.h"
#include "bl_time.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int import_path(const BlPaths *p, char *out, size_t cap)
{
    return p && p->import[0] && snprintf(out, cap, "%s/%s", p->import, BL_IMPORT_FILE) < (int)cap ? 0 : -1;
}

static int current_path(const BlPaths *p, char *out, size_t cap)
{
    return p && p->saves[0] && snprintf(out, cap, "%s/save/%s", p->saves, BL_IMPORT_FILE) < (int)cap ? 0 : -1;
}

BlImportState bl_import_check(const BlPaths *p)
{
    char imp[400], cur[400];
    if (import_path(p, imp, sizeof imp) || !bl_fs_exists(imp)) return BL_IMPORT_NONE;
    FILE *f = fopen(imp, "rb");
    unsigned char hdr[12];
    size_t n = f ? fread(hdr, 1, sizeof hdr, f) : 0;
    if (f) fclose(f);
    uint32_t magic = n == 12 ? (uint32_t)hdr[0] | (uint32_t)hdr[1] << 8 | (uint32_t)hdr[2] << 16 | (uint32_t)hdr[3] << 24 : 0;
    if (n != 12 || magic != BL_SAVE_MAGIC) return BL_IMPORT_INVALID;
    if (!current_path(p, cur, sizeof cur) && bl_fs_exists(cur)) return BL_IMPORT_CONFLICT;
    return BL_IMPORT_READY;
}

/* import -> <import>/blursave.<what>-<stamp> */
static int retire(const char *imp, const char *what)
{
    char stamp[BL_STAMP_LEN], to[500];
    bl_utc_stamp(stamp);
    if (snprintf(to, sizeof to, "%s.%s-%s", imp, what, stamp) >= (int)sizeof to) return -1;
    return bl_fs_rename(imp, to);
}

static int copy_checked(const char *from, const char *to)
{
    char a[65], b[65], dir[500];
    snprintf(dir, sizeof dir, "%s", to);
    char *s = strrchr(dir, '/');
    if (s) { *s = 0; if (bl_fs_mkdirs(dir) != 0) return -1; }
    if (bl_fs_sha256(from, a, NULL) != 0 || bl_fs_copy(from, to, 0644) != 0 || bl_fs_sha256(to, b, NULL) != 0) return -1;
    return strcmp(a, b) ? -1 : 0;
}

int bl_import_apply(const BlPaths *p, int replace, char *backup_out, size_t cap)
{
    if (backup_out && cap) backup_out[0] = 0;
    char imp[400], cur[400];
    BlImportState st = bl_import_check(p);
    if (st == BL_IMPORT_NONE) return 1;
    if (st == BL_IMPORT_INVALID || import_path(p, imp, sizeof imp) || current_path(p, cur, sizeof cur)) return -1;
    if (st == BL_IMPORT_CONFLICT && !replace) return retire(imp, "kept") == 0 ? 0 : -1;
    if (st == BL_IMPORT_CONFLICT) {
        /* The current save is copied away (and checked) before it changes. */
        char dir[400], to[500];
        if (bl_backup_new_dir(p->backup, dir, sizeof dir) != 0) return -1;
        if (snprintf(to, sizeof to, "%s/save/%s", dir, BL_IMPORT_FILE) >= (int)sizeof to || copy_checked(cur, to) != 0) return -1;
        if (backup_out && cap) snprintf(backup_out, cap, "%s", dir);
    }
    if (copy_checked(imp, cur) != 0) return -1;
    return retire(imp, "imported") == 0 ? 0 : -1;
}
