/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#define _POSIX_C_SOURCE 200809L
#include "bl_patcher.h"
#include "bl_fs.h"
#include "bl_time.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_LIST 32768
/* backup.txt: every parked file (name encoded, up to 3 bytes a character) and every added one. */
#define MAX_BACKUP_LIST (4096 + (size_t)(BL_PATCH_MAX_PARK + 2 * BL_PATCH_MAX_ITEMS) * (3 * REL_MAX + 120))
#define MAX_KNOWN 64
#define MAX_GLOBS 32
#define MAX_DEPTH 8
#define REL_MAX 128

typedef enum Kind { K_FILE = 0, K_AMAX, K_SEED, K_ONLINE, K_BLUR12 } Kind;

/* Lines installed only together with the AMAX set (online). */
static int online_kind(Kind k) { return k == K_ONLINE || k == K_AMAX || k == K_BLUR12; }
/* Lines whose source is the extracted zips (amax_dir), not blur-files. */
static int zip_kind(Kind k) { return k == K_AMAX || k == K_BLUR12; }

typedef struct Manifest {
    struct { char name[REL_MAX], src[REL_MAX], sha[65], edit[65]; unsigned long long size; Kind kind; } file[BL_PATCH_MAX_ITEMS];
    unsigned count;
    char known[MAX_KNOWN][65];
    unsigned known_count;
    char glob[MAX_GLOBS][64];
    unsigned glob_count;
} Manifest;

unsigned bl_patcher_plan_count;
char bl_patch_error[200];
const char *bl_patch_hash_cache;

/* Why the last apply or restore failed: "<step> <file> errno=<n>". */
int bl_patch_errno;
char bl_patch_error_name[128];

static int fail(const char *step, const char *name)
{
    bl_patch_errno = errno;
    snprintf(bl_patch_error_name, sizeof bl_patch_error_name, "%s", name && name[0] && name[0] != '/' ? name : "");
    snprintf(bl_patch_error, sizeof bl_patch_error, "%.24s %.127s errno=%d", step, name && name[0] ? name : "-", errno);
    return -1;
}

static int is_hex64(const char *s)
{
    if (strlen(s) != 64) return 0;
    for (const char *p = s; *p; p++) if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return 0;
    return 1;
}

/* A relative path we may write under the game folder: "/" separated
 * segments, none empty, none starting with a dot, none "Blur.exe"; printable
 * ASCII without spaces or backslashes. */
static int safe_rel(const char *n)
{
    size_t len = strlen(n);
    if (!len || len >= REL_MAX || n[0] == '/' || n[len - 1] == '/') return 0;
    for (const char *p = n; *p; p++) if ((unsigned char)*p <= 0x20 || (unsigned char)*p >= 0x7F || *p == '\\') return 0;
    const char *seg = n;
    for (;;) {
        const char *end = strchr(seg, '/');
        size_t sl = end ? (size_t)(end - seg) : strlen(seg);
        if (sl == 0 || seg[0] == '.') return 0;
        if (sl == 8 && !strncasecmp(seg, "Blur.exe", 8)) return 0;
        if (!end) break;
        seg = end + 1;
    }
    return 1;
}

/* A blur12 name: safe_rel, or Blur.exe (any letter case) at the top level. */
static int safe_rel_blur12(const char *n)
{
    return safe_rel(n) || !strcasecmp(n, "Blur.exe");
}

static int is_exe_name(const char *n) { return !strcasecmp(n, "Blur.exe"); }

/* A foreign file found on disk that Patch may move: anything a player's mod
 * can be called (spaces, non-ASCII, leading dots) except a way out of the
 * game folder ("..", ".", empty segments, a leading "/", control bytes). */
static int safe_park(const char *n)
{
    size_t len = strlen(n);
    if (!len || len >= REL_MAX || n[0] == '/' || n[len - 1] == '/') return 0;
    for (const char *p = n; *p; p++) if ((unsigned char)*p < 0x20 || (unsigned char)*p == 0x7F || *p == '\\') return 0;
    const char *seg = n;
    for (;;) {
        const char *end = strchr(seg, '/');
        size_t sl = end ? (size_t)(end - seg) : strlen(seg);
        if (sl == 0 || (sl == 1 && seg[0] == '.') || (sl == 2 && seg[0] == '.' && seg[1] == '.')) return 0;
        if (!end) break;
        seg = end + 1;
    }
    return 1;
}

/* backup.txt holds one token per name: bytes <= 0x20, '%' and >= 0x7F are %XX. */
static void name_encode(const char *in, char *out, size_t cap)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && n + 4 < cap; p++) {
        if (*p <= 0x20 || *p == '%' || *p >= 0x7F) { out[n++] = '%'; out[n++] = HEX[*p >> 4]; out[n++] = HEX[*p & 15]; }
        else out[n++] = (char)*p;
    }
    out[n] = 0;
}

static int hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

static int name_decode(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    for (const char *p = in; *p; p++) {
        if (n + 1 >= cap) return -1;
        if (*p == '%') {
            int a = hexval(p[1]), b = a < 0 ? -1 : hexval(p[2]);
            if (b < 0) return -1;
            out[n++] = (char)(a * 16 + b);
            p += 2;
        } else out[n++] = *p;
    }
    out[n] = 0;
    return 0;
}

