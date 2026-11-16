/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * UTC time stamps "YYYYMMDDTHHMMSSZ" without the C library's locale code.
 * The title links a static libc whose strftime (and other locale-aware
 * calls) read the current locale through a thread-local variable; on the
 * PS4 that read faulted on the first events.log line (BLUR 1.00 r2 crash,
 * 2026-11-14). Launcher code formats time only through this header
 * (tests/test_no_tls_libc.sh checks it).
 */
#ifndef BL_TIME_H
#define BL_TIME_H

#define BL_STAMP_LEN 17   /* 16 characters and the terminator */

/* Seconds since 1970-01-01T00:00:00Z (negative allowed) -> stamp. */
void bl_utc_stamp_from(long long t, char out[BL_STAMP_LEN]);
/* The stamp for time(NULL). */
void bl_utc_stamp(char out[BL_STAMP_LEN]);

#endif
