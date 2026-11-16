/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* bl_time: the locale-free UTC stamp used instead of gmtime_r + strftime.
 * On the title the CRT's strftime reads the locale through __tls_get_addr
 * and faulted on the first events.log line (BLUR 1.00 r2 crash, klog
 * 2026-11-14). */
#define _POSIX_C_SOURCE 200809L
#include "bl_test.h"
#include "bl_time.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void expect(long long t, const char *want)
{
    char out[BL_STAMP_LEN];
    memset(out, 'x', sizeof out);
    bl_utc_stamp_from(t, out);
    CHECK(strcmp(out, want) == 0);
    if (strcmp(out, want) != 0) fprintf(stderr, "  %lld: got %s want %s\n", t, out, want);
}

int main(void)
{
    expect(0, "19700101T000000Z");
    expect(951782400, "20000229T000000Z");      /* leap day of a /400 year */
    expect(1709251199, "20240229T235959Z");     /* leap day, last second */
    expect(4107542399LL, "21000228T235959Z");   /* 2100 is not a leap year */
    expect(4107542400LL, "21000301T000000Z");
    expect(1794677108, "20261114T172508Z");     /* the crash coredump's time */
    expect(253402300799LL, "99991231T235959Z");
    expect(-1, "19691231T235959Z");
    expect(-86400LL * 365, "19690101T000000Z");

    /* Same text as the host's gmtime_r + strftime over 60 years in odd steps. */
    for (long long t = 0; t < 1900000000LL; t += 86413 * 7 + 3601) {
        char want[32], got[BL_STAMP_LEN];
        time_t tt = (time_t)t;
        struct tm tm;
        gmtime_r(&tt, &tm);
        strftime(want, sizeof want, "%Y%m%dT%H%M%SZ", &tm);
        bl_utc_stamp_from(t, got);
        if (strcmp(want, got) != 0) { fprintf(stderr, "  %lld: got %s want %s\n", t, got, want); CHECK(0); break; }
    }

    char now[BL_STAMP_LEN];
    bl_utc_stamp(now);
    CHECK(strlen(now) == 16 && now[8] == 'T' && now[15] == 'Z');
    return bl_test_report("time");
}
