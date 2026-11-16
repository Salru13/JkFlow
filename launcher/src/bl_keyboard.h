/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The launcher's own on-screen keyboard, driven by the pad and drawn with
 * bl_ui (owner finding 12: the PS4 system keyboard opened in the top-left
 * corner and could not be typed in or confirmed).
 *
 * A 10-column grid: digits, three letter rows (or a symbols page), then
 * Shift, #+= / abc, Space, Del, Cancel and OK. D-pad moves (wraps), Cross
 * types the focused key, Square deletes, Triangle toggles Shift, L1/R1 move
 * the text cursor, Circle cancels, Options or R2 confirms; on a password L2
 * shows or hides the text (owner finding 39). ASCII only, at
 * most BL_KB_MAX_LEN bytes (the AMAX field limit).
 *
 * Integration (no own loop; the frontend runs it):
 *   - on BL_SET_EV_EDIT: bl_keyboard_init(&kb, title, secret, text, cap - 1)
 *     and switch the frontend to a keyboard mode;
 *   - each frame in that mode: draw the screen underneath (or keep it),
 *     then bl_keyboard_draw(frame, &kb); feed pad presses (with BL_KB_PAD_R2
 *     if the host reports R2) to bl_keyboard_input();
 *   - BL_KB_DONE: copy bl_keyboard_text() out (what osk() returned with 0);
 *     BL_KB_CANCEL: treat as osk() < 0 (the text is the initial text);
 *   - then bl_keyboard_wipe(&kb) (passwords) and leave keyboard mode;
 *     bl_keyboard_release() frees the cached grid image (about 3.5 MB).
 */
#ifndef BL_KEYBOARD_H
#define BL_KEYBOARD_H

#include <stddef.h>
#include <stdint.h>

#include "bl_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BL_KB_MAX_LEN 31
#define BL_KB_PAD_R2 0x200u           /* scePadRead R2 bit */
#define BL_KB_PAD_L2 0x100u           /* scePadRead L2 bit */

enum { BL_KB_EDITING = 0, BL_KB_DONE, BL_KB_CANCEL };
enum { BL_KB_ROWS = 5, BL_KB_COLS = 10 };

typedef struct BlKeyboard {
    char title[96];
    char text[BL_KB_MAX_LEN + 1];
    char initial[BL_KB_MAX_LEN + 1];
    int len, cursor, max_len;
    int secret, shift, symbols;
    int shown;                        /* secret only: L2 shows the text; hidden at every init */
    int row, col;                     /* focused cell; wide keys span cells */
    int result;
} BlKeyboard;

void bl_keyboard_init(BlKeyboard *kb, const char *title, int secret, const char *initial, int max_len);
/* pressed: newly pressed BL_PAD_* bits. Returns BL_KB_EDITING, BL_KB_DONE or BL_KB_CANCEL. */
int bl_keyboard_input(BlKeyboard *kb, uint32_t pressed);
/* The entered text; after BL_KB_CANCEL the initial text. */
const char *bl_keyboard_text(const BlKeyboard *kb);
/* The text as shown in the field: one bullet (U+2022) per byte when secret and not shown. */
void bl_keyboard_visible(const BlKeyboard *kb, char *out, size_t cap);
/* Label of the focused key ("a", "Shift", "OK" ...). */
const char *bl_keyboard_focus_label(const BlKeyboard *kb);
/* The button bar text; a password adds "L2 Show" or "L2 Hide". */
const char *bl_keyboard_hints(const BlKeyboard *kb);
void bl_keyboard_draw(BlFrame *f, const BlKeyboard *kb);
void bl_keyboard_wipe(BlKeyboard *kb);
void bl_keyboard_release(void);

#ifdef __cplusplus
}
#endif

#endif
