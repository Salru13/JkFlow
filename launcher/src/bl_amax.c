/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#define _POSIX_C_SOURCE 200809L
#include "bl_amax.h"
#include "bl_fs.h"
#include "bl_strings.h"
#include "bl_zip.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

const char *bl_amax_zip_sha = BL_AMAX_ZIP_SHA;
long long bl_amax_zip_size = BL_AMAX_ZIP_SIZE;
const char *bl_blur12_zip_sha = BL_BLUR12_ZIP_SHA;
long long bl_blur12_zip_size = BL_BLUR12_ZIP_SIZE;
const char *bl_online_downloading = "";

#define MAX_LINES 64

typedef struct Line { char rel[128], sha[65]; int blur12; } Line;

typedef struct Zip {
    const char *name, *url, *sha;
    long long size;
    char path[700];           /* placed or downloaded */
    int placed, downloaded;
} Zip;

/* The manifest's "amax" and "blur12" lines. Count, <0 unreadable. */
static int zip_lines(const char *files_dir, Line *out, int max)
{
    char path[1100], *buf = malloc(32768);
    if (!buf || snprintf(path, sizeof path, "%s/manifest.txt", files_dir) >= (int)sizeof path || bl_fs_read(path, buf, 32768) < 0) {
        free(buf);
        return -1;
    }
    int n = 0;
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line && n < max; line = strtok_r(NULL, "\n", &save)) {
        char kind[16] = "", a[200] = "", b[200] = "";
        if (sscanf(line, "%15s %199s %199s", kind, a, b) != 3 || (strcmp(kind, "amax") && strcmp(kind, "blur12"))) continue;
        if (strlen(a) >= sizeof out[n].rel || strlen(b) != 64 || strstr(a, "..") || a[0] == '/') continue;
        memcpy(out[n].rel, a, strlen(a) + 1);
        memcpy(out[n].sha, b, 65);
        out[n].blur12 = kind[0] == 'b';
        n++;
    }
    free(buf);
    return n;
}

static void remove_tree(const char *dir, int depth)
{
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        char p[1300];
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) continue;
            struct stat st;
            if (lstat(p, &st) == 0 && S_ISDIR(st.st_mode)) { if (depth < 12) remove_tree(p, depth + 1); }
            else unlink(p);
        }
        closedir(d);
    }
    rmdir(dir);
}

int bl_online_find_zip(const char *zip_dir, const char *name, char *out, size_t cap)
{
    DIR *d = zip_dir ? opendir(zip_dir) : NULL;
    if (!d) return 0;
    int found = 0;
    struct dirent *e;
    while (!found && (e = readdir(d))) {
        struct stat st;
        if (strcasecmp(e->d_name, name) || snprintf(out, cap, "%s/%s", zip_dir, e->d_name) >= (int)cap) continue;
        found = stat(out, &st) == 0 && S_ISREG(st.st_mode);
    }
    closedir(d);
    return found;
}

static void say(char *err, size_t cap, BlStringId fmt, const char *a, const char *b, const char *c)
{
    snprintf(err, cap, bl_str(fmt), a, b ? b : "", c ? c : "");
}

/* The pinned size and SHA-256 (the hash cached by size and change time). */
static int zip_ok(const BlPaths *p, const Zip *z)
{
    struct stat st;
    if (stat(z->path, &st) != 0 || (long long)st.st_size != z->size || !z->sha) return 0;
    char cache[200], hex[65];
    int have_cache = snprintf(cache, sizeof cache, "%s/%s", p->state, BL_HASH_CACHE_FILE) < (int)sizeof cache;
    return bl_fs_sha256_cached(have_cache ? cache : NULL, z->path, hex, NULL) == 0 && !strcmp(hex, z->sha);
}

void bl_online_unstage(const BlPaths *p)
{
    char path[300];
    if (!p || !p->state[0]) return;
    if (snprintf(path, sizeof path, "%s/%s", p->state, BL_ONLINE_STAGE) < (int)sizeof path) remove_tree(path, 0);
}

