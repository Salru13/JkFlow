#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# The title links a static musl libc whose locale-aware functions read the
# current locale through its own __tls_get_addr. On the PS4 that read faulted
# (BLUR 1.00 r2: SIGBUS in strftime on the first events.log line, klog
# 2026-11-14). Launcher code must not call those functions; time stamps go
# through bl_time.h. Static list: functions in the r2 PRX whose direct call
# graph reaches __tls_get_addr, plus their time/locale relatives.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
pat='\b(strftime|strftime_l|gmtime|gmtime_r|localtime|localtime_r|mktime|ctime|asctime|strerror|strerror_r|perror|setlocale|newlocale|uselocale|nl_langinfo|wctomb|wcrtomb|mbtowc|mbrtowc|btowc|mblen|mbstowcs|wcstombs|fwide|fputwc|fgetwc)[[:space:]]*\('
if grep -nE "$pat" "$root"/src/*.c "$root"/src/*.h | grep -v "^[^:]*:[0-9]*: *\(/\*\|\*\)"; then
    echo "FAIL test_no_tls_libc: locale/TLS libc call in launcher/src (use bl_time.h)"; exit 1
fi
# printf %m / %lc / %ls and scanf %lc / %ls / %l[ reach strerror / wcrtomb / mbrtowc.
if grep -nE '"[^"]*%[-+ #0-9.*]*(m|lc|ls|l\[|C|S)[^"]*"' "$root"/src/*.c | grep -vE 'strftime'; then
    echo "FAIL test_no_tls_libc: printf/scanf conversion that reaches a TLS libc path"; exit 1
fi
echo "ok   test_no_tls_libc"
