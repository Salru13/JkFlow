#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# Owner finding 12 (2026-11-15): the PS4 system keyboard opened in the top
# left corner and nothing could be typed or confirmed; opening it again and
# again also preceded the failed module loads of finding 13. The launcher
# uses its own keyboard (bl_keyboard) only: no source may call the host's
# osk service. The field stays in BlServices (the title still fills it).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
status=0
if grep -nE -- '->osk\b|\.osk\b' "$root"/src/*.c; then
    echo "FAIL test_no_system_osk: the launcher calls the system keyboard service above"; status=1
fi
if ! grep -q 'bl_keyboard_input' "$root/src/bl_frontend.c"; then
    echo "FAIL test_no_system_osk: the frontend does not drive bl_keyboard"; status=1
fi
[ $status -eq 0 ] && echo "test_no_system_osk: ok"
exit $status
