/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* "patch" screen (BLUR 1.00 plan Task L9): the nine Patch steps with their
 * state, one button, progress and the failure (step, file, code). */
#include "bl_screens.h"
#include "bl_exe.h"

#include <stdio.h>
#include <string.h>

BlPatchView bl_patch_last_drawn;

BlPatchEvent bl_patch_input(BlPatchView *v, uint32_t pressed)
{
    if (v->busy || !pressed) return BL_PATCH_EV_NONE;
    if (pressed & BL_PAD_TRIANGLE) {
        if (v->confirm_restore) { v->confirm_restore = 0; return BL_PATCH_EV_RESTORE; }
        v->confirm_restore = 1;
        v->message = BL_S_PATCH_CONFIRM_RESTORE;
        v->message_text[0] = 0;
        return BL_PATCH_EV_NONE;
    }
    if (v->confirm_restore) {                  /* any other button cancels */
        v->confirm_restore = 0;
        v->message = -1;
        return BL_PATCH_EV_NONE;
    }
    if (pressed & BL_PAD_CIRCLE) return BL_PATCH_EV_BACK;
    if (pressed & BL_PAD_SQUARE) return BL_PATCH_EV_HELP;
    if (pressed & BL_PAD_UP) v->choice = 0;
    if (pressed & BL_PAD_DOWN) v->choice = 1;
    if (pressed & BL_PAD_CROSS) return BL_PATCH_EV_APPLY;
    return BL_PATCH_EV_NONE;
}

/* Panels (fixed). */
enum { LX = 150, LY = 290, LW = 1000, LH = 560, RX = 1190, RY = 290, RW = 580, RH = 560 };

/* Square says what it opens (audit 24): the online files after an online
 * choice or stop, else the Blur folder (bl_frontend opens Help there). */
static const BlHint HINTS[2][5] = {
    { { BL_KEY_UPDOWN, BL_S_HINT_CHOOSE }, { BL_KEY_CROSS, BL_S_HINT_PATCH }, { BL_KEY_SQUARE, BL_S_HINT_HELP_FOLDER },
      { BL_KEY_TRIANGLE, BL_S_HINT_RESTORE }, { BL_KEY_CIRCLE, BL_S_HINT_BACK } },
    { { BL_KEY_UPDOWN, BL_S_HINT_CHOOSE }, { BL_KEY_CROSS, BL_S_HINT_PATCH }, { BL_KEY_SQUARE, BL_S_HINT_HELP_ONLINE },
      { BL_KEY_TRIANGLE, BL_S_HINT_RESTORE }, { BL_KEY_CIRCLE, BL_S_HINT_BACK } },
};
static int help_online(const BlPatchView *v) { return v->choice || v->help_line; }

/* The parts that never change, in drawing order: up to the left panel ... */
static void static_left(BlFrame *f)
{
    bl_screen_background(f, BL_BG_LIST);
    bl_fill_alpha(f, 0, 0, (int)f->width, (int)f->height, 0x05080F, 60);
    bl_top_bar_frame(f, BL_ICON_WRENCH);
    bl_pill_header(f, BL_ICON_WRENCH, bl_str(BL_S_PATCH_TITLE));
    bl_glass(f, LX, LY, LW, LH, 24, 0);
}

/* ... the right panel and its title ... */
static void static_right(BlFrame *f)
{
    bl_glass(f, RX, RY, RW, RH, 24, 0);
    bl_panel_title(f, RX + 40, RY + 30, bl_str(BL_S_PATCH_PROGRESS));
}

/* ... and the band and hints. */
static void static_bottom(BlFrame *f, int online)
{
    bl_band(f, bl_str(BL_S_BAND_PATCH_SCREEN));
    bl_hints(f, HINTS[online != 0], 5);
}

/* Left panel text: the steps. Returns its right edge (f NULL: measure only). */
static int left_text(BlFrame *f, const BlPatchView *v)
{
    int y = LY + 28, right = 0;
    char none[200];
    snprintf(none, sizeof none, bl_str(BL_S_PATCH_EXE_NONE), v->game_dir[0] ? v->game_dir : "/data/JkFlow/Blur");
    const char *exe = v->exe_verdict == BL_EXE_TESTED ? bl_str(BL_S_PATCH_EXE_TESTED)
                    : v->exe_verdict == BL_EXE_UNSUPPORTED ? bl_str(BL_S_PATCH_EXE_OTHER) : none;
    y = bl_note(f, LX + 44, y, LW - 88, exe, v->exe_verdict == BL_EXE_TESTED ? BL_RGB_CYAN : BL_RGB_AMBER, BL_FONT_NOTE) + 8;
    right = LX + 44 + LW - 88;
    for (int i = 0; i < BL_PATCH_STEPS; i++) {
        static const uint32_t COLOUR[] = { 0x8090A8, BL_RGB_WHITE, BL_RGB_CYAN, BL_RGB_ERROR };
        static const char *const MARK[] = { "\xC2\xB7", "\xE2\x80\xBA", "\xE2\x80\xA2", "\xC3\x97" };   /* in the baked font */
        BlStepState s = v->steps[i] <= BL_STEP_FAIL ? v->steps[i] : BL_STEP_WAIT;
        char line[160];
        snprintf(line, sizeof line, "%d  %s", i + 1, bl_str((BlStringId)(BL_S_STEP_1 + i)));
        bl_text(f, BL_FONT_BODY, LX + 44, y, MARK[s], COLOUR[s]);
        int w = bl_text(f, BL_FONT_BODY, LX + 90, y, line, COLOUR[s]);
        if (LX + 90 + w > right) right = LX + 90 + w;
        y += bl_font_line_height(BL_FONT_BODY) - 8;
    }
    return right;
}

