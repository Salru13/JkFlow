/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests for the launcher's own on-screen keyboard (owner finding 12). */
#define _DEFAULT_SOURCE
#include "bl_frontend.h"
#include "bl_keyboard.h"
#include "bl_png.h"
#include "bl_sha256.h"
#include "bl_test.h"

#include <stdlib.h>
#include <string.h>

#define U BL_PAD_UP
#define D BL_PAD_DOWN
#define L BL_PAD_LEFT
#define R BL_PAD_RIGHT
#define X BL_PAD_CROSS
#define SQ BL_PAD_SQUARE
#define TRI BL_PAD_TRIANGLE
#define O BL_PAD_CIRCLE

static int press(BlKeyboard *kb, uint32_t b, int times)
{
    int r = BL_KB_EDITING;
    for (int i = 0; i < times; i++) r = bl_keyboard_input(kb, b);
    return r;
}

/* Move to the key with this label from the top-left (letters/symbols page). */
static int go(BlKeyboard *kb, const char *label)
{
    for (int r = 0; r < BL_KB_ROWS; r++) {
        for (int c = 0; c < BL_KB_COLS; c++) {
            if (!strcmp(bl_keyboard_focus_label(kb), label)) return 1;
            bl_keyboard_input(kb, R);
        }
        bl_keyboard_input(kb, D);
    }
    return !strcmp(bl_keyboard_focus_label(kb), label);
}

static void type(BlKeyboard *kb, const char *s)
{
    for (; *s; s++) {
        char l[2] = { *s, 0 };
        CHECK(go(kb, l));
        bl_keyboard_input(kb, X);
    }
}

static void test_start_and_wrap(void)
{
    BlKeyboard kb;
    bl_keyboard_init(&kb, "User name", 0, "", 31);
    CHECK(!strcmp(bl_keyboard_focus_label(&kb), "1"));
    press(&kb, L, 1);
    CHECK(!strcmp(bl_keyboard_focus_label(&kb), "0"));          /* wraps left to right */
    press(&kb, R, 1);
    CHECK(!strcmp(bl_keyboard_focus_label(&kb), "1"));
    press(&kb, U, 1);
    CHECK(!strcmp(bl_keyboard_focus_label(&kb), "Shift"));      /* wraps top to bottom row */
    press(&kb, D, 1);
    CHECK(!strcmp(bl_keyboard_focus_label(&kb), "1"));
    press(&kb, D, 1);
    CHECK(!strcmp(bl_keyboard_focus_label(&kb), "q"));
    press(&kb, D, 1);
    CHECK(!strcmp(bl_keyboard_focus_label(&kb), "a"));
    /* Bottom row: wide keys are one stop each. */
    press(&kb, D, 2);
    CHECK(!strcmp(bl_keyboard_focus_label(&kb), "Shift"));
    const char *want[] = { "#+=", "Space", "Del", "Cancel", "OK", "Shift" };
    for (int i = 0; i < 6; i++) {
        press(&kb, R, 1);
        CHECK(!strcmp(bl_keyboard_focus_label(&kb), want[i]));
    }
    press(&kb, L, 1);
    CHECK(!strcmp(bl_keyboard_focus_label(&kb), "OK"));
    press(&kb, U, 1);
    CHECK(!strcmp(bl_keyboard_focus_label(&kb), "-"));          /* column kept */
}

static void test_typing_and_ok(void)
{
    BlKeyboard kb;
    bl_keyboard_init(&kb, "User name", 0, "", 31);
    type(&kb, "joker13");
    CHECK(!strcmp(bl_keyboard_text(&kb), "joker13"));
    CHECK(go(&kb, "OK"));
    CHECK_EQ(bl_keyboard_input(&kb, X), BL_KB_DONE);
    CHECK(!strcmp(bl_keyboard_text(&kb), "joker13"));
    bl_keyboard_init(&kb, "User name", 0, "abc", 31);
    CHECK_EQ(bl_keyboard_input(&kb, BL_PAD_OPTIONS), BL_KB_DONE);
    CHECK(!strcmp(bl_keyboard_text(&kb), "abc"));
    bl_keyboard_init(&kb, "User name", 0, "abc", 31);
    CHECK_EQ(bl_keyboard_input(&kb, BL_KB_PAD_R2), BL_KB_DONE);
}

