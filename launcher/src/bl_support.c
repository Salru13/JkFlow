/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#define _POSIX_C_SOURCE 200809L
#include "bl_support.h"
#include "bl_account.h"
#include "bl_events.h"
#include "bl_fs.h"
#include "bl_time.h"
#include "bl_exe.h"
#include "bl_native.h"
#include "bl_paths.h"
#include "bl_session.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* The fixed set of the support bundle (BLUR 1.00 plan Task L10), in bundle
 * order; also the README table. Never account.cfg, account_cfg.lua,
 * blursave or online.secret. */
const BlLogSpec bl_log_specs[] = {
    { BL_LOG_STATE, "events.log", 1, 0, "launcher (always on, 32 KiB x 2)", "every start, patch, Play check and session outcome with its code" },
    { BL_LOG_STATE, "events.log.1", 1, 0, "launcher (previous events.log)", "older events" },
    { BL_LOG_STATE, "session.txt", 1, 0, "launcher at play, title at first frame and at exit", "how the last session ended (is it still there?)" },
    { BL_LOG_STATE, "patch.txt", 1, 0, "launcher (Patch)", "which package the last Patch installed, and when" },
    { BL_LOG_STATE, "launcher.ini", 1, 0, "launcher (settings)", "the settings the start used" },
    { BL_LOG_DATA, "*", 1, 0, "title runtime, Wine, the renderer and the driver (BLUR_LOG_DIR)", "runtime start, faults, renderer and driver errors, exit reason" },
    { BL_LOG_GAME, "dxn_stage.txt", 1, 0, "older native clients only (normal when missing)", "last stage the native renderer reached (crash at start)" },
    { BL_LOG_GAME, "Blur_d3d9.log", 0, 1, "d3d9 client (logs on only)", "with logs on: renderer errors, frame times, waits" },
    { BL_LOG_GAME, "amax/log/*.log", 0, 0, "AMAX online client", "online login and lobby problems" },
};
const int bl_log_spec_count = (int)(sizeof bl_log_specs / sizeof bl_log_specs[0]);

/* Logs the launcher bounds before each start (<name>.old above BL_LOG_ROTATE_BYTES). */
static const struct { BlLogWhere where; const char *name; } ROTATE[] = {
    { BL_LOG_GAME, "Blur_d3d9.log" }, { BL_LOG_DATA, "radv.log" }, { BL_LOG_DATA, "dxn_server.txt" },
    { BL_LOG_DATA, "wowblur.log" },
};

/* Names that never go into a bundle, whatever folder they are in. */
static int secret_name(const char *name)
{
    static const char *const NEVER[] = { "account", "blursave", "online.secret" };
    for (unsigned i = 0; i < sizeof NEVER / sizeof NEVER[0]; i++) if (strstr(name, NEVER[i])) return 1;
    return 0;
}

long long (*bl_free_bytes)(const char *path) = NULL;
const char *bl_support_local_user = NULL;

static int join(char *out, size_t cap, const char *dir, const char *name)
{
    return dir && name && snprintf(out, cap, "%s/%s", dir, name) < (int)cap ? 0 : -1;
}

static void stamp(char out[32])
{
    bl_utc_stamp(out);
}

static long long file_size(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode) ? (long long)st.st_size : -1;
}


static const char *folder_of(BlLogWhere where, const char *game_dir, const char *data_root, char *buf, size_t cap)
{
    if (where == BL_LOG_GAME) return game_dir;
    if (where == BL_LOG_DATA) {               /* the logs folder (bl_paths: /data/JkFlow/Logs) */
        BlPaths p;
        bl_paths_init(&p, data_root);
        return data_root && p.logs[0] && snprintf(buf, cap, "%s", p.logs) < (int)cap ? buf : NULL;
    }
    return data_root && snprintf(buf, cap, "%s/state", data_root) < (int)cap ? buf : NULL;
}

/* ---- rotation ---- */

typedef struct Sess { char name[128]; time_t mtime; } Sess;

