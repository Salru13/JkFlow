/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests for the console clock check (BLUR 1.00 plan Task L8, Review
 * Focus 5, decision D7). */
#include "bl_clock.h"
#include "bl_strings.h"
#include "bl_test.h"

#include <stdio.h>
#include <string.h>

static void test_parse_fixdate(void)
{
    long long t = 0;
    CHECK_EQ(bl_http_date_parse("Sun, 15 Nov 2026 03:04:05 GMT", &t), 0);
    CHECK_EQ(t, 1794711845LL);
    CHECK_EQ(bl_http_date_parse("Thu, 01 Jan 1970 00:00:00 GMT", &t), 0);
    CHECK_EQ(t, 0);
    CHECK_EQ(bl_http_date_parse("Thu, 01 Jan 2026 00:00:00 GMT", &t), 0);
    CHECK_EQ(t, BL_CLOCK_MIN_EPOCH);
    CHECK_EQ(bl_http_date_parse("Tue, 29 Feb 2028 23:59:59 GMT\r\n", &t), 0);   /* leap day; line end tolerated */
    CHECK_EQ(t, 1835481599LL);
    CHECK_EQ(bl_http_date_parse("Sat, 31 Dec 2039 12:00:00 GMT", &t), 0);
    CHECK_EQ(t, 2208945600LL);
}

static void test_parse_rejects_garbage(void)
{
    const char *bad[] = { "", "garbage", "Sun, 15 Nov 2026 03:04:05", "Sun, 15 Nov 2026 03:04:05 UTC", "Sun, 32 Nov 2026 03:04:05 GMT",
                          "Sun, 15 Foo 2026 03:04:05 GMT", "Sun, 15 Nov 2026 24:04:05 GMT", "Sun, 15 Nov 2026 03:60:05 GMT",
                          "Sun, 29 Feb 2027 00:00:00 GMT", "Sunday, 15-Nov-26 03:04:05 GMT", "Sun Nov 15 03:04:05 2026",
                          "Sun, 15 Nov 26 03:04:05 GMT", "Sun, 5 Nov 2026 03:04:05 GMT", "Xyz, 15 Nov 2026 03:04:05 GMT",
                          "Sun, 15 Nov 2026 03:04:05 GMT trailing" };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        long long t = 42;
        CHECK(bl_http_date_parse(bad[i], &t) < 0);
        if (bl_http_date_parse(bad[i], &t) >= 0) fprintf(stderr, "  accepted \"%s\"\n", bad[i]);
    }
    long long t;
    CHECK(bl_http_date_parse(NULL, &t) < 0);
}

static void test_ok_within_15min(void)
{
    long long off = 0;
    CHECK_EQ(bl_clock_judge(1794711845LL + 899, 1, 1794711845LL, &off), BL_CLOCK_OK);
    CHECK_EQ(off, 899);
    CHECK_EQ(bl_clock_judge(1794711845LL + 900, 1, 1794711845LL, &off), BL_CLOCK_OK);
}

static void test_off_901s(void)
{
    long long off = 0;
    CHECK_EQ(bl_clock_judge(1794711845LL + 901, 1, 1794711845LL, &off), BL_CLOCK_OFF);
    CHECK_EQ(off, 901);
    CHECK_EQ(bl_clock_minutes(off), 16);
}

static void test_negative_offset(void)
{
    long long off = 0;
    CHECK_EQ(bl_clock_judge(1794711845LL - 3600, 1, 1794711845LL, &off), BL_CLOCK_OFF);
    CHECK_EQ(off, -3600);
    CHECK_EQ(bl_clock_minutes(off), 60);
    CHECK_EQ(bl_clock_judge(1794711845LL - 899, 1, 1794711845LL, &off), BL_CLOCK_OK);
}

static void test_no_server_sane_year_no_warning(void)
{
    long long off = 7;
    CHECK_EQ(bl_clock_judge(1794711845LL, 0, 0, &off), BL_CLOCK_OK);
    CHECK_EQ(off, 0);
    CHECK_EQ(bl_clock_judge(BL_CLOCK_MIN_EPOCH, 0, 0, &off), BL_CLOCK_OK);
    CHECK_EQ(bl_clock_judge(4102444800LL, 0, 0, NULL), BL_CLOCK_OK);   /* 2100: no server, no proof it is wrong */
}

static void test_no_server_year_2025_warns(void)
{
    long long off = 0;
    CHECK_EQ(bl_clock_judge(BL_CLOCK_MIN_EPOCH - 1, 0, 0, &off), BL_CLOCK_UNKNOWN_BAD_YEAR);
    CHECK_EQ(bl_clock_judge(1735689600LL, 0, 0, &off), BL_CLOCK_UNKNOWN_BAD_YEAR);   /* 2025-01-01 */
    CHECK_EQ(bl_clock_judge(0, 0, 0, &off), BL_CLOCK_UNKNOWN_BAD_YEAR);
}

static void test_copy(void)
{
    char line[400];
    snprintf(line, sizeof line, bl_str(BL_S_CLOCK_OFF), 16LL);
    CHECK(!strcmp(line, "Your console clock is 16 minutes off. AMAX online refuses logins when the clock is off by more than 30 minutes. Set the date and time in PS4 Settings, then try again."));
    CHECK(!strcmp(bl_str(BL_S_CLOCK_PLAY_ANYWAY), "Play anyway"));
    CHECK(!strcmp(bl_str(BL_S_CLOCK_BACK), "Back"));
}

int main(void)
{
    RUN(test_parse_fixdate);
    RUN(test_parse_rejects_garbage);
    RUN(test_ok_within_15min);
    RUN(test_off_901s);
    RUN(test_negative_offset);
    RUN(test_no_server_sane_year_no_warning);
    RUN(test_no_server_year_2025_warns);
    RUN(test_copy);
    return bl_test_report("test_clock");
}
