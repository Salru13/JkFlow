/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#define _POSIX_C_SOURCE 200809L
#include "bl_paths.h"
#include "bl_fs.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static char default_root[64];

void bl_paths_set_default_root(const char *root)
{
    default_root[0] = 0;
    if (root && root[0] && strlen(root) < sizeof default_root) snprintf(default_root, sizeof default_root, "%s", root);
}

/* A root ".../<parent>/System": Logs is its sibling, in the same parent. */
static int logs_beside(const char *root, char *out, size_t cap)
{
    const char *slash = strrchr(root, '/');
    if (!slash || slash == root || strcmp(slash + 1, "System")) return 0;
    return snprintf(out, cap, "%.*s/Logs", (int)(slash - root), root) < (int)cap;
}

#define SET(field, base, sub) ok = ok && snprintf(p->field, sizeof p->field, "%s/%s", base, sub) < (int)sizeof p->field

void bl_paths_init(BlPaths *p, const char *root)
{
    if (!p) return;
    memset(p, 0, sizeof *p);
    if (!root || !root[0]) root = default_root[0] ? default_root : BL_DATA_ROOT;
    int ok = snprintf(p->root, sizeof p->root, "%s", root) < (int)sizeof p->root;
    SET(game, root, "game");
    SET(backup, root, "backup");
    SET(prefix, root, "prefix");
    SET(cache, root, "cache");
    SET(state, root, "state");
    SET(import, root, "import");
    if (ok && logs_beside(root, p->logs, sizeof p->logs)) {
        SET(support, p->logs, "support");
    } else {
        SET(logs, root, "logs");
        SET(support, root, "support");
    }
    SET(saves, root, "prefix/drive_c/users/BLUR/AppData/Roaming/Bizarre Creations/Blur");
    if (!ok) memset(p, 0, sizeof *p);
}

int bl_paths_make(const BlPaths *p)
{
    if (!p || !p->root[0]) return -1;
    const char *dirs[] = { p->backup, p->prefix, p->cache, p->logs, p->support, p->state, p->import };
    for (unsigned i = 0; i < sizeof dirs / sizeof dirs[0]; i++)
        if (bl_fs_mkdirs(dirs[i]) != 0) return -1;
    return 0;
}

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Up to two matches of name (letter case ignored) among the folders in dir,
 * in strcmp order. Returns how many there are (2 means "two or more"). */
static int find_ci(const char *dir, const char *name, char m[2][256])
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcasecmp(e->d_name, name) || strlen(e->d_name) >= 256) continue;
        char p[1024];
        if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p || !is_dir(p)) continue;
        if (n < 2) snprintf(m[n++], 256, "%s", e->d_name);
        else if (strcmp(e->d_name, m[1]) < 0) snprintf(m[1], 256, "%s", e->d_name);
        if (n == 2 && strcmp(m[0], m[1]) > 0) { char t[256]; memcpy(t, m[0], 256); memcpy(m[0], m[1], 256); memcpy(m[1], t, 256); }
    }
    closedir(d);
    return n;
}

int bl_game_dir_find(const char *data, char *out, size_t cap, char *other, size_t ocap)
{
    if (other && ocap) other[0] = 0;
    if (!out || !cap) return 0;
    snprintf(out, cap, "%s/%s/%s", data ? data : "", BL_GAME_PARENT, BL_GAME_LEAF);
    if (!data) return 0;
    char parents[2][256], found[4][1024];
    int np = find_ci(data, BL_GAME_PARENT, parents), nf = 0;
    for (int i = 0; i < np; i++) {
        char pdir[600], leaves[2][256];
        if (snprintf(pdir, sizeof pdir, "%s/%s", data, parents[i]) >= (int)sizeof pdir) continue;
        int nl = find_ci(pdir, BL_GAME_LEAF, leaves);
        for (int j = 0; j < nl; j++)
            if (snprintf(found[nf], sizeof found[0], "%s/%s", pdir, leaves[j]) < (int)sizeof found[0]) nf++;
    }
    if (nf == 0) return 0;
    snprintf(out, cap, "%s", found[0]);
    if (nf == 1) return 1;
    if (other && ocap) snprintf(other, ocap, "%s", found[1]);
    return -1;
}