int bl_logs_rotate(const char *game_dir, const char *data_root, const char *keep)
{
    int n = 0;
    char state[1024], p[1200], old[1300];
    for (unsigned i = 0; i < sizeof ROTATE / sizeof ROTATE[0]; i++) {
        const char *dir = folder_of(ROTATE[i].where, game_dir, data_root, state, sizeof state);
        if (join(p, sizeof p, dir, ROTATE[i].name)) continue;
        if (file_size(p) > BL_LOG_ROTATE_BYTES && snprintf(old, sizeof old, "%s.old", p) < (int)sizeof old && rename(p, old) == 0) n++;
    }
    /* Session logs: the title's ring is bounded; this also bounds strays. */
    char dir[1100];
    if (!folder_of(BL_LOG_DATA, game_dir, data_root, dir, sizeof dir)) return n;
    DIR *d = opendir(dir);
    if (!d) return n;
    Sess *list = calloc(256, sizeof *list);
    int count = 0;
    struct dirent *e;
    while (list && (e = readdir(d)) && count < 256) {
        size_t len = strlen(e->d_name);
        if (strncmp(e->d_name, "session-", 8) || len < 12 || len >= sizeof list[0].name || strcmp(e->d_name + len - 4, ".log")) continue;
        if (keep && !strcmp(e->d_name, keep)) continue;
        struct stat st;
        if (join(p, sizeof p, dir, e->d_name) || stat(p, &st) != 0) continue;
        memcpy(list[count].name, e->d_name, len + 1);
        list[count++].mtime = st.st_mtime;
    }
    closedir(d);
    int keep_n = BL_LOG_SESSION_KEEP - (keep ? 1 : 0);
    while (list && count > keep_n) {          /* drop the oldest */
        int oldest = 0;
        for (int i = 1; i < count; i++) if (list[i].mtime < list[oldest].mtime) oldest = i;
        if (!join(p, sizeof p, dir, list[oldest].name) && unlink(p) == 0) n++;
        list[oldest] = list[--count];
    }
    free(list);
    return n;
}

/* ---- disk space ---- */

int bl_space_ok(const char *path, long long need)
{
    if (!bl_free_bytes || !path) return 1;
    long long free_b = bl_free_bytes(path);
    return free_b < 0 || free_b >= need + 16LL * 1024 * 1024;
}

/* ---- manifest lines ---- */

typedef struct ManLine { char kind[8], name[128], sha[65]; } ManLine;

static int read_manifest(const char *files_dir, ManLine *out, int max)
{
    char p[1200], *buf = malloc(32768);
    if (!buf || join(p, sizeof p, files_dir, "manifest.txt") || bl_fs_read(p, buf, 32768) < 0) { free(buf); return -1; }
    int n = 0;
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line && n < max; line = strtok_r(NULL, "\n", &save)) {
        char kind[16] = "", a[128] = "", b[128] = "";
        int k = sscanf(line, "%15s %127s %127s", kind, a, b);
        if ((!strcmp(kind, "file") || !strcmp(kind, "seed") || !strcmp(kind, "amax")) && k >= 3 && strlen(a) < 128 && strlen(b) == 64
            && !strstr(a, "..") && a[0] != '/' && a[0] != '.') {
            snprintf(out[n].kind, sizeof out[n].kind, "%.7s", kind);
            snprintf(out[n].name, sizeof out[n].name, "%.127s", a);
            snprintf(out[n].sha, sizeof out[n].sha, "%.64s", b);
            n++;
        } else if ((!strcmp(kind, "known") || !strcmp(kind, "tested")) && k >= 2 && strlen(a) == 64) {
            snprintf(out[n].kind, sizeof out[n].kind, "%.7s", kind);
            out[n].name[0] = 0;
            snprintf(out[n].sha, sizeof out[n].sha, "%.64s", a);
            n++;
        }
    }
    free(buf);
    return n;
}

/* ---- Blur.exe ---- */

