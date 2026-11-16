/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#define _POSIX_C_SOURCE 200809L
#include "bl_config.h"
#include "bl_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_FILE 65536

/* ---- known keys ---- */

typedef enum Key { K_SCHEMA, K_MODEL, K_MODE, K_HOST, K_AUTH, K_LOBBY, K_PORT, K_LOGIN, K_USER, K_LOGGING, K_STATS, K_RENDERER, K_FRAMERATE, K_COUNT } Key;
static const struct { const char *section, *key; } KEYS[K_COUNT] = {
    [K_SCHEMA] = { "launcher", "schema" }, [K_MODEL] = { "console", "model" },
    [K_MODE] = { "online", "mode" }, [K_HOST] = { "online", "host" }, [K_AUTH] = { "online", "auth_host" },
    [K_LOBBY] = { "online", "lobby_host" }, [K_PORT] = { "online", "port" }, [K_LOGIN] = { "online", "login" },
    [K_USER] = { "online", "username" }, [K_LOGGING] = { "logging", "enabled" }, [K_STATS] = { "overlay", "stats" },
    [K_RENDERER] = { "renderer", "mode" }, [K_FRAMERATE] = { "game", "frame_rate" },
};
static const char *const SECTIONS[] = { "launcher", "console", "renderer", "online", "logging", "overlay", "game" };
#define N_SECTIONS (int)(sizeof SECTIONS / sizeof SECTIONS[0])
static const char *const MODEL_NAMES[] = { "auto", "base", "pro" };
static const char *const MODE_NAMES[] = { "official", "private", "off" };
static const char *const STATS_NAMES[] = { "off", "fps", "full" };
static const char *const RENDERER_NAMES[] = { "native", "compatible" };

void bl_config_defaults(BlConfig *c)
{
    memset(c, 0, sizeof *c);
    c->schema = 1;
    c->model = BL_MODEL_AUTO;
    c->online_mode = BL_ONLINE_OFFICIAL;
    c->stats = BL_STATS_OFF;
}

static void warn(char *w, size_t cap, const char *msg)
{
    if (!w || !cap) return;
    size_t n = strlen(w);
    if (n && n + 2 < cap) { strcat(w, "; "); n += 2; }
    if (n < cap) snprintf(w + n, cap - n, "%s", msg);
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

static int pick(const char *v, const char *const *names, int n)
{
    for (int i = 0; i < n; i++) if (strcmp(v, names[i]) == 0) return i;
    return -1;
}

/* Printable ASCII without the INI comment characters. */
static int text_ok(const char *v, size_t cap)
{
    size_t n = strlen(v);
    if (n >= cap) return 0;
    for (const char *p = v; *p; p++) if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7E || *p == ';' || *p == '#') return 0;
    return 1;
}

/* Value without an inline comment (" ; ..." or " # ..."). */
static char *value_of(char *v)
{
    for (char *p = v; *p; p++)
        if ((*p == ';' || *p == '#') && (p == v || p[-1] == ' ' || p[-1] == '\t')) { *p = 0; break; }
    return trim(v);
}

static void apply(BlConfig *c, Key k, char *v, char *w, size_t cap)
{
    char msg[160];
    int i;
    switch (k) {
    case K_SCHEMA: break;
    case K_MODEL:
        if ((i = pick(v, MODEL_NAMES, 3)) >= 0) c->model = (BlModel)i;
        else warn(w, cap, "console model is not auto/base/pro, using auto");
        break;
    case K_MODE:
        if ((i = pick(v, MODE_NAMES, 3)) >= 0) c->online_mode = (BlOnlineMode)i;
        else warn(w, cap, "online mode is not official/private/off, using official");
        break;
    case K_HOST: case K_AUTH: case K_LOBBY: {
        char *dst = k == K_HOST ? c->host : k == K_AUTH ? c->auth_host : c->lobby_host;
        if (text_ok(v, 128) && !strchr(v, ' ')) snprintf(dst, 128, "%s", v);
        else { snprintf(msg, sizeof msg, "online %s is not a valid address, ignored", KEYS[k].key); warn(w, cap, msg); }
        break;
    }
    case K_PORT: {
        char *end;
        long p = strtol(v, &end, 10);
        if (*v && !*end && p >= 0 && p <= 65535) c->port = (int)p;
        else warn(w, cap, "online port must be 0-65535, using the game's default");
        break;
    }
    case K_LOGIN:
        if (strcmp(v, "anonymous") == 0) c->login_account = 0;
        else if (strcmp(v, "account") == 0) c->login_account = 1;
        else warn(w, cap, "online login is not anonymous/account, using anonymous");
        break;
    case K_USER:
        if (text_ok(v, 64)) snprintf(c->username, sizeof c->username, "%s", v);
        else warn(w, cap, "online username is too long or has unsupported characters, ignored");
        break;
    case K_LOGGING:
        if (strcmp(v, "true") == 0 || strcmp(v, "1") == 0 || strcmp(v, "on") == 0) c->logging = 1;
        else if (strcmp(v, "false") == 0 || strcmp(v, "0") == 0 || strcmp(v, "off") == 0) c->logging = 0;
        else warn(w, cap, "logging enabled is not true/false, using false");
        break;
    case K_STATS:
        if ((i = pick(v, STATS_NAMES, 3)) >= 0) c->stats = (BlStats)i;
        else warn(w, cap, "overlay stats is not off/fps/full, using off");
        break;
    case K_RENDERER:
        if ((i = pick(v, RENDERER_NAMES, 2)) >= 0) c->renderer = (BlRenderer)i;
        else warn(w, cap, "renderer mode is not native/compatible, using native");
        break;
    case K_FRAMERATE:
        if (strcmp(v, "30") == 0) c->frame_rate = BL_FPS_30;
        else if (strcmp(v, "60") == 0) c->frame_rate = BL_FPS_60;
        else warn(w, cap, "game frame rate is not 30/60, using 30");
        break;
    case K_COUNT: break;
    }
}

