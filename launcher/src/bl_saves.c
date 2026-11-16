/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#define _POSIX_C_SOURCE 200809L
#include "bl_saves.h"
#include "bl_fs.h"
#include "bl_time.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_DEPTH 6
#define LIST_TEXT (BL_SAVES_MAX_FILES * 340 + 1)

typedef struct Tree {
    char rel[BL_SAVES_MAX_FILES][256];
    char sha[BL_SAVES_MAX_FILES][65];
    unsigned long long size[BL_SAVES_MAX_FILES];
    int n;
} Tree;

static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }

int bl_saves_find2(const BlPaths *p, char *out, size_t cap)
{
    if (!p || !p->saves[0] || !out || !cap) return -1;
    if (!is_dir(p->saves) || snprintf(out, cap, "%s", p->saves) >= (int)cap) return -1;
    return 0;
}

/* Every regular file under root, with its hash. hash 0: sizes only. */
static int walk(const char *root, const char *rel, int depth, Tree *t, int hash)
{
    char dir[1400];
    if (snprintf(dir, sizeof dir, "%s%s%s", root, rel[0] ? "/" : "", rel) >= (int)sizeof dir) return -1;
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    int rc = 0;
    while (rc == 0 && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char r[256], p[1700];
        struct stat st;
        if (snprintf(r, sizeof r, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name) >= (int)sizeof r
            || snprintf(p, sizeof p, "%s/%s", root, r) >= (int)sizeof p || stat(p, &st) != 0) { rc = -1; break; }
        if (S_ISDIR(st.st_mode)) { rc = depth < MAX_DEPTH ? walk(root, r, depth + 1, t, hash) : -1; continue; }
        if (!S_ISREG(st.st_mode)) continue;
        if (t->n >= BL_SAVES_MAX_FILES) { rc = -1; break; }
        uint64_t size = (uint64_t)st.st_size;
        if (hash && bl_fs_sha256(p, t->sha[t->n], &size) != 0) { rc = -1; break; }
        memcpy(t->rel[t->n], r, strlen(r) + 1);
        t->size[t->n++] = size;
    }
    closedir(d);
    return rc;
}

static const Tree *g_sort_tree;
static int cmp_index(const void *a, const void *b)
{
    return strcmp(g_sort_tree->rel[*(const int *)a], g_sort_tree->rel[*(const int *)b]);
}

/* files.txt of a tree, sorted by path so equal trees give equal text. */
static char *tree_text(const Tree *t, size_t *len)
{
    int *order = malloc(sizeof *order * (size_t)(t->n ? t->n : 1));
    char *s = malloc(LIST_TEXT);
    if (!order || !s) { free(order); free(s); return NULL; }
    for (int i = 0; i < t->n; i++) order[i] = i;
    g_sort_tree = t;
    qsort(order, (size_t)t->n, sizeof *order, cmp_index);
    size_t n = 0;
    s[0] = 0;
    for (int k = 0; k < t->n && n < LIST_TEXT; k++) {
        int i = order[k];
        n += (size_t)snprintf(s + n, LIST_TEXT - n, "%s %llu %s\n", t->sha[i], t->size[i], t->rel[i]);
    }
    free(order);
    if (n >= LIST_TEXT) { free(s); return NULL; }
    *len = n;
    return s;
}

static unsigned long seq_of(const char *name)
{
    char *end;
    if (name[0] < '0' || name[0] > '9') return 0;
    unsigned long v = strtoul(name, &end, 10);
    return *end == '-' && !strchr(name, '.') ? v : 0;
}

