#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# Nothing the player sees may name the host projects (production review
# 2026-11-14, requirement 5): not the launcher source, not its assets, not
# the package texts. The one
# exception is SOURCES.txt, whose LGPL source offer must link the upstream
# repositories by their real URLs. The old loading ring must not be linked
# into the launcher (plan Task 3 step 2), and the author alias in the package
# texts must match src/bl_brand.h. A source line marked "/* internal path"
# names a file the runtime writes (not shown in the UI) and is allowed.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
repo="$(cd "$root/.." && pwd)"   # the package texts are at the repository root
status=0
# The one owner-approved credit (2026-11-15): allowed only as this exact
# phrase, and only in THIRD_PARTY.md since the About screen went (finding 32).
credit="Thanks to plastikman for orbis-win and his PS4 Wine port (github.com/plastikman)"
if grep -rniE "prospero|plastikman" "$root/src" "$root/assets" "$repo/LICENSE" "$repo/THIRD_PARTY.md" "$repo/README-player.md" --exclude=OFL.txt | grep -vF "/* internal path" | grep -vF "$credit"; then
    echo "FAIL test_no_branding: branding text found above"; status=1
fi
if grep -niE "prospero|plastikman" "$repo/SOURCES.txt" | grep -vE "https://github.com/" ; then
    echo "FAIL test_no_branding: SOURCES.txt names a host project outside its source links"; status=1
fi
# BLUR 1.00 (plan Task L9): no host-project name and no bench path anywhere
# in the launcher source or the files Patch installs; no allow-list.
if grep -rniE 'prospero|/data/Pc|data\\Pc' "$root/src" "$root/assets/blur-files"; then
    echo "FAIL test_no_branding: a host-project name or a bench path in src/ or assets/blur-files/"; status=1
fi
# English only: no Swedish letters in the source, the installed files or the tests.
if LC_ALL=C grep -rnE $'\xc3[\xa5\xa4\xb6\x85\x84\x96]' "$root/src" "$root/assets/blur-files" "$root"/tests/*.c; then
    echo "FAIL test_no_branding: Swedish characters (English only)"; status=1
fi
if grep -rnF "$credit" "$root/src" "$root/assets"; then
    echo "FAIL test_no_branding: the credit line is only in THIRD_PARTY.md (owner finding 32)"; status=1
fi
if ! grep -qF "$credit" "$repo/THIRD_PARTY.md" || ! grep -q "| LGPL-2.1" "$repo/THIRD_PARTY.md"; then
    echo "FAIL test_no_branding: THIRD_PARTY.md must keep the credit and name the LGPL components"; status=1
fi
alias="$(sed -n 's/^#define BL_AUTHOR_ALIAS "\(.*\)"$/\1/p' "$root/src/bl_brand.h")"
for f in LICENSE THIRD_PARTY.md SOURCES.txt; do
    if [ -z "$alias" ] || ! grep -qF "$alias" "$repo/$f" || grep -q "{{AUTHOR_ALIAS}}" "$repo/$f"; then
        echo "FAIL test_no_branding: $f does not carry the author alias of src/bl_brand.h"; status=1
    fi
done
for b in "$root"/build/test/test_*; do
    [ -x "$b" ] || continue
    if nm "$b" 2>/dev/null | grep -q "pw_spinner_draw"; then
        echo "FAIL test_no_branding: $(basename "$b") links pw_spinner_draw"; status=1
    fi
done
[ $status -eq 0 ] && echo "test_no_branding: ok"
exit $status
