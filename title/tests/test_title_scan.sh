#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
# Source scan of a prepared BLUR 1.00 title tree (owner findings 16, 19, 30):
# the product title has no developer mode, no dev switch files, no live log
# channel, no old /data/BLUR root and no "JokerFlow".   test_title_scan.sh TREE
set -uo pipefail
tree="${1:?usage: test_title_scan.sh TREE}"
main="$tree/prospero/native/wine64_main.c"
diag="$tree/prospero/native/pw_diagnostics.c"
wow="$tree/orbis/src/native/pw_wow_log_ps4.c"
app="$tree/orbis/scripts/build-app-ps4.sh"
fail=0 n=0
no()  { n=$((n+1)); if grep -n -F -- "$2" "$1" >/dev/null; then echo "  FAIL $(basename "$1") still has: $2"; grep -n -F -- "$2" "$1" | head -3 | sed 's/^/    /'; fail=1; fi; }
yes() { n=$((n+1)); if ! grep -q -F -- "$2" "$1"; then echo "  FAIL $(basename "$1") lacks: $2"; fail=1; fi; }

# Every source the title compiles (build-app-ps4.sh lines 105-136), launcher included.
title_sources=("$tree"/prospero/src/*.c "$main" "$tree"/prospero/native/pw_data_mount.c "$diag"
    "$tree"/prospero/native/pw_wine_display.c "$tree"/prospero/native/pw_wine_library.c
    "$tree"/prospero/native/pw_wine_prefix.c "$tree"/prospero/native/native27*.c "$tree"/prospero/native/native28*.c "$tree"/prospero/native/blur100-*.c
    "$tree"/prospero/native/pw_osk_ps5.c "$tree"/prospero/native/native245_seed.c
    "$tree"/prospero/native/pw_pad_ps5.c "$tree"/prospero/native/pw_audio_ps5.c "$tree"/prospero/native/pw_hid_ps5.c
    "$tree"/orbis/src/native/pw_videoout_ps4.c "$tree"/orbis/src/native/pw_pad_ps4.c
    "$tree"/orbis/src/native/pw_audio_ps4.c "$tree"/orbis/src/native/pw_platform_bind_ps4.c
    "$wow" "$tree"/orbis/src/native/pw_osk_ps4.c "$tree"/orbis/src/native/pw_netctl_ps4.c)
# Code only: a comment that names a dev file is history, not a switch.
nocode() { n=$((n+1)); if grep -n -F -- "$2" "$1" | grep -v -E '^[0-9]+:[[:space:]]*(\*|/\*|//)' | grep -q .; then
    echo "  FAIL $(basename "$1") still reads: $2"; fail=1; fi; }
for f in "${title_sources[@]}"; do
    [ "$(basename "$f")" = pw_script_input.c ] && continue     # not compiled into the title (below)
    case "$(basename "$f")" in
        bl_*) # launcher sources belong to the launcher owner: reported, not failed here
            for s in blur-maintenance pw_remote_control /data/blurdev /data/prospero-win radv.env dev.conf \
                     .enable\" grab.req hang260 autopad AUTOTEST /data/BLUR JokerFlow; do
                grep -q -F -- "$s" "$f" && echo "  PENDING (launcher) $(basename "$f") has $s: $(grep -c -F -- "$s" "$f") lines"
            done ;;
        *) for s in blur-maintenance pw_remote_control /data/blurdev /data/prospero-win radv.env dev.conf \
                    .enable\" grab.req hang260 autopad AUTOTEST pw_script_keys pw_script_input; do
               nocode "$f" "$s"
           done
           no "$f" /data/BLUR; no "$f" JokerFlow ;;
    esac
done

# T7: no ps5log dev.conf probe and no live channel at all; ps5log.c is not built.
no "$main" "ps5log_load_config"
no "$main" "ps5log_init"
no "$app" 'native/ps5log/*.c'
no "$app" 'devconf'
yes "$app" 'native/ps5log/ps5log_ps5_net.c'
# The script/macro replay parser is not compiled into the title.
yes "$app" '[ "${f##*/}" = pw_script_input.c ] && continue'
# T6: no reconnect thread, nothing sent.
no "$diag" "retry_network"
no "$diag" "ps5log_reconnect"
no "$diag" "ps5log_line"
# T2 stick sampler, the script/autotest replay and the dev A/B switches: gone.
no "$main" "stick n="
no "$main" "PW_WINE64_SCRIPT"
no "$main" "script_input"
no "$main" "PW_WINE64_SECONDS"
no "$main" "PW_WINE64_WAIT_WATCHDOG"
no "$main" "PW_WINE64_HEAP_FLEXIBLE"
no "$main" "PW_WINE64_SERVER_DIRECT_OFF"
# T3: the low-memory poll only with Enable logs.
yes "$main" "if (blur_logs_verbose && tick % 30 == 0) {"
# T12 + finding 10: a guest exit records the session end, then goes back to the launcher.
n=$((n+1))
if ! grep -B2 -F 'restart_title(NULL, launch.cycle + 1u, "guest-exit");' "$main" | grep -q -F 'blur_session_end();'; then
    echo "  FAIL guest-exit does not call blur_session_end() first"; fail=1