/* A park pattern: one top-level name, '*' and '?' allowed. */
static int safe_glob(const char *g)
{
    size_t len = strlen(g);
    if (!len || len >= 64 || g[0] == '.' || strchr(g, '/') || strchr(g, '\\') || strstr(g, "..")) return 0;
    for (const char *p = g; *p; p++) if ((unsigned char)*p <= 0x20 || (unsigned char)*p >= 0x7F) return 0;
    return 1;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* Case-insensitive match of '*' and '?' (Wine ignores letter case). */
static int glob_match(const char *g, const char *s)
{
    if (!*g) return !*s;
    if (*g == '*') {
        for (;; s++) {
            if (glob_match(g + 1, s)) return 1;
            if (!*s) return 0;
        }
    }
    if (!*s) return 0;
    if (*g != '?' && lower((unsigned char)*g) != lower((unsigned char)*s)) return 0;
    return glob_match(g + 1, s + 1);
}

static int path_join(char *out, size_t cap, const char *dir, const char *name)
{
    return snprintf(out, cap, "%s/%s", dir, name) < (int)cap ? 0 : -1;
}

static int parent_mkdirs(const char *path)
{
    char p[1400];
    if (snprintf(p, sizeof p, "%s", path) >= (int)sizeof p) return -1;
    char *s = strrchr(p, '/');
    if (!s || s == p) return 0;
    *s = 0;
    return bl_fs_mkdirs(p);
}

static int load_manifest(const char *files_dir, Manifest *m)
{
    char path[1024], *buf = malloc(MAX_LIST);
    if (!buf) return -1;
    memset(m, 0, sizeof *m);
    if (path_join(path, sizeof path, files_dir, "manifest.txt") || bl_fs_read(path, buf, MAX_LIST) < 0) { free(buf); return -1; }
    int bad = 0;
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char kind[16] = "", a[200] = "", b[200] = "", c[200] = "";
        unsigned long long size = 0;
        if (line[0] == '#' || line[0] == '\r' || !line[0]) continue;
        int n = sscanf(line, "%15s %199s %199s %llu %199s", kind, a, b, &size, c);
        if (!strcmp(kind, "file") || !strcmp(kind, "seed") || !strcmp(kind, "amax") || !strcmp(kind, "online") || !strcmp(kind, "blur12")) {
            Kind k = !strcmp(kind, "file") ? K_FILE : !strcmp(kind, "amax") ? K_AMAX : !strcmp(kind, "seed") ? K_SEED
                   : !strcmp(kind, "online") ? K_ONLINE : K_BLUR12;
            const char *src = a, *edit = "";
            if (n == 5 && k == K_AMAX) {
                if (strncmp(c, "edit=", 5) || !is_hex64(c + 5)) { bad = 1; continue; }
                edit = c + 5;
            } else if (n == 5) {
                if ((k != K_FILE && k != K_ONLINE) || strncmp(c, "src=", 4) || !safe_rel(c + 4)) { bad = 1; continue; }
                src = c + 4;
            }
            int name_ok = k == K_BLUR12 ? safe_rel_blur12(a) : safe_rel(a);
            if (n < 4 || !name_ok || !is_hex64(b) || m->count >= BL_PATCH_MAX_ITEMS) { bad = 1; continue; }
            memcpy(m->file[m->count].name, a, strlen(a) + 1);     /* safe_rel: < REL_MAX */
            memcpy(m->file[m->count].src, src, strlen(src) + 1);
            memcpy(m->file[m->count].sha, b, 65);                 /* is_hex64 */
            memcpy(m->file[m->count].edit, edit, strlen(edit) + 1);   /* "" or is_hex64 */
            m->file[m->count].size = size;
            m->file[m->count].kind = k;
            m->count++;
        } else if (!strcmp(kind, "known")) {
            if (n < 2 || !is_hex64(a)) { bad = 1; continue; }
            if (m->known_count < MAX_KNOWN) memcpy(m->known[m->known_count++], a, 65);
        } else if (!strcmp(kind, "park")) {
            if (n < 2 || !safe_glob(a)) { bad = 1; continue; }
            if (m->glob_count < MAX_GLOBS) memcpy(m->glob[m->glob_count++], a, strlen(a) + 1);
        }
        /* Other keywords ("tested", future versions) are ignored here. */
    }
    free(buf);
    return bad || m->count == 0 ? -1 : 0;
}

