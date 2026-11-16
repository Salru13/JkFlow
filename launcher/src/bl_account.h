/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The player's AMAX online account (BLUR 1.00 plan Task L7, decisions D3-D5).
 * One account per console, kept in <state>/account.cfg (mode 0600, plain
 * text, like Blur's own save). At every Play the launcher writes it for our
 * AMAX account plugin as <game>/amax/plugins/account_cfg.lua, or deletes
 * that file when no account is set. Blur copies each value into a 32-byte
 * field without a bound, so each is 1..31 bytes of printable ASCII.
 * The values are never logged.
 */
#ifndef BL_ACCOUNT_H
#define BL_ACCOUNT_H

#include "bl_paths.h"

#define BL_ACCOUNT_FILE "account.cfg"                    /* <state>/account.cfg, "blur-account 1\nuser=<u>\npass=<p>\n" */
#define BL_ACCOUNT_LUA  "amax/plugins/account_cfg.lua"   /* relative to game, mode 0600 */
#define BL_ACCOUNT_MAX 31

typedef struct BlAccount { char user[BL_ACCOUNT_MAX + 1]; char pass[BL_ACCOUNT_MAX + 1]; } BlAccount;

/* 1..31 bytes, each 0x20..0x7E. */
int bl_account_valid(const char *s);
/* 0 ok, 1 none, <0 error (unreadable or damaged file). */
int bl_account_load(const BlPaths *p, BlAccount *a);
/* Audit 29a: the user name only, the password is not kept (the Start idle
 * prefetch; the password is loaded when Settings > Online account opens).
 * 0 ok, 1 none, <0 error; user "" unless 0. */
int bl_account_user(const BlPaths *p, char *user, size_t cap);
/* bl_account_load calls so far (tests). */
extern int bl_account_load_count;
/* Both values must be valid; nothing is written otherwise. 0 ok. */
int bl_account_save(const BlPaths *p, const BlAccount *a);
/* Removes account.cfg and account_cfg.lua. 0 ok. */
int bl_account_clear(const BlPaths *p);
/* Writes BL_ACCOUNT_LUA as  return { username = "<esc>", password = "<esc>" }
 * (\ and " escaped), or deletes it when no account is set. 0 ok. */
int bl_account_apply(const BlPaths *p);
/* Deletes BL_ACCOUNT_LUA from the game folder (owner finding 27: the
 * password must not stay there; the plugin deletes it once read, and the
 * launcher at its next start when the plugin could not). account.cfg stays.
 * 0 ok (also when there was none). */
int bl_account_remove_prefill(const BlPaths *p);

#endif
