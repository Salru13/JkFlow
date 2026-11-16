/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#define _POSIX_C_SOURCE 200809L
#include "bl_account.h"
#include "bl_events.h"
#include "bl_fs.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int bl_account_valid(const char *s)
{
    if (!s) return 0;
    size_t n = 0;
    for (; s[n]; n++) {
        if (n >= BL_ACCOUNT_MAX) return 0;
        if ((unsigned char)s[n] < 0x20 || (unsigned char)s[n] > 0x7E) return 0;
    }
    return n >= 1;
}

/* A field of a BlAccount: terminated within its 32 bytes and valid. */
static int field_ok(const char *s, size_t cap)
{
    return memchr(s, 0, cap) != NULL && bl_account_valid(s);
}

static int path_of(const BlPaths *p, int lua, char *out, size_t cap)
{
    if (!p) return -1;
    const char *dir = lua ? p->game : p->state;
    if (!dir[0]) return -1;
    return snprintf(out, cap, "%s/%s", dir, lua ? BL_ACCOUNT_LUA : BL_ACCOUNT_FILE) < (int)cap ? 0 : -1;
}

int bl_account_load_count;

static int load(const BlPaths *p, BlAccount *a);

int bl_account_load(const BlPaths *p, BlAccount *a)
{
    bl_account_load_count++;
    return load(p, a);
}

int bl_account_user(const BlPaths *p, char *user, size_t cap)
{
    BlAccount a;
    if (!user || !cap) return -1;
    user[0] = 0;
    int r = load(p, &a);
    if (r == 0) snprintf(user, cap, "%s", a.user);
    memset(&a, 0, sizeof a);
    return r;
}

static int load(const BlPaths *p, BlAccount *a)
{
    char path[300], buf[256];
    if (!a) return -1;
    memset(a, 0, sizeof *a);
    if (path_of(p, 0, path, sizeof path)) return -1;
    if (!bl_fs_exists(path)) return 1;
    long n = bl_fs_read(path, buf, sizeof buf);
    if (n < 0) return -1;
    int have_u = 0, have_p = 0, header = 0;
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!strcmp(line, "blur-account 1")) header = 1;
        else if (!strncmp(line, "user=", 5) && bl_account_valid(line + 5)) { memcpy(a->user, line + 5, strlen(line + 5) + 1); have_u = 1; }
        else if (!strncmp(line, "pass=", 5) && bl_account_valid(line + 5)) { memcpy(a->pass, line + 5, strlen(line + 5) + 1); have_p = 1; }
    }
    memset(buf, 0, sizeof buf);
    if (!header || !have_u || !have_p) { memset(a, 0, sizeof *a); return -1; }
    return 0;
}

int bl_account_save(const BlPaths *p, const BlAccount *a)
{
    char path[300], text[128];
    if (!a || !field_ok(a->user, sizeof a->user) || !field_ok(a->pass, sizeof a->pass)) {
        bl_events_log(BL_E_INFO, "account: not saved (invalid value)");
        return -1;
    }
    if (path_of(p, 0, path, sizeof path) || bl_fs_mkdirs(p->state) != 0) return -1;
    int n = snprintf(text, sizeof text, "blur-account 1\nuser=%s\npass=%s\n", a->user, a->pass);
    int rc = n > 0 && n < (int)sizeof text && bl_fs_write_atomic(path, text, (size_t)n, 0600) == 0 ? 0 : -1;
    memset(text, 0, sizeof text);
    bl_events_log(BL_E_INFO, "account: %s", rc == 0 ? "saved" : "could not be saved");
    return rc;
}

int bl_account_clear(const BlPaths *p)
{
    char path[300];
    int rc = 0;
    for (int lua = 0; lua < 2; lua++)
        if (path_of(p, lua, path, sizeof path) || (unlink(path) != 0 && errno != ENOENT)) rc = -1;
    bl_events_log(BL_E_INFO, "account: removed%s", rc ? " (with errors)" : "");
    return rc;
}

int bl_account_remove_prefill(const BlPaths *p)
{
    char path[300];
    if (path_of(p, 1, path, sizeof path)) return -1;
    if (unlink(path) == 0) { bl_events_log(BL_E_INFO, "account: pre-fill file removed"); return 0; }
    return errno == ENOENT ? 0 : -1;
}

/* "<s>" with \ and " escaped (other bytes are 0x20..0x7E already). */
static size_t lua_quote(char *out, const char *s)
{
    size_t n = 0;
    out[n++] = '"';
    for (; *s; s++) {
        if (*s == '\\' || *s == '"') out[n++] = '\\';
        out[n++] = *s;
    }
    out[n++] = '"';
    out[n] = 0;
    return n;
}

int bl_account_apply(const BlPaths *p)
{
    char path[300];
    if (path_of(p, 1, path, sizeof path)) return -1;
    BlAccount a;
    int r = bl_account_load(p, &a);
    if (r != 0) {
        /* No account (or a damaged one): no pre-fill file. */
        int rc = unlink(path) == 0 || errno == ENOENT ? 0 : -1;
        bl_events_log(BL_E_INFO, "account: none at play (%s)", r == 1 ? "not set" : "account.cfg damaged");
        return rc;
    }
    char u[2 * BL_ACCOUNT_MAX + 3], pw[2 * BL_ACCOUNT_MAX + 3], text[256];
    lua_quote(u, a.user);
    lua_quote(pw, a.pass);
    int n = snprintf(text, sizeof text, "return { username = %s, password = %s }\n", u, pw);
    char dir[300];
    snprintf(dir, sizeof dir, "%s/amax/plugins", p->game);
    int rc = n > 0 && n < (int)sizeof text && bl_fs_mkdirs(dir) == 0 && bl_fs_write_atomic(path, text, (size_t)n, 0600) == 0 ? 0 : -1;
    memset(text, 0, sizeof text);
    memset(pw, 0, sizeof pw);
    memset(&a, 0, sizeof a);
    bl_events_log(BL_E_INFO, "account: pre-fill %s", rc == 0 ? "written" : "could not be written");
    return rc;
}