int bl_patch_seed_cache(const char *files_dir, const char *cache_dir)
{
    char path[1024], *buf = malloc(MAX_LIST);
    bl_patch_error[0] = 0;
    if (!buf) return -1;
    if (!files_dir || !cache_dir || path_join(path, sizeof path, files_dir, "manifest.txt") || bl_fs_read(path, buf, MAX_LIST) < 0) {
        free(buf);
        return fail("cache-manifest", "manifest.txt");
    }
    int installed = 0, rc = 0;
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line && rc == 0; line = strtok_r(NULL, "\n", &save)) {
        char kind[16] = "", rel[200] = "", sha[200] = "";
        unsigned long long size = 0;
        if (sscanf(line, "%15s %199s %199s %llu", kind, rel, sha, &size) != 4 || strcmp(kind, "cache")) continue;
        if (!safe_rel(rel) || !is_hex64(sha)) { errno = EINVAL; rc = fail("cache-line", rel); break; }
        char src[1300], dst[1300], hex[65];
        if (path_join(src, sizeof src, files_dir, rel) || path_join(dst, sizeof dst, cache_dir, rel)) { rc = fail("path", rel); break; }
        struct stat st;
        if (stat(dst, &st) == 0 && (unsigned long long)st.st_size > BL_SEED_HEADER_BYTES) continue;   /* grown: keep */
        if (bl_fs_sha256(src, hex, NULL) != 0 || strcmp(hex, sha)) { rc = fail("cache-source-check", rel); break; }
        if (parent_mkdirs(dst) != 0) { rc = fail("mkdir", rel); break; }
        if (bl_fs_copy(src, dst, 0644) != 0) { rc = fail("copy", rel); break; }
        if (bl_fs_sha256(dst, hex, NULL) != 0 || strcmp(hex, sha)) { rc = fail("verify", rel); break; }
        installed++;
    }
    free(buf);
    return rc ? rc : installed;
}

/* Blur.exe in any letter case, as a regular file. Only looked for here (the
 * tested-exe gate is bl_exe). 1 found. */
static int find_exe(const char *game_dir, char *name, size_t cap, unsigned long long *size)
{
    DIR *d = opendir(game_dir);
    if (!d) return 0;
    int found = 0;
    struct dirent *e;
    char path[1200];
    while (!found && (e = readdir(d))) {
        struct stat st;
        if (strcasecmp(e->d_name, "Blur.exe") || snprintf(path, sizeof path, "%s/%s", game_dir, e->d_name) >= (int)sizeof path) continue;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (cap < sizeof "Blur.exe") break;
        memcpy(name, e->d_name, sizeof "Blur.exe");          /* matched "Blur.exe": 8 characters */
        *size = (unsigned long long)st.st_size;
        found = 1;
    }
    closedir(d);
    return found;
}

/* Files the launcher or AMAX write at run time inside parked folders. */
static int kept(const char *rel)
{
    return !strncasecmp(rel, "amax/log/", 9) || !strcasecmp(rel, "amax/plugins/account_cfg.lua");
}

/* Letter case ignored (Wine): AMAX/ is the same folder as amax/. */
static int in_set(const Manifest *m, const char *rel)
{
    for (unsigned i = 0; i < m->count; i++) if (!strcasecmp(m->file[i].name, rel)) return 1;
    return 0;
}

static void add_park(BlPatchPlan *plan, unsigned *total, const char *rel)
{
    for (unsigned i = 0; i < plan->park_count; i++) if (!strcmp(plan->park[i], rel)) return;
    (*total)++;
    if (plan->park_count < BL_PATCH_MAX_PARK && strlen(rel) < sizeof plan->park[0])
        memcpy(plan->park[plan->park_count++], rel, strlen(rel) + 1);
    else if (!plan->park_over[0])
        snprintf(plan->park_over, sizeof plan->park_over, "%s", rel);
}

