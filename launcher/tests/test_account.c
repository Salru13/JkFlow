/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests for the AMAX online account (BLUR 1.00 plan Task L7, Review
 * Focus 2, decisions D3-D5). */
#define _POSIX_C_SOURCE 200809L
#include "bl_account.h"
#include "bl_events.h"
#include "bl_fs.h"
#include "bl_screens.h"
#include "bl_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[256];
static BlPaths P;

static void sh(const char *cmd) { if (system(cmd) != 0) fprintf(stderr, "  command failed: %s\n", cmd); }

static void fresh(void)
{
    char cmd[400];
    if (root[0]) { snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); sh(cmd); }
    snprintf(root, sizeof root, "/tmp/bl-account-XXXXXX");
    if (!mkdtemp(root)) exit(1);
    bl_paths_init(&P, root);
    bl_paths_make(&P);
}

static char *slurp(const char *path)
{
    static char buf[4096];
    if (bl_fs_read(path, buf, sizeof buf) < 0) buf[0] = 0;
    return buf;
}

static void lua_path(char *out, size_t cap) { snprintf(out, cap, "%s/%s", P.game, BL_ACCOUNT_LUA); }
static void cfg_path(char *out, size_t cap) { snprintf(out, cap, "%s/%s", P.state, BL_ACCOUNT_FILE); }

static BlAccount acc(const char *u, const char *p)
{
    BlAccount a;
    memset(&a, 0, sizeof a);
    snprintf(a.user, sizeof a.user, "%s", u);
    snprintf(a.pass, sizeof a.pass, "%s", p);
    return a;
}

/* Lua's own reading of the written file: python3 has no Lua, so the escape
 * rules are checked by decoding the two quoted strings by hand here. */
static int lua_string_after(const char *text, const char *key, char *out, size_t cap)
{
    const char *k = strstr(text, key);
    if (!k) return -1;
    const char *q = strchr(k, '"');
    if (!q) return -1;
    size_t n = 0;
    for (const char *c = q + 1; *c; c++) {
        if (*c == '"') { out[n] = 0; return 0; }
        if (*c == '\\') { c++; if (*c != '\\' && *c != '"') return -1; }
        if (n + 1 >= cap) return -1;
        out[n++] = *c;
    }
    return -1;
}

static void test_lua_escape_and_limits(void)
{
    fresh();
    BlAccount a = acc("a\"b\\c", "p]]w\"x\\");
    CHECK_EQ(bl_account_save(&P, &a), 0);
    CHECK_EQ(bl_account_apply(&P), 0);
    char lp[400]; lua_path(lp, sizeof lp);
    const char *text = slurp(lp);
    CHECK(!strncmp(text, "return {", 8));
    char u[64], p[64];
    CHECK_EQ(lua_string_after(text, "username =", u, sizeof u), 0);
    CHECK_EQ(lua_string_after(text, "password =", p, sizeof p), 0);
    CHECK(!strcmp(u, "a\"b\\c"));
    CHECK(!strcmp(p, "p]]w\"x\\"));
    BlAccount b;
    CHECK_EQ(bl_account_load(&P, &b), 0);
    CHECK(!strcmp(b.user, a.user) && !strcmp(b.pass, a.pass));

    /* refused by bl_account_valid, and never written */
    char s32[33]; memset(s32, 'x', 32); s32[32] = 0;
    char s31[32]; memset(s31, 'x', 31); s31[31] = 0;
    CHECK(bl_account_valid(s31));
    CHECK(!bl_account_valid(s32));
    CHECK(!bl_account_valid(""));
    CHECK(!bl_account_valid("del\x7f"));
    CHECK(!bl_account_valid("new\nline"));
    CHECK(!bl_account_valid("caf\xC3\xA9"));
    CHECK(!bl_account_valid("tab\there"));
    CHECK(bl_account_valid("space ok ~!"));
    fresh();
    const char *bad[] = { s32, "del\x7f", "new\nline", "caf\xC3\xA9" };
    for (unsigned i = 0; i < 4; i++) {
        BlAccount x;
        memset(&x, 0, sizeof x);
        snprintf(x.user, sizeof x.user, "%s", "fine");
        memcpy(x.pass, bad[i], strlen(bad[i]) < sizeof x.pass ? strlen(bad[i]) + 1 : sizeof x.pass);
        x.pass[sizeof x.pass - 1] = 0;
        if (strlen(bad[i]) >= sizeof x.pass) { memset(x.pass, 'x', sizeof x.pass); }   /* not terminated: 32 bytes */
        CHECK(bl_account_save(&P, &x) < 0);
        BlAccount y = acc("fine", "fine");
        snprintf(y.user, sizeof y.user, "%.31s", bad[i]);
        if (strlen(bad[i]) <= 31) CHECK(bl_account_save(&P, &y) < 0);
    }
    char cp[400]; cfg_path(cp, sizeof cp);
    CHECK(!bl_fs_exists(cp));
    char lp2[400]; lua_path(lp2, sizeof lp2);
    CHECK(!bl_fs_exists(lp2));
}

