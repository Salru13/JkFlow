/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#include "bl_net.h"

#include <stdio.h>
#include <string.h>

static const char AUTH[] = "blur-pc-live.auth.mmp3.demonware.net";
static const char LOBBY[] = "blur-pc-live.lsg.mmp3.demonware.net";
static const char *const STUN[] = { "stun.us.demonware.net", "stun.eu.demonware.net",
                                    "stun.jp.demonware.net", "stun.au.demonware.net" };

int bl_ipv4_valid(const char *s)
{
    if (!s || !*s) return 0;
    int parts = 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        int v = 0, digits = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); digits++; s++; if (digits > 3) return 0; }
        if (v > 255) return 0;
        parts++;
        if (*s == '.') { s++; if (!*s) return 0; }
        else if (*s) return 0;
    }
    return parts == 4;
}

int bl_host_valid(const char *host)
{
    if (!host) return 0;
    size_t n = strlen(host);
    if (n == 0 || n > 127) return 0;
    int all_digits_dots = 1;
    for (const char *p = host; *p; p++)
        if (!((*p >= '0' && *p <= '9') || *p == '.')) all_digits_dots = 0;
    if (all_digits_dots) return bl_ipv4_valid(host);
    const char *label = host;
    for (const char *p = host;; p++) {
        if (*p == '.' || *p == 0) {
            size_t len = (size_t)(p - label);
            if (len == 0 || len > 63 || label[0] == '-' || p[-1] == '-') return 0;
            if (*p == 0) break;
            label = p + 1;
            continue;
        }
        char ch = *p;
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-')) return 0;
    }
    return 1;
}

static int add(char *out, size_t cap, size_t *used, const char *name, const char *ip)
{
    int n = snprintf(out + *used, cap - *used, "%s%s=%s", *used ? ";" : "", name, ip);
    if (n < 0 || (size_t)n >= cap - *used) return -1;
    *used += (size_t)n;
    return 0;
}

/* The address for one name: the setting itself when it is an IPv4, else the
 * looked-up address, which must be a valid IPv4. NULL when not usable. */
static const char *address(const char *setting, const char *resolved)
{
    if (bl_ipv4_valid(setting)) return setting;
    if (!bl_host_valid(setting) || !bl_ipv4_valid(resolved)) return NULL;
    return resolved;
}

int bl_hosts_env(const BlConfig *c, const char *host_ip, const char *auth_ip, const char *lobby_ip, char *out, size_t cap)
{
    if (!c || !out || cap == 0) return -1;
    out[0] = 0;
    if (c->online_mode == BL_ONLINE_OFFICIAL) return 0;
    const char *main_a, *auth_a, *lobby_a;
    if (c->online_mode == BL_ONLINE_OFF) {
        main_a = auth_a = lobby_a = "0.0.0.0";
    } else {
        main_a = address(c->host, host_ip);
        auth_a = c->auth_host[0] ? address(c->auth_host, auth_ip) : main_a;
        lobby_a = c->lobby_host[0] ? address(c->lobby_host, lobby_ip) : main_a;
        if (!main_a || !auth_a || !lobby_a) return -1;
    }
    size_t used = 0;
    int rc = add(out, cap, &used, AUTH, auth_a);
    rc |= add(out, cap, &used, LOBBY, lobby_a);
    for (size_t i = 0; i < 4; i++) rc |= add(out, cap, &used, STUN[i], main_a);
    if (rc) { out[0] = 0; return -1; }
    return 0;
}