/* Every file under game/rel that is not in the set and not kept. */
static void walk_park(const char *game_dir, const char *rel, int depth, const Manifest *m, BlPatchPlan *plan, unsigned *total)
{
    char dir[1400];
    if (depth > MAX_DEPTH || snprintf(dir, sizeof dir, "%s/%s", game_dir, rel) >= (int)sizeof dir) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char r[REL_MAX + 64], p[1700];
        struct stat st;
        if (snprintf(r, sizeof r, "%s/%s", rel, e->d_name) >= REL_MAX || snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p
            || lstat(p, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { walk_park(game_dir, r, depth + 1, m, plan, total); continue; }
        if (kept(r) || in_set(m, r)) continue;
        add_park(plan, total, r);
    }
    closedir(d);
}

/* rel with each segment spelled as on disk (letter case ignored) where a
 * match exists; the rest as given. */
static void resolve_case(const char *game_dir, const char *rel, char *out, size_t cap)
{
    char dir[1400], seg[REL_MAX];
    size_t used = 0;
    snprintf(dir, sizeof dir, "%s", game_dir);
    out[0] = 0;
    const char *p = rel;
    int on_disk = 1;
    while (*p) {
        const char *end = strchr(p, '/');
        size_t sl = end ? (size_t)(end - p) : strlen(p);
        if (sl >= sizeof seg) sl = sizeof seg - 1;
        char want[REL_MAX];
        memcpy(want, p, sl);
        want[sl] = 0;
        memcpy(seg, want, sl + 1);
        DIR *d = on_disk ? opendir(dir) : NULL;
        struct dirent *e;
        int found = 0;
        while (d && (e = readdir(d))) {
            if (!strcmp(e->d_name, want)) { memcpy(seg, want, sl + 1); found = 1; break; }
            if (!found && !strcasecmp(e->d_name, want)) { memcpy(seg, e->d_name, sl + 1); found = 2; }
        }
        if (d) closedir(d);
        if (!found) on_disk = 0;
        int w = snprintf(out + used, cap - used, "%s%s", used ? "/" : "", seg);
        if (w < 0 || used + (size_t)w >= cap) { snprintf(out, cap, "%s", rel); return; }
        used += (size_t)w;
        size_t dl = strlen(dir);
        snprintf(dir + dl, sizeof dir - dl, "/%s", seg);
        p = end ? end + 1 : p + strlen(p);
    }
}

/* Is entry i part of the set in this mode? Offline: file and seed lines.
 * Online: all lines, except a file/seed line whose name an amax or blur12
 * line takes over (the root d3d9.dll: V7 offline, the AMAX proxy online). */
static int active(const Manifest *m, unsigned i, int online)
{
    Kind k = m->file[i].kind;
    if (online_kind(k)) return online;
    if (!online) return 1;
    for (unsigned j = 0; j < m->count; j++)
        if (zip_kind(m->file[j].kind) && !strcmp(m->file[j].name, m->file[i].name)) return 0;
    return 1;
}

static int plan_with(const char *game_dir, const char *amax_dir, const Manifest *m, BlPatchPlan *plan)
{
    int online = amax_dir != NULL;
    memset(plan, 0, sizeof *plan);
    char path[1400];
    if (!find_exe(game_dir, plan->exe_name, sizeof plan->exe_name, &plan->exe_size)) {
        plan->game = BL_GAME_MISSING;
        return 0;
    }
    plan->game = BL_GAME_FOUND;
    unsigned park_total = 0;
    for (unsigned i = 0; i < m->count; i++) {
        if (!active(m, i, online)) continue;
        BlPlanItem *it = &plan->items[plan->count++];
        it->entry = i;
        /* The spelling on disk (Wine ignores case): never a second copy beside it. */
        resolve_case(game_dir, m->file[i].name, it->name, sizeof it->name);
        char hex[65];
        it->seed = m->file[i].kind == K_SEED;
        it->amax = zip_kind(m->file[i].kind);
        it->online = online_kind(m->file[i].kind);
        if (it->seed) {
            /* Never hashed: the game grows this file. Only missing or header-only counts. */
            struct stat st;
            int have = !path_join(path, sizeof path, game_dir, it->name) && stat(path, &st) == 0 && S_ISREG(st.st_mode)
                       && (unsigned long long)st.st_size > BL_SEED_HEADER_BYTES;
            it->state = have ? BL_FILE_OK : BL_FILE_MISSING;
        } else if (path_join(path, sizeof path, game_dir, it->name) || bl_fs_sha256_cached(bl_patch_hash_cache, path, hex, NULL) != 0) {
            it->state = BL_FILE_MISSING;
        } else if (strcmp(hex, m->file[i].edit[0] ? m->file[i].edit : m->file[i].sha) == 0) {
            it->state = BL_FILE_OK;
        } else {
            it->state = BL_FILE_DIFFERENT;
            it->foreign = 1;
            for (unsigned k = 0; k < m->known_count; k++) if (!strcmp(hex, m->known[k])) it->foreign = 0;
            /* An edited file is never replaced unparked: AMAX's own list (or
             * the player's) comes back at Restore. */
            for (unsigned k = 0; k < m->count && !m->file[i].edit[0]; k++)
                if (active(m, k, online) && !strcmp(hex, m->file[k].sha)) it->foreign = 0;
            if (it->foreign) add_park(plan, &park_total, it->name);
        }
        if (it->state != BL_FILE_OK) plan->pending++;
    }
    /* Foreign names at the top level (parking globs). */
    DIR *d = m->glob_count ? opendir(game_dir) : NULL;
    struct dirent *e;
    while (d && (e = readdir(d))) {
        const char *n = e->d_name;
        if (n[0] == '.' || !strcasecmp(n, "Blur.exe") || strlen(n) >= REL_MAX) continue;
        int match = 0;
        for (unsigned g = 0; g < m->glob_count && !match; g++) match = glob_match(m->glob[g], n);
        struct stat st;
        if (!match || path_join(path, sizeof path, game_dir, n) || lstat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) walk_park(game_dir, n, 1, m, plan, &park_total);
        else if (!in_set(m, n) && !kept(n)) add_park(plan, &park_total, n);
    }
    if (d) closedir(d);
    /* Foreign set items are already counted as pending items. */
    unsigned foreign = 0;
    for (unsigned i = 0; i < plan->count; i++) foreign += plan->items[i].foreign;
    plan->pending += park_total - foreign;
    plan->park_total = park_total;
    return 0;
}

int bl_patch_online_state(const char *game_dir, const char *files_dir)
{
    if (!game_dir || !files_dir) return BL_ONLINE_NONE;
    Manifest *m = malloc(sizeof *m);
    BlPatchPlan *plan = malloc(sizeof *plan);
    int r = BL_ONLINE_NONE;
    if (m && plan && load_manifest(files_dir, m) == 0 && plan_with(game_dir, BL_PATCH_IN_PLACE, m, plan) == 0
        && plan->game == BL_GAME_FOUND) {
        int any = 0, all = 1, lines = 0;
        for (unsigned i = 0; i < plan->count; i++) {
            const BlPlanItem *it = &plan->items[i];
            if (!it->online) continue;
            lines++;
            if (it->state != BL_FILE_OK) all = 0;
            else if (m->file[it->entry].kind != K_BLUR12) any = 1;   /* the player's own 1.2 exe is no sign of AMAX */
        }
        r = !lines || !any ? BL_ONLINE_NONE : all ? BL_ONLINE_READY : BL_ONLINE_PARTIAL;
    }
    free(m);
    free(plan);
    return r;
}

int bl_patch_online_in_place(const char *game_dir, const char *files_dir)
{
    return bl_patch_online_state(game_dir, files_dir) == BL_ONLINE_READY;
}

int bl_patch_plan(const char *game_dir, const char *files_dir, const char *amax_dir, BlPatchPlan *plan)
{
    bl_patcher_plan_count++;
    if (!game_dir || !files_dir || !plan) return -1;
    memset(plan, 0, sizeof *plan);
    Manifest *m = malloc(sizeof *m);
    if (!m) return -1;
    int r = load_manifest(files_dir, m) == 0 ? plan_with(game_dir, amax_dir, m, plan) : -1;
    free(m);
    return r;
}

/* ---- backups ---- */

/* The counter at the front of a backup name ("000012-2026..."), or 0. */
static unsigned long backup_seq(const char *name)
{
    char *end;
    if (name[0] < '0' || name[0] > '9') return 0;
    unsigned long v = strtoul(name, &end, 10);
    return *end == '-' ? v : 0;
}

static const char *backup_base(const char *game_dir, const char *backup_root, char *buf, size_t cap)
{
    if (backup_root) return backup_root;
    return snprintf(buf, cap, "%s/.blur-launcher/backup", game_dir) < (int)cap ? buf : NULL;
}

int bl_backup_new_dir(const char *backup_root, char *out, size_t cap)
{
    char stamp[BL_STAMP_LEN];
    if (!backup_root || !out || bl_fs_mkdirs(backup_root) != 0) return -1;
    bl_utc_stamp(stamp);
    unsigned long seq = 0;
    DIR *d = opendir(backup_root);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) { unsigned long s = backup_seq(e->d_name); if (s > seq) seq = s; }
        closedir(d);
    }
    for (unsigned long i = 1; i < 100; i++) {
        if (snprintf(out, cap, "%s/%06lu-%s", backup_root, seq + i, stamp) >= (int)cap) return -1;
        if (mkdir(out, 0755) == 0) return 0;
        if (errno != EEXIST) return -1;
    }
    return -1;
}