int bl_exe_info(const char *game_dir, const char *files_dir, BlExeInfo *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    if (!game_dir) return 0;
    DIR *d = opendir(game_dir);
    if (!d) return 0;
    struct dirent *e;
    char p[1200];
    while (!out->found && (e = readdir(d))) {
        if (strcasecmp(e->d_name, "Blur.exe") || join(p, sizeof p, game_dir, e->d_name) || file_size(p) < 0) continue;
        snprintf(out->name, sizeof out->name, "%.63s", e->d_name);
        out->found = 1;
    }
    closedir(d);
    if (!out->found) return 0;
    uint64_t size = 0;
    if (join(p, sizeof p, game_dir, out->name) || bl_fs_sha256(p, out->sha, &size) != 0) { out->sha[0] = 0; return 0; }
    out->size = size;
    ManLine m[96];
    int n = files_dir ? read_manifest(files_dir, m, 96) : -1;
    for (int i = 0; i < n; i++) if (!strcmp(m[i].kind, "tested") && !strcmp(m[i].sha, out->sha)) out->tested = 1;
    return 0;
}

/* ---- support bundle ---- */

typedef struct Out { char *buf; size_t cap, n; } Out;
static void put(Out *o, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
#include <stdarg.h>
static void put(Out *o, const char *fmt, ...)
{
    if (o->n >= o->cap) return;
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(o->buf + o->n, o->cap - o->n, fmt, ap);
    va_end(ap);
    if (k > 0) o->n = o->n + (size_t)k < o->cap ? o->n + (size_t)k : o->cap;
}

/* ---- masking (owner finding 27) ---- */

/* The AMAX user name of this console while a bundle is made ("" none). */
static char g_mask_user[BL_ACCOUNT_MAX + 1];

static int is_digit(int c) { return c >= '0' && c <= '9'; }

/* A dotted IPv4 address at s (not part of a longer dotted number): its
 * length, or 0. Each part 0..255, 1 to 3 digits. */
static size_t ipv4_at(const char *s, size_t n, unsigned char oct[4])
{
    size_t i = 0;
    for (int k = 0; k < 4; k++) {
        if (k) { if (i >= n || s[i] != '.') return 0; i++; }
        unsigned v = 0;
        size_t d = 0;
        while (i < n && is_digit((unsigned char)s[i]) && d < 4) { v = v * 10 + (unsigned)(s[i] - '0'); i++; d++; }
        if (d == 0 || d > 3 || v > 255) return 0;
        oct[k] = (unsigned char)v;
    }
    if (i < n && is_digit((unsigned char)s[i])) return 0;
    if (i + 1 < n && s[i] == '.' && is_digit((unsigned char)s[i + 1])) return 0;   /* 1.2.3.4.5: not an address */
    return i;
}

/* Private and link-local ranges: the player's own network. */
static int lan_ip(const unsigned char o[4])
{
    return o[0] == 10 || (o[0] == 172 && o[1] >= 16 && o[1] <= 31) || (o[0] == 192 && o[1] == 168) || (o[0] == 169 && o[1] == 254);
}

static int is_alnum(int c) { return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

/* A name at buf+i (any letter case): 3 or more characters anywhere; a
 * shorter one only as a whole word (audit 26: names of any length). */
static int name_at(const char *buf, size_t n, size_t i, const char *name, size_t len)
{
    if (!len || i + len > n || strncasecmp(buf + i, name, len)) return 0;
    if (len >= 3) return 1;
    return (i == 0 || !is_alnum((unsigned char)buf[i - 1])) && (i + len == n || !is_alnum((unsigned char)buf[i + len]));
}

/* Copy src to dst with the AMAX user name as "<user>", the PS4 local user
 * name as "<ps4-user>" and LAN addresses as "<lan-ip>". 0 ok, -1 the source
 * could not be read, -2 the copy could not be written whole (removed). */
static int copy_masked(const char *src, const char *dst)
{
    long long size = file_size(src);
    if (size < 0 || size > 64LL * 1024 * 1024) return -1;
    char *buf = malloc((size_t)size + 1);
    FILE *in = buf ? fopen(src, "rb") : NULL;
    size_t n = in ? fread(buf, 1, (size_t)size, in) : 0;
    if (in) fclose(in);
    if (!buf || !in || (long long)n != size) { free(buf); return -1; }
    FILE *out = fopen(dst, "wb");
    if (!out) { free(buf); return -2; }
    const char *local = bl_support_local_user ? bl_support_local_user : "";
    size_t ul = strlen(g_mask_user), ll = strlen(local), from = 0;
    int bad = 0;
    for (size_t i = 0; i < n && !bad;) {
        unsigned char o[4];
        size_t len = 0;
        const char *with = NULL;
        if (name_at(buf, n, i, local, ll)) { len = ll; with = "<ps4-user>"; }
        else if (name_at(buf, n, i, g_mask_user, ul)) { len = ul; with = "<user>"; }
        else if (is_digit((unsigned char)buf[i]) && (i == 0 || (!is_digit((unsigned char)buf[i - 1]) && buf[i - 1] != '.'))
                 && (len = ipv4_at(buf + i, n - i, o)) > 0 && lan_ip(o)) with = "<lan-ip>";
        if (!with) { i++; continue; }
        bad |= fwrite(buf + from, 1, i - from, out) != i - from || fputs(with, out) == EOF;
        i += len;
        from = i;
    }
    if (!bad) bad = fwrite(buf + from, 1, n - from, out) != n - from;
    free(buf);
    bad |= fclose(out) != 0;
    if (bad) { unlink(dst); return -2; }
    return 0;
}

/* A copy in this bundle could not be written whole (audit 26): the bundle fails. */
static int g_write_failed;

/* Copy one file into the bundle (masked) and list it in the manifest. A
 * missing file (dxn_stage.txt: the product engine does not write it) is normal. */
static void bundle_file(Out *man, const char *bundle, const char *src, const char *as)
{
    char dst[1300], hex[65];
    uint64_t size = 0;
    if (file_size(src) < 0 || secret_name(src) || secret_name(as)) return;
    if (snprintf(dst, sizeof dst, "%s/%s", bundle, as) >= (int)sizeof dst) return;
    int r = copy_masked(src, dst);
    if (r == -2) g_write_failed = 1;
    if (r != 0 || bl_fs_sha256(dst, hex, &size) != 0) { put(man, "copy-failed %s\n", src); return; }
    struct stat st;
    long long mtime = stat(src, &st) == 0 ? (long long)st.st_mtime : 0;
    put(man, "file %s %llu %s mtime=%lld from=%s\n", as, (unsigned long long)size, hex, mtime, src);
}

static void readme(Out *o)
{
    put(o, "JkFlow support bundle\n=====================\n\n"
           "MANIFEST.txt lists every file here with its size, SHA-256, time and the\n"
           "console path it came from, plus the build, the Blur.exe check, the\n"
           "package manifest and our installed files. Your AMAX account and your\n"
           "saves are never included; your AMAX user name shows as <user>, your\n"
           "PS4 user name as <ps4-user> and addresses in your own network as <lan-ip>.\n\n"
           "Start with events.log: each failure screen shows a code (E101, E301, ...)\n"
           "and the same code starts a line there.\n\n"
           "File | written by | answers\n---- | ---------- | -------\n");
    for (int i = 0; i < bl_log_spec_count; i++)
        put(o, "%s | %s | %s\n", bl_log_specs[i].name, bl_log_specs[i].writer, bl_log_specs[i].answers);
}

/* Every regular file of dir whose name matches suffix ("" = any) into the
 * bundle as <prefix><name>. */
static void bundle_dir(Out *man, const char *bundle, const char *dir, const char *suffix, const char *prefix)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        size_t len = strlen(e->d_name), sl = strlen(suffix);
        if (e->d_name[0] == '.' || len < sl || strcmp(e->d_name + len - sl, suffix)) continue;
        char src[1400], as[200];
        if (snprintf(src, sizeof src, "%s/%s", dir, e->d_name) >= (int)sizeof src) continue;
        snprintf(as, sizeof as, "%s%.150s", prefix, e->d_name);
        bundle_file(man, bundle, src, as);
    }
    if (d) closedir(d);
}

