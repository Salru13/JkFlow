/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests for blur100-display-name.c (audit finding 4): the PS4 local user
 * name becomes the game's display name without the system keyboard. A name
 * that is already portable stays byte-identical; any other name is folded to
 * ASCII, disallowed characters become '_', repeats collapse, '_' '.' '-' are
 * trimmed at both ends, at most 16 bytes, and "Player" when nothing is left. */
#include <stdio.h>
#include <string.h>

#include "blur100-display-name.h"

static int failures, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { failures++; fprintf(stderr, "  FAIL %s:%d %s\n", __FILE__, __LINE__, what); } } while (0)

static void sanitize_case(const char *in, const char *want, int want_sanitized)
{
    char out[PW_BLUR100_NAME_MAX + 1];
    int sanitized = -1;
    memset(out, 0x5a, sizeof(out));
    size_t n = pw_blur100_name_sanitize(in, strlen(in) + 1, out, &sanitized);
    char what[256];
    snprintf(what, sizeof what, "sanitize \"%s\" -> \"%s\" (want \"%s\") n=%zu sanitized=%d",
             in, memchr(out, 0, sizeof out) ? out : "<unterminated>", want, n, sanitized);
    CHECK(n == strlen(want) && out[n] == 0 && !strcmp(out, want), what);
    CHECK(sanitized == want_sanitized, what);
    CHECK(n >= 1 && n <= PW_BLUR100_NAME_MAX, what);
    /* Every result is itself portable, so a second pass leaves it alone. */
    char again[PW_BLUR100_NAME_MAX + 1];
    int s2 = -1;
    size_t n2 = pw_blur100_name_sanitize(out, n + 1, again, &s2);
    CHECK(n2 == n && !strcmp(again, out) && s2 == 0, what);
}

/* ---- fake UserService ---- */
static int fg_rc, name_rc, fg_user = 7, unterminated;
static const char *fake_name;
static int fake_foreground(int32_t *user) { if (!fg_rc) *user = fg_user; return fg_rc; }
static int fake_username(int32_t user, char *out, size_t cap)
{
    CHECK(user == fg_user, "username asked for the foreground user");
    if (name_rc) return name_rc;
    if (unterminated) { memset(out, 'Q', cap); return 0; }
    snprintf(out, cap, "%s", fake_name);
    return 0;
}
static const PwBlurNameOps fake_ops = { fake_foreground, fake_username };

static void select_case(int frc, int nrc, const char *name, const char *want, int want_local, int want_sanitized)
{
    PwBlurName got;
    int sanitized = -1;
    fg_rc = frc; name_rc = nrc; fake_name = name;
    memset(&got, 0x5a, sizeof got);
    int local = pw_blur100_display_name(&fake_ops, &got, &sanitized);
    char what[256];
    snprintf(what, sizeof what, "select fg=%d name=%d \"%s\" -> \"%.64s\" local=%d sanitized=%d",
             frc, nrc, name ? name : "(null)", got.utf8, local, sanitized);
    CHECK(local == want_local, what);
    CHECK(sanitized == want_sanitized, what);
    CHECK(!strcmp(got.utf8, want) && got.bytes == strlen(want), what);
    CHECK(got.source == PW_BLUR_NAME_LOCAL, what);
    CHECK(got.user_id == (want_local ? fg_user : -1), what);
    PwBlurName check;
    CHECK(pw_blur_name_validate(got.utf8, sizeof got.utf8, &check), what);
}

