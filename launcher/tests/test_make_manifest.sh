#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# make_manifest.py v2 (BLUR 1.00 plan Task L3): subfolder files through
# --overlay, the AMAX set through --amax (overlaid AMAX files become "known"),
# park globs, and refusal of unsafe names.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
t="$(mktemp -d)"
trap 'rm -rf "$t"' EXIT
fail() { echo "FAIL test_make_manifest: $*"; exit 1; }

mkdir -p "$t/files/amax-overlay/plugins/account" "$t/amax/amax/dlls" "$t/amax/amax/plugins" "$t/amax/amax/log"
printf 'S7' > "$t/files/d3d9_blur_dxvk.dll"
printf 'ours' > "$t/files/amax-overlay/plugins/plugins.lua"
printf 'acct' > "$t/files/amax-overlay/plugins/account/account.lua"
printf 'proxy' > "$t/amax/d3d9.dll"
printf 'auth' > "$t/amax/amax/dlls/amax_auth.asi"
printf 'theirs' > "$t/amax/amax/plugins/plugins.lua"
printf '' > "$t/amax/amax/log/.gitkeep"

python3 -I "$root/tools/make_manifest.py" "$t/files" d3d9_blur_dxvk.dll --overlay amax-overlay amax \
    --amax "$t/amax" --park dinput8.dll --park '*.asi' --park amax --tested "$(printf '%064d' 0)"
