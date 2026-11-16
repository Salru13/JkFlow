#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# Package inputs for JkFlow 1.00 (title ID BLUR00001, SFO TITLE JkFlow; data
# in /data/JkFlow: System = the launcher/title root, Logs, and the player's
# game folder Blur, any letter case; title r4 + the product engine), in WSL,
# after prepare_blur100.py r4 and its build-launcher.sh (plan Task I1 step 1-2):
#   JKFLOW_BUILD_ROOT=<build root> JKFLOW_STATE_CACHE=<dxvk-native.dxvk-cache> \
#   AMAX_ZIP=<amax_client_files.zip> BLUR12_ZIP=<blur_12.zip> make_package_inputs.sh [OUT]
# OUT defaults to $JKFLOW_BUILD_ROOT/blur-launcher-r4/package-inputs-1.00
# and must not exist yet; the composed view is OUT's name with package-inputs
# replaced by package-view. The package is built from these inputs in a
# separate step; this only prepares inputs.
#
# OUT is a delta over the order-284 package (BLUR 1.00 r2, extracted at
# blur100-inventory/extracted/uroot): every file listed in SHA256SUMS replaces
# or adds that path, REMOVE.txt lists what must not stay in the package, and
# seed245/blur-native248/ replaces that folder as a whole. A symlink view of
# the composed package is written next to OUT (package-view*) and the
# name scanner (launcher/tools/scan_names.py, E6) runs over both.
#
# Every input is pinned by SHA-256 below; the script stops on any mismatch.
# No Blur artwork is shipped: sce_sys/ icon0/pic0/pic1 are our own JkFlow art.
# The game folder is the player's own /data/JkFlow/Blur; Patch works on it in
# place (no copy-in of the game). Licence texts ship as the whole
# LICENSES/ folder of this repository (incl. musl-MIT.txt, orbis-compat-MIT.txt).
# No AMAX or Activision file is shipped (owner spec 2026-11-15 v3). "Patch for
# online play" downloads amax_client_files.zip and blur_12.zip from amax-emu.com,
# or, without internet, the player puts them into /data/JkFlow beside the Blur
# folder. This script only reads the
# owner's local copies of the two pinned zips (never copied into OUT) to write
# their members as "amax" and "blur12" manifest lines, the check for the
# player's zips. Offline: our V7 d3d9.dll and S7 in the game root (the chain
# before AMAX); online: the AMAX proxy d3d9.dll takes the root, V7 and S7 go to
# amax/ ("online" lines, with our AMAX plugins).
# The engine's state cache seed is a "cache" line: Patch puts it into
# /data/JkFlow/System/cache (DXVK_STATE_CACHE_PATH), never into the game folder.
# No whats_new.txt (owner decision 2026-11-16): not shipped, and REMOVE.txt
# takes the order-284 copy out.
set -euo pipefail
base="${JKFLOW_BUILD_ROOT:?set JKFLOW_BUILD_ROOT to the build root}"
r4="$base/blur-launcher-r4"
title="$r4/reloc-build"
out="${1:-$r4/package-inputs-1.00}"
view="$(dirname "$out")/$(basename "$out" | sed 's/^package-inputs/package-view/')"
[ "$view" != "$out" ] || { echo "OUT must be named package-inputs*" >&2; exit 2; }
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"                   # this repository
launcher="$repo/launcher"
u284="$base/blur100-inventory/extracted/uroot"   # the order-284 package as built
old="$base/native294-package268/payload-inputs/blur-files"   # V7, S7, dxvk.conf of the 0.8.33 package
# Audit finding 6 (2026-11-16): no build-host path in the package. blur100-paths6 holds the
# same modules with every build-host home prefix replaced by an equal-length neutral one
# (our build host -> /build/jkflw/, the upstream prebuilt Wine PE builder -> /build/jkflow/,
# /home/runner/ -> /build/ci-rt/;
# prx: patched link ELF -> the same create-fself-repro conversion; PE: .rdata bytes + CheckSum).
# Each file differs from its predecessor only in those path bytes (and the fself digest / PE
# CheckSum that cover them); controls reproduced the old pins byte for byte.
p6="$base/blur100-paths6"
p6b="$base/blur100-paths6b"   # audit 10: developer env files disabled (prx only)
p7="$base/blur100-paths7"     # finding 41: developer switch paths -> /dev/null/bld/ (postlink_devpaths.py)
amax_zip="${AMAX_ZIP:?set AMAX_ZIP to a copy of amax_client_files.zip (only hashed, never copied)}"
blur12_zip="${BLUR12_ZIP:?set BLUR12_ZIP to a copy of blur_12.zip (only hashed, never copied)}"

