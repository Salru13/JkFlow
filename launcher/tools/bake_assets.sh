#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
#
# Generate bl_font.h / bl_font_data.c (Nunito glyph atlases) and bl_logo.c
# (BLUR's own wordmark from the package icon, drawn by tools/make_art.c) into OUTDIR. Host tool; needs
# a C compiler, Python 3 and FreeType (headers via FREETYPE_INCLUDE, library
# via FREETYPE_LIB). Outputs are only rebuilt when an input changed.
set -euo pipefail
out="${1:?usage: bake_assets.sh OUTDIR}"
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mkdir -p "$out"

: "${FREETYPE_INCLUDE:=/usr/include/freetype2}"
[ -f "$FREETYPE_INCLUDE/ft2build.h" ] || { echo "bake_assets.sh: no ft2build.h in $FREETYPE_INCLUDE; set FREETYPE_INCLUDE to the FreeType headers (e.g. FreeType 2.13 src/include)" >&2; exit 2; }
: "${FREETYPE_LIB:=/usr/lib/x86_64-linux-gnu/libfreetype.so.6}"

fonts="$root/assets/font"
if [ ! -f "$out/bl_font_data.c" ] || [ "$here/bake_font.c" -nt "$out/bl_font_data.c" ] \
   || [ "$fonts/Nunito-VF.ttf" -nt "$out/bl_font_data.c" ] || [ "$fonts/Nunito-Italic-VF.ttf" -nt "$out/bl_font_data.c" ]; then
    cc -std=c11 -O2 -Wall -Wextra -I"$FREETYPE_INCLUDE" -o "$out/bake_font" "$here/bake_font.c" "$FREETYPE_LIB" -lm
    "$out/bake_font" "$fonts/Nunito-VF.ttf" "$fonts/Nunito-Italic-VF.ttf" "$out/bl_font.h" "$out/bl_font_data.c"
fi

icon="$root/assets/logo/blur-icon0.png"
if [ ! -f "$out/bl_logo.c" ] || [ "$here/bake_logo.py" -nt "$out/bl_logo.c" ] || [ "$icon" -nt "$out/bl_logo.c" ]; then
    python3 -I "$here/bake_logo.py" "$icon" "$out/bl_logo.c"
fi