/* Complete backups (with files.txt), newest first; at most max names. */
static int list(const char *base, char names[][64], int max)
{
    DIR *d = opendir(base);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        unsigned long s = seq_of(e->d_name);
        char p[1200];
        if (!s || strlen(e->d_name) >= 64 || snprintf(p, sizeof p, "%s/%s/files.txt", base, e->d_name) >= (int)sizeof p || !bl_fs_exists(p)) continue;
        int at = n < max ? n++ : max;
        while (at > 0 && seq_of(names[at - 1]) < s) { if (at < max) memcpy(names[at], names[at - 1], 64); at--; }
        if (at < max) memcpy(names[at], e->d_name, strlen(e->d_name) + 1);
    }
    closedir(d);
    return n;
}

static void remove_tree(const char *dir, int depth)
{
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        char p[1700];
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) continue;
            if (is_dir(p)) { if (depth <= MAX_DEPTH) remove_tree(p, depth + 1); }
            else unlink(p);
        }
        closedir(d);
    }
    rmdir(dir);
}

static int parent_mkdirs(const char *path)
{
    char p[1700];
    if (snprintf(p, sizeof p, "%s", path) >= (int)sizeof p) return -1;
    char *s = strrchr(p, '/');
    if (!s) return 0;
    *s = 0;
    return bl_fs_mkdirs(p);
}

static int base_dir(const BlPaths *p, char *out, size_t cap)
{
    return p && p->backup[0] && snprintf(out, cap, "%s/saves", p->backup) < (int)cap ? 0 : -1;
}

int bl_saves_count(const BlPaths *paths, char *newest, size_t cap)
{
    char base[1024], all[64][64];
    if (newest && cap) newest[0] = 0;
    if (base_dir(paths, base, sizeof base)) return 0;
    int n = list(base, all, 64);
    if (n > 0 && newest && cap) snprintf(newest, cap, "%s", all[0]);
    return n;
}

long long bl_saves_bytes(const BlPaths *paths)
{
    char saves[1400];
    if (bl_saves_find2(paths, saves, sizeof saves) != 0) return 0;
    Tree *t = calloc(1, sizeof *t);
    long long total = 0;
    if (t && walk(saves, "", 0, t, 0) == 0)
        for (int i = 0; i < t->n; i++) total += (long long)t->size[i];
    free(t);
    return total;
}

int bl_saves_backup(const BlPaths *paths)
{
    char saves[1400], base[1024];
    if (base_dir(paths, base, sizeof base)) return -1;
    if (bl_saves_find2(paths, saves, sizeof saves) != 0) return 1;
    Tree *t = calloc(1, sizeof *t);
    if (!t) return -1;
    if (walk(saves, "", 0, t, 1) != 0) { free(t); return -1; }
    if (t->n == 0) { free(t); return 1; }
    size_t len = 0;
    char *text = tree_text(t, &len);
    if (!text) { free(t); return -1; }

    char (*names)[64] = malloc(sizeof(char[64]) * (BL_SAVES_KEEP + 64));
    if (!names) { free(text); free(t); return -1; }
    int n = list(base, names, BL_SAVES_KEEP + 64);
    if (n > 0) {   /* the same as the newest backup: nothing to do */
        char p[1200], *old = malloc(len + 2);
        long got = old && snprintf(p, sizeof p, "%s/%s/files.txt", base, names[0]) < (int)sizeof p ? bl_fs_read(p, old, len + 2) : -1;
        int same = got == (long)len && memcmp(old, text, len) == 0;
        free(old);
        if (same) { free(names); free(text); free(t); return 1; }
    }
    char stamp[32], dir[1200], partial[1300], p[1400];
    bl_utc_stamp(stamp);
    unsigned long seq = 0;
    {   /* above every number ever used, unfinished backups included */
        DIR *d = opendir(base);
        struct dirent *e;
        while (d && (e = readdir(d))) {
            char *end;
            unsigned long s = strtoul(e->d_name, &end, 10);
            if (*end == '-' && s > seq) seq = s;
        }
        if (d) closedir(d);
    }
    int ok = snprintf(dir, sizeof dir, "%s/%06lu-%s", base, seq + 1, stamp) < (int)sizeof dir
             && snprintf(partial, sizeof partial, "%s.partial", dir) < (int)sizeof partial && bl_fs_mkdirs(partial) == 0;
    for (int i = 0; ok && i < t->n; i++) {
        char from[1700], to[1700], hex[65];
        ok = snprintf(from, sizeof from, "%s/%s", saves, t->rel[i]) < (int)sizeof from
             && snprintf(to, sizeof to, "%s/%s", partial, t->rel[i]) < (int)sizeof to
             && parent_mkdirs(to) == 0 && bl_fs_copy(from, to, 0644) == 0
             && bl_fs_sha256(to, hex, NULL) == 0 && strcmp(hex, t->sha[i]) == 0;
    }
    if (ok) ok = snprintf(p, sizeof p, "%s/files.txt", partial) < (int)sizeof p && bl_fs_write_atomic(p, text, len, 0644) == 0;
    if (ok) ok = rename(partial, dir) == 0;
    if (!ok) remove_tree(partial, 0);
    free(text);
    free(t);
    if (ok) {   /* keep the newest BL_SAVES_KEEP */
        n = list(base, names, BL_SAVES_KEEP + 64);
        for (int i = BL_SAVES_KEEP; i < n && i < BL_SAVES_KEEP + 64; i++)
            if (snprintf(p, sizeof p, "%s/%s", base, names[i]) < (int)sizeof p) remove_tree(p, 0);
    }
    free(names);
    return ok ? 0 : -1;
}