check() { [ "$(sha256sum "$1" | cut -d' ' -f1)" = "$2" ] || { echo "hash mismatch: $1" >&2; exit 1; }; }
[ ! -e "$out" ] || { echo "exists: $out" >&2; exit 2; }
[ ! -e "$view" ] || { echo "exists: $view" >&2; exit 2; }

# Title r4 (Task T1).
# Title r4 rebuilt 2026-11-16 (launcher 905244f + audit 26 local_user, seed END ed7b2bdb,
# post-link clean): eboot unchanged, prx 354bf12c (uncleaned 01a25295; PKG 289; 288 was dc975e24).
check "$title/eboot.bin" a71f3f0277f12d16c61e05caca7a515aec99233e5c6c1207584a6e1ef995e83a
check "$title/prx/relocated_wine_app.prx" d0cee9f775ee9197f50a2003db86038b49bd54c10923c1b4ead1f06cbf808414   # order 290 (findings 37-40); 289: 354bf12c
grep -q 'TITLE_ID=BLUR00001 TITLE=JkFlow PW_BUILD_ID=BLUR-1.00 PKG_VERSION=01.00' "$r4/build-launcher.sh"
# Engine E2-E5 (the pinned Wine/engine module builds).
declare -A prx=(
  # Audit 10 (2026-11-16): paths6b = the same modules with the developer env files made
  # unopenable (/data/{orbis,tempest,retroarch,retroarch-glcore}-env.txt and two developer config
  # files -> equal-length /dev/null/... paths; fopen fails like an absent file). Only those bytes
  # and the 32-byte SELF digest differ from the previous pin (blur100-paths6b/prx/*/prx-proof.json).
  # Product engine (JKFLOW_PRODUCT=1: no developer
  # switches; post-link path clean; was eb49cac4 in PKG 288).
  [dxvknative.prx]="$base/jkflow100-engine-product/dxvknative.prx b2c117c8d380a88e701965d5999f306e6f7008604c5af49a8c457acbd79098f7"
  # libvulkan also has a Mesa developer log fallback file (used when BLUR_LOG_DIR is unset,
  # i.e. logs off) -> /dev/null/dev/radv.log; Mesa keeps its stderr sink when the fopen fails.
  [libvulkan.prx]="$p7/prx/libvulkan/new/libvulkan.prx 9b00fc046864ae67e9b31865b82dd579994dc5b39950c445ad7e635b1f8ceb32"   # was d96939a4 < b6d0f24f
  [ntdll.prx]="$p7/prx/ntdll/new/ntdll.prx 1b06ca727c73317f29719e869185e29ffbc830773d8fa1ad625f068db934b65b"   # was ecdc4c9d
  [wineserver.prx]="$p7/prx/wineserver/new/wineserver.prx e1e793dc843e60ec42cd58d1eef16c12e335865c5571532a2765edd4824a684d"   # was 36ec8c22
  [win32u.prx]="$p7/prx/win32u/new/win32u.prx 1efd3932f9c6e1b2ac9c1dc39c38c7af043070bb48fc0df6b082050d0a6f8899"   # was cebf32c1
  [ws2_32.prx]="$p7/prx/ws2_32/new/ws2_32.prx 35b8e2e89535dc8fb7cedbaac701c618189856f21505176d220aa61245e3e66d"   # was 058949e3
  [opengl32.prx]="$p6b/prx/opengl32/new/opengl32.prx 834fd994555104b2fd20b09c27f4e26effbca020fd2fbdcc7917dae4422dbf56"   # was 3635039b
  [winevulkan.prx]="$p7/prx/winevulkan/new/winevulkan.prx 7654dd62eb74ade6bb95d557380dccee238686666b0036c70ee6dd9367048ab5"   # was fd43827b
  [wow64native.prx]="$p7/prx/wow64native/new/wow64native.prx f2704f07ec7dd0fe8cdc62c4491741dbefb327d6b24f98558f6fdc1ef3a648d7"   # was d864baf4
  [wowblur.prx]="$p7/prx/wowblur/new/wowblur.prx 6ba57d93dffa26c6177bc3ba7380d8ea6671937f302a56457d9adef047e0b6e6"   # was 61775584
  [crypt32.prx]="$p6b/prx/crypt32/new/crypt32.prx 0cf12294294980650622d04d53d077977420e2b41aa5655a0a109dd61be05a3e"   # was 0658d3d8
  [dwrite.prx]="$p6b/prx/dwrite/new/dwrite.prx 70b0f8b53e03bfaab14a05fbcd0be4a6034736231713eb4ce4de7badfa90af0a"   # was ab9027e8
  [libfreetype.prx]="$p6b/prx/libfreetype/new/libfreetype.prx 4a7f64abfc1448ec803ea7e834905812b3e473ac4056614f9728e7485b55d5c5"   # was 3e51cd30
  [wineps5.prx]="$p6b/prx/wineps5/new/wineps5.prx 77ed4f8c547adc42b8940c0bf77d6791736214ea3ba4d65e4e79c36db0e698dd"   # was 9aecbebb
  [xinput1_3.prx]="$p6b/prx/xinput1_3/new/xinput1_3.prx febeacc05842f3e73bb963b85b62eafafa9e7a55503e4758a240661628bc5355"   # was b2341dff
)
for n in "${!prx[@]}"; do set -- ${prx[$n]}; check "$1" "$2"; done
wow64_dll="$base/blur100-wow/blur100/wow64.dll";   check "$wow64_dll" 8540553a075b1b083b2a0e4e4502b4b865550a05ac069a21be52128e4c61e478
wowblur_dll="$base/blur100-wow/blur100/wowblur.dll"; check "$wowblur_dll" 4f4cb082673e9e5c44c7f48f5cc212c84781bcf5dc227153f25bff42545f497b
# Seed of 4f9bef01 with the registry font paths and the runtime pins of the paths6 PE files
# (blur100-paths6/seed-build; the title must be built with this END, checked below).
seed="$p6/seed-build/seed/seed245/blur-native248"
seed_end=ed7b2bdb557d3d3e49c2ca72b027fc28cdb6ec7d9d689d49204c5d23e93925e7
[ "$(tail -c 65 "$seed/seed.idx" | head -c 64)" = "$seed_end" ] || { echo "seed END is not $seed_end" >&2; exit 1; }
check "$seed/seed.idx" 2c5f2df3426a6de98df89f1c08a251d9b20cd68ee4c5240961ff6d4082b34dab
grep -qaF "$seed_end" "$title/prx/relocated_wine_app.prx" || { echo "title not built for seed END $seed_end" >&2; exit 1; }
# The 520 path-cleaned Wine PE files (457 upstream prebuilt, 63 ours), replacing the order-284 copies.
check "$p6/pe-SHA256SUMS" 10042a31c52575e439fcfd4e6bba7f2907c53eecf01bd8de0659382e228c3101
(cd "$p6/pe" && sha256sum --quiet -c "$p6/pe-SHA256SUMS") || { echo "paths6 PE tree differs from its pins" >&2; exit 1; }
# The seed needs exactly this runtime in the package (blur100-seed-plan.json "requires").
check "$u284/win/wine/lib/wine/i386-windows/winevulkan.dll" 7ba220027012496c381f2193048acf62b0ddc9cb160f3e96e0a5ea77f79d46d5
# Client: the product build of the same engine (jkflow100-engine-product.json; PKG 287/288 shipped
# 02e98dff, the developer build, which stays a known file below so Patch replaces it).
c988="$base/jkflow100-engine-product/d3d9.c988.dll"; check "$c988" 69e48bda217a2410ba9dfd3d5235d81b93cda717b0a8543a64ecb785394102c4
# V7, S7, dxvk.conf and the native state cache of 0.8.33/0.8.34.
check "$old/d3d9.dll" 55ba8a0d64012ae44de2247c3cd07a51d3bec5a7c62fd6a74a8cd0776da63908
check "$old/d3d9_blur_dxvk.dll" 0748b79592f7e26fbd044389386ca9c9f8d330bb6298a0dbf817f3a64868fb3b
check "$old/dxvk.conf" 95372d213671bbdfaa50281f800be800d43728f4a3e1b3414d7df9f665a41b6c
cache="${JKFLOW_STATE_CACHE:?set JKFLOW_STATE_CACHE to the DXVK state cache seed dxvk-native.dxvk-cache}"; check "$cache" 58dbdf9100d92535a5ce344692033316c198d409f210d294a7ab9b3ca3ff99a9
[ "$(head -c 4 "$cache")" = DXVK ] || { echo "not a DXVK state cache: $cache" >&2; exit 1; }
check "$amax_zip" 4ee93bc7676e4f9f03d2bd657300c47594fad5265fc15c2cd911f190567425c7
check "$blur12_zip" bfebfba6d5f459bfa6cb64278f874cc49c76663685815520e534a51bdd398d9a
check "$launcher/assets/blur-files/model-base.ini" c4d1661b85eb5187b1fb083d82d793aba5ea0281a8bf62cf6a03e345a5a6eaad   # audit 25
art="$launcher/assets/sce_sys"
# Own JkFlow artwork (no official Blur logo or art).
check "$art/icon0.png" 1f7ccbfa4c802bb630d740fe0e5b1980bdb9bab0ebf0e7ef7d57c9ecce3b73c0
check "$art/pic0.png" 8cf901c8f242613b6cae0747c6fdd9cb8a70e35f80cc5e145b08a46011ad07fc
check "$art/pic1.png" 293083352f2261448dea7cf78840280c9dbf4478aef3e121d34059e9c41e9231
tested_exe=dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f

