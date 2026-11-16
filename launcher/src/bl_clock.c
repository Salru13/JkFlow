/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#include "bl_clock.h"

#include <string.h>
#include <time.h>

long long (*bl_clock_now)(void);

long long bl_clock_console_now(void) { return bl_clock_now ? bl_clock_now() : (long long)time(NULL); }

static const char *const DAYS[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
static const char *const MONTHS[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

/* n decimal digits at s, or -1. */
static int digits(const char *s, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

static int leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

/* Days from 1970-01-01 to y-m-d (proleptic Gregorian). */
static long long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    long long yoe = y - era * 400;
    long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int bl_http_date_parse(const char *v, long long *epoch)
{
    /* "Sun, 15 Nov 2026 03:04:05 GMT": 29 characters, then only line-end space. */
    if (!v || !epoch || strlen(v) < 29) return -1;
    for (const char *t = v + 29; *t; t++) if (*t != '\r' && *t != '\n' && *t != ' ' && *t != '\t') return -1;
    int day_ok = 0, mon = -1;
    for (int i = 0; i < 7; i++) if (!strncmp(v, DAYS[i], 3)) day_ok = 1;
    for (int i = 0; i < 12; i++) if (!strncmp(v + 8, MONTHS[i], 3)) mon = i + 1;
    if (!day_ok || mon < 0 || v[3] != ',' || v[4] != ' ' || v[7] != ' ' || v[11] != ' ' || v[16] != ' '
        || v[19] != ':' || v[22] != ':' || strncmp(v + 25, " GMT", 4)) return -1;
    int d = digits(v + 5, 2), y = digits(v + 12, 4), h = digits(v + 17, 2), mi = digits(v + 20, 2), s = digits(v + 23, 2);
    static const int MDAYS[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (d < 1 || y < 0 || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 60) return -1;
    if (d > MDAYS[mon - 1] + (mon == 2 && leap(y))) return -1;
    *epoch = days_from_civil(y, mon, d) * 86400LL + h * 3600LL + mi * 60LL + s;
    return 0;
}

BlClock bl_clock_judge(long long console_now, int have_server, long long server_now, long long *offset_s)
{
    if (offset_s) *offset_s = 0;
    if (!have_server) return console_now < BL_CLOCK_MIN_EPOCH ? BL_CLOCK_UNKNOWN_BAD_YEAR : BL_CLOCK_OK;
    long long off = console_now - server_now;
    if (offset_s) *offset_s = off;
    return off > BL_CLOCK_WARN_S || off < -BL_CLOCK_WARN_S ? BL_CLOCK_OFF : BL_CLOCK_OK;
}

long long bl_clock_minutes(long long offset_s)
{
    if (offset_s < 0) offset_s = -offset_s;
    return (offset_s + 59) / 60;
}