/* Complete, not yet restored patch backups (counter name without '.', with
 * backup.txt), newest (highest counter) first. At most max. */
static int list_backups(const char *base, char names[][64], int max)
{
    char p[1400];
    if (max <= 0 || !base) return 0;
    DIR *d = opendir(base);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        const char *name = e->d_name;
        unsigned long seq = backup_seq(name);
        if (!seq || strchr(name, '.') || strlen(name) >= 64) continue;
        if (snprintf(p, sizeof p, "%s/%s/backup.txt", base, name) >= (int)sizeof p || !bl_fs_exists(p)) continue;
        int at = n < max ? n++ : max;            /* insertion sort, keep the newest max */
        while (at > 0 && backup_seq(names[at - 1]) < seq) {
            if (at < max) memcpy(names[at], names[at - 1], 64);
            at--;
        }
        if (at < max) memcpy(names[at], name, strlen(name) + 1);
    }
    closedir(d);
    return n;
}

int bl_patch_last_backup(const char *backup_root, char *name, size_t cap)
{
    char names[1][64];
    if (name && cap) name[0] = 0;
    if (list_backups(backup_root, names, 1) != 1) return 0;
    if (name && cap) snprintf(name, cap, "%s", names[0]);
    return 1;
}

void bl_backup_label(const char *name, char *out, unsigned cap)
{
    if (!out || !cap) return;
    out[0] = 0;
    const char *s = name ? strchr(name, '-') : NULL;
    unsigned y, mo, da, h, mi;
    if (s && sscanf(s + 1, "%4u%2u%2uT%2u%2u", &y, &mo, &da, &h, &mi) == 5)
        snprintf(out, cap, "%04u-%02u-%02u %02u:%02u UTC", y, mo, da, h, mi);
    else if (name)
        snprintf(out, cap, "%s", name);
}

/* ---- AMAX's plugin list (finding 25) ---- */

static size_t skip_indent(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
    return i;
}

static int starts(const char *s, size_t n, const char *word)
{
    size_t w = strlen(word);
    return n >= w && !memcmp(s, word, w);
}