mkdir -p "$out/sce_sys" "$out/sce_module" "$out/blur-files" "$out/seed245" \
         "$out/win/wine/lib/wine/x86_64-unix" "$out/win/wine/lib/wine/x86_64-windows"
cp "$title/eboot.bin" "$out/eboot.bin"
cp "$title/prx/relocated_wine_app.prx" "$out/sce_module/"
for n in "${!prx[@]}"; do
    set -- ${prx[$n]}
    cp "$1" "$out/sce_module/$n"
    cp "$1" "$out/win/wine/lib/wine/x86_64-unix/$n"
done
cp "$wow64_dll" "$out/win/wine/lib/wine/x86_64-windows/wow64.dll"
cp "$wowblur_dll" "$out/win/wine/lib/wine/x86_64-windows/wowblur.dll"
cp -rL "$seed" "$out/seed245/blur-native248"
cp -r "$p6/pe/win" "$out/"
cp "$art/icon0.png" "$art/pic0.png" "$art/pic1.png" "$out/sce_sys/"

# blur-files: what Patch installs. Offline: our client, V7 as d3d9.dll and S7
# in the game root (Blur.exe -> d3d9.dll V7 -> d3d9_blur_dxvk.dll S7, found in
# V7's own folder -> d3d9.c988.dll). Online: the AMAX proxy d3d9.dll loads
# amax\dxvk.dll, so V7 is also amax/dxvk.dll with S7 beside it, plus our
# account plugin; AMAX's own plugins.lua gets our require line at Patch time
# (--edit-plugins; JkFlow ships no AMAX file). dxvk.conf in the root
# (fallback; the title writes cache/dxvk.conf); the state cache seed goes to
# the launcher's cache folder.
bf="$out/blur-files"
cp "$c988" "$bf/d3d9.c988.dll"
cp "$old/d3d9.dll" "$bf/d3d9.dll"
cp "$old/d3d9_blur_dxvk.dll" "$bf/d3d9_blur_dxvk.dll"
mkdir -p "$bf/amax-dxvk"
cp "$old/d3d9.dll" "$bf/amax-dxvk/dxvk.dll"
cp "$old/d3d9_blur_dxvk.dll" "$bf/amax-dxvk/d3d9_blur_dxvk.dll"
cp "$old/dxvk.conf" "$bf/dxvk.conf"
cp "$cache" "$bf/dxvk-native.dxvk-cache"
cp -r "$launcher/assets/blur-files/amax-overlay" "$bf/amax-overlay"
cp "$launcher/assets/blur-files/model-base.ini" "$bf/"
# The two zips are extracted outside OUT, only to hash their members.
amax="$(mktemp -d)"; b12="$(mktemp -d)"; known="$(mktemp)"; zip_hashes="$(mktemp)"
trap 'rm -rf "$amax" "$b12" "$known" "$zip_hashes"' EXIT
python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$amax_zip" "$amax"
python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$blur12_zip" "$b12"
check "$b12/Blur.exe" "$tested_exe"
# Earlier packaged c988 builds count as ours (replaced, not parked), and so
# does the amax/plugins/plugins.lua that PKG 286 shipped (425c8866, review
# finding 2): Patch puts the edited AMAX list over it, Restore brings back
# AMAX's own list (bl_patcher: kept in the backup), never 286's.
printf 'known %s\n' 425c88668242071199db76e7a18d671d26a0ad7b607e84c1facfb96b3148342a \
    04ef430750521acea1c089868d2e219cee3f1fbf7a5d9be83bdcd97871b4bbe1     02e98dff98f6d2724c4c123830590349097e8aa7c6cc64d40ba6eee105de28b5 \
    8dd190a39b1604405500bc1d125c91d074dbcea69206aaf9451a78b2fe2d37c2 \
    4542a14aca0159a4b0d584c2a86c96252ee2f3f39059f5e1dd42989bf083b52f \
    65c163ea057bbe4d12503e98709a2e84d984978f6d1d96e0eeebf2e26e685877 \
    3b49c1693ca1de67f35f83f22fbecaad2434e33654df047d5877da2be7608869 \
    9c9578edec25cf8ac1338afa5f53e2149398681342528a88baffcd7b229b69aa \
    55ba8a0d64012ae44de2247c3cd07a51d3bec5a7c62fd6a74a8cd0776da63908 \
    0748b79592f7e26fbd044389386ca9c9f8d330bb6298a0dbf817f3a64868fb3b > "$known"
