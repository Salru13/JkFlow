#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# The AMAX account plugin (owner finding 14, 2026-11-15): the login form
# showed the launcher's account only one start late, because Blur's profile
# load from blursave overwrote the values the plugin wrote once. The plugin
# must stop only after its values survived a later screen change, run after
# a new screen too (Post_GoScreen / Post_PushScreen), keep the 31-byte limit
# and never print or log anything. No Lua on the build host: the rules are
# checked in the source text.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
f="$root/assets/blur-files/amax-overlay/plugins/account/account.lua"
status=0
failed=0
fail() { echo "FAIL test_account_plugin: $1"; status=1; failed=$((failed + 1)); }
checks=0
need() { checks=$((checks + 1)); grep -qE "$1" "$f" || fail "$2"; }
none() { checks=$((checks + 1)); if grep -nE "$1" "$f"; then fail "$2"; fi; }

need '^function M\.GoScreen\(' "GoScreen hook"
need '^function M\.PushScreen\(' "PushScreen hook"
need '^function M\.Post_GoScreen\(' "Post_GoScreen hook (after the new screen)"
need '^function M\.Post_PushScreen\(' "Post_PushScreen hook (after the new screen)"
need '^local held = 0' "counts hooks with the values in place"
# done only when the values match AND a write of ours came before.
need 'if held >= 3 then done = true end' "latches only after the values survived a screen change"
need '^  held = 0 ' "a missing or overwritten value is written again"
none '^[[:space:]]*done = true[[:space:]]*$' "an unconditional latch (the one-start-late bug)"
need '#u > 31 or #p > 31' "31-byte limit"
# Owner finding 27: the password must not stay in plain text in the game
# folder. Once the values are read (the module stays loaded), the file is
# deleted; package.searchpath finds it the way require did. The launcher
# deletes it at its next start when this could not.
need 'package\.searchpath\("account_cfg", package\.path\)' "finds account_cfg.lua the way require does"
need 'os\.remove\(' "deletes account_cfg.lua after reading it"
none '^[^-]*os\.remove\([^)]*\.\.' "builds the path to delete by hand"
none '(^|[^_[:alnum:]])(print|print_api|io\.write|io\.open|os\.execute|log)[[:space:]]*\(' "prints or logs (secrets)"
echo "test_account_plugin: $checks checks, $failed failed"
exit $status