void bl_online_cleanup(const BlPaths *p, const char *zip_dir)
{
    (void)zip_dir;              /* <name>.part stays: the service resumes it (audit 8) */
    bl_online_unstage(p);
}

/* The check for each unpacked member: a member the manifest lists must have
 * its pinned hash. */
typedef struct Check { Line *l; int n; char *seen; char bad[128]; } Check;

static int member_ok(void *ctx, const char *name, const char *path)
{
    Check *c = ctx;
    for (int i = 0; i < c->n; i++) {
        if (strcmp(c->l[i].rel, name)) continue;
        char hex[65];
        if (bl_fs_sha256(path, hex, NULL) != 0 || strcmp(hex, c->l[i].sha)) { snprintf(c->bad, sizeof c->bad, "%s", name); return -1; }
        c->seen[i] = 1;
    }
    return 0;
}

int bl_online_prepare(const BlPaths *p, const char *files_dir, const char *zip_dir, BlHttpsGet https_get, void *ctx,
                      int net_up, void (*prog)(void *, long long, long long), void *pctx,
                      char stage_out[256], BlOnlineSource *src, char *err, size_t cap)
{
    if (stage_out) stage_out[0] = 0;
    if (src) *src = BL_ONLINE_SRC_NONE;
    if (err && cap) err[0] = 0;
    if (!p || !p->state[0] || !files_dir || !zip_dir || !stage_out || !src || !err) return -1;
    Zip zips[2] = {
        { BL_AMAX_ZIP_NAME, BL_AMAX_ZIP_URL, bl_amax_zip_sha, bl_amax_zip_size, "", 0, 0 },
        { BL_BLUR12_ZIP_NAME, BL_BLUR12_ZIP_URL, bl_blur12_zip_sha, bl_blur12_zip_size, "", 0, 0 },
    };
    int can_get = https_get && net_up, good[2] = { 0, 0 };
    for (int i = 0; i < 2; i++) {
        zips[i].placed = bl_online_find_zip(zip_dir, zips[i].name, zips[i].path, sizeof zips[i].path);
        good[i] = zips[i].placed && zip_ok(p, &zips[i]);
    }
    if (!good[0] && !good[1] && !zips[0].placed && !zips[1].placed && !can_get) {
        say(err, cap, net_up ? BL_S_ONLINE_NO_ZIPS : BL_S_ONLINE_NO_NETWORK, zip_dir, NULL, NULL);
        return BL_ONLINE_NO_ZIPS;
    }
    /* Without a download: say what is wrong with the placed zips. */
    if (!can_get) {
        for (int i = 0; i < 2; i++)
            if (zips[i].placed && !good[i]) {
                if (bl_zip_is_whole(zips[i].path)) say(err, cap, BL_S_ONLINE_ZIP_NEWER, zips[i].name, NULL, NULL);
                else say(err, cap, BL_S_ONLINE_ZIP_BAD, zips[i].name, zip_dir, NULL);
                return -1;
            }
        for (int i = 0; i < 2; i++)
            if (!zips[i].placed) { say(err, cap, BL_S_ONLINE_ONLY_ONE, zips[1 - i].name, zip_dir, zips[i].name); return -1; }
    }
    /* A zip that is missing or wrong is downloaded into the zip folder under
     * its own name (the spelling on disk when one is there), so a download and
     * a placed zip are the same thing. The service writes <dest>.part and
     * renames it only when complete; a file that is still wrong is deleted. */
    for (int i = 0; i < 2; i++) {
        Zip *z = &zips[i];
        if (good[i]) continue;
        if (!z->placed && snprintf(z->path, sizeof z->path, "%s/%s", zip_dir, z->name) >= (int)sizeof z->path) return -1;
        /* A kept <name>.part is the service's to resume (audit 8): never deleted
         * here, except after a finished download with the wrong hash. */
        char part[720];
        snprintf(part, sizeof part, "%s.part", z->path);
        z->downloaded = 1;
        bl_online_downloading = z->name;
        if (prog) prog(pctx, 0, 0);               /* audit 17: connecting; the last zip's line and bar go */
        int got = https_get(ctx, z->url, z->path, z->size + BL_ONLINE_DL_MARGIN, prog, pctx);
        bl_online_downloading = "";
        if (got != 0) {
            BlStringId why = got == BL_DL_NO_CONNECTION ? BL_S_DL_NO_CONNECTION : got == BL_DL_INTERRUPTED ? BL_S_DL_INTERRUPTED
                           : got == BL_DL_SERVER_ERROR ? BL_S_DL_SERVER_ERROR : BL_S_ONLINE_DOWNLOAD_FAILED;
            if (got == BL_DL_CERT_ERROR) say(err, cap, BL_S_CLOCK_TLS, bl_str(BL_S_CLOCK_GOLDHEN), NULL, NULL);   /* audit 9 */
            else say(err, cap, why, z->name, zip_dir, NULL);
            bl_online_unstage(p);
            return BL_ONLINE_DOWNLOAD_ERR;
        }
        if (!zip_ok(p, z)) {
            /* A whole zip we do not know: AMAX released a newer set
             * (finding 22); downloading again cannot help. */
            unlink(z->path);
            unlink(part);
            say(err, cap, BL_S_DL_MISMATCH, NULL, NULL, NULL);
            bl_online_unstage(p);
            return BL_ONLINE_DOWNLOAD_ERR;
        }
    }
    /* Both checked: unpack into the stage (streamed, see bl_zip.h); every
     * member the package lists is checked before it gets its name. */
    char stage[256];
    if (snprintf(stage, sizeof stage, "%s/%s", p->state, BL_ONLINE_STAGE) >= (int)sizeof stage) return -1;
    remove_tree(stage, 0);
    Check c;
    memset(&c, 0, sizeof c);
    c.l = malloc(sizeof *c.l * MAX_LINES);
    c.n = c.l ? zip_lines(files_dir, c.l, MAX_LINES) : -1;
    c.seen = calloc(MAX_LINES, 1);
    if (c.n < 0 || !c.seen) { free(c.l); free(c.seen); snprintf(err, cap, "%s", bl_str(BL_S_PATCH_PACKAGE_BROKEN)); return -1; }
    int rc = 0;
    for (int i = 0; i < 2 && rc == 0; i++) {
        int r = bl_zip_extract_checked(zips[i].path, stage, BL_ONLINE_MAX_TOTAL, member_ok, &c);
        if (r == 0) continue;
        rc = -1;
        if (r == BL_ZIP_NOMEM) say(err, cap, BL_S_ONLINE_UNPACK_MEMORY, zips[i].name, NULL, NULL);
        else if (r == BL_ZIP_WRITE) say(err, cap, BL_S_ONLINE_UNPACK_FAILED, zips[i].name, NULL, NULL);
        else if (r == BL_ZIP_CHECK) say(err, cap, BL_S_ONLINE_MEMBER_BAD, c.bad, zips[i].name, NULL);
        else say(err, cap, BL_S_ONLINE_ZIP_BAD, zips[i].name, zip_dir, NULL);
    }
    for (int i = 0; i < c.n && rc == 0; i++)
        if (!c.seen[i]) { rc = -1; say(err, cap, BL_S_ONLINE_MEMBER_BAD, c.l[i].rel, c.l[i].blur12 ? BL_BLUR12_ZIP_NAME : BL_AMAX_ZIP_NAME, NULL); }
    free(c.l);
    free(c.seen);
    if (rc != 0) { bl_online_unstage(p); return -1; }
    snprintf(stage_out, 256, "%s", stage);
    *src = zips[0].downloaded && zips[1].downloaded ? BL_ONLINE_SRC_DOWNLOAD
         : zips[0].downloaded || zips[1].downloaded ? BL_ONLINE_SRC_MIXED : BL_ONLINE_SRC_PLACED;
    return 0;
}