static Key key_of(const char *section, const char *key)
{
    for (int k = 0; k < K_COUNT; k++)
        if (strcmp(KEYS[k].section, section) == 0 && strcmp(KEYS[k].key, key) == 0) return (Key)k;
    return K_COUNT;
}

/* A file is usable if it has no NUL bytes and fits MAX_FILE. */
static long read_usable(const char *path, char *buf, int *corrupt)
{
    *corrupt = 0;
    long n = bl_fs_read(path, buf, MAX_FILE);
    if (n == -1) return -1;
    if (n == -2 || (long)strlen(buf) != n) { *corrupt = 1; buf[0] = 0; return 0; }
    return n;
}

int bl_config_load(const char *path, BlConfig *c, char *warning, size_t cap)
{
    if (!c) return -1;
    bl_config_defaults(c);
    if (warning && cap) warning[0] = 0;
    char *buf = malloc(MAX_FILE);
    if (!buf) return -1;
    int corrupt;
    long n = read_usable(path, buf, &corrupt);
    if (n < 0) { free(buf); return 0; }               /* no file yet: defaults */
    if (corrupt) { warn(warning, cap, "settings file is unreadable, defaults used"); free(buf); return 1; }

    char section[64] = "";
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
        if (!eq) continue;
        *eq = 0;
        Key k = key_of(section, trim(s));
        if (k != K_COUNT) apply(c, k, value_of(eq + 1), warning, cap);
    }
    free(buf);
    return warning && warning[0] ? 1 : 0;
}

static void value_text(const BlConfig *c, Key k, char *out, size_t cap)
{
    switch (k) {
    case K_SCHEMA: snprintf(out, cap, "%d", c->schema > 0 ? c->schema : 1); break;
    case K_MODEL: snprintf(out, cap, "%s", MODEL_NAMES[(unsigned)c->model % 3]); break;
    case K_MODE: snprintf(out, cap, "%s", MODE_NAMES[(unsigned)c->online_mode % 3]); break;
    case K_HOST: snprintf(out, cap, "%s", c->host); break;
    case K_AUTH: snprintf(out, cap, "%s", c->auth_host); break;
    case K_LOBBY: snprintf(out, cap, "%s", c->lobby_host); break;
    case K_PORT: snprintf(out, cap, "%d", c->port >= 0 && c->port <= 65535 ? c->port : 0); break;
    case K_LOGIN: snprintf(out, cap, "%s", c->login_account ? "account" : "anonymous"); break;
    case K_USER: snprintf(out, cap, "%s", text_ok(c->username, 64) ? c->username : ""); break;
    case K_LOGGING: snprintf(out, cap, "%s", c->logging ? "true" : "false"); break;
    case K_STATS: snprintf(out, cap, "%s", STATS_NAMES[(unsigned)c->stats % 3]); break;
    case K_RENDERER: snprintf(out, cap, "%s", RENDERER_NAMES[(unsigned)c->renderer % 2]); break;
    case K_FRAMERATE: snprintf(out, cap, "%s", c->frame_rate == BL_FPS_60 ? "60" : "30"); break;
    case K_COUNT: out[0] = 0; break;
    }
}