/* "Press [Square] Help for step-by-step instructions.": the Square key cap
 * the hint bar draws (the font has no square glyph) where {square} stands,
 * the words in cyan BODY type, wrapped to w. Returns the y below it. */
static int help_line(BlFrame *f, int x, int y, int w)
{
    return bl_note_keys(f, x, y, w, bl_str(BL_S_PATCH_HELP_MORE), BL_RGB_CYAN, BL_FONT_BODY);
}

void bl_patch_status_line(const BlPatchView *v, char *out, size_t cap)
{
    if (!cap) return;
    out[0] = 0;
    if (v->busy && v->downloading && v->done == 0) snprintf(out, cap, "%s", bl_str(BL_S_PATCH_CONNECTING));   /* audit 17 */
    else if (v->busy) snprintf(out, cap, bl_str(v->downloading ? BL_S_PATCH_DOWNLOADING : BL_S_PATCH_COPYING), v->progress_name);
    else if (v->message_text[0]) snprintf(out, cap, "%s", v->message_text);
    else if (v->message >= 0) snprintf(out, cap, "%s", bl_str((BlStringId)v->message));
}

/* Right panel: progress, message, the two choices. Returns the y below the
 * text (f NULL: measure only). */
static int right_dynamic(BlFrame *f, const BlPatchView *v)
{
    int y = RY + 30 + bl_font_line_height(BL_FONT_HEAD) + 4, bottom;
    char line[400];
    bl_patch_status_line(v, line, sizeof line);
    const char *msg = line;
    int failed = 0;
    for (int i = 0; i < BL_PATCH_STEPS; i++) failed |= v->steps[i] == BL_STEP_FAIL;
    int bar_w = RW - 80;
    if (msg[0] || v->total) {
        /* {square} and {cross} as key caps (audit 8 download results). */
        y = (strchr(msg, '{') ? bl_note_keys : bl_note)(f, RX + 40, y, RW - 80, msg, failed ? BL_RGB_AMBER : BL_RGB_WHITE, BL_FONT_NOTE);
        bottom = y + 8 + 16 + 1;
        if (f) bl_round_rect(f, RX + 40, y + 8, bar_w, 16, 8, 0x050F28, 0x050F28, 210, BL_RGB_RIM, 2, 0, 0);
        if (v->help_line && !v->busy && v->message_text[0] && !strstr(v->message_text, "{square}")) bottom = help_line(f, RX + 40, bottom + 14, RW - 80);
        if (f && v->total) {
            unsigned long long done = v->done > v->total ? v->total : v->done;
            int fill = (int)((unsigned long long)(bar_w - 4) * done / v->total);
            if (fill > 0) bl_round_rect(f, RX + 42, y + 10, fill, 12, 6, BL_RGB_RIM, BL_RGB_CYAN, 255, BL_RGB_CYAN, 0, BL_RGB_CYAN, 10);
        }
    } else {                                    /* idle: what the focused choice does */
        int note = v->choice ? BL_S_PATCH_ONLINE_NOTE : v->online != BL_ONLINE_NONE ? BL_S_PATCH_SWITCH_OFFLINE : BL_S_PATCH_OFFLINE_NOTE;
        bottom = bl_note(f, RX + 40, y, RW - 80, bl_str((BlStringId)note), 0xA9BBDC, BL_FONT_NOTE);
        if (v->choice)                          /* where the files go: Help (owner finding 17) */
            bottom = bl_note(f, RX + 40, bottom + 8, RW - 80, bl_str(BL_S_PATCH_ONLINE_HELP), BL_RGB_WHITE, BL_FONT_NOTE);
    }
    /* The two choices. */
    if (f) {
        bl_button(f, RX + 40, RY + RH - 200, bl_str(BL_S_PATCH_OFFLINE), !v->busy && v->choice == 0);
        bl_button(f, RX + 40, RY + RH - 110, bl_str(BL_S_PATCH_ONLINE), !v->busy && v->choice == 1);
    }
    return bottom;
}

void bl_patch_draw(BlFrame *f, const BlPatchView *v)
{
    bl_patch_last_drawn = *v;
    bl_frame_key_set(f, bl_hash_bytes(bl_layer_key("patch", 0), v, sizeof *v));
    /* The left text is drawn before the right panel: the cached way needs
     * it clear of that panel (with its glow); every text stays above the
     * band. Otherwise everything is drawn in order, as before. */
    int clear = left_text(NULL, v) < RX - 36 - 6 && right_dynamic(NULL, v) < 872 - 30;
    uint64_t key = bl_layer_key("patch", (uint64_t)help_online(v));
    if (clear && bl_layer_restore(f, key)) {
        left_text(f, v);
        right_dynamic(f, v);
        return;
    }
    static_left(f);
    if (clear) {
        static_right(f);
        static_bottom(f, help_online(v));
        bl_layer_save(f, key);
        left_text(f, v);
        right_dynamic(f, v);
        return;
    }
    left_text(f, v);
    static_right(f);
    right_dynamic(f, v);
    static_bottom(f, help_online(v));
}
