/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#include "bl_profile.h"
#include "bl_fs.h"
#include "bl_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ENTRIES 96
#define MAX_PRESET 16384

typedef struct Entry { char section[32], key[48], value[256]; } Entry;
typedef struct Preset { Entry e[MAX_ENTRIES]; int n; } Preset;

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

/* Merge a preset file into p: same section+key replaces, new keys append. */
static int merge(Preset *p, const char *path)
{
    char *buf = malloc(MAX_PRESET);
    if (!buf) return -1;
    if (bl_fs_read(path, buf, MAX_PRESET) < 0) { free(buf); return -1; }
    char section[32] = "";
    for (char *line = buf, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *s = trim(line);
        if (!*s || *s == ';' || *s == '#') continue;
        if (*s == '[') {
            char *e = strchr(s, ']');
            if (e) { *e = 0; snprintf(section, sizeof section, "%s", trim(s + 1)); }
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq || !section[0]) continue;
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);
        if (strlen(k) >= sizeof p->e[0].key || strlen(v) >= sizeof p->e[0].value) { free(buf); return -1; }
        int i;
        for (i = 0; i < p->n; i++) if (!strcmp(p->e[i].section, section) && !strcmp(p->e[i].key, k)) break;
        if (i == p->n) {
            if (p->n == MAX_ENTRIES) { free(buf); return -1; }
            p->n++;
            snprintf(p->e[i].section, sizeof p->e[i].section, "%s", section);
            snprintf(p->e[i].key, sizeof p->e[i].key, "%s", k);
        }
        snprintf(p->e[i].value, sizeof p->e[i].value, "%s", v);
    }
    free(buf);
    return 0;
}

int bl_decision_setenv(BlDecision *d, const char *name, const char *value)
{
    if (!d || !name || !value || strlen(name) >= sizeof d->env[0].name || strlen(value) >= sizeof d->env[0].value) return -1;
    unsigned i;
    for (i = 0; i < d->env_count; i++) if (!strcmp(d->env[i].name, name)) break;
    if (i == d->env_count) {
        if (d->env_count >= sizeof d->env / sizeof d->env[0]) return -1;
        d->env_count++;
        snprintf(d->env[i].name, sizeof d->env[i].name, "%s", name);
    }
    snprintf(d->env[i].value, sizeof d->env[i].value, "%s", value);
    return 0;
}

void bl_decision_unsetenv(BlDecision *d, const char *name)
{
    if (!d || !name) return;
    for (unsigned i = 0; i < d->env_count; i++) {
        if (strcmp(d->env[i].name, name)) continue;
        memmove(&d->env[i], &d->env[i + 1], (d->env_count - i - 1) * sizeof d->env[0]);
        d->env_count--;
        memset(&d->env[d->env_count], 0, sizeof d->env[0]);
        return;
    }
}

static int append(char *dst, size_t cap, const char *s)
{
    size_t n = strlen(dst), m = strlen(s);
    if (n + m + 1 > cap) return -1;
    memcpy(dst + n, s, m + 1);
    return 0;
}