int main(void)
{
    /* The owner's table. */
    sanitize_case("John Smith", "John_Smith", 1);
    sanitize_case("\xc3\x85sa", "Asa", 1);                                     /* Åsa */
    sanitize_case("\xc3\x9c" "n" "\xc3\xaf" "c" "\xc3\xb8" "d" "\xc3\xa9" " " "\xc3\x91" "ame", "Unicode_Name", 1); /* Ünïcødé Ñame */
    sanitize_case("\xe2\x98\x85\xe2\x98\x86", "Player", 1);                    /* ★☆ */
    sanitize_case("The Quick Brown Fox Jumps Over Lazy Dog!", "The_Quick_Brown", 1); /* 40 chars */
    sanitize_case("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN", "abcdefghijklmnop", 1);
    sanitize_case("XxXJoker13XxX", "XxXJoker13XxX", 0);
    sanitize_case("", "Player", 1);
    /* Folding: Latin-1 Supplement and Latin Extended-A. */
    sanitize_case("\xc3\x84\xc3\x80\xc3\xb6\xc3\xa9", "AAoe", 1);              /* ÄÀöé */
    sanitize_case("Stra\xc3\x9f" "e", "Strasse", 1);                            /* Straße */
    sanitize_case("\xc3\x86sir \xc3\xa6", "AEsir_ae", 1);                       /* Æsir æ */
    sanitize_case("S\xc3\xb8ren \xc3\x98", "Soren_O", 1);                       /* Søren Ø */
    sanitize_case("\xc5\x81ukasz \xc5\xbb\xc3\xb3\xc5\x82\xc4\x87", "Lukasz_Zolc", 1); /* Łukasz Żółć */
    sanitize_case("Jos\xc3\xa9 Mar\xc3\xad" "a", "Jose_Maria", 1);              /* José María */
    sanitize_case("\xc5\x92uvre \xc4\xb2", "OEuvre_IJ", 1);                     /* Œuvre Ĳ */
    sanitize_case("\xc3\x9e" "or \xc3\xb0" "a", "THor_da", 1);                  /* Þor ða */
    sanitize_case("\xc4\x80\xc5\xbf\xc5\xbe", "Asz", 1);                        /* Āſž */
    sanitize_case("\xc8\x98tef\xc8\x9b", "Steft", 1); /* Ștefț (Latin Ext-B) */
    /* Separators, collapse and trim. */
    sanitize_case("  __a  ", "a", 1);
    sanitize_case("a  @@  b", "a_b", 1);
    sanitize_case(".Joker", "Joker", 1);
    sanitize_case("Joker.", "Joker", 1);
    sanitize_case("-._Joker_.-", "-._Joker_.-", 0);                             /* portable: no dot at either end */
    sanitize_case("a...b", "a...b", 0);
    sanitize_case("abcdefghijklmnop", "abcdefghijklmnop", 0);                   /* exactly 16 */
    sanitize_case("abcdefghijklmno p", "abcdefghijklmno", 1);                   /* cut lands on '_' */
    sanitize_case("x\xc3\x97y\xc3\xb7z", "x_y_z", 1);                           /* × ÷ are not letters */
    sanitize_case("e\xcc\x81t\xcc\x88", "et", 1);                               /* combining marks drop */
    sanitize_case("\xff\xfe" "AB\xc3", "AB", 1);                                /* malformed UTF-8 */
    sanitize_case("___", "___", 0);                                             /* portable as it is */
    sanitize_case("_ . -", "Player", 1);
    sanitize_case("\xe5\xa4\xaa\xe9\x83\x8e", "Player", 1);                     /* 太郎 */
    sanitize_case("\xe5\xa4\xaa\xe9\x83\x8e Taro", "Taro", 1);

    /* Input bounded by its extent, unterminated service output. */
    {
        char out[PW_BLUR100_NAME_MAX + 1];
        int sanitized = -1;
        CHECK(pw_blur100_name_sanitize("John Smith", 4, out, &sanitized) == 4 && !strcmp(out, "John") &&
              sanitized == 1, "extent bounds the input");
        CHECK(pw_blur100_name_sanitize(NULL, 0, out, &sanitized) == 6 && !strcmp(out, "Player") &&
              sanitized == 1, "NULL input");
    }

    /* The local user name through the UserService ops. */
    select_case(0, 0, "XxXJoker13XxX", "XxXJoker13XxX", 1, 0);
    select_case(0, 0, "John Smith", "John_Smith", 1, 1);
    select_case(0, 0, "\xc3\x85sa", "Asa", 1, 1);
    select_case(0, 0, "\xe2\x98\x85", "Player", 1, 1);
    select_case(0, 0, "", "Player", 1, 1);
    select_case(-1, 0, "XxXJoker13XxX", "Player", 0, 1);                        /* no foreground user */
    select_case(0, -5, "XxXJoker13XxX", "Player", 0, 1);                        /* GetUserName failed */
    unterminated = 1;
    select_case(0, 0, NULL, "QQQQQQQQQQQQQQQQ", 1, 1);                          /* no NUL: bounded */
    unterminated = 0;
    {
        PwBlurName got;
        int sanitized = -1;
        CHECK(pw_blur100_display_name(NULL, &got, &sanitized) == 0 && !strcmp(got.utf8, "Player") &&
              sanitized == 1, "no ops");
    }

    printf("test_display_name: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