long bl_patch_plugins_edit(const char *in, size_t n, char *out, size_t cap)
{
    static const char ACCOUNT[] = "require(\"account.account\")";
    if (!in || !out) return -1;
    const char *eol = "\n";
    for (size_t i = 0; i + 1 < n; i++) if (in[i] == '\r' && in[i + 1] == '\n') { eol = "\r\n"; break; }
    /* The indent of the first require line, the last "}" line, our line. */
    char indent[32] = "\t";
    long close = -1;
    int have_account = 0, indent_set = 0;
    for (size_t at = 0; at < n;) {
        const char *nl = memchr(in + at, '\n', n - at);
        size_t len = nl ? (size_t)(nl - (in + at)) + 1 : n - at;
        size_t ind = skip_indent(in + at, len);
        const char *body = in + at + ind;
        if (!indent_set && starts(body, len - ind, "require(") && ind < sizeof indent) {
            memcpy(indent, in + at, ind);
            indent[ind] = 0;
            indent_set = 1;
        }
        if (starts(body, len - ind, ACCOUNT)) have_account = 1;
        if (starts(body, len - ind, "}")) close = (long)at;
        at += len;
    }
    if (close < 0) return -1;
    size_t o = 0;
#define EMIT(p, l) do { size_t l_ = (l); if (o + l_ >= cap) return -1; memcpy(out + o, (p), l_); o += l_; } while (0)
    for (size_t at = 0; at < n;) {
        const char *nl = memchr(in + at, '\n', n - at);
        size_t len = nl ? (size_t)(nl - (in + at)) + 1 : n - at;
        size_t ind = skip_indent(in + at, len);
        const char *body = in + at + ind;
        if (!have_account && (long)at == close) {
            EMIT(indent, strlen(indent));
            EMIT(ACCOUNT, sizeof ACCOUNT - 1);
            EMIT(",", 1);
            EMIT(eol, strlen(eol));
        }
        if (starts(body, len - ind, "require(\"solo.solo\")") || starts(body, len - ind, "require(\"fps.fps\")")) {
            EMIT(in + at, ind);
            EMIT("-- ", 3);
            EMIT(body, len - ind);
        } else {
            EMIT(in + at, len);
        }
        at += len;
    }
#undef EMIT
    out[o] = 0;
    return (long)o;
}

/* src (AMAX's list, already hash-checked) edited into path, atomically. */
static int install_edited(const char *src, const char *path)
{
    enum { MAX_LIST_FILE = 64 * 1024 };
    char *in = malloc(MAX_LIST_FILE), *out = malloc(MAX_LIST_FILE + 256);
    long n = in && out ? bl_fs_read(src, in, MAX_LIST_FILE) : -1;
    long e = n >= 0 ? bl_patch_plugins_edit(in, (size_t)n, out, MAX_LIST_FILE + 256) : -1;
    int rc = e > 0 && bl_fs_write_atomic(path, out, (size_t)e, 0644) == 0 ? 0 : -1;
    if (rc != 0 && e <= 0) errno = EINVAL;
    free(in);
    free(out);
    return rc;
}

/* ---- apply ---- */

/* AMAX's plugins.lua over an older version of ours (known hash, e.g. the
 * list PKG 286 shipped): that one is not parked, AMAX's own list from the
 * zip goes into the backup in its place. */
static int keeps_amax_original(const Manifest *m, const BlPlanItem *it, const char *amax_dir)
{
    return it->entry < m->count && m->file[it->entry].edit[0] && it->state == BL_FILE_DIFFERENT && !it->foreign &&
           amax_dir && amax_dir[0];
}

