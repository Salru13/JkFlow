/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Native renderer arming (BLUR 1.00: native only, decisions 19, 30, D15).
 *
 * The native client (d3d9.c988.dll) only tries the native DXVK device when
 * <game_dir>/dxn_arm.txt exists, and deletes that file first (one-shot). The
 * launcher writes it at every Play. There is no fallback in BLUR: with
 * DXVK_PS4_NATIVE_ONLY=1 a native failure ends the game (D16), and a start
 * that dies before its first frame shows E101 at the next launcher start
 * (bl_session.h).
 */
#ifndef BL_NATIVE_H
#define BL_NATIVE_H

#include <stddef.h>

#define BL_NATIVE_ARM_FILE "dxn_arm.txt"

/* native 1: (re)write the arm file. native 0: delete it. 0 ok, <0 when it
 * could not be written or a stale one could not be removed (Play stops: E104). */
int bl_native_prepare(const char *game_dir, int native);

/* Entries in dir other than "." and ".." (0 when it does not exist). */
int bl_dir_entries(const char *dir);

#endif