typedef struct Out { char *p; size_t n, cap; int overflow; } Out;
static void emit(Out *o, const char *s)
{
    size_t len = strlen(s);
    if (o->n + len + 1 > o->cap) { o->overflow = 1; return; }
    memcpy(o->p + o->n, s, len + 1);
    o->n += len;
}

static void emit_key(Out *o, const BlConfig *c, Key k)
{
    char v[160], line[256];
    value_text(c, k, v, sizeof v);
    snprintf(line, sizeof line, "%s = %s\n", KEYS[k].key, v);
    emit(o, line);
}

/* Write the keys of `section` that were not written yet. */
static void emit_missing(Out *o, const BlConfig *c, const char *section, int written[K_COUNT])
{
    for (int k = 0; k < K_COUNT; k++)
        if (!written[k] && strcmp(KEYS[k].section, section) == 0) { emit_key(o, c, (Key)k); written[k] = 1; }
}

int bl_config_save(const char *path, const BlConfig *c)
{
    if (!path || !c) return -1;
    char *old = malloc(MAX_FILE), *outbuf = malloc(2 * MAX_FILE);
    if (!old || !outbuf) { free(old); free(outbuf); return -1; }
    int corrupt;
    long n = read_usable(path, old, &corrupt);
    if (n < 0) old[0] = 0;
    Out o = { outbuf, 0, 2 * MAX_FILE, 0 };
    outbuf[0] = 0;
    int written[K_COUNT] = { 0 }, seen[N_SECTIONS] = { 0 };
    char section[64] = "";

    for (char *line = old, *next; *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0; else next = line + strlen(line);
        char *raw = line;                                     /* kept whole, however long */
        char *cr = strchr(raw, '\r'); if (cr) *cr = 0;
        /* Only the start of a line decides what it is (section or key). */
        char copy[1024];
        size_t len = strlen(raw);
        if (len >= sizeof copy) len = sizeof copy - 1;
        memcpy(copy, raw, len);
        copy[len] = 0;
        char *s = trim(copy);
        if (*s == '[') {
            emit_missing(&o, c, section, written);           /* close the previous section */
            char *e = strchr(s, ']');
            if (e) { *e = 0; snprintf(section, sizeof section, "%s", trim(s + 1)); }
            for (int i = 0; i < N_SECTIONS; i++) if (strcmp(section, SECTIONS[i]) == 0) seen[i] = 1;
        } else if (*s && *s != ';' && *s != '#' && strchr(s, '=')) {
            char *eq = strchr(s, '=');
            *eq = 0;
            Key k = key_of(section, trim(s));
            if (k != K_COUNT) {
                if (!written[k]) { emit_key(&o, c, k); written[k] = 1; }
                continue;                                     /* drop duplicates of known keys */
            }
        }
        emit(&o, raw);
        emit(&o, "\n");
    }
    emit_missing(&o, c, section, written);
    for (int i = 0; i < N_SECTIONS; i++) {
        if (seen[i]) continue;
        char head[80];
        snprintf(head, sizeof head, "%s[%s]\n", o.n ? "\n" : "", SECTIONS[i]);
        emit(&o, head);
        emit_missing(&o, c, SECTIONS[i], written);
    }
    free(old);
    if (o.overflow) { free(outbuf); return -1; }

    if (bl_fs_exists(path)) {
        char bak[1024];
        snprintf(bak, sizeof bak, "%s.bak", path);
        bl_fs_copy(path, bak, 0644);                      /* best effort */
    }
    int r = bl_fs_write_atomic(path, outbuf, o.n, 0644);
    free(outbuf);
    return r;
}

int bl_secret_load(const char *path, char *password, size_t cap)
{
    if (!password || !cap) return -1;
    password[0] = 0;
    char buf[256];
    long n = bl_fs_read(path, buf, sizeof buf);
    if (n == -1) return 1;
    if (n < 0) return -1;
    char *e = buf + strlen(buf);
    while (e > buf && (e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
    if (strlen(buf) >= cap) return -1;
    snprintf(password, cap, "%s", buf);
    return buf[0] ? 0 : 1;
}

int bl_secret_save(const char *path, const char *password)
{
    if (!path || !password) return -1;
    if (!*password) return (unlink(path) == 0 || !bl_fs_exists(path)) ? 0 : -1;
    if (strlen(password) > 200 || strchr(password, '\n') || strchr(password, '\r')) return -1;
    return bl_fs_write_atomic(path, password, strlen(password), 0600);
}