int bl_saves_restore(const BlPaths *paths)
{
    char base[1024], newest[64], saves[1400];
    if (base_dir(paths, base, sizeof base)) return -1;
    if (bl_saves_count(paths, newest, sizeof newest) == 0) return 1;
    /* The current saves first, so a restore can be undone. */
    if (bl_saves_backup(paths) < 0) return -1;
    if (snprintf(saves, sizeof saves, "%s", paths->saves) >= (int)sizeof saves || bl_fs_mkdirs(saves) != 0) return -1;
    char bdir[1200], p[1700], *text = malloc(LIST_TEXT), *copy = malloc(LIST_TEXT);
    if (!text || !copy || snprintf(bdir, sizeof bdir, "%s/%s", base, newest) >= (int)sizeof bdir
        || snprintf(p, sizeof p, "%s/files.txt", bdir) >= (int)sizeof p || bl_fs_read(p, text, LIST_TEXT) < 0) {
        free(text); free(copy); return -1;
    }
    memcpy(copy, text, strlen(text) + 1);
    int ok = 1;
    char *save_ptr = NULL;
    for (char *line = strtok_r(copy, "\n", &save_ptr); ok && line; line = strtok_r(NULL, "\n", &save_ptr)) {
        char sha[65], rel[256], hex[65], from[1700], to[1700];
        unsigned long long size;
        if (sscanf(line, "%64s %llu %255[^\n]", sha, &size, rel) != 3 || strstr(rel, "..") || rel[0] == '/') { ok = 0; break; }
        ok = snprintf(from, sizeof from, "%s/%s", bdir, rel) < (int)sizeof from && snprintf(to, sizeof to, "%s/%s", saves, rel) < (int)sizeof to
             && parent_mkdirs(to) == 0 && bl_fs_copy(from, to, 0644) == 0 && bl_fs_sha256(to, hex, NULL) == 0 && !strcmp(hex, sha);
    }
    /* Files the backup did not have go (they are in the backup made above). */
    Tree *t = ok ? calloc(1, sizeof *t) : NULL;
    if (t && walk(saves, "", 0, t, 0) == 0) {
        for (int i = 0; i < t->n; i++) {
            char needle[300], q[1700];
            snprintf(needle, sizeof needle, " %s\n", t->rel[i]);
            if (!strstr(text, needle) && snprintf(q, sizeof q, "%s/%s", saves, t->rel[i]) < (int)sizeof q) unlink(q);
        }
    }
    free(t);
    free(text);
    free(copy);
    return ok ? 0 : -1;
}
