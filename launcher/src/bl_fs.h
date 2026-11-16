/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * File helpers for the launcher's own files and the patcher. Writes are
 * atomic: data goes to "<path>.tmp", is flushed, then renamed over the
 * target, so a power cut leaves the old or the new file, never a partial one.
 * Plain C library + POSIX calls only (available in the PS4 libc).
 */
#ifndef BL_FS_H
#define BL_FS_H

#include <stddef.h>
#include <stdint.h>

/* rename(), replaceable by tests to simulate a failure at that point. */
extern int (*bl_fs_rename)(const char *from, const char *to);
/* write(), replaceable by tests to simulate a full disk. */
extern long (*bl_fs_write)(int fd, const void *buf, size_t n);
long bl_fs_write_default(int fd, const void *buf, size_t n);
/* fsync of a folder after a rename into it, replaceable by tests. */
extern int (*bl_fs_sync_dir)(const char *dir);
int bl_fs_sync_dir_default(const char *dir);

/* Read a whole file of at most cap-1 bytes into buf (NUL-terminated).
 * Returns its size; -1 missing or unreadable; -2 larger than cap-1. */
long bl_fs_read(const char *path, char *buf, size_t cap);
/* Atomic write of n bytes with the given mode (e.g. 0644, 0600). 0 ok. */
int bl_fs_write_atomic(const char *path, const void *data, size_t n, unsigned mode);
/* Atomic copy of a file. 0 ok. */
int bl_fs_copy(const char *from, const char *to, unsigned mode);
/* SHA-256 and size of a file. 0 ok, -1 missing or unreadable. */
int bl_fs_sha256(const char *path, char hex[65], uint64_t *size);
/* bl_fs_sha256 with a cache file for files of at least BL_FS_CACHE_MIN
 * bytes (Blur.exe, gamedata.pak and the zips take seconds on the console):
 * a file with the same path, size, inode, mtime and ctime is not hashed
 * again (ctime too, so a rewrite in the same second is seen); a hit does not
 * rewrite the cache file. cache NULL: no cache. 0 ok, -1 missing or unreadable. */
#define BL_FS_CACHE_MIN (1024u * 1024u)
int bl_fs_sha256_cached(const char *cache, const char *path, char hex[65], uint64_t *size);
/* How many files bl_fs_sha256 hashed (for tests). */
extern unsigned bl_fs_hash_count;
/* Create a directory and its parents (mode 0755). 0 ok or already there. */
int bl_fs_mkdirs(const char *path);
int bl_fs_exists(const char *path);

#endif