m="$t/files/manifest.txt"
sha() { python3 -I -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"; }

grep -qx "file d3d9_blur_dxvk.dll $(sha "$t/files/d3d9_blur_dxvk.dll") 2" "$m" || fail "root file line"
grep -qx "file amax/plugins/plugins.lua $(sha "$t/files/amax-overlay/plugins/plugins.lua") 4 src=amax-overlay/plugins/plugins.lua" "$m" || fail "overlay line"
grep -qx "file amax/plugins/account/account.lua $(sha "$t/files/amax-overlay/plugins/account/account.lua") 4 src=amax-overlay/plugins/account/account.lua" "$m" || fail "overlay account line"
grep -qx "amax d3d9.dll $(sha "$t/amax/d3d9.dll") 5" "$m" || fail "amax root line"
grep -qx "amax amax/dlls/amax_auth.asi $(sha "$t/amax/amax/dlls/amax_auth.asi") 4" "$m" || fail "amax subfolder line"
grep -q "^amax amax/plugins/plugins.lua" "$m" && fail "an overlaid AMAX file must not be an amax line"
grep -qx "known $(sha "$t/amax/amax/plugins/plugins.lua")" "$m" || fail "overlaid AMAX file not known"
grep -q "gitkeep" "$m" && fail "dot files are not listed"
grep -qx "park dinput8.dll" "$m" || fail "park line"
grep -qx "park \*.asi" "$m" || fail "park glob line"
grep -qx "park amax" "$m" || fail "park folder line"

# --cache NAME: "cache" line (installed into the launcher cache folder (bl_paths) by the launcher, never into the game folder).
printf 'DXVKseed' > "$t/files/dxvk-native.dxvk-cache"
python3 -I "$root/tools/make_manifest.py" "$t/files" d3d9_blur_dxvk.dll --cache dxvk-native.dxvk-cache
grep -qx "cache dxvk-native.dxvk-cache $(sha "$t/files/dxvk-native.dxvk-cache") 8" "$m" || fail "cache line"
grep -q "^file dxvk-native.dxvk-cache\|^seed dxvk-native.dxvk-cache" "$m" && fail "a cache file must not be a file or seed line"
if python3 -I "$root/tools/make_manifest.py" "$t/files" d3d9_blur_dxvk.dll --cache ../x >/dev/null 2>&1; then fail "accepted cache ../x"; fi

# Unsafe names and park globs are refused.
for bad in "../x" ".hidden" "Blur.exe"; do
    if python3 -I "$root/tools/make_manifest.py" "$t/files" "$bad" >/dev/null 2>&1; then fail "accepted $bad"; fi
done
for bad in "../x" "a/b" ".x"; do
    if python3 -I "$root/tools/make_manifest.py" "$t/files" d3d9_blur_dxvk.dll --park "$bad" >/dev/null 2>&1; then fail "accepted park $bad"; fi
done
# Offline and online sets (owner spec 2026-11-15 v3): V7 in the root is a file
# line and the AMAX proxy of the same name an amax line; our AMAX-only files
# are online lines; the blur_12.zip members are blur12 lines (Blur.exe allowed).
mkdir -p "$t/files/amax-dxvk" "$t/b12/cache"
printf 'V7' > "$t/files/d3d9.dll"
printf 'V7' > "$t/files/amax-dxvk/dxvk.dll"
printf 'exe12' > "$t/b12/Blur.exe"
printf 'pak12' > "$t/b12/cache/gamedata.pak"
python3 -I "$root/tools/make_manifest.py" "$t/files" d3d9.dll d3d9_blur_dxvk.dll \
    --online-overlay amax-dxvk amax --online-overlay amax-overlay amax --amax "$t/amax" --blur12 "$t/b12"
grep -qx "file d3d9.dll $(sha "$t/files/d3d9.dll") 2" "$m" || fail "offline root d3d9.dll line"
grep -qx "amax d3d9.dll $(sha "$t/amax/d3d9.dll") 5" "$m" || fail "online root d3d9.dll line"
grep -qx "online amax/dxvk.dll $(sha "$t/files/amax-dxvk/dxvk.dll") 2 src=amax-dxvk/dxvk.dll" "$m" || fail "online overlay line"
grep -qx "online amax/plugins/plugins.lua $(sha "$t/files/amax-overlay/plugins/plugins.lua") 4 src=amax-overlay/plugins/plugins.lua" "$m" || fail "online plugins line"
grep -q "^amax amax/plugins/plugins.lua" "$m" && fail "an online-overlaid AMAX file must not be an amax line"
grep -qx "known $(sha "$t/amax/amax/plugins/plugins.lua")" "$m" || fail "online-overlaid AMAX file not known"
grep -qx "blur12 Blur.exe $(sha "$t/b12/Blur.exe") 5" "$m" || fail "blur12 exe line"
grep -qx "blur12 cache/gamedata.pak $(sha "$t/b12/cache/gamedata.pak") 5" "$m" || fail "blur12 pak line"
grep -q "^file amax/" "$m" && fail "AMAX-only files must not be offline file lines"
# Owner finding 25: JkFlow ships no plugins.lua. --edit-plugins REL keeps
# AMAX's own list as an amax line (its hash in the zip) with the hash of the
# list after Patch adds our account plugin ("edit=<sha256>").
rm "$t/files/amax-overlay/plugins/plugins.lua"
printf 'return {\r\n\trequire("solo.solo"),\r\n\trequire("fps.fps"),\r\n\trequire("revs.revs"),\r\n}\r\n' > "$t/amax/amax/plugins/plugins.lua"
printf 'return {\r\n\t-- require("solo.solo"),\r\n\t-- require("fps.fps"),\r\n\trequire("revs.revs"),\r\n\trequire("account.account"),\r\n}\r\n' > "$t/edited.lua"
python3 -I "$root/tools/make_manifest.py" "$t/files" d3d9.dll --online-overlay amax-overlay amax --amax "$t/amax" \
    --edit-plugins amax/plugins/plugins.lua
size="$(wc -c < "$t/amax/amax/plugins/plugins.lua" | tr -d ' ')"
grep -qx "amax amax/plugins/plugins.lua $(sha "$t/amax/amax/plugins/plugins.lua") $size edit=$(sha "$t/edited.lua")" "$m" || fail "edited plugins line"
grep -q "^online amax/plugins/plugins.lua\|^file amax/plugins/plugins.lua" "$m" && fail "plugins.lua must not be shipped"
grep -qx "known $(sha "$t/amax/amax/plugins/plugins.lua")" "$m" && fail "AMAX's own list must not be known (it is parked, so Restore puts it back)"
if python3 -I "$root/tools/make_manifest.py" "$t/files" d3d9.dll --amax "$t/amax" --edit-plugins amax/nothing.lua >/dev/null 2>&1; then
    fail "accepted --edit-plugins for a file the AMAX set does not have"
fi
printf 'return { require("x.x") ' > "$t/amax/amax/plugins/plugins.lua"
if python3 -I "$root/tools/make_manifest.py" "$t/files" d3d9.dll --amax "$t/amax" --edit-plugins amax/plugins/plugins.lua >/dev/null 2>&1; then
    fail "accepted a plugins.lua without a closing brace"
fi
# Blur.exe is still refused anywhere else, and below the top level of blur_12.
mkdir -p "$t/b12bad/sub" && printf 'x' > "$t/b12bad/sub/Blur.exe"
if python3 -I "$root/tools/make_manifest.py" "$t/files" d3d9.dll --blur12 "$t/b12bad" >/dev/null 2>&1; then fail "accepted sub/Blur.exe"; fi
echo "ok   test_make_manifest"