int bl_support_bundle(const BlSupportInfo *in, char *out, size_t cap)
{
    if (!out || !cap) return -1;
    out[0] = 0;
    if (!in || !in->game_dir || !in->data_root) return -1;
    BlPaths paths;
    bl_paths_init(&paths, in->data_root);
    if (!paths.root[0]) return -1;
    char bundle[1200], when[32], state[1024];
    stamp(when);
    if (snprintf(bundle, sizeof bundle, "%s/%s", paths.support, when) >= (int)sizeof bundle) return -1;
    if (!bl_space_ok(paths.root, 32LL * 1024 * 1024)) return -2;
    if (bl_fs_mkdirs(bundle) != 0) return -1;

    BlAccount acc;
    g_write_failed = 0;
    g_mask_user[0] = 0;
    if (bl_account_load(&paths, &acc) == 0) memcpy(g_mask_user, acc.user, sizeof g_mask_user);
    memset(&acc, 0, sizeof acc);
    Out man = { malloc(65536), 65536, 0 };
    Out rd = { malloc(8192), 8192, 0 };
    if (!man.buf || !rd.buf) { free(man.buf); free(rd.buf); return -1; }
    put(&man, "blur-launcher support bundle 2\ncreated %s\nbuild %s\nrenderer %s\n", when,
        in->build_id ? in->build_id : "-", in->renderer ? in->renderer : "-");
    snprintf(paths.game, sizeof paths.game, "%.90s", in->game_dir);
    char exe_name[64];
    BlExeVerdict v = bl_exe_verify(&paths, exe_name);
    put(&man, "blur.exe %s %s\n", v == BL_EXE_MISSING ? "-" : exe_name,
        v == BL_EXE_TESTED ? "tested" : v == BL_EXE_UNSUPPORTED ? "not tested" : "missing");

    /* The package manifest and our installed files against it. */
    char mp[1200], mhex[65];
    if (in->files_dir && !join(mp, sizeof mp, in->files_dir, "manifest.txt") && bl_fs_sha256(mp, mhex, NULL) == 0)
        put(&man, "package manifest.txt %s\n", mhex);
    ManLine m[96];
    int n = in->files_dir ? read_manifest(in->files_dir, m, 96) : -1;
    for (int i = 0; i < n; i++) {
        if (strcmp(m[i].kind, "file") && strcmp(m[i].kind, "seed") && strcmp(m[i].kind, "amax")) continue;
        char p[1200], hex[65] = "-";
        uint64_t size = 0;
        int have = !join(p, sizeof p, in->game_dir, m[i].name) && bl_fs_sha256(p, hex, &size) == 0;
        put(&man, "installed %s %s %llu %s %s\n", m[i].kind, m[i].name, (unsigned long long)size, have ? hex : "missing",
            !have ? "MISSING" : !strcmp(hex, m[i].sha) ? "ok" : !strcmp(m[i].kind, "seed") ? "grown" : "DIFFERENT");
    }

    /* The fixed set. */
    for (int i = 0; i < bl_log_spec_count; i++) {
        const BlLogSpec *s = &bl_log_specs[i];
        const char *dir = folder_of(s->where, in->game_dir, in->data_root, state, sizeof state);
        if (!dir) continue;
        if (!strcmp(s->name, "*")) {           /* the whole logs folder */
            bundle_dir(&man, bundle, dir, "", "logs_");
            continue;
        }
        if (!strcmp(s->name, "amax/log/*.log")) {
            char sub[1100];
            if (snprintf(sub, sizeof sub, "%s/amax/log", dir) < (int)sizeof sub) bundle_dir(&man, bundle, sub, ".log", "amax_log_");
            continue;
        }
        char src[1200];
        if (join(src, sizeof src, dir, s->name)) continue;
        bundle_file(&man, bundle, src, s->name);
        if (s->rotate && snprintf(src + strlen(src), sizeof src - strlen(src), ".old") > 0) {
            char as[160];
            snprintf(as, sizeof as, "%s.old", s->name);
            bundle_file(&man, bundle, src, as);
        }
    }
    readme(&rd);
    char p[1300];
    int ok = snprintf(p, sizeof p, "%s/README.txt", bundle) < (int)sizeof p && bl_fs_write_atomic(p, rd.buf, rd.n, 0644) == 0;
    ok = ok && snprintf(p, sizeof p, "%s/MANIFEST.txt", bundle) < (int)sizeof p && bl_fs_write_atomic(p, man.buf, man.n, 0644) == 0;
    free(man.buf);
    free(rd.buf);
    memset(g_mask_user, 0, sizeof g_mask_user);
    if (!ok || g_write_failed) return -1;
    snprintf(out, cap, "%s", bundle);
    return 0;
}