int bl_profile_compose(const BlConfig *c, int detected_pro, const char *files_dir, BlDecision *out)
{
    if (!c || !files_dir || !out) return -1;
    char remote[sizeof out->remote_profile];
    memcpy(remote, out->remote_profile, sizeof remote);
    memset(out, 0, sizeof *out);
    memcpy(out->remote_profile, remote, sizeof remote);
    out->remote_profile[sizeof remote - 1] = 0;

    Preset *p = calloc(1, sizeof *p);
    if (!p) return -1;
    char path[1024];
    snprintf(path, sizeof path, "%s/model-base.ini", files_dir);
    if (merge(p, path) != 0) { free(p); return -1; }
    int pro = c->model == BL_MODEL_PRO || (c->model == BL_MODEL_AUTO && detected_pro == 1);
    if (pro) {
        snprintf(path, sizeof path, "%s/model-pro.ini", files_dir);
        if (bl_fs_exists(path) && merge(p, path) != 0) { free(p); return -1; }
    }

    int rc = 0;
    const char *winedebug = c->logging ? "+tid,err+all,+loaddll" : "-all";
    /* Profile: every section except env and dxvk.conf, in first-seen order. */
    char done[16][32];
    int nd = 0;
    for (int i = 0; i < p->n && rc == 0; i++) {
        const char *sec = p->e[i].section;
        if (!strcmp(sec, "env") || !strcmp(sec, "dxvk.conf") || !strcmp(sec, "debug")) continue;
        int seen = 0;
        for (int k = 0; k < nd; k++) if (!strcmp(done[k], sec)) seen = 1;
        if (seen || nd == 16) continue;
        snprintf(done[nd++], 32, "%s", sec);
        char line[400];
        snprintf(line, sizeof line, "[%s]\n", sec);
        rc |= append(out->profile_text, sizeof out->profile_text, line);
        for (int j = i; j < p->n; j++) {
            if (strcmp(p->e[j].section, sec)) continue;
            snprintf(line, sizeof line, "%s = %s\n", p->e[j].key, p->e[j].value);
            rc |= append(out->profile_text, sizeof out->profile_text, line);
        }
    }
    char dbg[96];
    snprintf(dbg, sizeof dbg, "[debug]\nwinedebug = %s\n", winedebug);
    rc |= append(out->profile_text, sizeof out->profile_text, dbg);

    for (int i = 0; i < p->n && rc == 0; i++) {
        if (!strcmp(p->e[i].section, "dxvk.conf")) {
            char line[400];
            snprintf(line, sizeof line, "%s = %s\n", p->e[i].key, p->e[i].value);
            rc |= append(out->dxvk_conf, sizeof out->dxvk_conf, line);
        } else if (!strcmp(p->e[i].section, "env")) {
            rc |= bl_decision_setenv(out, p->e[i].key, p->e[i].value);
        }
    }
    free(p);
    if (rc) return -1;

    /* Production (Logging off): no renderer log is written (audit 15), nothing is measured
     * (PROD_AUDIT.md). Logging on: the diagnostics support needs, each
     * bounded by size (bl_logs_rotate) and by the module's own caps. */
    /* BLUR 1.00 (plan Task L9): engine paths from bl_paths (/data/JkFlow/System), native only
     * (D16). No DXVK_LOG_PATH: the client finds dxn_arm.txt in the working
     * directory, game/, where bl_native_prepare writes it. */
    BlPaths paths;
    bl_paths_init(&paths, NULL);
    char conf[160];
    snprintf(conf, sizeof conf, "%s/dxvk.conf", paths.cache);
    /* Only with logs on (owner finding 19, L5): without BLUR_LOG_DIR the
     * engine writes no wowblur.log, radv.log or dxn_fault.txt. */
    if (c->logging) rc |= bl_decision_setenv(out, "BLUR_LOG_DIR", paths.logs);
    rc |= bl_decision_setenv(out, "DXVK_CONFIG_FILE", conf);
    rc |= bl_decision_setenv(out, "DXVK_STATE_CACHE_PATH", paths.cache);
    rc |= bl_decision_setenv(out, "DXVK_PS4_NATIVE_ONLY", "1");
    rc |= bl_decision_setenv(out, "WINEDEBUG", winedebug);
    rc |= bl_decision_setenv(out, "DXVK_LOG_LEVEL", c->logging ? "info" : "none");
    rc |= bl_decision_setenv(out, "BLUR_LOGS", c->logging ? "1" : "0");
    if (c->logging) rc |= bl_decision_setenv(out, "DXVK_PS4_FRAME_LOG", "1");
    /* Show FPS / ms: off or on (an older "full" counts as on). */
    rc |= bl_decision_setenv(out, "DXVK_PS4_FPS_HUD", c->stats == BL_STATS_OFF ? "0" : "fps");
    return rc ? -1 : 0;
}

int bl_profile_set_game(BlDecision *d, const char *game_dir)
{
    if (!d || !game_dir || game_dir[0] != '/') return -1;
    char win[600];
    if (snprintf(win, sizeof win, "Z:%s", game_dir) >= (int)sizeof win) return -1;
    for (char *c = win; *c; c++) if (*c == '/') *c = '\\';
    char out[sizeof d->profile_text];
    size_t n = 0;
    int replaced = 0;
    const char *p = d->profile_text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) + 1 : strlen(p);
        const char *k = p + strspn(p, " \t");                 /* keys may be indented */
        char line[700];
        const char *src = p;
        size_t slen = len;
        if (!strncmp(k, "executable", 10) && (k[10] == ' ' || k[10] == '\t' || k[10] == '=')) {
            slen = (size_t)snprintf(line, sizeof line, "executable = %s\\Blur.exe\n", win);
            src = line;
            replaced = 1;
        } else if (!strncmp(k, "working_directory", 17) && (k[17] == ' ' || k[17] == '\t' || k[17] == '=')) {
            slen = (size_t)snprintf(line, sizeof line, "working_directory = %s\n", win);
            src = line;
        }
        if (src == line && slen >= sizeof line) return -1;
        if (n + slen >= sizeof out) return -1;
        memcpy(out + n, src, slen);
        n += slen;
        p += len;
    }
    if (!replaced) return -1;                                 /* Blur would start from nowhere */
    out[n] = 0;
    memcpy(d->profile_text, out, n + 1);
    return 0;
}
