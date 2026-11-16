/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#define _POSIX_C_SOURCE 200809L
#include "bl_exe.h"
#include "bl_fs.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

const char *bl_exe_expected_sha = BL_TESTED_EXE_SHA;
unsigned bl_exe_hash_count;

BlExeVerdict bl_exe_verify(const BlPaths *p, char name_out[64])
{
    if (name_out) name_out[0] = 0;
    if (!p || !p->game[0]) return BL_EXE_MISSING;
    DIR *d = opendir(p->game);
    if (!d) return BL_EXE_MISSING;
    char name[64] = "", path[300];
    struct stat st;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d))) {
        if (strcasecmp(e->d_name, "Blur.exe")) continue;
        if (snprintf(path, sizeof path, "%s/%s", p->game, e->d_name) >= (int)sizeof path) continue;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        memcpy(name, e->d_name, sizeof "Blur.exe");          /* matched "Blur.exe": 8 characters */
        found = 1;
    }
    closedir(d);
    if (!found) return BL_EXE_MISSING;
    if (name_out) memcpy(name_out, name, strlen(name) + 1);
    unsigned long long size = (unsigned long long)st.st_size;
    if (size != BL_TESTED_EXE_SIZE) return BL_EXE_UNSUPPORTED;

    /* Cached hash: "blur-exe-check 1 <size> <mtime> <sha256>". */
    char cache[200] = "", line[200], sha[65] = "";
    long long mtime = (long long)st.st_mtime;
    int have_cache = p->state[0] && snprintf(cache, sizeof cache, "%s/%s", p->state, BL_EXE_CHECK_FILE) < (int)sizeof cache;
    if (have_cache && bl_fs_read(cache, line, sizeof line) > 0) {
        unsigned long long csize = 0;
        long long cmtime = 0;
        char csha[65] = "";
        if (sscanf(line, "blur-exe-check 1 %llu %lld %64s", &csize, &cmtime, csha) == 3 && csize == size && cmtime == mtime
            && strlen(csha) == 64)
            memcpy(sha, csha, sizeof sha);
    }
    if (!sha[0]) {
        uint64_t hsize = 0;
        bl_exe_hash_count++;
        if (bl_fs_sha256(path, sha, &hsize) != 0) return BL_EXE_UNSUPPORTED;
        if (have_cache) {
            int n = snprintf(line, sizeof line, "blur-exe-check 1 %llu %lld %s\n", size, mtime, sha);
            if (n > 0 && n < (int)sizeof line) (void)bl_fs_write_atomic(cache, line, (size_t)n, 0644);
        }
    }
    return bl_exe_expected_sha && !strcmp(sha, bl_exe_expected_sha) ? BL_EXE_TESTED : BL_EXE_UNSUPPORTED;
}