python3 -I "$launcher/tools/make_manifest.py" "$bf" d3d9.c988.dll d3d9.dll d3d9_blur_dxvk.dll dxvk.conf \
    --online-overlay amax-dxvk amax --online-overlay amax-overlay amax --amax "$amax" --blur12 "$b12" \
    --edit-plugins amax/plugins/plugins.lua \
    --park dinput8.dll --park dsound.dll --park winmm.dll --park version.dll --park scripts --park texmod \
    --park '*.asi' --park amax \
    --cache dxvk-native.dxvk-cache --known "$known" --tested "$tested_exe"
grep -q '^blur12 Blur.exe ' "$bf/manifest.txt" && grep -q '^blur12 cache/gamedata.pak ' "$bf/manifest.txt" \
    && grep -q '^blur12 cache/gamedata.sig ' "$bf/manifest.txt" && grep -q '^amax d3d9.dll ' "$bf/manifest.txt" \
    || { echo "manifest lacks the blur12/amax lines" >&2; exit 1; }
# Never ship an AMAX or Activision file: no file may have the hash of a zip
# member (checked here over OUT, and below over the composed view). The hits
# are captured, not piped into grep -q (SIGPIPE under pipefail).
(cd "$amax" && find . -type f -size +0 -exec sha256sum {} +; cd "$b12" && find . -type f -size +0 -exec sha256sum {} +) \
    | cut -d' ' -f1 | sort -u > "$zip_hashes"
