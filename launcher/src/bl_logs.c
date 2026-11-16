/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#define _POSIX_C_SOURCE 200809L
#include "bl_logs.h"
#include "bl_paths.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef int (*Visit)(const char *path, long long size, void *ctx);

static void visit_if_file(const char *path, Visit fn, void *ctx)
{
    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) fn(path, (long long)st.st_size, ctx);
}

/* Every known log file except <logs>/<keep>, without a limit. The logs
 * folder is the one bl_paths gives for data_root (/data/JkFlow/Logs). */
static void walk(const char *game_dir, const char *data_root, const char *keep, Visit fn, void *ctx)
{
    char p[1024];
    /* The diagnostic files of the fixed set (bl_support.c) and their
     * rotated .old copies. The launcher's own state (events.log, session.txt)
     * is not a log the player clears. */
    static const char *const GAME[] = { "Blur_d3d9.log", "Blur_d3d9.log.old" };
    for (unsigned i = 0; game_dir && i < sizeof GAME / sizeof GAME[0]; i++)
        if (snprintf(p, sizeof p, "%s/%s", game_dir, GAME[i]) < (int)sizeof p) visit_if_file(p, fn, ctx);
    if (!data_root) return;
    BlPaths paths;
    bl_paths_init(&paths, data_root);
    const char *dir = paths.logs;
    if (!dir[0]) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        /* Every engine log goes to the logs folder (BLUR_LOG_DIR). */
        if (e->d_name[0] == '.' || (keep && !strcmp(e->d_name, keep))) continue;
        if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) continue;
        visit_if_file(p, fn, ctx);
    }
    closedir(d);
}

typedef struct List { BlLogFile *out; int max, n; } List;
static int add_to_list(const char *path, long long size, void *ctx)
{
    List *l = ctx;
    if (l->n >= l->max || snprintf(l->out[l->n].path, sizeof l->out[l->n].path, "%s", path) >= (int)sizeof l->out[l->n].path) return 0;
    l->out[l->n++].size = size;
    return 0;
}

int bl_logs_list(const char *game_dir, const char *data_root, BlLogFile *out, int max)
{
    if (!out || max <= 0) return 0;
    List l = { out, max, 0 };
    walk(game_dir, data_root, NULL, add_to_list, &l);
    return l.n;
}

typedef struct Sum { int n; long long bytes; } Sum;
static int add_to_sum(const char *path, long long size, void *ctx)
{
    Sum *s = ctx;
    (void)path;
    s->n++;
    s->bytes += size;
    return 0;
}

int bl_logs_summary(const char *game_dir, const char *data_root, const char *keep, long long *bytes)
{
    Sum s = { 0, 0 };
    walk(game_dir, data_root, keep, add_to_sum, &s);
    if (bytes) *bytes = s.bytes;
    return s.n;
}

static int delete_one(const char *path, long long size, void *ctx)
{
    (void)size;
    if (unlink(path) == 0) ++*(int *)ctx;
    return 0;
}

int bl_logs_clear(const char *game_dir, const char *data_root, const char *keep)
{
    int deleted = 0;
    walk(game_dir, data_root, keep, delete_one, &deleted);
    return deleted;
}
