/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#include "bl_time.h"

#include <time.h>

static void digits(char *p, long long v, int n)
{
    for (int i = n - 1; i >= 0; i--) { p[i] = (char)('0' + v % 10); v /= 10; }
}

void bl_utc_stamp_from(long long t, char out[BL_STAMP_LEN])
{
    long long days = t / 86400, secs = t % 86400;
    if (secs < 0) { secs += 86400; days--; }
    /* civil_from_days (H. Hinnant): proleptic Gregorian, integer only. */
    long long z = days + 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    long long doe = z - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = yoe + era * 400;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long long mp = (5 * doy + 2) / 153;
    long long d = doy - (153 * mp + 2) / 5 + 1;
    long long m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y++;
    if (y < 0 || y > 9999) y = y < 0 ? 0 : 9999;   /* keeps 4 digits; never reached by time(NULL) */
    digits(out, y, 4);
    digits(out + 4, m, 2);
    digits(out + 6, d, 2);
    out[8] = 'T';
    digits(out + 9, secs / 3600, 2);
    digits(out + 11, secs / 60 % 60, 2);
    digits(out + 13, secs % 60, 2);
    out[15] = 'Z';
    out[16] = 0;
}

void bl_utc_stamp(char out[BL_STAMP_LEN])
{
    bl_utc_stamp_from((long long)time(NULL), out);
}
