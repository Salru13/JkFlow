/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#define _POSIX_C_SOURCE 200809L
#include "bl_fs.h"
#include "bl_sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define IO_BUF 65536                 /* on the heap: game threads may have small stacks */

long bl_fs_write_default(int fd, const void *buf, size_t n) { return (long)write(fd, buf, n); }

int bl_fs_sync_dir_default(const char *dir)
{
    int fd = open(dir, O_RDONLY);
    if (fd < 0) return -1;
    int r = fsync(fd);
    close(fd);
    return r;
}

int (*bl_fs_rename)(const char *from, const char *to) = rename;
long (*bl_fs_write)(int fd, const void *buf, size_t n) = bl_fs_write_default;
int (*bl_fs_sync_dir)(const char *dir) = bl_fs_sync_dir_default;

long bl_fs_read(const char *path, char *buf, size_t cap)
{
    if (!path || !buf || cap == 0) return -1;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    size_t n = 0;
    for (;;) {
        if (n == cap - 1) {
            char extra;
            long more = (long)read(fd, &extra, 1);
            close(fd);
            buf[n] = 0;
            return more > 0 ? -2 : (long)n;
        }
        ssize_t r = read(fd, buf + n, cap - 1 - n);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) { close(fd); return -1; }
        if (r == 0) break;
        n += (size_t)r;
    }
    close(fd);
    buf[n] = 0;
    return (long)n;
}

/* A fresh "<path>.tmp": whatever is there (a leftover, or a planted link)
 * is removed first, and the new file is never opened through a link. */
static int open_tmp(const char *path, char *tmp, size_t cap, unsigned mode)
{
    if (!path || snprintf(tmp, cap, "%s.tmp", path) >= (int)cap) return -1;
    unlink(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, (mode_t)mode);
    if (fd >= 0) fchmod(fd, (mode_t)mode);
    return fd;
}

/* Rename tmp over path, then flush the folder so the new name survives a
 * power cut. 0 ok. */
static int commit_tmp(const char *tmp, const char *path)
{
    if (bl_fs_rename(tmp, path) != 0) { unlink(tmp); return -1; }
    char dir[1024];
    const char *slash = strrchr(path, '/');
    if (!slash) snprintf(dir, sizeof dir, ".");
    else if (slash == path) snprintf(dir, sizeof dir, "/");
    else if ((size_t)(slash - path) < sizeof dir) { memcpy(dir, path, (size_t)(slash - path)); dir[slash - path] = 0; }
    else return 0;
    bl_fs_sync_dir(dir);              /* best effort: the data itself is already on disk */
    return 0;
}

static int write_all(int fd, const char *p, size_t left)
{
    while (left) {
        long w = bl_fs_write(fd, p, left);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        p += w; left -= (size_t)w;
    }
    return 0;
}

int bl_fs_write_atomic(const char *path, const void *data, size_t n, unsigned mode)
{
    char tmp[1024];
    int fd = open_tmp(path, tmp, sizeof tmp, mode);
    if (fd < 0) return -1;
    if (write_all(fd, data, n) != 0) { close(fd); unlink(tmp); return -1; }
    if (fsync(fd) != 0 || close(fd) != 0) { unlink(tmp); return -1; }
    return commit_tmp(tmp, path);
}

int bl_fs_copy(const char *from, const char *to, unsigned mode)
{
    char tmp[1024];
    if (!from || !to) return -1;
    int in = open(from, O_RDONLY);
    if (in < 0) return -1;
    char *buf = malloc(IO_BUF);
    int out = buf ? open_tmp(to, tmp, sizeof tmp, mode) : -1;
    if (out < 0) { free(buf); close(in); return -1; }
    int ok = 1;
    for (;;) {
        ssize_t r = read(in, buf, IO_BUF);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) { ok = 0; break; }
        if (r == 0) break;
        if (write_all(out, buf, (size_t)r) != 0) { ok = 0; break; }
    }
    free(buf);
    close(in);
    if (fsync(out) != 0) ok = 0;
    if (close(out) != 0) ok = 0;
    if (!ok) { unlink(tmp); return -1; }
    return commit_tmp(tmp, to);
}