/* One pass: backup.txt, park (move), install, seed. */
/* made: the backup folder this pass created, "" when none. */
static int apply_once(const char *game_dir, const char *files_dir, const char *amax_dir, const char *base,
                      const Manifest *m, const BlPatchPlan *plan, int first,
                      void (*progress)(void *, const char *, unsigned, unsigned), void *ctx, char *made, size_t made_cap)
{
    char path[1400], src[1400], bdir[1024];
    unsigned total = plan->park_count, done = 0;
    for (unsigned i = 0; i < plan->count; i++) if (plan->items[i].state != BL_FILE_OK) total++;
    made[0] = 0;

    /* 1. The backup record is complete before any game file moves. Its
     * first line is the journal: the mode of this run ("done" beside it once
     * the run finished, bl_patch_unfinished). */
    size_t cap = 512 + (size_t)(plan->park_count + plan->count) * (3 * REL_MAX + 120), used = 0;
    char *list = malloc(cap);
    if (!list) return fail("memory", NULL);
    size_t head = (size_t)snprintf(list, cap, "run %s\n", amax_dir ? "online" : "offline");
    used = head;
    for (unsigned i = 0; i < plan->park_count; i++) {
        char hex[65];
        uint64_t size = 0;
        char enc[3 * REL_MAX + 1];
        if (!safe_park(plan->park[i])) { free(list); return fail("plan", plan->park[i]); }
        if (path_join(path, sizeof path, game_dir, plan->park[i])) continue;
        if (bl_fs_sha256(path, hex, &size) != 0) {
            if (!bl_fs_exists(path)) continue;     /* gone since the plan: nothing to park */
            /* Audit 22: a file that cannot be read would be moved but not
             * listed, so Restore could never bring it back. Nothing moved yet. */
            free(list);
            return fail(first ? "unreadable" : "park-unreadable", plan->park[i]);
        }
        name_encode(plan->park[i], enc, sizeof enc);
        used += (size_t)snprintf(list + used, cap - used, "parked %s %s %llu\n", enc, hex, (unsigned long long)size);
    }
    for (unsigned i = 0; i < plan->count; i++) {
        const BlPlanItem *it = &plan->items[i];
        if (it->entry >= m->count || !(m->file[it->entry].kind == K_BLUR12 ? safe_rel_blur12(it->name) : safe_rel(it->name))) {
            free(list);
            return fail("plan", it->name);
        }
        /* A parked foreign item comes back over ours at restore: not "added". */
        if (!it->seed && it->state == BL_FILE_MISSING)
            used += (size_t)snprintf(list + used, cap - used, "added %s\n", it->name);
        if (keeps_amax_original(m, it, amax_dir)) {
            char enc[3 * REL_MAX + 1];
            name_encode(it->name, enc, sizeof enc);
            used += (size_t)snprintf(list + used, cap - used, "parked %s %s %llu\n", enc, m->file[it->entry].sha,
                                     (unsigned long long)m->file[it->entry].size);
        }
    }
    if (used > head) {
        if (used >= cap || bl_backup_new_dir(base, bdir, sizeof bdir) != 0) { free(list); return fail("backup", base); }
        snprintf(made, made_cap, "%s", bdir);
        if (path_join(path, sizeof path, bdir, "backup.txt") || bl_fs_write_atomic(path, list, used, 0644) != 0) {
            free(list);
            int r = fail("backup", "backup.txt");
            rmdir(bdir);
            return r;
        }
        /* 2. Park: move, keeping the relative path. */
        for (unsigned i = 0; i < plan->park_count; i++) {
            if (progress) progress(ctx, plan->park[i], done, total);
            if (path_join(src, sizeof src, game_dir, plan->park[i]) || path_join(path, sizeof path, bdir, plan->park[i])) { free(list); return fail("path", plan->park[i]); }
            if (!bl_fs_exists(src)) { done++; continue; }
            /* Blur.exe is copied, not moved: the folder never lacks it. */
            int copy = is_exe_name(plan->park[i]);
            if (parent_mkdirs(path) != 0 || (copy ? bl_fs_copy(src, path, 0644) : bl_fs_rename(src, path)) != 0) {
                free(list);
                return fail("park", plan->park[i]);
            }
            done++;
        }
        /* An older list of ours (a known hash) is replaced unparked, so the
         * backup gets AMAX's own list instead: Restore puts that back. */
        for (unsigned i = 0; i < plan->count; i++) {
            const BlPlanItem *it = &plan->items[i];
            if (!keeps_amax_original(m, it, amax_dir)) continue;
            char hex[65];
            if (path_join(src, sizeof src, amax_dir, m->file[it->entry].name) || path_join(path, sizeof path, bdir, it->name)) { free(list); return fail("path", it->name); }
            if (bl_fs_sha256(src, hex, NULL) != 0 || strcmp(hex, m->file[it->entry].sha)) { free(list); return fail("amax-source-check", it->name); }
            if (parent_mkdirs(path) != 0 || bl_fs_copy(src, path, 0644) != 0) { free(list); return fail("park", it->name); }
        }
    }
    free(list);

    /* 3. Install our files and the AMAX set; 4. seed. */
    for (unsigned i = 0; i < plan->count; i++) {
        const BlPlanItem *it = &plan->items[i];
        if (it->state == BL_FILE_OK) continue;
        int k = (int)it->entry;
        if (progress) progress(ctx, it->name, done, total);
        if (path_join(path, sizeof path, game_dir, it->name)) return fail("path", it->name);
        if (zip_kind(m->file[k].kind)) {
            if (!amax_dir || !amax_dir[0]) { errno = ENOENT; return fail("amax-source", it->name); }
            if (path_join(src, sizeof src, amax_dir, m->file[k].name)) return fail("path", it->name);
        } else if (path_join(src, sizeof src, files_dir, m->file[k].src)) {
            return fail("path", it->name);
        }
        if (it->seed) {                           /* grown since the plan: leave it */
            struct stat st;
            if (stat(path, &st) == 0 && (unsigned long long)st.st_size > BL_SEED_HEADER_BYTES) { done++; continue; }
        }
        char hex[65];
        const char *want = m->file[k].sha;
        /* The source copy must itself be right before it goes in. */
        if (bl_fs_sha256(src, hex, NULL) != 0 || strcmp(hex, want)) return fail(it->amax ? "amax-source-check" : "package-copy-check", it->name);
        if (parent_mkdirs(path) != 0) return fail("mkdir", it->name);
        if (m->file[k].edit[0]) {                 /* AMAX's plugins.lua with our account plugin */
            if (install_edited(src, path) != 0) return fail("edit", it->name);
            want = m->file[k].edit;
        } else if (bl_fs_copy(src, path, 0644) != 0) return fail("copy", it->name);
        if (bl_fs_sha256(path, hex, NULL) != 0 || strcmp(hex, want)) return fail("verify", it->name);
        done++;
        if (progress) progress(ctx, it->name, done, total);
    }
    return 0;
}