int bl_jkflow_dir(const char *data, char *out, size_t cap, int create)
{
    if (!data || !out || !cap) return -1;
    char first[256] = "", exact[256] = "", game[256] = "";
    int games = 0;
    DIR *d = opendir(data);
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL) {
        char dir[1024], leaves[2][256];
        if (strcasecmp(e->d_name, BL_GAME_PARENT) || strlen(e->d_name) >= sizeof first) continue;
        if (snprintf(dir, sizeof dir, "%s/%s", data, e->d_name) >= (int)sizeof dir || !is_dir(dir)) continue;
        if (!first[0] || strcmp(e->d_name, first) < 0) snprintf(first, sizeof first, "%s", e->d_name);
        if (!strcmp(e->d_name, BL_GAME_PARENT)) snprintf(exact, sizeof exact, "%s", e->d_name);
        if (find_ci(dir, BL_GAME_LEAF, leaves) > 0 && games++ == 0) snprintf(game, sizeof game, "%s", e->d_name);
    }
    if (d) closedir(d);
    const char *pick = games == 1 ? game : exact[0] ? exact : first;
    if (pick[0]) return snprintf(out, cap, "%s/%s", data, pick) < (int)cap ? 1 : -1;
    if (snprintf(out, cap, "%s/%s", data, BL_GAME_PARENT) >= (int)cap) return -1;
    if (create && mkdir(out, 0777) != 0 && errno != EEXIST) return -1;
    if (create) (void)chmod(out, 0777);        /* read over FTP by a server that is not the title */
    return 0;
}

static void say(BlPathsLog log, void *ctx, const char *fmt, const char *a, const char *b)
{
    if (!log) return;
    char line[2000];
    snprintf(line, sizeof line, fmt, a, b ? b : "");
    log(ctx, line);
}

static int entry_exists(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0;
}

/* The folder that holds path: made when missing. */
static int make_parent(const char *path)
{
    char parent[600];
    const char *slash = strrchr(path, '/');
    if (!slash || slash == path) return 0;
    snprintf(parent, sizeof parent, "%.*s", (int)(slash - path), path);
    return bl_fs_mkdirs(parent);
}

/* 1 when dir holds a file named name (letter case ignored) directly. */
static int has_file_ci(const char *dir, const char *name)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    int found = 0;
    struct dirent *e;
    while (!found && (e = readdir(d)) != NULL) {
        char p[1024];
        struct stat st;
        if (strcasecmp(e->d_name, name) || snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) continue;
        found = stat(p, &st) == 0 && S_ISREG(st.st_mode);
    }
    closedir(d);
    return found;
}

static int dir_empty(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    int empty = 1;
    struct dirent *e;
    while (empty && (e = readdir(d)) != NULL) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) empty = 0;
    closedir(d);
    return empty;
}

/* Audit 5 (owner 2026-11-16; the title has the same rule): an old root
 * holding Blur.exe directly (any letter case) is the player's GAME, not old
 * launcher data. It becomes <JkFlow>/BLUR (the folder above p->root) in one
 * rename when no game is there (missing, or an empty folder of any letter
 * case); with a game there nothing moves. Never merged into System.
 * 1: a game folder, *moved = entries moved (1 or 0); 0: not a game folder. */
static int migrate_game(const char *old_root, const BlPaths *p, BlPathsLog log, void *ctx, int *moved)
{
    *moved = 0;
    if (!has_file_ci(old_root, "Blur.exe")) return 0;
    char jkflow[96], target[700], m[2][256];
    const char *slash = strrchr(p->root, '/');
    if (!slash || slash == p->root || (size_t)(slash - p->root) >= sizeof jkflow) {
        say(log, ctx, "migrate: %s kept (no JkFlow folder for it)", old_root, NULL);
        return 1;
    }
    snprintf(jkflow, sizeof jkflow, "%.*s", (int)(slash - p->root), p->root);
    int there = find_ci(jkflow, "BLUR", m);
    snprintf(target, sizeof target, "%s/%s", jkflow, there ? m[0] : "BLUR");
    if (there > 1 || (there == 1 && !dir_empty(target))) {
        say(log, ctx, "migrate: %s kept (game already present)", old_root, NULL);
        return 1;
    }
    if (bl_fs_mkdirs(jkflow) != 0 || rename(old_root, target) != 0) {
        say(log, ctx, "migrate: %s kept (game folder not moved to %s)", old_root, target);
        return 1;
    }
    say(log, ctx, "migrate: %s is a game folder -> JkFlow/BLUR", old_root, NULL);
    *moved = 1;
    return 1;
}