unsigned bl_fs_hash_count;

int bl_fs_sha256(const char *path, char hex[65], uint64_t *size)
{
    bl_fs_hash_count++;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char *buf = malloc(IO_BUF);
    if (!buf) { close(fd); return -1; }
    BlSha256 s;
    bl_sha256_init(&s);
    uint64_t total = 0;
    int rc = 0;
    for (;;) {
        ssize_t r = read(fd, buf, IO_BUF);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) { rc = -1; break; }
        if (r == 0) break;
        bl_sha256_update(&s, buf, (size_t)r);
        total += (uint64_t)r;
    }
    free(buf);
    close(fd);
    if (rc) return -1;
    bl_sha256_final_hex(&s, hex);
    if (size) *size = total;
    return 0;
}

int bl_fs_mkdirs(const char *path)
{
    char p[1024];
    if (!path || snprintf(p, sizeof p, "%s", path) >= (int)sizeof p) return -1;
    for (char *c = p + 1; *c; c++) {
        if (*c != '/') continue;
        *c = 0;
        if (mkdir(p, 0755) != 0 && errno != EEXIST) return -1;
        *c = '/';
    }
    if (mkdir(p, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

int bl_fs_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0;
}

/* Cache lines: "<sha256> <size> <inode> <mtime s> <ns> <ctime s> <ns> <path>". */
#define CACHE_LINES 32
#define CACHE_MAX (CACHE_LINES * 1200)

int bl_fs_sha256_cached(const char *cache, const char *path, char hex[65], uint64_t *size)
{
    struct stat st;
    if (!path || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    if (!cache || (unsigned long long)st.st_size < BL_FS_CACHE_MIN) return bl_fs_sha256(path, hex, size);
    unsigned long long sz = (unsigned long long)st.st_size, ino = (unsigned long long)st.st_ino;
    long long ms = (long long)st.st_mtim.tv_sec, mn = (long long)st.st_mtim.tv_nsec;
    long long cs = (long long)st.st_ctim.tv_sec, cn = (long long)st.st_ctim.tv_nsec;
    char *old = malloc(CACHE_MAX), *out = malloc(CACHE_MAX);
    if (!old || !out) { free(old); free(out); return bl_fs_sha256(path, hex, size); }
    if (bl_fs_read(cache, old, CACHE_MAX) < 0) old[0] = 0;
    size_t used = 0;
    int kept = 0, rc = 1;
    char *save = NULL;
    for (char *line = strtok_r(old, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char sha[65] = "";
        unsigned long long a = 0, b = 0;
        long long c = 0, d = 0, e = 0, g = 0;
        int off = 0;
        if (sscanf(line, "%64s %llu %llu %lld %lld %lld %lld %n", sha, &a, &b, &c, &d, &e, &g, &off) < 7 || !off || strlen(sha) != 64) continue;
        if (!strcmp(line + off, path)) {
            if (rc == 1 && a == sz && b == ino && c == ms && d == mn && e == cs && g == cn) {
                memcpy(hex, sha, 65);
                if (size) *size = sz;
                rc = 0;
            }
            continue;                                   /* written again below */
        }
        if (kept < CACHE_LINES - 1 && used + strlen(line) + 2 < CACHE_MAX) {
            used += (size_t)snprintf(out + used, CACHE_MAX - used, "%s\n", line);
            kept++;
        }
    }
    int hit = rc == 0;                                  /* a hit leaves the cache file alone */
    if (rc == 1) rc = bl_fs_sha256(path, hex, size);
    if (rc == 0 && !hit) {
        int w = snprintf(out + used, CACHE_MAX - used, "%s %llu %llu %lld %lld %lld %lld %s\n", hex, sz, ino, ms, mn, cs, cn, path);
        if (w > 0 && used + (size_t)w < CACHE_MAX) (void)bl_fs_write_atomic(cache, out, used + (size_t)w, 0644);
    }
    free(old);
    free(out);
    return rc;
}