never_ship() {
    local hits
    hits="$(find -L "$1" -type f -exec sha256sum {} + | cut -d' ' -f1 | sort -u | comm -12 - "$zip_hashes")"
    [ -z "$hits" ] || { echo "an AMAX or blur_12 file is in $1: $hits" >&2; exit 1; }
}
never_ship "$out"
cp -r "$repo/LICENSES" "$out/LICENSES"
cp "$repo/LICENSE" "$repo/THIRD_PARTY.md" "$repo/SOURCES.txt" "$out/"
cat > "$out/REMOVE.txt" <<'REM'
# Paths of the order-284 package that must not be in JkFlow 1.00 (renamed by E4).
sce_module/wowprospero.prx
win/wine/lib/wine/x86_64-unix/wowprospero.prx
win/wine/lib/wine/x86_64-windows/wowprospero.dll
# Replaced as a whole folder (paths6 seed, END ed7b2bdb...): delete the old one first.
seed245/blur-native248/
# The About screen's list (owner finding 32: no About screen; nothing reads it).
blur-files/about_components.txt
# Not shipped (owner decision 2026-11-16).
blur-files/whats_new.txt
REM

# Visible-text and personal-data checks (r3 method).
visible=("$out/LICENSE" "$out/THIRD_PARTY.md" "$bf/model-base.ini" "$bf/manifest.txt")
[ ! -e "$bf/whats_new.txt" ] || { echo "whats_new.txt must not ship" >&2; exit 1; }
# The one owner-approved credit (2026-11-15, as in tests/test_no_branding.sh) is
# allowed only as this exact phrase; any other mention of those names fails.
credit="Thanks to plastikman for orbis-win and his PS4 Wine port (github.com/plastikman)"
names_outside_credit() {   # prints offending lines, returns 0 if any were found
    awk -v c="$credit" '{ l = $0; while ((i = index(l, c)) > 0) l = substr(l, 1, i - 1) substr(l, i + length(c))
        if (tolower(l) ~ /prospero|plastikman/) { print FILENAME ":" FNR ": " $0; bad = 1 } } END { exit !bad }' "$@"
}
if names_outside_credit "${visible[@]}"; then echo "host-project name in a visible file" >&2; exit 1; fi
src_lines="$(grep -iv "https://github.com/" "$out/SOURCES.txt" || true)"
src_lines="${src_lines,,}"
if [[ $src_lines == *prospero* || $src_lines == *plastikman* ]]; then echo "SOURCES.txt names a host project outside its links" >&2; exit 1; fi
private="@users.noreply|@gmail|@outlook|@hotmail${PRIVATE_NAMES:+|$PRIVATE_NAMES}"
# LICENSES/ holds upstream notices whose author credits (many @gmail addresses)
# must ship unchanged (audit 11); there only our own markers count. Every
# address in them is listed in scan-allow.txt for the name scan below.
if grep -rilaE --exclude-dir=LICENSES "$private" "$out"; then echo "personal data in a package text" >&2; exit 1; fi
if grep -rilaE "@users.noreply${PRIVATE_NAMES:+|$PRIVATE_NAMES}" "$out/LICENSES"; then echo "personal data in a licence text" >&2; exit 1; fi