int bl_patch_apply(const char *game_dir, const char *files_dir, const char *amax_dir, const char *backup_root,
                   const BlPatchPlan *plan, void (*progress)(void *ctx, const char *name, unsigned done, unsigned total), void *ctx)
{
    bl_patch_error[0] = 0;
    if (!game_dir || !files_dir || !plan || plan->game == BL_GAME_MISSING) return fail("no-game", NULL);
    if (plan->pending == 0) return 0;
    /* Audit 21: more than one plan can park: refused before any change. */
    if (plan->park_total > BL_PATCH_MAX_PARK) { errno = 0; return fail("too-many-files", plan->park_over); }
    char basebuf[1100];
    const char *base = backup_base(game_dir, backup_root, basebuf, sizeof basebuf);
    if (!base) return fail("path", "backup");
    Manifest *m = malloc(sizeof *m);
    BlPatchPlan *now = malloc(sizeof *now);
    if (!m || !now) { free(m); free(now); return fail("memory", NULL); }
    int rc = 0;
    if (load_manifest(files_dir, m) != 0) rc = fail("manifest", "manifest.txt");
    const BlPatchPlan *cur = plan;
    char made[8][1024];
    int nmade = 0;
    /* More foreign files than one plan holds: park and plan again (bounded). */
    for (int round = 0; rc == 0 && round < 8; round++) {
        int r = apply_once(game_dir, files_dir, amax_dir, base, m, cur, round == 0, progress, ctx, made[nmade], sizeof made[0]);
        if (made[nmade][0]) nmade++;
        if (r != 0) { rc = -1; break; }
        unsigned before = cur->pending;
        if (plan_with(game_dir, amax_dir, m, now) != 0) { rc = fail("replan", NULL); break; }
        if (now->pending == 0) break;
        if (now->pending >= before || round == 7) {
            const char *name = now->park_count ? now->park[0] : "-";
            for (unsigned i = 0; i < now->count; i++) if (now->items[i].state != BL_FILE_OK) { name = now->items[i].name; break; }
            errno = 0;
            rc = fail("still-pending", name);
            break;
        }
        cur = now;
    }
    /* The run finished: its backups say so. */
    for (int i = 0; rc == 0 && i < nmade; i++) {
        char p[1100];
        if (path_join(p, sizeof p, made[i], "done") || bl_fs_write_atomic(p, "done\n", 5, 0644) != 0) rc = fail("journal", "done");
    }
    free(m);
    free(now);
    return rc;
}

/* ---- restore ---- */

static int restore_one(const char *game_dir, const char *bdir)
{
    char path[1400], dst[1400], *buf = malloc(MAX_BACKUP_LIST);
    if (!buf) return -1;
    if (snprintf(path, sizeof path, "%s/backup.txt", bdir) >= (int)sizeof path || bl_fs_read(path, buf, MAX_BACKUP_LIST) < 0) { free(buf); return -1; }
    int rc = 0;
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line && rc == 0; line = strtok_r(NULL, "\n", &save)) {
        char kind[16] = "", raw[400] = "", name[REL_MAX + 8] = "", sha[200] = "";
        if (sscanf(line, "%15s %399s %199s", kind, raw, sha) < 2) continue;
        if (!strcmp(kind, "parked")) { if (name_decode(raw, name, sizeof name) || !safe_park(name)) continue; }
        else if (strlen(raw) >= sizeof name || !safe_rel(raw)) continue;
        else memcpy(name, raw, strlen(raw) + 1);
        if (path_join(dst, sizeof dst, game_dir, name) || path_join(path, sizeof path, bdir, name)) { rc = -1; break; }
        if (!strcmp(kind, "added")) {
            if (bl_fs_exists(dst) && unlink(dst) != 0) rc = -1;
        } else if (!strcmp(kind, "parked")) {
            /* A move the interrupted Patch never made: the file is still in place. */
            if (bl_fs_exists(path) && (parent_mkdirs(dst) != 0 || bl_fs_rename(path, dst) != 0)) rc = -1;
        } else if (!strcmp(kind, "file")) {      /* launcher r1-r3 backups: copies */
            char hex[65];
            if (bl_fs_copy(path, dst, 0644) != 0 || bl_fs_sha256(dst, hex, NULL) != 0 || strcmp(hex, sha)) rc = -1;
        }
    }
    free(buf);
    if (rc) return -1;
    char done[1200];
    if (snprintf(done, sizeof done, "%s.restored", bdir) >= (int)sizeof done) return -1;
    return rename(bdir, done) == 0 ? 0 : -1;
}

int bl_patch_restore(const char *game_dir, const char *backup_root)
{
    bl_patch_error[0] = 0;
    if (!game_dir) return fail("no-game", NULL);
    char basebuf[1100], names[64][64], bdir[1200];
    const char *base = backup_base(game_dir, backup_root, basebuf, sizeof basebuf);
    if (!base) return fail("path", "backup");
    int n = list_backups(base, names, 64);
    if (n == 0) return 1;
    for (int i = 0; i < n && i < 64; i++) {
        if (snprintf(bdir, sizeof bdir, "%s/%s", base, names[i]) >= (int)sizeof bdir) return -1;
        if (restore_one(game_dir, bdir) != 0) return fail("restore", names[i]);
    }
    return 0;
}

int bl_patch_unfinished(const char *game_dir, const char *backup_root)
{
    char basebuf[1100], names[64][64];
    if (!game_dir) return 0;
    const char *base = backup_base(game_dir, backup_root, basebuf, sizeof basebuf);
    int n = list_backups(base, names, 64);
    for (int i = 0; i < n && i < 64; i++) {
        char p[1300], first[16] = "";
        if (snprintf(p, sizeof p, "%s/%s/backup.txt", base, names[i]) >= (int)sizeof p) continue;
        int fd = open(p, O_RDONLY);
        if (fd < 0) continue;
        ssize_t r = read(fd, first, sizeof first - 1);
        close(fd);
        if (r <= 0 || strncmp(first, "run online\n", 11)) continue;
        if (snprintf(p, sizeof p, "%s/%s/done", base, names[i]) >= (int)sizeof p) continue;
        if (!bl_fs_exists(p)) return 1;
    }
    return 0;
}
