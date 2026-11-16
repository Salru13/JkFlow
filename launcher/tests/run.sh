#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# Build the generated assets and run launcher host tests (WSL/Linux).
#   launcher/tests/run.sh               all tests
#   launcher/tests/run.sh test_ui       one test (test_* or perf_*)
# test_*.c run with ASan/UBSan; perf_*.c run at -O2 without sanitizers;
# test_*.sh run last. Objects are cached in build/obj-* and rebuilt when the
# source or a header is newer.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
gen="$root/build/gen"
bin="$root/build/test"
mkdir -p "$gen" "$bin"

"$root/tools/bake_assets.sh" "$gen"

COMMON=(-std=c11 -Wall -Wextra -Werror -I"$root/src" -I"$gen" -I"$here" -DBL_TEST_DIR="\"$here\"")
# BL_ONLINE_UI=1 tests/run.sh: the build with the hidden online settings shown.
variant=""
if [ "${BL_ONLINE_UI:-0}" = 1 ]; then COMMON+=(-DBL_ONLINE_UI=1); variant="-online"; fi
SAN=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
OPT=(-O2)
SRC=("$root"/src/*.c "$gen/bl_font_data.c" "$gen/bl_logo.c")
newest_header=$(ls -t "$root"/src/*.h "$gen"/*.h | head -1)

# objects MODE FLAGS... -> prints object paths
objects() {
    local mode="$1"; shift
    local dir="$root/build/obj-$mode$variant"
    mkdir -p "$dir"
    local objs=()
    for s in "${SRC[@]}"; do
        local o="$dir/$(basename "$s" .c).o"
        if [ ! -f "$o" ] || [ "$s" -nt "$o" ] || [ "$newest_header" -nt "$o" ]; then
            cc "${COMMON[@]}" "$@" -c -o "$o" "$s"
        fi
        objs+=("$o")
    done
    printf '%s\n' "${objs[@]}"
}

tests=("$@")
if [ ${#tests[@]} -eq 0 ]; then
    for t in "$here"/test_*.c "$here"/perf_*.c "$here"/test_*.sh; do tests+=("$(basename "$t")"); done
fi

objects san "${SAN[@]}" > "$root/build/obj-san.list"
objects opt "${OPT[@]}" > "$root/build/obj-opt.list"
mapfile -t SAN_OBJ < "$root/build/obj-san.list"
mapfile -t OPT_OBJ < "$root/build/obj-opt.list"

status=0
for t in "${tests[@]}"; do
    t="${t%.c}"
    case "$t" in
        *.sh) bash "$here/$t" || status=1 ;;
        perf_*) cc "${COMMON[@]}" "${OPT[@]}" -o "$bin/$t" "$here/$t.c" "${OPT_OBJ[@]}" -lm && { "$bin/$t" || status=1; } || status=1 ;;
        *) cc "${COMMON[@]}" "${SAN[@]}" -o "$bin/$t" "$here/$t.c" "${SAN_OBJ[@]}" -lm && { "$bin/$t" || status=1; } || status=1 ;;
    esac
done
exit $status
