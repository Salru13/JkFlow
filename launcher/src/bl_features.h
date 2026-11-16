/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Build-time feature switches for the launcher UI.
 *
 * BL_ONLINE_UI: the "Online server" settings (private server, sign-in,
 * test connection). The user decided on 2026-11-13 that they wait for a
 * later release. At 0 the item is not listed, and a server saved by an
 * older launcher is kept in launcher.ini but not used: the game goes to
 * the official servers and no BLUR_HOSTS is set. Build with
 * -DBL_ONLINE_UI=1 to bring it back; tests: BL_ONLINE_UI=1 tests/run.sh.
 */
#ifndef BL_FEATURES_H
#define BL_FEATURES_H

#ifndef BL_ONLINE_UI
#define BL_ONLINE_UI 0
#endif

#endif
