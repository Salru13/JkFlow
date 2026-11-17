#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# The texts shipped in the package (owner findings 16, 23-26 and 32,
# 2026-11-15; no About screen, no about_components.txt): the name is JkFlow; AMAX is downloaded by the player
# and not included; the launcher code is LGPL-2.1-or-later while the V7/S7
# DLLs stay all rights reserved; SOURCES.txt names where the source is
# published (a placeholder the lead fills at gate D), not an open question;
# no whats_new.txt ships (owner decision 2026-11-16).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
pkg="$(cd "$root/.." && pwd)"   # the package texts are at the repository root
bf="$root/assets/blur-files"
status=0
failed=0
checks=0
fail() { echo "FAIL test_package_texts: $1"; status=1; failed=$((failed + 1)); }
need() { checks=$((checks + 1)); grep -qE -- "$2" "$1" || fail "$3"; }
none() { checks=$((checks + 1)); if grep -nE -- "$2" "$1"; then fail "$3"; fi; }

for f in "$pkg/LICENSE" "$pkg/SOURCES.txt" "$pkg/THIRD_PARTY.md" "$pkg/README-player.md" \
         "$pkg/README.md"; do
    checks=$((checks + 1))
    [ -f "$f" ] || { fail "$f is missing"; continue; }
    none "$f" 'JokerFlow' "$(basename "$f"): the name is JkFlow (finding 16)"
done

# No what's new (owner decision 2026-11-16): the package script neither copies
# whats_new.txt nor lets the order-284 copy stay (REMOVE.txt), and stops if it
# would ship.
mpi="$pkg/title/make_package_inputs.sh"
none "$mpi" '^[^#]*cp .*whats_new' "make_package_inputs.sh copies whats_new.txt"
need "$mpi" '^blur-files/whats_new\.txt$' "make_package_inputs.sh: REMOVE.txt lists blur-files/whats_new.txt"
need "$mpi" 'whats_new\.txt must not ship' "make_package_inputs.sh: stops when whats_new.txt is in OUT"

# AMAX: downloaded by the player from amax-emu.com, not included (finding 25).
checks=$((checks + 1))
[ ! -e "$bf/about_components.txt" ] || fail "about_components.txt is still in blur-files (finding 32: nothing reads it)"
need "$pkg/THIRD_PARTY.md" 'downloaded by the player from amax-emu\.com, not included' "THIRD_PARTY: AMAX is downloaded by the player"
none "$pkg/THIRD_PARTY.md" 'blur-files/amax-files' "THIRD_PARTY names a shipped AMAX folder"

# Licences (finding 24).
need "$pkg/LICENSE" 'launcher.*LGPL-2\.1-or-later|LGPL-2\.1-or-later.*launcher' "LICENSE: the launcher is LGPL-2.1-or-later"
need "$pkg/LICENSE" 'V7.*All rights reserved|All rights reserved.*V7' "LICENSE: V7/S7 stay all rights reserved"
need "$pkg/THIRD_PARTY.md" '^\| the JkFlow launcher .*\| LGPL-2\.1-or-later' "THIRD_PARTY: the launcher line is LGPL-2.1-or-later"
need "$pkg/THIRD_PARTY.md" 'S7 shader fixes.*All rights reserved' "THIRD_PARTY: V7/S7 all rights reserved"
need "$pkg/THIRD_PARTY.md" '^\| DXVK 1\.10\.3, the base of our native renderer \(`dxvknative\.prx`\).*\| zlib/libpng \|' "THIRD_PARTY: DXVK base row (finding 32)"
need "$pkg/LICENSE" 'Native DXVK by XxXJoker13XxX' "LICENSE: native DXVK credit line (owner 2026-11-16)"
need "$pkg/LICENSES/JkFlow-Native-DXVK.txt" '^ +Native DXVK by XxXJoker13XxX$' "Native DXVK licence: the credit line"
need "$pkg/THIRD_PARTY.md" '^\| Native DXVK: our changes in `sce_module/dxvknative\.prx`' "THIRD_PARTY: dxvknative.prx is ours (owner 2026-11-16)"
none "$pkg/THIRD_PARTY.md" '^\| the launcher .*All rights reserved' "THIRD_PARTY: the launcher is not all rights reserved"

