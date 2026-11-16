#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
# Host tests for the BLUR 1.00 title sources (Task T1), run in WSL against a
# prepared title tree (prepare_blur100.py r4):  run_title_tests.sh TREE
#   test_seed_import2   native245_seed.c n245_seed_import2 (first start, update, resume)
#   test_title_profile  the launcher's profile through the title's parser, extra[] count
#   test_https_get      wine64_main.c blur_host_https_get against a fake sceHttp and clock; resume,
#                       150 s, no-connection fast path and distinct results (audit finding 8)
#   test_title_free     wine64_main.c blur_host_free_bytes against a fake fstatfs (audit finding 16)
#   test_title_r4       wine64_main.c's blur100 section: back to the launcher after Blur exits,
#                       session end after the first frame, JkFlow roots + /data/BLUR migration,
#                       refused module loads in the session log; a migration that leaves old data
#                       behind stops the title (review finding 1)
#   test_title_env      wine64_main.c blur_apply_decision: fail closed on env overflow (finding 3)
#   test_title_logs     pw_diagnostics.c (session logs in Logs, no network) and pw_wow_log_ps4.c
#   test_display_name   blur100-display-name.c: the local PS4 name made portable, no keyboard (audit finding 4)
#   test_present_speed  pw_present.c: every mode as defined; the 1:1 present within 2 ms (finding 34a),
#                       a timing miss only warns unless BL_PERF_STRICT=1 (audit finding 20)
#   postlink_clean.py   selftest; a built tree's prx and eboot hold no build-host path / dev env file
#   test_title_scan.sh  no developer mode, no /data/BLUR, the JkFlow name
# The scan of a built tree needs POSTLINK_PATH_MAP (see ../postlink-path-map.example).
set -euo pipefail
tree="${1:?usage: run_title_tests.sh TREE}"
here="$(cd "$(dirname "$0")" && pwd)"
launcher="$(cd "$here/../../launcher" && pwd)"   # launcher/ of this repository
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
san=(-fsanitize=address,undefined -fno-sanitize-recover=all)

cc -std=gnu11 -Wall -Wextra -Werror -DN245_TEST_HOOKS "${san[@]}" \
   -I"$tree/prospero/native" -o "$out/test_seed_import2" \
   "$here/test_seed_import2.c" "$tree/prospero/native/native245_seed.c"
"$out/test_seed_import2"