(cd "$out" && find . -type f ! -name SHA256SUMS ! -name SIZES | sort | xargs -d '\n' sha256sum > SHA256SUMS)
(cd "$out" && find . -type f ! -name SHA256SUMS ! -name SIZES | sort | xargs -d '\n' stat -c '%s %n' > SIZES)

# The composed package view: 284 + this delta - REMOVE.txt (symlinks only).
mkdir -p "$view"
cp -rs "$u284/." "$view/"
rm -rf "$view/seed245/blur-native248"
grep -v '^#' "$out/REMOVE.txt" | while read -r p; do [ -n "$p" ] && rm -rf "$view/$p"; done
(cd "$out" && find . -type f ! -name SHA256SUMS ! -name SIZES ! -name REMOVE.txt) | while read -r p; do
    mkdir -p "$(dirname "$view/$p")"; ln -sfn "$out/${p#./}" "$view/${p#./}"
done

never_ship "$view"
cat "$bf/manifest.txt"
echo "== scan inputs"
set +e
python3 -I "$launcher/tools/scan_names.py" "$out" --allow "$here/scan-allow.txt"; rc_in=$?
echo "== scan composed view"
python3 -I "$launcher/tools/scan_names.py" "$view" --allow "$here/scan-allow.txt"; rc_view=$?
set -e
echo "scan inputs rc=$rc_in view rc=$rc_view"