static void test_shift_symbols_space(void)
{
    BlKeyboard kb;
    bl_keyboard_init(&kb, "Password", 1, "", 31);
    press(&kb, TRI, 1);
    CHECK(!strcmp(bl_keyboard_focus_label(&kb), "1"));
    type(&kb, "AB");
    press(&kb, TRI, 1);
    type(&kb, "c");
    CHECK(go(&kb, "Shift")); bl_keyboard_input(&kb, X);       /* Shift key = Triangle */
    type(&kb, "D");
    bl_keyboard_input(&kb, TRI);
    CHECK(go(&kb, "#+=")); bl_keyboard_input(&kb, X);
    CHECK(!strcmp(bl_keyboard_focus_label(&kb), "abc"));
    type(&kb, "!#$%&*+=?");
    CHECK(go(&kb, "abc")); bl_keyboard_input(&kb, X);
    CHECK(go(&kb, "Space")); bl_keyboard_input(&kb, X);
    type(&kb, "@._-");
    CHECK(!strcmp(bl_keyboard_text(&kb), "ABcD!#$%&*+=? @._-"));
}

static void test_backspace_cursor_and_max(void)
{
    BlKeyboard kb;
    bl_keyboard_init(&kb, "User name", 0, "abcd", 31);
    press(&kb, SQ, 1);
    CHECK(!strcmp(bl_keyboard_text(&kb), "abc"));
    press(&kb, BL_PAD_L1, 2);                                    /* cursor after "a" */
    press(&kb, SQ, 1);
    CHECK(!strcmp(bl_keyboard_text(&kb), "bc"));
    press(&kb, SQ, 3);                                           /* nothing before the cursor */
    CHECK(!strcmp(bl_keyboard_text(&kb), "bc"));
    type(&kb, "x");
    CHECK(!strcmp(bl_keyboard_text(&kb), "xbc"));
    press(&kb, BL_PAD_R1, 5);
    CHECK(go(&kb, "Del")); bl_keyboard_input(&kb, X);
    CHECK(!strcmp(bl_keyboard_text(&kb), "xb"));
    /* Max length: 31 bytes even if the caller asks for more. */
    bl_keyboard_init(&kb, "User name", 0, "", 255);
    CHECK(go(&kb, "a"));
    press(&kb, X, 40);
    CHECK_EQ(strlen(bl_keyboard_text(&kb)), BL_KB_MAX_LEN);
    bl_keyboard_init(&kb, "Port", 0, "", 5);
    CHECK(go(&kb, "1"));
    press(&kb, X, 9);
    CHECK(!strcmp(bl_keyboard_text(&kb), "11111"));
    /* Initial text: longer than the limit and non-ASCII bytes are dropped. */
    bl_keyboard_init(&kb, "User name", 0, "ab\xC3\xA5" "cdefghijklmnopqrstuvwxyz0123456789", 31);
    CHECK_EQ(strlen(bl_keyboard_text(&kb)), BL_KB_MAX_LEN);
    CHECK(!strncmp(bl_keyboard_text(&kb), "abcdef", 6));
}

static void test_cancel_keeps_initial(void)
{
    BlKeyboard kb;
    bl_keyboard_init(&kb, "User name", 0, "player", 31);
    type(&kb, "zz");
    press(&kb, SQ, 4);
    CHECK_EQ(bl_keyboard_input(&kb, O), BL_KB_CANCEL);
    CHECK(!strcmp(bl_keyboard_text(&kb), "player"));
    bl_keyboard_init(&kb, "User name", 0, "player", 31);
    type(&kb, "q");
    CHECK(go(&kb, "Cancel"));
    CHECK_EQ(bl_keyboard_input(&kb, X), BL_KB_CANCEL);
    CHECK(!strcmp(bl_keyboard_text(&kb), "player"));
    /* Finished: further input changes nothing. */
    CHECK_EQ(bl_keyboard_input(&kb, X | SQ), BL_KB_CANCEL);
    CHECK(!strcmp(bl_keyboard_text(&kb), "player"));
}