# The two pieces of wine64_main.c the profile test needs, cut verbatim.
main_c="$tree/prospero/native/wine64_main.c"
{
    sed -n '/^static const char direct_profile_text\[\] =/,/;$/p' "$main_c"
    sed -n '/^static int direct_profile_with_prefix(/,/^}/p' "$main_c"
} > "$out/title_profile_cut.inc"
grep -q 'direct_profile_text' "$out/title_profile_cut.inc"
grep -q 'direct_profile_with_prefix' "$out/title_profile_cut.inc"
# blur_apply_decision and the title's env capacity line, cut verbatim (review
# finding 3, audit finding 7): used by test_title_profile and test_title_env.
grep -E '^enum \{ BLUR_ENV_CAPACITY = ' "$main_c" > "$out/env_cap_cut.inc"
[ "$(wc -l < "$out/env_cap_cut.inc")" = 1 ]
awk '/^static [a-z]+ blur_apply_decision\(/{on=1} on{print} on&&/^}/{exit}' "$main_c" > "$out/env_cut.inc"
grep -q 'blur_apply_decision(' "$out/env_cut.inc"
gen="$launcher/build/gen"
srcs=("$launcher"/src/*.c)
cc -std=gnu11 -w "${san[@]}" -I"$out" -I"$launcher/src" -I"$gen" -I"$tree/prospero/src" -I"$tree/prospero/native" \
   -o "$out/test_title_profile" "$here/test_title_profile.c" \
   "${srcs[@]}" "$gen/bl_font_data.c" "$gen/bl_logo.c" \
   "$tree/prospero/src/pw_game_profile.c" "$tree/prospero/src/pw_app_profile.c" "$tree/prospero/src/pw_result.c" -lm
"$out/test_title_profile" "$launcher/assets/blur-files"

# blur_host_https_get, cut verbatim: from its section comment to the function's end.
awk '/^\/\* ---- https_get:/{on=1} on{print} on&&/^static int blur_host_https_get\(/{fn=1} fn&&/^}/{exit}'     "$main_c" > "$out/https_cut.inc"
grep -q 'static int blur_host_https_get(' "$out/https_cut.inc"
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function "${san[@]}" -I"$out"    -o "$out/test_https_get" "$here/test_https_get.c" -lpthread
for mode in "" initfail nomodule; do "$out/test_https_get" $mode | grep -v '^  log:'; done

# wine64_main.c's blur100 section (restart_title, blur_session_end,
# blur_find_roots, blur_install_dl_report), cut verbatim between its markers.
sed -n '/^\/\* ---- blur100 section/,/^\/\* ---- end blur100 section/p' "$main_c" > "$out/r4_cut.inc"
grep -q 'static void restart_title(' "$out/r4_cut.inc"
grep -q 'static int blur_install_dl_report(' "$out/r4_cut.inc"
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function "${san[@]}" -I"$out" -I"$launcher/src" \
   -I"$tree/prospero/wine/ps5" -I"$tree/prospero/src" -I"$tree/prospero/include" \
   -o "$out/test_title_r4" "$here/test_title_r4.c" "$launcher/src/bl_paths.c" "$launcher/src/bl_fs.c" \
   "$launcher/src/bl_sha256.c" "$launcher/src/bl_strings.c"
"$out/test_title_r4"

# blur_apply_decision, cut verbatim (review finding 3: an env line that does
# not fit stops the start instead of being dropped).
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function "${san[@]}" -I"$out" -I"$launcher/src" -I"$gen" \
   -I"$tree/prospero/src" -o "$out/test_title_env" "$here/test_title_env.c"
"$out/test_title_env"

# The two log writers: pw_diagnostics.c (session logs in Logs, no network)
# and pw_wow_log_ps4.c (wowblur.log only in $BLUR_LOG_DIR).
cc -std=gnu11 -Wall -Wextra -Werror -DPW_DIAGNOSTICS_TESTING "${san[@]}" -I"$tree/prospero/native" \
   -o "$out/test_title_logs" "$here/test_title_logs.c" "$tree/prospero/native/pw_diagnostics.c" \
   "$tree/orbis/src/native/pw_wow_log_ps4.c" -lpthread
"$out/test_title_logs"

# blur_host_free_bytes (audit finding 16), cut verbatim: the free space the
# launcher's disk checks use, unknown when the kernel's answer is not trusted.
awk '/^\/\* ---- free_bytes:/{on=1} on{print} on&&/^static long long blur_host_free_bytes\(/{fn=1} fn&&/^}/{exit}' \
    "$main_c" > "$out/free_cut.inc"
grep -q 'static long long blur_host_free_bytes(' "$out/free_cut.inc"
cc -std=gnu11 -Wall -Wextra -Werror "${san[@]}" -I"$out" -o "$out/test_title_free" "$here/test_title_free.c"
"$out/test_title_free"

# blur100-display-name.c (audit finding 4): the PS4 local name made portable,
# never the system keyboard.
cc -std=gnu11 -Wall -Wextra -Werror "${san[@]}" -I"$tree/prospero/native" \
   -o "$out/test_display_name" "$here/test_display_name.c" "$tree/prospero/native/blur100-display-name.c" \
   "$tree/prospero/native/native279-username.c"
"$out/test_display_name"

# pw_present.c (owner finding 34a): the same pixels as the plain definition in
# every mode (ASan/UBSan), and the launcher's 1:1 present within 2 ms (-O2).
cc -std=gnu11 -Wall -Wextra -Werror "${san[@]}" -I"$tree/prospero/src" \
   -o "$out/test_present_speed" "$here/test_present_speed.c" "$tree/prospero/src/pw_present.c"
"$out/test_present_speed"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -I"$tree/prospero/src" \
   -o "$out/bench_present_speed" "$here/test_present_speed.c" "$tree/prospero/src/pw_present.c"
"$out/bench_present_speed" bench

# Audit findings 6+10: the post-link clean's patch/proof logic, and in a built
# tree a title prx and eboot with no build-host path and no dev env-file name.
python3 -I "$here/../postlink_clean.py" selftest
if [ -f "$tree/reloc-build/prx/relocated_wine_app.prx" ]; then
    python3 -I "$here/../postlink_clean.py" scan "$tree/reloc-build/prx/relocated_wine_app.prx" \
        "$tree/reloc-build/eboot.bin"
    [ -f "$tree/reloc-build/prx/postlink-receipt.json" ]
fi

# No developer mode, no old root, the JkFlow name (source scan).
bash "$here/test_title_scan.sh" "$tree"
