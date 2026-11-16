/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Streaming zip extraction (see bl_zip.h). */
#define _POSIX_C_SOURCE 200809L
#include "bl_zip.h"
#include "bl_fs.h"
#include "bl_inflate.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NAME_MAX_LEN 200
#define READ_CHUNK (256u * 1024u)
#define TAIL_MAX (22u + 65535u)
#define CD_MAX (2u * 1024u * 1024u)

static uint32_t u16(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t u32(const unsigned char *p) { return u16(p) | u16(p + 2) << 16; }

typedef struct Member {
    char name[NAME_MAX_LEN];
    uint32_t method, crc, csize, usize, local;
    int dir, made, named;     /* made: its .zpart exists; named: renamed to its name by this call */
} Member;

/* A member name that stays inside dest: "/" segments, none empty (a final
 * "/" marks a folder), none "." or "..", no backslash, colon or control. */
static int safe_name(const char *n, size_t len, int *dir)
{
    if (!len || len >= NAME_MAX_LEN || n[0] == '/') return 0;
    *dir = n[len - 1] == '/';
    size_t end = *dir ? len - 1 : len;
    if (!end) return 0;
    size_t seg = 0;
    for (size_t i = 0; i <= end; i++) {
        char c = i < end ? n[i] : '/';
        if (c == '/') {
            if (seg == 0) return 0;
            if ((seg == 1 && n[i - 1] == '.') || (seg == 2 && n[i - 1] == '.' && n[i - 2] == '.')) return 0;
            seg = 0;
            continue;
        }
        if ((unsigned char)c < 0x20 || (unsigned char)c >= 0x7F || c == '\\' || c == ':') return 0;
        seg++;
    }
    return 1;
}

/* pread of exactly n bytes at off. 0 ok. */
static int read_at(int fd, unsigned char *buf, size_t n, long long off)
{
    while (n) {
        ssize_t r = pread(fd, buf, n, (off_t)off);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        buf += r; n -= (size_t)r; off += r;
    }
    return 0;
}

/* The end record and the central directory into members. Count, or <0. */
static int parse(int fd, long long len, Member **out, long long max_total)
{
    *out = NULL;
    if (len < 22) return BL_ZIP_DAMAGED;
    size_t tail = len < (long long)TAIL_MAX ? (size_t)len : TAIL_MAX;
    unsigned char *t = bl_zip_malloc(tail);
    if (!t) return BL_ZIP_NOMEM;
    if (read_at(fd, t, tail, len - (long long)tail) != 0) { free(t); return BL_ZIP_DAMAGED; }
    long long eocd = -1;
    for (size_t i = tail - 22 + 1; i-- > 0;) if (u32(t + i) == 0x06054b50u) { eocd = len - (long long)tail + (long long)i; break; }
    if (eocd < 0) { free(t); return BL_ZIP_DAMAGED; }
    const unsigned char *e = t + (eocd - (len - (long long)tail));
    uint32_t count = u16(e + 10), cd_size = u32(e + 12), cd_off = u32(e + 16);
    free(t);
    if (count == 0xFFFF || count > BL_ZIP_MAX_MEMBERS || cd_off == 0xFFFFFFFFu || (long long)cd_off + cd_size > eocd || cd_size > CD_MAX)
        return BL_ZIP_DAMAGED;
    unsigned char *cd = bl_zip_malloc(cd_size ? cd_size : 1);
    Member *m = bl_zip_malloc(sizeof *m * (count ? count : 1));
    if (!cd || !m) { free(cd); free(m); return BL_ZIP_NOMEM; }
    memset(m, 0, sizeof *m * (count ? count : 1));
    int rc = read_at(fd, cd, cd_size, cd_off) == 0 ? 0 : BL_ZIP_DAMAGED;
    size_t at = 0;
    long long total = 0;
    for (uint32_t i = 0; i < count && rc == 0; i++) {
        if (at + 46 > cd_size || u32(cd + at) != 0x02014b50u) { rc = BL_ZIP_DAMAGED; break; }
        uint32_t flags = u16(cd + at + 8), nlen = u16(cd + at + 28), xlen = u16(cd + at + 30), clen = u16(cd + at + 32);
        m[i].method = u16(cd + at + 10);
        m[i].crc = u32(cd + at + 16);
        m[i].csize = u32(cd + at + 20);
        m[i].usize = u32(cd + at + 24);
        m[i].local = u32(cd + at + 42);
        if (at + 46 + nlen + xlen + clen > cd_size) { rc = BL_ZIP_DAMAGED; break; }
        if (flags & 1u) { rc = BL_ZIP_DAMAGED; break; }                                 /* encrypted */
        if (m[i].method != 0 && m[i].method != 8) { rc = BL_ZIP_DAMAGED; break; }
        if (m[i].csize == 0xFFFFFFFFu || m[i].usize == 0xFFFFFFFFu || m[i].local == 0xFFFFFFFFu) { rc = BL_ZIP_DAMAGED; break; }
        if (memchr(cd + at + 46, 0, nlen) || !safe_name((const char *)cd + at + 46, nlen, &m[i].dir)) { rc = BL_ZIP_UNSAFE; break; }
        memcpy(m[i].name, cd + at + 46, nlen);
        m[i].name[nlen] = 0;
        if (m[i].method == 0 && m[i].csize != m[i].usize) { rc = BL_ZIP_DAMAGED; break; }
        if ((long long)m[i].local + 30 + m[i].csize > cd_off) { rc = BL_ZIP_DAMAGED; break; }
        total += m[i].usize;
        if (total > max_total) { rc = BL_ZIP_TOO_BIG; break; }
        at += 46 + nlen + xlen + clen;
    }
    free(cd);
    if (rc) { free(m); return rc; }
    *out = m;
    return (int)count;
}

static int parent_mkdirs(const char *path)
{
    char p[1200];
    if (snprintf(p, sizeof p, "%s", path) >= (int)sizeof p) return -1;
    char *s = strrchr(p, '/');
    if (!s || s == p) return 0;
    *s = 0;
    return bl_fs_mkdirs(p);
}

/* ---- one member: file -> (inflate) -> .zpart ---- */

typedef struct Pipe {
    int fd, out;
    unsigned char *buf;
    long long off;            /* next compressed byte in the zip */
    uint32_t left;            /* compressed bytes not yet read */
    uint32_t crc;
    unsigned long long written;
    int werr;
} Pipe;

static size_t pipe_in(void *ctx, const unsigned char **buf)
{
    Pipe *p = ctx;
    if (!p->left) return 0;
    size_t n = p->left < READ_CHUNK ? p->left : READ_CHUNK;
    if (read_at(p->fd, p->buf, n, p->off) != 0) { p->left = 0; return 0; }
    p->off += (long long)n;
    p->left -= (uint32_t)n;
    *buf = p->buf;
    return n;
}

static int pipe_out(void *ctx, const unsigned char *buf, size_t len)
{
    Pipe *p = ctx;
    p->crc = bl_crc32_update(p->crc, buf, len);
    p->written += len;
    while (len) {
        long w = bl_fs_write(p->out, buf, len);
        if (w <= 0) { if (w < 0 && errno == EINTR) continue; p->werr = 1; return -1; }
        buf += w; len -= (size_t)w;
    }
    return 0;
}

static int zpart_path(const char *dest, const Member *m, char *out, size_t cap)
{
    return snprintf(out, cap, "%s/%s.zpart", dest, m->name) < (int)cap ? 0 : -1;
}

static int extract_member(int fd, long long len, const char *dest, Member *m, unsigned char *buf)
{
    unsigned char h[30];
    if (read_at(fd, h, 30, m->local) != 0 || u32(h) != 0x04034b50u) return BL_ZIP_DAMAGED;
    long long data = (long long)m->local + 30 + u16(h + 26) + u16(h + 28);
    if (data + m->csize > len) return BL_ZIP_DAMAGED;
    char path[1200];
    if (zpart_path(dest, m, path, sizeof path) != 0 || parent_mkdirs(path) != 0) return BL_ZIP_WRITE;
    Pipe p = { fd, -1, buf, data, m->csize, 0, 0, 0 };
    p.out = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (p.out < 0) return BL_ZIP_WRITE;
    m->made = 1;
    int rc = 0;
    if (m->method == 0) {
        const unsigned char *b;
        size_t n;
        while (rc == 0 && (n = pipe_in(&p, &b)) > 0) if (pipe_out(&p, b, n) != 0) rc = BL_ZIP_WRITE;
        if (rc == 0 && p.left) rc = BL_ZIP_DAMAGED;
    } else {
        int r = bl_inflate_back(pipe_in, &p, pipe_out, &p, m->usize, NULL);
        rc = r == BL_INFLATE_OK ? 0 : r == BL_INFLATE_NOMEM ? BL_ZIP_NOMEM : p.werr ? BL_ZIP_WRITE : BL_ZIP_DAMAGED;
    }
    if (fsync(p.out) != 0 && rc == 0) rc = BL_ZIP_WRITE;
    if (close(p.out) != 0 && rc == 0) rc = BL_ZIP_WRITE;
    if (rc == 0 && (p.written != m->usize || p.crc != m->crc)) rc = BL_ZIP_DAMAGED;
    return rc;
}

static void remove_zparts(const char *dest, Member *m, int n)
{
    char path[1200];
    for (int i = 0; i < n; i++)
        if (m[i].made && zpart_path(dest, &m[i], path, sizeof path) == 0) { unlink(path); m[i].made = 0; }
}

int bl_zip_is_whole(const char *zip)
{
    int fd = zip ? open(zip, O_RDONLY) : -1;
    if (fd < 0) return 0;
    struct stat st;
    Member *m = NULL;
    int n = fstat(fd, &st) == 0 && st.st_size > 0 && st.st_size <= BL_ZIP_MAX_FILE
            ? parse(fd, (long long)st.st_size, &m, 1LL << 40) : BL_ZIP_DAMAGED;
    free(m);
    close(fd);
    return n > 0;
}

int bl_zip_extract_checked(const char *zip, const char *dest_dir, long long max_total, BlZipCheck check, void *ctx)
{
    if (!zip || !dest_dir) return BL_ZIP_DAMAGED;
    int fd = open(zip, O_RDONLY);
    if (fd < 0) return BL_ZIP_DAMAGED;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > BL_ZIP_MAX_FILE) { close(fd); return BL_ZIP_DAMAGED; }
    Member *m = NULL;
    int n = parse(fd, (long long)st.st_size, &m, max_total);
    if (n < 0) { close(fd); return n; }
    unsigned char *buf = bl_zip_malloc(READ_CHUNK);
    int rc = buf ? 0 : BL_ZIP_NOMEM;
    if (rc == 0 && bl_fs_mkdirs(dest_dir) != 0) rc = BL_ZIP_WRITE;
    /* 1. every member into its .zpart, checked */
    for (int i = 0; i < n && rc == 0; i++) {
        if (m[i].dir) continue;
        rc = extract_member(fd, (long long)st.st_size, dest_dir, &m[i], buf);
        char path[1200];
        if (rc == 0 && check && (zpart_path(dest_dir, &m[i], path, sizeof path) != 0 || check(ctx, m[i].name, path) != 0)) rc = BL_ZIP_CHECK;
    }
    /* 2. only then the names */
    for (int i = 0; i < n && rc == 0; i++) {
        char path[1200], part[1200];
        if (snprintf(path, sizeof path, "%s/%s", dest_dir, m[i].name) >= (int)sizeof path) { rc = BL_ZIP_WRITE; break; }
        if (m[i].dir) { if (bl_fs_mkdirs(path) != 0) rc = BL_ZIP_WRITE; continue; }
        if (zpart_path(dest_dir, &m[i], part, sizeof part) != 0 || bl_fs_rename(part, path) != 0) { rc = BL_ZIP_WRITE; break; }
        m[i].made = 0;
        m[i].named = 1;
    }
    if (rc != 0) {
        /* A failure while naming: the members already named go too. */
        for (int i = 0; i < n; i++) {
            char path[1200];
            if (m[i].named && snprintf(path, sizeof path, "%s/%s", dest_dir, m[i].name) < (int)sizeof path) unlink(path);
        }
        remove_zparts(dest_dir, m, n);
    }
    free(buf);
    free(m);
    close(fd);
    return rc;
}

int bl_zip_extract(const char *zip, const char *dest_dir, long long max_total)
{
    return bl_zip_extract_checked(zip, dest_dir, max_total, NULL, NULL);
}