fi
yes "$main" "sceSystemServiceLoadExec(eboots[i], next)"
# Findings 4+5: roots found at run time; session logs straight into Logs.
yes "$main" "blur_find_roots(\"/data\""
yes "$main" "pw_diagnostics_open_dir("
yes "$wow" 'getenv("BLUR_LOG_DIR")'
# Finding 13: ntdll's loader reports through the title.
yes "$main" '"pw_wine_dl_configure"'
# Finding 16: the visible name.
yes "$tree/prospero/native/pw_osk_ps5.c" '"JkFlow"'
yes "$tree/prospero/src/pw_game_menu.c" '"JkFlow"'
yes "$main" 'E110 JkFlow could not prepare'
# Review finding 1: old data that could not move stops the title on its own
# screen before the prefix import (the first thing blur_launcher_screen does).
n=$((n+1))
first="$(awk '/^static void blur_launcher_screen\(/{on=1} on && /blur_migrate_blocked|blur_prepare_prefix\(/{print; exit}' "$main")"
if [[ $first != *blur_migrate_blocked* ]]; then
    echo "  FAIL blur_launcher_screen does not stop on blur_migrate_blocked before blur_prepare_prefix"; fail=1
fi
yes "$main" 'blur_stop_screen(blur_migrate_text,'
# Owner decision 2026-11-16: a /data/BLUR holding Blur.exe is a game: JkFlow/BLUR, never System.
yes "$main" 'moved = blur_migrate_game(old, jkflow) ? 0 : bl_paths_migrate(old, &paths, blur_migrate_log, NULL);'
# Review finding 3: an env line that does not fit stops the start (fail closed).
yes "$main" 'blur_apply_decision(extra, &config.extra_env_count,'
n=$((n+1))
if ! grep -A3 -F 'blur_apply_decision(extra, &config.extra_env_count,' "$main" | grep -q 'blur_env_refused()'; then
    echo "  FAIL the start does not stop when blur_apply_decision fails"; fail=1
fi
yes "$main" 'blur_stop_screen(bl_str(BL_S_TOO_MANY_SETTINGS),'
no "$main" 'launcher env dropped='
# Audit finding 4: the local PS4 name is made portable; no system keyboard and
# no endless wait for a player name, and the setup page is not compiled at all.
yes "$main" 'pw_blur100_display_name_from_orbis(&blur_display_name, &name_sanitized)'
yes "$main" 'display_name source=%s sanitized=%d bytes=%u'
yes "$app" '"$PW"/native/blur100-display-name.c "$PW"/native/blur100-display-name-orbis.c'
no "$main" 'direct_player_name_setup'
no "$main" 'pw_blur_name_setup_'
no "$main" 'native280-username-ui.h'
no "$main" 'for(;;)usleep('
no "$main" 'saved-or-entered'
no "$app" 'native280-username-ui'
no "$app" 'native279-username-orbis.c'
no "$tree/prospero/src/pw_launcher_render.c" 'pw_blur_name_setup_render'
# Owner option A (2026-11-17): no Blur logo bitmap in the title; the uncalled
# start page that drew it is gone, and no title source includes the bitmap.
no "$tree/prospero/src/pw_launcher_render.c" 'pw_blur_start_render'
no "$tree/prospero/src/pw_launcher_render.c" 'blur_logo('
for f in "${title_sources[@]}" "$tree"/prospero/src/*.h "$tree"/prospero/native/*.h; do
    [ "$(basename "$f")" = pw_blur_logo.h ] && continue
    no "$f" 'pw_blur_logo'
done

# Audit finding 23 (text only): the line about the console's own message is on
# the two screens whose Close ends in leave_to_home (E110, old data blocked)
# and nowhere else (not the env-overflow screen, which goes back to the launcher).
yes "$main" "#define BLUR_HOME_NOTE \"The PS4 may then show 'Something went wrong'. That is expected here; start JkFlow again.\""
n=$((n+1))
if ! grep -A1 -F '"E110 JkFlow could not prepare its files. Cross: try again   Circle: close");' "$main" | grep -q -F 'blur_home_note(&draw.frame, 0);'; then
    echo "  FAIL the E110 screen lacks the home note"; fail=1
fi
yes "$main" 'if (then_home) blur_home_note(&frame, below + 12);'
yes "$main" 'blur_stop_screen(blur_migrate_text, "blur-migrate-blocked", 1);'
yes "$main" 'blur_stop_screen(bl_str(BL_S_TOO_MANY_SETTINGS), "blur-env-overflow", 0);'
n=$((n+1))
if [ "$(grep -c 'blur_home_note(' "$main")" != 3 ] || [ "$(grep -c 'blur_stop_screen(' "$main")" != 3 ]; then
    echo "  FAIL the home note is used on other screens too"; fail=1
fi

echo "test_title_scan: $n checks, $([ $fail = 0 ] && echo 0 || echo some) failed"
exit $fail