static int by_logs_first(const void *a, const void *b)
{
    const char *x = *(const char *const *)a, *y = *(const char *const *)b;
    int lx = !strcmp(x, "logs"), ly = !strcmp(y, "logs");
    if (lx != ly) return ly - lx;          /* logs first: support goes inside the Logs it becomes */
    return strcmp(x, y);
}

int bl_paths_migrate(const char *old_root, const BlPaths *p, BlPathsLog log, void *ctx)
{
    if (!p || !p->root[0]) return -1;
    if (!old_root || !is_dir(old_root)) return 0;
    int game_moved;
    if (migrate_game(old_root, p, log, ctx, &game_moved)) return game_moved;
    char **names = NULL;
    int n = 0, cap = 0, moved = 0, failed = 0;
    DIR *d = opendir(old_root);
    if (!d) { say(log, ctx, "migrate: %s cannot be read, not moved", old_root, NULL); return -1; }
    struct dirent *e;
    /* Any number of entries; out of memory leaves the rest for the next start. */
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (n == cap) {
            char **more = realloc(names, (size_t)(cap ? cap * 2 : 64) * sizeof *names);
            if (!more) { failed++; break; }
            names = more;
            cap = cap ? cap * 2 : 64;
        }
        if (!(names[n] = strdup(e->d_name))) { failed++; break; }
        n++;
    }
    closedir(d);
    if (n) qsort(names, (size_t)n, sizeof names[0], by_logs_first);
    for (int i = 0; i < n; i++) {
        char from[600], to[600], alt[620];
        const char *target = !strcmp(names[i], "logs") ? p->logs : !strcmp(names[i], "support") ? p->support : NULL;
        int ok = snprintf(from, sizeof from, "%s/%s", old_root, names[i]) < (int)sizeof from &&
                 (target ? snprintf(to, sizeof to, "%s", target) : snprintf(to, sizeof to, "%s/%s", p->root, names[i])) < (int)sizeof to;
        if (!ok) { failed++; say(log, ctx, "migrate: %s%s: name too long, not moved", old_root, names[i]); continue; }
        if (make_parent(to) != 0) { failed++; say(log, ctx, "migrate: %s not moved: cannot make the folder for %s", from, to); continue; }
        const char *dest = to;
        /* An empty folder there holds nothing of the player's (an earlier
         * build made it fresh after a failed move): the old entry takes its
         * place instead of being kept beside it. */
        if (is_dir(to) && rmdir(to) == 0) say(log, ctx, "migrate: %s was empty, replaced by the old entry", to, NULL);
        if (entry_exists(to)) {
            dest = NULL;
            for (int k = 1; k <= 9 && !dest; k++) {
                if (k == 1) snprintf(alt, sizeof alt, "%s.old-BLUR", to);
                else snprintf(alt, sizeof alt, "%s.old-BLUR%d", to, k);
                if (!entry_exists(alt)) dest = alt;
            }
            if (!dest) { failed++; say(log, ctx, "migrate: %s not moved: %s and its .old-BLUR names exist", from, to); continue; }
        }
        if (rename(from, dest) != 0) { failed++; say(log, ctx, "migrate: %s not moved to %s", from, dest); continue; }
        moved++;
        if (dest == to) say(log, ctx, "migrate: moved %s -> %s", from, to);
        else {
            char line[2000];
            snprintf(line, sizeof line, "migrate: kept both: %s exists, the old %s is now %s", to, from, dest);
            if (log) log(ctx, line);
        }
    }
    for (int i = 0; i < n; i++) free(names[i]);
    free(names);
    if (failed == 0 && rmdir(old_root) == 0) { say(log, ctx, "migrate: removed the empty %s", old_root, NULL); return moved; }
    say(log, ctx, "migrate: %s not removed: %s", old_root, failed ? "some entries could not be moved" : "not empty");
    return -1;
}

int bl_paths_pending(const char *old_root)
{
    if (!old_root || !is_dir(old_root)) return 0;
    DIR *d = opendir(old_root);
    if (!d) return 1;                          /* there, but unreadable: assume it holds data */
    struct dirent *e;
    int any = 0;
    while (!any && (e = readdir(d)) != NULL) any = strcmp(e->d_name, ".") && strcmp(e->d_name, "..");
    closedir(d);
    return any;
}
