/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Online settings -> the BLUR_HOSTS variable (spec 8.3). The launcher only
 * changes where Blur connects: it maps the game's DemonWare host names to
 * the player's server. The engine's Winsock resolver shim reads BLUR_HOSTS
 * (integration side). Nothing is sent anywhere by these functions.
 *
 *   official: ""                         (the game's own servers)
 *   off:      every name = 0.0.0.0       (fails fast, stays offline)
 *   private:  every name = the server's IPv4 (auth_host / lobby_host, names
 *             or IPv4s, when set, override the auth / lobby names)
 * Format: "name=ipv4;name=ipv4;..."
 */
#ifndef BL_NET_H
#define BL_NET_H

#include <stddef.h>

#include "bl_config.h"

/* 1 if host is a DNS name (letters, digits, dots, dashes; labels not empty
 * and not starting or ending with a dash; up to 127 characters) or a valid
 * dotted IPv4 address. */
int bl_host_valid(const char *host);
int bl_ipv4_valid(const char *s);
/* host_ip, auth_ip, lobby_ip: the IPv4 each name in host, auth_host and
 * lobby_host was looked up to (NULL where the setting is already an IPv4
 * or empty; an empty auth_host or lobby_host means the main server).
 * 0 ok; <0 when private mode lacks a valid address for any of them, or
 * when out is too small. */
int bl_hosts_env(const BlConfig *c, const char *host_ip, const char *auth_ip, const char *lobby_ip, char *out, size_t cap);

#endif