static void test_file_mode_0600(void)
{
    fresh();
    BlAccount a = acc("racer", "secret1");
    CHECK_EQ(bl_account_save(&P, &a), 0);
    CHECK_EQ(bl_account_apply(&P), 0);
    struct stat st;
    char cp[400]; cfg_path(cp, sizeof cp);
    CHECK(stat(cp, &st) == 0 && (st.st_mode & 0777) == 0600);
    char lp[400]; lua_path(lp, sizeof lp);
    CHECK(stat(lp, &st) == 0 && (st.st_mode & 0777) == 0600);
    CHECK(!strcmp(slurp(cp), "blur-account 1\nuser=racer\npass=secret1\n"));
}

static void test_apply_without_account_deletes_lua(void)
{
    fresh();
    char lp[400]; lua_path(lp, sizeof lp);
    char dir[400]; snprintf(dir, sizeof dir, "%s/amax/plugins", P.game);
    bl_fs_mkdirs(dir);
    FILE *f = fopen(lp, "w"); fputs("return { username = \"old\", password = \"old\" }\n", f); fclose(f);
    BlAccount a;
    CHECK_EQ(bl_account_load(&P, &a), 1);                    /* none */
    CHECK_EQ(bl_account_apply(&P), 0);
    CHECK(!bl_fs_exists(lp));
    CHECK_EQ(bl_account_apply(&P), 0);                       /* nothing to delete: still fine */
}

static void test_clear_removes_both(void)
{
    fresh();
    BlAccount a = acc("racer", "secret1");
    CHECK_EQ(bl_account_save(&P, &a), 0);
    CHECK_EQ(bl_account_apply(&P), 0);
    CHECK_EQ(bl_account_clear(&P), 0);
    char cp[400]; cfg_path(cp, sizeof cp);
    char lp[400]; lua_path(lp, sizeof lp);
    CHECK(!bl_fs_exists(cp) && !bl_fs_exists(lp));
    CHECK_EQ(bl_account_load(&P, &a), 1);
    /* a damaged file is an error, never a half account */
    FILE *f = fopen(cp, "w"); fputs("blur-account 1\nuser=racer\n", f); fclose(f);
    CHECK(bl_account_load(&P, &a) < 0);
}

/* Audit 29a: the user name alone (the Start idle prefetch): the password
 * is not loaded (bl_account_load is not called). */
static void test_user_only(void)
{
    fresh();
    BlAccount a = acc("racer", "secret1");
    CHECK_EQ(bl_account_save(&P, &a), 0);
    char user[BL_ACCOUNT_MAX + 1] = "x";
    int loads = bl_account_load_count;
    CHECK_EQ(bl_account_user(&P, user, sizeof user), 0);
    CHECK(!strcmp(user, "racer"));
    CHECK_EQ(bl_account_load_count, loads);
    CHECK_EQ(bl_account_load(&P, &a), 0);
    CHECK_EQ(bl_account_load_count, loads + 1);
    CHECK_EQ(bl_account_clear(&P), 0);
    CHECK_EQ(bl_account_user(&P, user, sizeof user), 1);
    CHECK(user[0] == 0);
}

static void test_never_in_events_log(void)
{
    fresh();
    bl_events_open(P.state);
    BlAccount a = acc("UniqueRacerName", "Pw0rdSecretXYZ");
    CHECK_EQ(bl_account_save(&P, &a), 0);
    CHECK_EQ(bl_account_apply(&P), 0);
    CHECK_EQ(bl_account_clear(&P), 0);
    BlAccount b = acc("UniqueRacerName", "bad\x7f");
    CHECK(bl_account_save(&P, &b) < 0);
    char ev[400]; snprintf(ev, sizeof ev, "%s/%s", P.state, BL_EVENTS_FILE);
    const char *log = slurp(ev);
    CHECK(log[0] != 0);                                      /* the actions are logged ... */
    CHECK(strstr(log, "UniqueRacerName") == NULL);           /* ... never the values */
    CHECK(strstr(log, "Pw0rdSecretXYZ") == NULL);
    bl_events_open(NULL);
}