# Source offer (finding 23/24).
none "$pkg/SOURCES.txt" '[Tt]o confirm at package freeze' "SOURCES.txt still has the open freeze block"
need "$pkg/SOURCES.txt" '^Source: https://github\.com/Salru13/JkFlow [(]tag v1\.00[)]$' "SOURCES.txt: the real source link (audit 3)"
none "$pkg/SOURCES.txt" '<release repository' "SOURCES.txt still has the placeholder"
none "$pkg/SOURCES.txt" 'LEAD:' "SOURCES.txt still has an internal note"
for f in "$pkg/LICENSE" "$pkg/THIRD_PARTY.md" "$pkg/README-player.md"; do none "$f" 'LEAD:' "$f has an internal note"; done
need "$pkg/SOURCES.txt" 'launcher' "SOURCES.txt: the launcher source is published too"

# Review G1 C1/H2/M8 (2026-11-16): one licence per file. Native DXVK (d3d9.c988.dll,
# dxvknative.prx) is free to use with credit, so no sentence of SOURCES.txt puts it
# under "All rights reserved"; the offer names its gaps; the licence keeps the DXVK
# zlib notice with the files.
checks=$((checks + 1))
if tr '\n' ' ' < "$pkg/SOURCES.txt" | sed 's/\. /.\n/g' | grep -iE 'native.*all rights reserved|all rights reserved.*native'; then
    fail "SOURCES.txt puts Native DXVK under all rights reserved (review C1)"
fi
need "$pkg/SOURCES.txt" 'Native DXVK by XxXJoker13XxX' "SOURCES.txt: the Native DXVK credit line (review C1)"
need "$pkg/SOURCES.txt" 'Gaps' "SOURCES.txt: names where the gaps of the source offer are listed (review H2)"
need "$pkg/LICENSE" 'Gaps' "LICENSE: names where the gaps of the source offer are listed (review H2)"
need "$pkg/LICENSES/JkFlow-Native-DXVK.txt" 'LICENSES/DXVK-zlib\.txt are included' "Native DXVK licence: the DXVK notice stays with the files (review M8)"
none "$pkg/README-player.md" '\(draft\)' "README-player is not a draft (review H4)"

# Player README (finding 26).
need "$pkg/README-player.md" 'GoldHEN' "README-player: GoldHEN"
need "$pkg/README-player.md" 'FTP' "README-player: FTP"
need "$pkg/README-player.md" 'USB' "README-player: USB is not supported"
need "$pkg/README-player.md" 'amax-emu\.com/how_to_play' "README-player: the AMAX account page"

# Owner finding 37: the same full paths as Help's "Your Blur folder", no ASCII tree.
need "$pkg/README-player.md" '^/data/JkFlow/Blur$' "README-player: the game folder as a full path"
need "$pkg/README-player.md" '^/data/JkFlow/Blur/Blur\.exe$' "README-player: the full path of Blur.exe"
need "$pkg/README-player.md" '^/data/JkFlow/Blur/binkw32\.dll$' "README-player: the full path of binkw32.dll"
need "$pkg/README-player.md" 'Blur\.exe must be directly in this folder, not in a folder inside it' "README-player: Blur.exe directly in the folder"
none "$pkg/README-player.md" '(^|[[:space:]])(`--|\|--)' "README-player: no ASCII folder tree"

# Frame rate (owner 2026-11-16): 30 FPS default, 60 FPS for PS4 Pro with Boost Mode; no 50+ claim.
need "$pkg/README-player.md" '^## Frame rate' "README-player: a Frame rate section"
need "$pkg/README-player.md" 'Boost Mode' "README-player: 60 FPS needs Boost Mode on a PS4 Pro"
need "$pkg/README-player.md" 'not a verified Blur' "README-player: an unverified Blur.exe runs at 30 FPS"
none "$pkg/README-player.md" '50\+' "README-player: no 50+ FPS claim"

# Audit 13: there is no contact address, so no text asks the player to send something to us
# (lines joined, so a phrase split over two lines is caught too).
checks=$((checks + 1))
if tr '\n' ' ' < "$pkg/README-player.md" | grep -qE '[Ss]end +us|send +it +to|to +us([^a-zA-Z]|$)'; then
    fail "README-player asks to send something to us (audit 13)"
fi
need "$pkg/README-player.md" 'copy it to a PC over FTP' "README-player: the support bundle can be copied to a PC over FTP (audit 13)"

# Audit 25: model-base.ini is a visible file: no developer comments (the launcher never reads them).
none "$bf/model-base.ini" '^[[:space:]]*[;#]' "model-base.ini has developer comments (audit 25)"
need "$bf/model-base.ini" '^\[application\]$' "model-base.ini: the [application] section is kept"
need "$bf/model-base.ini" '^DXVK_PS4_NATIVE = menu$' "model-base.ini: the env lines are kept"

echo "test_package_texts: $checks checks, $failed failed"
exit $status
