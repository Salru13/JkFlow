#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# Third-party licence notices in the package (audit A-Z finding 11, 2026-11-16):
#  1. no unfilled template text: no <year>, <copyright holders> or other <...>
#     placeholder in LICENSES/*, THIRD_PARTY.md, LICENSE or SOURCES.txt. Allowed:
#     <URL> and <e-mail> as written in upstream notices, and the few lines below
#     that are part of a licence's own verbatim text (its how-to-apply appendix);
#  2. every LICENSES/... file THIRD_PARTY.md names exists and is not empty;
#  3. every component row of THIRD_PARTY.md names at least one LICENSES/ file
#     (except a row for something that is not part of this package);
#  4. every file in LICENSES/ is named in THIRD_PARTY.md (no orphan texts);
#  5. the notices that were missing or templated carry real copyright lines.
# PKG_DIR=<dir> runs the same checks on another package folder (self-test).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
pkg="${PKG_DIR:-$(cd "$root/.." && pwd)}"   # the package texts are at the repository root
lic="$pkg/LICENSES"
tp="$pkg/THIRD_PARTY.md"
status=0
checks=0
failed=0
fail() { echo "FAIL test_licenses: $1"; status=1; failed=$((failed + 1)); }

checks=$((checks + 1))
[ -d "$lic" ] && [ -f "$tp" ] || { fail "LICENSES/ or THIRD_PARTY.md missing in $pkg"; echo "test_licenses: $checks checks, $failed failed"; exit 1; }

# 1. Placeholders. Allowed verbatim template lines, as "file<TAB>line" (trailing spaces trimmed).
allowed=$(printf '%s\n' \
  $'LGPL-2.1.txt\t    <one line to give the library\'s name and a brief idea of what it does.>' \
  $'LGPL-2.1.txt\t    Copyright (C) <year>  <name of author>' \
  $'LGPL-2.1.txt\t  <signature of Ty Coon>, 1 April 1990' \
  $'FreeType-FTL.txt\t    Portions of this software are copyright © <year> The FreeType' \
  $'FreeType-FTL.txt\t  Please replace <year> with the value from the FreeType version you')
scan=("$lic"/* "$tp")
for f in "$pkg/LICENSE" "$pkg/SOURCES.txt"; do [ -f "$f" ] && scan+=("$f"); done
allow_file="$(mktemp)"
trap 'rm -f "$allow_file"' EXIT
printf '%s\n' "$allowed" > "$allow_file"
checks=$((checks + ${#scan[@]}))
# One awk pass over every file: drop allowed lines, URLs and e-mail addresses in
# angle brackets; anything left in <...> (up to 80 characters) is a placeholder.
hits=$(awk -F'\n' '
    NR == FNR { allow[$0] = 1; next }
    FNR == 1 { base = FILENAME; sub(/.*\//, "", base) }
    {
        line = $0; sub(/[ \t\r]+$/, "", line)
        if ((base "\t" line) in allow) next
        t = line
        gsub(/<(https?|ftp|mailto):[^<>]*>/, "", t)
        gsub(/<[^<> ]+@[^<> ]+>/, "", t)
        gsub(/<[^<> ]+ at [^<> ]+>/, "", t)
        if (match(t, /<[^<>]+>/) && RLENGTH <= 82) print base ":" FNR ": " line
        else if (t ~ /\{\{[A-Z_]+\}\}/) print base ":" FNR ": " line
    }' "$allow_file" "${scan[@]}")
if [ -n "$hits" ]; then
    while IFS= read -r h; do fail "template placeholder: $h"; done <<< "$hits"
fi

# 2-3. The component table: the rows after the first "| Component |" header.
rows=$(awk '/^\| Component \|/{t=1; next} t && /^\|---/{next} t && /^\|/{print; next} t{exit}' "$tp")
checks=$((checks + 1))
[ -n "$rows" ] || fail "THIRD_PARTY.md has no component table"
while IFS= read -r row; do
    [ -n "$row" ] || continue
    checks=$((checks + 1))
    name="$(printf '%s' "$row" | cut -d'|' -f2 | sed -E 's/^ +| +$//g' | cut -c1-60)"
    refs=$(printf '%s' "$row" | grep -oE 'LICENSES/[A-Za-z0-9._+-]+' || true)
    if [ -z "$refs" ]; then
        printf '%s' "$row" | grep -q 'not part of this package' || fail "component without a licence file: $name"
    fi
done <<< "$rows"
for ref in $(grep -oE 'LICENSES/[A-Za-z0-9._+-]+' "$tp" | sort -u); do
    checks=$((checks + 1))
    [ -s "$pkg/$ref" ] || fail "THIRD_PARTY.md names $ref, which is missing or empty"
done

# 4. No orphan texts.
for f in "$lic"/*; do
    checks=$((checks + 1))
    grep -qF "LICENSES/$(basename "$f")" "$tp" || fail "LICENSES/$(basename "$f") is not named in THIRD_PARTY.md"
done

# 5. Real copyright lines where the audit found them missing.
need() { checks=$((checks + 1)); [ -f "$lic/$1" ] && grep -qE -- "$2" "$lic/$1" || fail "$1: $3"; }
need Mesa-MIT.txt '^Copyright .*(19|20)[0-9]{2}.*Advanced Micro Devices' "no real copyright lines (finding 11)"
need Mesa-MIT.txt '^Permission is hereby granted' "the MIT permission text"
need Mesa-other-licenses.txt 'Yann Collet' "xxHash notice"
need FreeType-notices.txt '^Portions of this software are copyright © 2024 The FreeType Project' "the FTL credit line with its year"
need Wine-bundled-libraries.txt 'Independent JPEG Group' "the IJG notice"
need LLVM-Apache-2.0-with-LLVM-exception.txt 'University of Illinois/NCSA' "the legacy LLVM licence with its copyright line"

# 6. Audit 29: the DEFLATE decoder names where its structure comes from.
checks=$((checks + 1))
grep -qF 'Based on the structure of puff.c (zlib contrib/puff, Mark Adler), zlib licence.' "$root/src/bl_inflate.c" \
    || fail "src/bl_inflate.c does not name puff.c (audit 29)"

echo "test_licenses: $checks checks, $failed failed"
exit $status