static void test_settings_page_rows(void)
{
    BlSettingsState st;
    bl_settings_init(&st, NULL);
    CHECK(bl_settings_listed(&st, BL_SET_ACCOUNT));
    CHECK_EQ(bl_settings_rows(&st, BL_SET_ACCOUNT), 4);
    CHECK(!strcmp(bl_str(BL_S_PANEL_ACCOUNT), "online account (AMAX)"));            /* audit 24: the case of the other panels */
    CHECK(!strcmp(bl_str(BL_S_ROW_USERNAME), "Username"));
    CHECK(!strcmp(bl_str(BL_S_ROW_PASSWORD), "Password"));
    CHECK(!strcmp(bl_str(BL_S_ROW_ACCOUNT_SAVE), "Save"));
    CHECK(!strcmp(bl_str(BL_S_ROW_ACCOUNT_REMOVE), "Remove account"));
    CHECK(!strcmp(bl_str(BL_S_NOTE_ACCOUNT_CREATE), "Online play uses the AMAX community servers and needs an AMAX account. How to get one: "
                  "amax-emu.com/how_to_play (official AMAX page). Then enter it in Settings > Online account."));
    CHECK(!strcmp(bl_str(BL_S_NOTE_ACCOUNT_STORED), "Your account is stored on this console in %s and sent only to the AMAX servers. "
                  "The password is saved on the PS4 in plain text, because AMAX needs it that way; Remove account deletes it."));
    /* move to the item, open it, edit the username */
    st.item = BL_SET_ACCOUNT;
    BlField field;
    CHECK_EQ(bl_settings_input(&st, BL_PAD_CROSS, &field), BL_SET_EV_NONE);
    CHECK_EQ(bl_settings_input(&st, BL_PAD_CROSS, &field), BL_SET_EV_EDIT);
    CHECK_EQ(field, BL_FIELD_USERNAME);
    CHECK(bl_settings_set_text(&st, BL_FIELD_USERNAME, "caf\xC3\xA9") < 0);
    CHECK_EQ(bl_settings_set_text(&st, BL_FIELD_USERNAME, "racer"), 0);
    CHECK(!strcmp(st.account_user, "racer"));
    CHECK_EQ(st.cfg.username[0], 0);                         /* not the hidden private-server field */
    CHECK_EQ(bl_settings_set_text(&st, BL_FIELD_PASSWORD, "secret1"), 0);
    CHECK(st.account_pass_set);
    /* the password is drawn as stars, never as text */
    char text[160];
    bl_settings_field_text(&st, BL_SET_ACCOUNT, 1, text, sizeof text);
    CHECK(strspn(text, "*") == strlen(text) && strlen(text) > 0);
    bl_settings_field_text(&st, BL_SET_ACCOUNT, 0, text, sizeof text);
    CHECK(!strcmp(text, "racer"));
    CHECK(sizeof st.account_user <= 32);
    /* Save and Remove are buttons */
    CHECK_EQ(bl_settings_input(&st, BL_PAD_DOWN, &field), BL_SET_EV_NONE);
    CHECK_EQ(bl_settings_input(&st, BL_PAD_DOWN, &field), BL_SET_EV_NONE);
    CHECK_EQ(bl_settings_input(&st, BL_PAD_CROSS, &field), BL_SET_EV_ACCOUNT_SAVE);
    CHECK_EQ(bl_settings_input(&st, BL_PAD_DOWN, &field), BL_SET_EV_NONE);
    CHECK_EQ(bl_settings_input(&st, BL_PAD_CROSS, &field), BL_SET_EV_NONE);   /* asks first */
    CHECK_EQ(bl_settings_input(&st, BL_PAD_CROSS, &field), BL_SET_EV_ACCOUNT_REMOVE);
    /* the online private-server panel stays hidden; the account item does not depend on it */
    CHECK(!bl_settings_listed(&st, BL_SET_ONLINE) || BL_ONLINE_UI);
    BlFrame f = { malloc(1920 * 1080 * 4), 1920, 1080, 1920 * 4 };
    bl_settings_draw(&f, &st);
    CHECK_EQ(bl_settings_last_drawn.item, BL_SET_ACCOUNT);
    free(f.pixels);
    bl_screens_release();
}

int main(void)
{
    RUN(test_lua_escape_and_limits);
    RUN(test_file_mode_0600);
    RUN(test_apply_without_account_deletes_lua);
    RUN(test_clear_removes_both);
    RUN(test_user_only);
    RUN(test_never_in_events_log);
    RUN(test_settings_page_rows);
    char cmd[400]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); sh(cmd);
    return bl_test_report("test_account");
}
