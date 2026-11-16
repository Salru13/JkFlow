/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#define _POSIX_C_SOURCE 200809L
#include "bl_native.h"
#include "bl_fs.h"
#include "bl_time.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int bl_native_prepare(const char *game_dir, int native)
{
    char arm[1024];
    if (!game_dir || snprintf(arm, sizeof arm, "%s/%s", game_dir, BL_NATIVE_ARM_FILE) >= (int)sizeof arm) return -1;
    if (unlink(arm) != 0 && errno != ENOENT) return -1;    /* a stale arm file must not survive */
    if (!native) return 0;
    char stamp[BL_STAMP_LEN], text[96];
    bl_utc_stamp(stamp);
    int n = snprintf(text, sizeof text, "armed by blur-launcher %s\n", stamp);
    if (bl_fs_write_atomic(arm, text, (size_t)n, 0644) != 0) { unlink(arm); return -1; }
    return 0;
}

int bl_dir_entries(const char *dir)
{
    DIR *d = dir ? opendir(dir) : NULL;
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n++;
    closedir(d);
    return n;
}
