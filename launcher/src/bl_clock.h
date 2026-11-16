/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Console clock check before Play (BLUR 1.00 plan Task L8, decision D7).
 * AMAX online refuses a login when the console clock is off by more than
 * 30 minutes. The title's http_date service asks amax-emu.com for its time
 * (one HEAD, 3 s at most); the launcher warns above 15 minutes and never
 * blocks. Without an answer only the year is checked: a clock before 2026
 * is clearly wrong. No libc time or locale calls (bl_time.h rule).
 */
#ifndef BL_CLOCK_H
#define BL_CLOCK_H

#define BL_CLOCK_WARN_S (15 * 60)
#define BL_CLOCK_MIN_EPOCH 1767225600LL     /* 2026-01-01T00:00:00Z */
#define BL_CLOCK_HOST "amax-emu.com"

/* RFC 7231 IMF-fixdate "Sun, 15 Nov 2026 03:04:05 GMT" -> epoch. 0 ok, <0 not parsed. */
int bl_http_date_parse(const char *value, long long *epoch);

typedef enum BlClock { BL_CLOCK_OK = 0, BL_CLOCK_OFF, BL_CLOCK_UNKNOWN_BAD_YEAR } BlClock;
/* have_server 0: only the year sanity check (*offset_s 0). *offset_s = console - server. */
BlClock bl_clock_judge(long long console_now, int have_server, long long server_now, long long *offset_s);
/* The console's clock in seconds since 1970 (time()); replaceable by tests. NULL: time(). */
extern long long (*bl_clock_now)(void);
long long bl_clock_console_now(void);
/* Whole minutes of an offset, rounded up, for the warning. */
long long bl_clock_minutes(long long offset_s);

#endif
