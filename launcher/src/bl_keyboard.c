/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The launcher's own on-screen keyboard (see bl_keyboard.h). */
#include "bl_keyboard.h"

#include "bl_frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cells: printable ASCII, or one of the special keys below (wide keys repeat). */
enum { K_SHIFT = 1, K_PAGE, K_SPACE, K_DEL, K_CANCEL, K_OK };

static const char letters[4][BL_KB_COLS + 1] = { "1234567890", "qwertyuiop", "asdfghjkl@", "zxcvbnm._-" };
static const char symbols[4][BL_KB_COLS + 1] = { "1234567890", "!#$%&*+=?@", ".-_~^()[]/", ":;,'\"<>{}|" };
static const char bottom[BL_KB_COLS] = { K_SHIFT, K_SHIFT, K_PAGE, K_PAGE, K_SPACE, K_SPACE,
                                         K_DEL, K_DEL, K_CANCEL, K_OK };

/* Panel geometry (pixels, relative to the panel). */
enum {
    PW = 1280, PH = 700,
    KX = 46, KY = 214, KW = 108, KH = 70, KPX = 120, KPY = 82,
    FX = 46, FY = 104, FW = PW - 92, FH = 72
};

static int cell(const BlKeyboard *kb, int r, int c)
{
    if (r == BL_KB_ROWS - 1) return bottom[c];
    char ch = (kb->symbols ? symbols : letters)[r][c];
    if (kb->shift && !kb->symbols && ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    return (unsigned char)ch;
}

static void span(int r, int c, int *first, int *last)
{
    *first = *last = c;
    if (r != BL_KB_ROWS - 1) return;
    while (*first > 0 && bottom[*first - 1] == bottom[c]) (*first)--;
    while (*last < BL_KB_COLS - 1 && bottom[*last + 1] == bottom[c]) (*last)++;
}

static const char *label(const BlKeyboard *kb, int key)
{
    static char one[8][2];
    static int next;
    switch (key) {
    case K_SHIFT: return "Shift";
    case K_PAGE: return kb->symbols ? "abc" : "#+=";
    case K_SPACE: return "Space";
    case K_DEL: return "Del";
    case K_CANCEL: return "Cancel";
    case K_OK: return "OK";
    default:
        next = (next + 1) & 7;
        one[next][0] = (char)key; one[next][1] = 0;
        return one[next];
    }
}

static int printable(int ch) { return ch >= 0x20 && ch < 0x7F; }

void bl_keyboard_init(BlKeyboard *kb, const char *title, int secret, const char *initial, int max_len)
{
    memset(kb, 0, sizeof *kb);
    snprintf(kb->title, sizeof kb->title, "%s", title ? title : "");
    kb->max_len = max_len < 0 ? 0 : max_len > BL_KB_MAX_LEN ? BL_KB_MAX_LEN : max_len;
    for (const char *p = initial ? initial : ""; *p && kb->len < kb->max_len; p++)
        if (printable((unsigned char)*p)) kb->text[kb->len++] = *p;
    memcpy(kb->initial, kb->text, sizeof kb->initial);
    kb->cursor = kb->len;
    kb->secret = secret != 0;
    kb->result = BL_KB_EDITING;
}

static void insert(BlKeyboard *kb, char ch)
{
    if (kb->len >= kb->max_len) return;
    memmove(kb->text + kb->cursor + 1, kb->text + kb->cursor, (size_t)(kb->len - kb->cursor + 1));
    kb->text[kb->cursor++] = ch;
    kb->len++;
}

static void backspace(BlKeyboard *kb)
{
    if (kb->cursor == 0) return;
    memmove(kb->text + kb->cursor - 1, kb->text + kb->cursor, (size_t)(kb->len - kb->cursor + 1));
    kb->cursor--;
    kb->len--;
}

static int finish(BlKeyboard *kb, int result)
{
    if (result == BL_KB_CANCEL) {
        memcpy(kb->text, kb->initial, sizeof kb->text);
        kb->len = kb->cursor = (int)strlen(kb->text);
    }
    return kb->result = result;
}

static int press_key(BlKeyboard *kb)
{
    int key = cell(kb, kb->row, kb->col);
    switch (key) {
    case K_SHIFT: kb->shift = !kb->shift; kb->symbols = 0; break;
    case K_PAGE: kb->symbols = !kb->symbols; break;
    case K_SPACE: insert(kb, ' '); break;
    case K_DEL: backspace(kb); break;
    case K_CANCEL: return finish(kb, BL_KB_CANCEL);
    case K_OK: return finish(kb, BL_KB_DONE);
    default: insert(kb, (char)key); break;
    }
    return BL_KB_EDITING;
}

int bl_keyboard_input(BlKeyboard *kb, uint32_t pressed)
{
    if (kb->result != BL_KB_EDITING) return kb->result;
    if (pressed & BL_PAD_CIRCLE) return finish(kb, BL_KB_CANCEL);
    if (pressed & (BL_PAD_OPTIONS | BL_KB_PAD_R2)) return finish(kb, BL_KB_DONE);
    int first, last;
    if (pressed & BL_PAD_UP) kb->row = (kb->row + BL_KB_ROWS - 1) % BL_KB_ROWS;
    if (pressed & BL_PAD_DOWN) kb->row = (kb->row + 1) % BL_KB_ROWS;
    if (pressed & BL_PAD_LEFT) {
        span(kb->row, kb->col, &first, &last);
        kb->col = (first + BL_KB_COLS - 1) % BL_KB_COLS;
        span(kb->row, kb->col, &first, &last);
        kb->col = first;
    }
    if (pressed & BL_PAD_RIGHT) {
        span(kb->row, kb->col, &first, &last);
        kb->col = (last + 1) % BL_KB_COLS;
    }
    if (pressed & BL_PAD_L1 && kb->cursor > 0) kb->cursor--;
    if (pressed & BL_PAD_R1 && kb->cursor < kb->len) kb->cursor++;
    if (pressed & BL_PAD_TRIANGLE) { kb->shift = !kb->shift; kb->symbols = 0; }
    if (pressed & BL_KB_PAD_L2 && kb->secret) kb->shown = !kb->shown;
    if (pressed & BL_PAD_SQUARE) backspace(kb);
    if (pressed & BL_PAD_CROSS) return press_key(kb);
    return BL_KB_EDITING;
}

const char *bl_keyboard_text(const BlKeyboard *kb) { return kb->text; }

void bl_keyboard_visible(const BlKeyboard *kb, char *out, size_t cap)
{
    if (!cap) return;
    if (!kb->secret || kb->shown) { snprintf(out, cap, "%s", kb->text); return; }
    size_t n = 0;
    for (int i = 0; i < kb->len && n + 3 < cap; i++) { memcpy(out + n, "\xE2\x80\xA2", 3); n += 3; }
    out[n] = 0;
}

const char *bl_keyboard_focus_label(const BlKeyboard *kb) { return label(kb, cell(kb, kb->row, kb->col)); }

void bl_keyboard_wipe(BlKeyboard *kb)
{
    volatile char *p = (volatile char *)kb;
    for (size_t i = 0; i < sizeof *kb; i++) p[i] = 0;
}

/* ---------------- drawing ---------------- */

/* The static panel (backdrop, title, empty field, every key cap unfocused,
 * hints) for one page/shift state, packed PW x PH BGRX. */
static uint8_t *cache;
static int cache_state = -1;
static char cache_title[sizeof ((BlKeyboard *)0)->title];

void bl_keyboard_release(void)
{
    free(cache);
    cache = NULL;
    cache_state = -1;
}

static void key_rect(int r, int c, int *x, int *y, int *w)
{
    int first, last;
    span(r, c, &first, &last);
    *x = KX + first * KPX;
    *y = KY + r * KPY;
    *w = (last - first) * KPX + KW;
}

static void draw_key(BlFrame *f, const BlKeyboard *kb, int ox, int oy, int r, int c, int focused)
{
    int x, y, w, key = cell(kb, r, c);
    key_rect(r, c, &x, &y, &w);
    x += ox; y += oy;
    if (focused)
        bl_round_rect(f, x, y, w, KH, 12, 0x2A8CFF, 0x0F4FB8, 255, BL_RGB_CYAN, 3, BL_RGB_CYAN, 14);
    else
        bl_round_rect(f, x, y, w, KH, 12, 0x17407E, 0x0C2452, 235, 0x2F6FD0, 2, 0, 0);
    BlFont font = key < 0x20 ? BL_FONT_BODY : BL_FONT_ITEM;
    const char *s = label(kb, key);
    uint32_t rgb = key == K_SHIFT && kb->shift ? BL_RGB_AMBER
                 : key == K_OK ? BL_RGB_CYAN : BL_RGB_WHITE;
    if (focused && key != K_SHIFT) rgb = BL_RGB_WHITE;
    bl_text(f, font, x + (w - bl_text_width(font, s)) / 2, y + (KH - bl_font_line_height(font)) / 2, s, rgb);
}

const char *bl_keyboard_hints(const BlKeyboard *kb)
{
    if (!kb->secret) return "Cross type    Square delete    Triangle shift    L1/R1 cursor    Circle cancel    Options OK";
    return kb->shown ? "Cross type    Square delete    Triangle shift    L1/R1 cursor    L2 Hide    Circle cancel    Options OK"
                     : "Cross type    Square delete    Triangle shift    L1/R1 cursor    L2 Show    Circle cancel    Options OK";
}

static void draw_static(BlFrame *f, const BlKeyboard *kb)
{
    bl_fill(f, 0, 0, PW, PH, 0x02060F);
    bl_glass(f, 10, 10, PW - 20, PH - 20, 24, 0);
    bl_text(f, BL_FONT_HEAD, FX, 36, kb->title, BL_RGB_CYAN);
    bl_round_rect(f, FX, FY, FW, FH, 12, 0x030A1E, 0x030A1E, 230, 0x4696FF, 2, 0, 0);
    for (int r = 0; r < BL_KB_ROWS; r++)
        for (int c = 0; c < BL_KB_COLS; c++) {
            int first, last;
            span(r, c, &first, &last);
            if (first == c) draw_key(f, kb, 0, 0, r, c, 0);
        }
    const char *hints = bl_keyboard_hints(kb);
    bl_text(f, BL_FONT_NOTE, (PW - bl_text_width(BL_FONT_NOTE, hints)) / 2, PH - 62, hints, 0xA9BBDC);
}

void bl_keyboard_draw(BlFrame *f, const BlKeyboard *kb)
{
    const int ox = ((int)f->width - PW) / 2, oy = ((int)f->height - PH) / 2;
    int state = kb->symbols * 2 + kb->shift + (kb->secret ? 4 + 4 * kb->shown : 0);   /* the hints differ too */
    if (!cache) {
        cache = malloc((size_t)PW * PH * 4);
        cache_state = -1;
    }
    if (!cache) return;
    if (state != cache_state || strcmp(cache_title, kb->title)) {
        BlFrame c = { cache, PW, PH, PW * 4 };
        draw_static(&c, kb);
        cache_state = state;
        snprintf(cache_title, sizeof cache_title, "%s", kb->title);
    }
    /* Blit the panel, clipped to the frame. */
    int x0 = ox < 0 ? -ox : 0, y0 = oy < 0 ? -oy : 0;
    int x1 = ox + PW > (int)f->width ? (int)f->width - ox : PW;
    int y1 = oy + PH > (int)f->height ? (int)f->height - oy : PH;
    if (x1 > x0)
        for (int y = y0; y < y1; y++)
            memcpy(f->pixels + (size_t)(oy + y) * f->stride + (size_t)(ox + x0) * 4,
                   cache + ((size_t)y * PW + (size_t)x0) * 4, (size_t)(x1 - x0) * 4);

    draw_key(f, kb, ox, oy, kb->row, kb->col, 1);

    char vis[3 * BL_KB_MAX_LEN + 1], before[3 * BL_KB_MAX_LEN + 1];
    bl_keyboard_visible(kb, vis, sizeof vis);
    size_t cut = (size_t)kb->cursor * (kb->secret && !kb->shown ? 3u : 1u);
    memcpy(before, vis, cut);
    before[cut] = 0;
    const int tx = ox + FX + 20, ty = oy + FY + (FH - bl_font_line_height(BL_FONT_ITEM)) / 2;
    bl_text(f, BL_FONT_ITEM, tx, ty, vis, BL_RGB_WHITE);
    bl_fill(f, tx + bl_text_width(BL_FONT_ITEM, before) + 1, oy + FY + 14, 3, FH - 28, BL_RGB_CYAN);
    char count[16];
    snprintf(count, sizeof count, "%d/%d", kb->len, kb->max_len);
    bl_text(f, BL_FONT_NOTE, ox + FX + FW - 20 - bl_text_width(BL_FONT_NOTE, count),
            oy + FY + (FH - bl_font_line_height(BL_FONT_NOTE)) / 2, count, 0x6F80A8);
}
