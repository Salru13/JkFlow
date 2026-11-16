#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# Audit 20: a missed wall-clock budget in perf_* prints "WARN timing" and does
# not fail run.sh on a loaded host; BL_PERF_STRICT=1 makes it a failure. A
# functional CHECK still fails either way.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
status=0
checks=0
failed=0
fail() { echo "FAIL test_perf_timing: $1"; status=1; failed=$((failed + 1)); }

cat > "$tmp/t.c" <<'EOF'
#include "bl_test.h"
#include <stdlib.h>
int main(int argc, char **argv)
{
    (void)argv;
    CHECK_TIMING(0);                /* a missed budget */
    if (argc > 1) CHECK(0);         /* a functional failure */
    return bl_test_report("t");
}
EOF
cc -std=c11 -Wall -Wextra -Werror -I"$here" -o "$tmp/t" "$tmp/t.c"

checks=$((checks + 1))
out="$(env -u BL_PERF_STRICT "$tmp/t" 2>&1)" || fail "a missed budget fails the run without BL_PERF_STRICT"
checks=$((checks + 1))
grep -q 'WARN timing' <<<"$out" || fail "a missed budget does not print WARN timing"
checks=$((checks + 1))
if BL_PERF_STRICT=1 "$tmp/t" >/dev/null 2>&1; then fail "BL_PERF_STRICT=1 does not make a missed budget fail"; fi
checks=$((checks + 1))
if env -u BL_PERF_STRICT "$tmp/t" functional >/dev/null 2>&1; then fail "a functional CHECK no longer fails"; fi

# Every wall-clock budget in the perf tests goes through CHECK_TIMING.
for f in "$here"/perf_*.c; do
    checks=$((checks + 1))
    grep -q 'CHECK_TIMING' "$f" || fail "$(basename "$f") has no CHECK_TIMING"
done

echo "test_perf_timing: $checks checks, $failed failed"
exit $status