static void test_secret_visible(void)
{
    BlKeyboard kb;
    char v[128];
    bl_keyboard_init(&kb, "Password", 1, "", 31);
    type(&kb, "pw1");
    bl_keyboard_visible(&kb, v, sizeof v);
    CHECK(!strcmp(v, "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2"));
    bl_keyboard_init(&kb, "User name", 0, "pw1", 31);
    bl_keyboard_visible(&kb, v, sizeof v);
    CHECK(!strcmp(v, "pw1"));
    bl_keyboard_wipe(&kb);
    CHECK(kb.text[0] == 0 && kb.initial[0] == 0);
}

static BlFrame new_frame(void)
{
    BlFrame f = { malloc(1920u * 1080u * 4u), 1920, 1080, 1920 * 4 };
    bl_background(&f, BL_BG_LIST);
    return f;
}

/* Owner finding 39: L2 shows and hides the password while it is typed. It
 * starts hidden every time, does nothing for other fields, and the hint says
 * what L2 does next. */
static void test_secret_show_hide(void)
{
    BlKeyboard kb;
    char v[128];
    bl_keyboard_init(&kb, "Password", 1, "", 31);
    type(&kb, "pw1");
    CHECK_EQ(bl_keyboard_input(&kb, BL_KB_PAD_L2), BL_KB_EDITING);
    bl_keyboard_visible(&kb, v, sizeof v);
    CHECK(!strcmp(v, "pw1"));
    CHECK(!strcmp(bl_keyboard_text(&kb), "pw1"));               /* L2 types nothing */
    bl_keyboard_input(&kb, BL_KB_PAD_L2);
    bl_keyboard_visible(&kb, v, sizeof v);
    CHECK(!strcmp(v, "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2"));
    bl_keyboard_input(&kb, BL_KB_PAD_L2);                       /* shown, then opened again: hidden */
    bl_keyboard_init(&kb, "Password", 1, "pw1", 31);
    bl_keyboard_visible(&kb, v, sizeof v);
    CHECK(!strcmp(v, "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2"));
    bl_keyboard_init(&kb, "User name", 0, "", 31);              /* not a password: L2 does nothing */
    type(&kb, "ab");
    bl_keyboard_input(&kb, BL_KB_PAD_L2);
    bl_keyboard_visible(&kb, v, sizeof v);
    CHECK(!strcmp(v, "ab"));
    CHECK(!strcmp(bl_keyboard_hints(&kb), "Cross type    Square delete    Triangle shift    L1/R1 cursor    Circle cancel    Options OK"));
    bl_keyboard_init(&kb, "Password", 1, "pw1", 31);
    CHECK(strstr(bl_keyboard_hints(&kb), "L2 Show") != NULL && strstr(bl_keyboard_hints(&kb), "L2 Hide") == NULL);
    bl_keyboard_input(&kb, BL_KB_PAD_L2);
    CHECK(strstr(bl_keyboard_hints(&kb), "L2 Hide") != NULL && strstr(bl_keyboard_hints(&kb), "L2 Show") == NULL);
    /* Shown: the frame has the text (like a plain field); hidden again: as before. */
    BlFrame a = new_frame(), b = new_frame(), c = new_frame();
    bl_keyboard_init(&kb, "Password", 1, "abc", 31);
    bl_keyboard_draw(&a, &kb);
    bl_keyboard_input(&kb, BL_KB_PAD_L2);
    bl_keyboard_draw(&b, &kb);
    CHECK(memcmp(a.pixels, b.pixels, (size_t)a.stride * a.height) != 0);
    bl_keyboard_input(&kb, BL_KB_PAD_L2);
    bl_keyboard_draw(&c, &kb);
    CHECK(memcmp(a.pixels, c.pixels, (size_t)a.stride * a.height) == 0);
    bl_keyboard_wipe(&kb);
    CHECK(kb.shown == 0);
    free(a.pixels); free(b.pixels); free(c.pixels);
}

/* Secret frames do not depend on which characters were typed. */
static void test_secret_draw_hides_text(void)
{
    BlFrame a = new_frame(), b = new_frame();
    BlKeyboard ka, kbb;
    bl_keyboard_init(&ka, "Password", 1, "abc", 31);
    bl_keyboard_init(&kbb, "Password", 1, "xyz", 31);
    bl_keyboard_draw(&a, &ka);
    bl_keyboard_draw(&b, &kbb);
    CHECK(memcmp(a.pixels, b.pixels, (size_t)a.stride * a.height) == 0);
    bl_keyboard_init(&kbb, "Password", 0, "xyz", 31);           /* plain text does differ */
    bl_keyboard_draw(&b, &kbb);
    CHECK(memcmp(a.pixels, b.pixels, (size_t)a.stride * a.height) != 0);
    free(a.pixels); free(b.pixels);
}

/* Cached and uncached drawing give the same frame. */
static void test_cache_matches(void)
{
    BlFrame a = new_frame(), b = new_frame();
    BlKeyboard kb;
    bl_keyboard_init(&kb, "User name", 0, "abc", 31);
    bl_keyboard_release();
    bl_keyboard_draw(&a, &kb);
    bl_keyboard_input(&kb, R);
    bl_keyboard_input(&kb, L);
    bl_keyboard_draw(&b, &kb);
    CHECK(memcmp(a.pixels, b.pixels, (size_t)a.stride * a.height) == 0);
    free(a.pixels); free(b.pixels);
}

static void golden(const char *name, const BlFrame *f)
{
    char hex[65], path[512], want[80] = "";
    bl_sha256_hex(f->pixels, (size_t)f->stride * f->height, hex);
    snprintf(path, sizeof path, BL_TEST_DIR "/golden/%s.sha256", name);
    char png[512]; snprintf(png, sizeof png, BL_TEST_DIR "/../build/golden-%s.png", name);
    if (getenv("BL_UPDATE_GOLDEN")) { FILE *o = fopen(path, "w"); fprintf(o, "%s\n", hex); fclose(o); }
    FILE *g = fopen(path, "r");
    if (g) { if (!fgets(want, sizeof want, g)) want[0] = 0; fclose(g); }
    want[64] = 0;
    CHECK(strcmp(hex, want) == 0);
    if (strcmp(hex, want) || getenv("BL_UPDATE_GOLDEN")) {
        bl_png_write(png, f);
        if (strcmp(hex, want)) fprintf(stderr, "  %s sha256 %s, golden %s\n", name, hex, want);
    }
}

static void test_golden(void)
{
    BlFrame f = new_frame();
    BlKeyboard kb;
    bl_keyboard_init(&kb, "AMAX user name", 0, "Joker", 31);
    press(&kb, D, 2);
    press(&kb, R, 3);                                             /* on "f" */
    bl_keyboard_draw(&f, &kb);
    golden("keyboard", &f);
    f = (free(f.pixels), new_frame());
    bl_keyboard_init(&kb, "AMAX password", 1, "", 31);
    type(&kb, "secret1");
    bl_keyboard_input(&kb, TRI);
    bl_keyboard_draw(&f, &kb);
    golden("keyboard-secret", &f);
    bl_keyboard_input(&kb, BL_KB_PAD_L2);
    bl_keyboard_draw(&f, &kb);
    golden("keyboard-secret-shown", &f);
    free(f.pixels);
    bl_keyboard_release();
}

int main(void)
{
    RUN(test_start_and_wrap);
    RUN(test_typing_and_ok);
    RUN(test_shift_symbols_space);
    RUN(test_backspace_cursor_and_max);
    RUN(test_cancel_keeps_initial);
    RUN(test_secret_visible);
    RUN(test_secret_draw_hides_text);
    RUN(test_secret_show_hide);
    RUN(test_cache_matches);
    RUN(test_golden);
    return bl_test_report("test_keyboard");
}
