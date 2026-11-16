/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* "settings" screen: a list like the game's list menus with a glass panel
 * for the focused item (spec 6; BLUR 1.00: Frame rate, Show FPS / ms, Enable
 * logs, Online account, Save support bundle). */
#include "bl_screens.h"
#include "bl_account.h"
#include "bl_net.h"
#include "bl_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum RowKind { ROW_SEG, ROW_FIELD, ROW_BUTTON } RowKind;
/* ev: the event a button sends; confirm: the message of a button that needs
 * a second Cross (-1: acts at once). */
typedef struct Row { BlStringId label; RowKind kind; BlField field; BlSetEvent ev; int confirm; } Row;

static const Row ONLINE_ROWS[] = {
    { BL_S_ROW_MODE, ROW_SEG, BL_FIELD_NONE, 0, -1 }, { BL_S_ROW_SERVER, ROW_FIELD, BL_FIELD_SERVER, 0, -1 },
    { BL_S_ROW_PORT, ROW_FIELD, BL_FIELD_PORT, 0, -1 }, { BL_S_ROW_SIGNIN, ROW_SEG, BL_FIELD_NONE, 0, -1 },
    { BL_S_ROW_USERNAME, ROW_FIELD, BL_FIELD_USERNAME, 0, -1 }, { BL_S_ROW_PASSWORD, ROW_FIELD, BL_FIELD_PASSWORD, 0, -1 },
    { BL_S_ROW_TEST, ROW_BUTTON, BL_FIELD_NONE, BL_SET_EV_TEST, -1 },
};
static const Row FRAMERATE_ROWS[] = { { BL_S_ROW_FRAMERATE, ROW_SEG, BL_FIELD_NONE, 0, -1 } };
static const Row STATS_ROWS[] = { { BL_S_ROW_SHOW, ROW_SEG, BL_FIELD_NONE, 0, -1 } };
static const Row LOG_ROWS[] = { { BL_S_ROW_LOGGING, ROW_SEG, BL_FIELD_NONE, 0, -1 }, { BL_S_ROW_CLEAR, ROW_BUTTON, BL_FIELD_NONE, BL_SET_EV_CLEAR_LOGS, -1 } };
static const Row ACCOUNT_ROWS[] = {
    { BL_S_ROW_USERNAME, ROW_FIELD, BL_FIELD_USERNAME, 0, -1 }, { BL_S_ROW_PASSWORD, ROW_FIELD, BL_FIELD_PASSWORD, 0, -1 },
    { BL_S_ROW_ACCOUNT_SAVE, ROW_BUTTON, BL_FIELD_NONE, BL_SET_EV_ACCOUNT_SAVE, -1 },
    { BL_S_ROW_ACCOUNT_REMOVE, ROW_BUTTON, BL_FIELD_NONE, BL_SET_EV_ACCOUNT_REMOVE, BL_S_CONFIRM_ACCOUNT_REMOVE },
};
static const Row SUPPORT_ROWS[] = { { BL_S_ROW_BUNDLE, ROW_BUTTON, BL_FIELD_NONE, BL_SET_EV_BUNDLE, -1 } };

static const struct { const Row *rows; int count; BlStringId name, panel, band; BlIcon icon; } ITEMS[BL_SET_COUNT] = {
    [BL_SET_ONLINE] = { ONLINE_ROWS, 7, BL_S_SET_ONLINE, BL_S_PANEL_ONLINE, BL_S_BAND_ONLINE, BL_ICON_GLOBE },
    [BL_SET_FRAMERATE] = { FRAMERATE_ROWS, 1, BL_S_SET_FRAMERATE, BL_S_PANEL_FRAMERATE, BL_S_BAND_FRAMERATE, BL_ICON_DISPLAY },
    [BL_SET_STATS] = { STATS_ROWS, 1, BL_S_SET_STATS, BL_S_PANEL_STATS, BL_S_BAND_STATS, BL_ICON_CHART },
    [BL_SET_LOGGING] = { LOG_ROWS, 2, BL_S_SET_LOGGING, BL_S_PANEL_LOGGING, BL_S_BAND_LOGGING, BL_ICON_LOG },
    [BL_SET_ACCOUNT] = { ACCOUNT_ROWS, 4, BL_S_SET_ACCOUNT, BL_S_PANEL_ACCOUNT, BL_S_BAND_ACCOUNT, BL_ICON_USER },
    [BL_SET_SUPPORT] = { SUPPORT_ROWS, 1, BL_S_SET_SUPPORT, BL_S_PANEL_SUPPORT, BL_S_BAND_SUPPORT, BL_ICON_WRENCH },
};

static const BlStringId MODE_OPTS[] = { BL_S_OPT_OFFICIAL, BL_S_OPT_PRIVATE, BL_S_OPT_OFF };
static const BlStringId SIGNIN_OPTS[] = { BL_S_OPT_ANONYMOUS, BL_S_OPT_ACCOUNT };
static const BlStringId ONOFF_OPTS[] = { BL_S_OPT_OFF, BL_S_OPT_ON };
static const BlStringId FPS_OPTS[] = { BL_S_OPT_FPS30, BL_S_OPT_FPS60 };

/* The frame rate Play uses: 30 for a Blur.exe that is not the tested one,
 * whatever is saved (the saved value is kept for a later tested exe). */
static int frame_rate_shown(const BlSettingsState *st)
{
    return st->exe_untested ? BL_FPS_30 : st->cfg.frame_rate == BL_FPS_60 ? BL_FPS_60 : BL_FPS_30;
}

int bl_settings_rows(const BlSettingsState *st, int item)
{
    (void)st;
    return item >= 0 && item < BL_SET_COUNT ? ITEMS[item].count : 0;
}

int bl_settings_listed(const BlSettingsState *st, int item)
{
    return item >= 0 && item < BL_SET_COUNT && !(st->hidden & (1u << item));
}

static int first_listed(const BlSettingsState *st)
{
    for (int i = 0; i < BL_SET_COUNT; i++) if (bl_settings_listed(st, i)) return i;
    return BL_SET_STATS;
}

static int listed_count(const BlSettingsState *st)
{
    int n = 0;
    for (int i = 0; i < BL_SET_COUNT; i++) n += bl_settings_listed(st, i);
    return n;
}

void bl_settings_hide(BlSettingsState *st, int item)
{
    if (item < 0 || item >= BL_SET_COUNT || item == BL_SET_STATS || item == BL_SET_FRAMERATE) return;   /* Frame rate and Show FPS / ms always stay */
    st->hidden |= 1u << item;
    if (!bl_settings_listed(st, st->item)) { st->item = first_listed(st); st->in_panel = 0; st->row = 0; }
}

void bl_settings_init(BlSettingsState *st, const BlConfig *cfg)
{
    memset(st, 0, sizeof *st);
    if (cfg) st->cfg = *cfg; else bl_config_defaults(&st->cfg);
    if (st->cfg.stats != BL_STATS_OFF) st->cfg.stats = BL_STATS_FPS;    /* Show FPS / ms: off or on */
    if (!BL_ONLINE_UI) st->hidden |= 1u << BL_SET_ONLINE;
    BlPaths p;                                 /* the default layout; the frontend sets its own */
    bl_paths_init(&p, NULL);
    snprintf(st->logs_dir, sizeof st->logs_dir, "%s", p.logs);
    snprintf(st->support_dir, sizeof st->support_dir, "%s", p.support);
    snprintf(st->state_dir, sizeof st->state_dir, "%s", p.state);
    st->item = first_listed(st);
    st->message = -1;
    st->confirm_row = -1;
}

/* The segmented value a row controls: pointer and option count. */
static int *seg_value(BlSettingsState *st, int item, int row, int *count)
{
    switch (item) {
    case BL_SET_ONLINE:
        if (row == 0) { *count = 3; return (int *)&st->cfg.online_mode; }
        if (row == 3) { *count = 2; return &st->cfg.login_account; }
        break;
    case BL_SET_FRAMERATE: *count = 2; return (int *)&st->cfg.frame_rate;     /* BL_FPS_30 0, BL_FPS_60 1 */
    case BL_SET_STATS: *count = 2; return (int *)&st->cfg.stats;          /* BL_STATS_OFF 0, BL_STATS_FPS 1 */
    case BL_SET_LOGGING: if (row == 0) { *count = 2; return &st->cfg.logging; } break;
    }
    *count = 0;
    return NULL;
}

BlSetEvent bl_settings_input(BlSettingsState *st, uint32_t pressed, BlField *field)
{
    if (field) *field = BL_FIELD_NONE;
    if (!pressed) return BL_SET_EV_NONE;
    if (!st->in_panel) {
        if (pressed & BL_PAD_UP)
            for (int i = st->item - 1; i >= 0; i--) if (bl_settings_listed(st, i)) { st->item = i; break; }
        if (pressed & BL_PAD_DOWN)
            for (int i = st->item + 1; i < BL_SET_COUNT; i++) if (bl_settings_listed(st, i)) { st->item = i; break; }
        if (pressed & (BL_PAD_CROSS | BL_PAD_RIGHT)) { st->in_panel = 1; st->row = 0; st->message = -1; st->message_text[0] = 0; }
        else if (pressed & BL_PAD_CIRCLE) return BL_SET_EV_BACK;
        return BL_SET_EV_NONE;
    }
    const Row *row = &ITEMS[st->item].rows[st->row];
    if (st->confirm_row >= 0) {
        int same = st->confirm_row == st->row && pressed == BL_PAD_CROSS;
        st->confirm_row = -1;
        st->message = -1;
        if (same) return row->ev;
        return BL_SET_EV_NONE;                     /* any other button only cancels */
    }
    if (pressed & BL_PAD_CIRCLE) { st->in_panel = 0; return BL_SET_EV_NONE; }
    if (pressed & BL_PAD_UP && st->row > 0) st->row--;
    if (pressed & BL_PAD_DOWN && st->row < bl_settings_rows(st, st->item) - 1) st->row++;
    if (pressed & (BL_PAD_UP | BL_PAD_DOWN)) return BL_SET_EV_NONE;
    int count;
    int *v = seg_value(st, st->item, st->row, &count);
    if (st->item == BL_SET_FRAMERATE && st->exe_untested) {
        /* Not the tested Blur.exe: 30 only; asking for 60 says why. */
        if (pressed & (BL_PAD_RIGHT | BL_PAD_CROSS)) { st->message = BL_S_FRAMERATE_UNTESTED; st->message_text[0] = 0; }
        return BL_SET_EV_NONE;
    }
    if (row->kind == ROW_SEG && v) {
        if (pressed & BL_PAD_LEFT && *v > 0) (*v)--;
        if (pressed & BL_PAD_RIGHT && *v < count - 1) (*v)++;
        if (pressed & BL_PAD_CROSS) *v = (*v + 1) % count;
        return BL_SET_EV_NONE;
    }
    if (!(pressed & BL_PAD_CROSS)) return BL_SET_EV_NONE;
    if (row->kind == ROW_FIELD) { if (field) *field = row->field; return BL_SET_EV_EDIT; }
    st->message_text[0] = 0;
    if (row->confirm >= 0) { st->confirm_row = st->row; st->message = row->confirm; return BL_SET_EV_NONE; }
    return row->ev;
}

static int plain_text(const char *s, size_t max)
{
    if (strlen(s) > max) return 0;
    for (const char *p = s; *p; p++) if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7E || *p == ';' || *p == '#') return 0;
    return 1;
}

int bl_settings_set_text(BlSettingsState *st, BlField field, const char *text)
{
    st->message = -1;
    st->message_text[0] = 0;
    if (!text) text = "";
    if (st->item == BL_SET_ACCOUNT && (field == BL_FIELD_USERNAME || field == BL_FIELD_PASSWORD)) {
        /* Blur copies each into a 32-byte field: 1..31 printable ASCII bytes. */
        if (!bl_account_valid(text)) { st->message = BL_S_ACCOUNT_INVALID; return -1; }
        if (field == BL_FIELD_USERNAME) snprintf(st->account_user, sizeof st->account_user, "%s", text);
        else st->account_pass_set = 1;              /* the text itself stays with the frontend */
        return 0;
    }
    switch (field) {
    case BL_FIELD_SERVER:
        if (*text && !bl_host_valid(text)) { st->message = BL_S_INVALID_HOST; return -1; }
        snprintf(st->cfg.host, sizeof st->cfg.host, "%s", text);
        return 0;
    case BL_FIELD_PORT: {
        char *end;
        long p = strtol(text, &end, 10);
        if (!*text) p = 0, end = (char *)text;
        if (*end || p < 0 || p > 65535) { st->message = BL_S_INVALID_PORT; return -1; }
        st->cfg.port = (int)p;
        return 0;
    }
    case BL_FIELD_USERNAME:
        if (!plain_text(text, 63)) { st->message = BL_S_INVALID_TEXT; return -1; }
        snprintf(st->cfg.username, sizeof st->cfg.username, "%s", text);
        return 0;
    case BL_FIELD_PASSWORD:
        st->password_set = text[0] != 0;
        return 0;
    case BL_FIELD_NONE: break;
    }
    return -1;
}

void bl_settings_field_text(const BlSettingsState *st, int item, int row, char *out, size_t cap)
{
    if (!out || !cap) return;
    out[0] = 0;
    if (item < 0 || item >= BL_SET_COUNT || row < 0 || row >= ITEMS[item].count) return;
    const BlConfig *c = &st->cfg;
    const Row *r = &ITEMS[item].rows[row];
    if (item == BL_SET_ACCOUNT) {
        if (r->field == BL_FIELD_USERNAME) snprintf(out, cap, "%s", st->account_user[0] ? st->account_user : bl_str(BL_S_NOT_SET));
        else if (r->field == BL_FIELD_PASSWORD) snprintf(out, cap, "%s", st->account_pass_set ? "********" : bl_str(BL_S_NOT_SET));
        return;
    }
    switch (r->field) {
    case BL_FIELD_SERVER: snprintf(out, cap, "%s", c->host[0] ? c->host : bl_str(BL_S_NOT_SET)); break;
    case BL_FIELD_PORT:
        if (c->port) snprintf(out, cap, "%d", c->port); else snprintf(out, cap, "%s", bl_str(BL_S_PORT_DEFAULT));
        break;
    case BL_FIELD_USERNAME: snprintf(out, cap, "%s", c->username[0] ? c->username : bl_str(BL_S_NOT_SET)); break;
    case BL_FIELD_PASSWORD: snprintf(out, cap, "%s", st->password_set ? "********" : bl_str(BL_S_NOT_SET)); break;
    default: break;
    }
}

void bl_settings_note(const BlSettingsState *st, int item, char *out, size_t cap)
{
    if (!out || !cap) return;
    out[0] = 0;
    if (item == BL_SET_FRAMERATE)
        snprintf(out, cap, "%s", bl_str(st->exe_untested ? BL_S_FRAMERATE_UNTESTED : frame_rate_shown(st) == BL_FPS_60 ? BL_S_NOTE_FPS60 : BL_S_NOTE_FPS30));
    else     if (item == BL_SET_LOGGING) snprintf(out, cap, bl_str(BL_S_NOTE_LOGGING), st->logs_dir);
    else if (item == BL_SET_SUPPORT) snprintf(out, cap, bl_str(BL_S_NOTE_SUPPORT), st->support_dir);
    else if (item == BL_SET_ACCOUNT) snprintf(out, cap, bl_str(BL_S_NOTE_ACCOUNT_STORED), st->state_dir);
}

static void value_summary(const BlSettingsState *st, int item, char *out, size_t cap)
{
    const BlConfig *c = &st->cfg;
    switch (item) {
    case BL_SET_ONLINE:
        if (c->online_mode == BL_ONLINE_PRIVATE)
            snprintf(out, cap, "%s \xC2\xB7 %s", bl_str(BL_S_OPT_PRIVATE), c->host[0] ? c->host : bl_str(BL_S_NOT_SET));
        else snprintf(out, cap, "%s", bl_str(MODE_OPTS[(unsigned)c->online_mode % 3]));
        break;
    case BL_SET_FRAMERATE: snprintf(out, cap, "%s", bl_str(FPS_OPTS[frame_rate_shown(st)])); break;
    case BL_SET_STATS: snprintf(out, cap, "%s", bl_str(ONOFF_OPTS[c->stats != BL_STATS_OFF])); break;
    case BL_SET_LOGGING: snprintf(out, cap, "%s", bl_str(ONOFF_OPTS[c->logging ? 1 : 0])); break;
    case BL_SET_ACCOUNT: snprintf(out, cap, "%s", st->account_user[0] ? st->account_user : bl_str(BL_S_NOT_SET)); break;
    case BL_SET_SUPPORT: snprintf(out, cap, "%s", bl_str(BL_S_SUMMARY_SUPPORT)); break;
    default: out[0] = 0;
    }
}

/* The item list; returns its right edge (f NULL: measure only). */
static int draw_list(BlFrame *f, const BlSettingsState *st)
{
    int n = listed_count(st), step = n <= 5 ? 102 : 540 / n, k = 0, right = 0;
    for (int i = 0; i < BL_SET_COUNT; i++) {
        if (!bl_settings_listed(st, i)) continue;
        int y = 300 + k++ * step, on = i == st->item;
        if (on) {
            bl_round_rect(f, 160, y + 4, 60, 60, 12, 0x123A80, 0x0B286C, 150, BL_RGB_SUBTITLE, 3, BL_RGB_SUBTITLE, 14);
        } else {
            bl_glow_dot(f, 190, y + 34, 40, 0x3C6EFF, 110);
            bl_round_rect(f, 160, y + 4, 60, 60, 30, 0x4D7DFF, 0x1F3FAE, 190, 0x4D7DFF, 0, 0, 0);
        }
        bl_icon(f, ITEMS[i].icon, 190, y + 34, 34, BL_RGB_WHITE);
        int w = bl_text_glow(f, BL_FONT_ITEM, 240, y, bl_str(ITEMS[i].name), on ? BL_RGB_WHITE : BL_RGB_DIM, on ? 0x404858 : 0x101010) + 5;
        if (on) {
            char sub[192];
            value_summary(st, i, sub, sizeof sub);
            int sw = bl_text(f, BL_FONT_BAND, 240, y + bl_font_line_height(BL_FONT_ITEM) - 6, sub, BL_RGB_SUBTITLE);
            if (sw > w) w = sw;
        }
        if (240 + w > right) right = 240 + w;
    }
    return right;
}

enum { PX = 760, PY = 180, PW = 1010, PH = 670 };

static void draw_panel(BlFrame *f, const BlSettingsState *st)
{
    const int px = PX, py = PY, pw = PW, ph = PH, right = px + pw - 64;
    bl_panel_title(f, px + 44, py + 30, bl_str(ITEMS[st->item].panel));
    const BlConfig *c = &st->cfg;
    int y = py + 96, btn_y = -1, btn_right = 0;
    for (int r = 0; r < bl_settings_rows(st, st->item); r++, y += 66) {
        const Row *row = &ITEMS[st->item].rows[r];
        int focused = st->in_panel && st->row == r;
        if (row->kind == ROW_BUTTON) {
            bl_button(f, px + 44, y, bl_str(row->label), focused);
            btn_y = y;
            btn_right = px + 44 + bl_text_width(BL_FONT_BODY, bl_str(row->label)) + 92;
            continue;
        }
        bl_row(f, px + 24, y, pw - 48, bl_str(row->label), focused);
        if (row->kind == ROW_SEG) {
            int count, *v = seg_value((BlSettingsState *)st, st->item, r, &count);
            if (st->item == BL_SET_FRAMERATE) {          /* 60 greyed for a Blur.exe that is not the tested one */
                bl_segments_greyed(f, right, y, FPS_OPTS, 2, frame_rate_shown(st), st->exe_untested ? 2u : 0u);
                continue;
            }
            const BlStringId *opts = st->item == BL_SET_ONLINE ? (r == 0 ? MODE_OPTS : SIGNIN_OPTS) : ONOFF_OPTS;
            bl_segments(f, right, y, opts, count, v ? (*v < count ? *v : count - 1) : 0);
            continue;
        }
        char text[160];
        bl_settings_field_text(st, st->item, r, text, sizeof text);
        int dim = !strcmp(text, bl_str(BL_S_NOT_SET)) || !strcmp(text, bl_str(BL_S_PORT_DEFAULT));
        if (st->item == BL_SET_ONLINE && c->online_mode != BL_ONLINE_PRIVATE) dim = 1;
        bl_field(f, right, y, row->field == BL_FIELD_PORT ? 260 : 520, text, dim);
    }

    y += 8;
    const char *msg = st->message_text[0] ? st->message_text : st->message >= 0 ? bl_str((BlStringId)st->message) : NULL;
    if (msg && st->item == BL_SET_FRAMERATE && st->message == BL_S_FRAMERATE_UNTESTED && !st->message_text[0]) msg = NULL;   /* the note says it */
    if (msg) {
        int bad = st->message == BL_S_INVALID_HOST || st->message == BL_S_INVALID_PORT || st->message == BL_S_INVALID_TEXT ||
                  st->message == BL_S_NET_FAIL || st->message == BL_S_NET_NO_HOST || st->message == BL_S_SETTINGS_SAVE_FAILED ||
                  st->message == BL_S_NET_UNRESOLVED || st->message == BL_S_ACCOUNT_INVALID || st->message == BL_S_ACCOUNT_INCOMPLETE ||
                  st->message == BL_S_ACCOUNT_SAVE_FAILED || st->message == BL_S_BUNDLE_FAILED;
        if (btn_y >= 0 && bl_settings_rows(st, st->item) <= 2)          /* results of a button go next to it */
            bl_note(f, btn_right + 28, btn_y + 6, px + pw - 44 - btn_right - 28, msg, bad ? BL_RGB_AMBER : BL_RGB_CYAN, BL_FONT_NOTE);
        else
            y = bl_note(f, px + 44, y, pw - 88, msg, bad ? BL_RGB_AMBER : BL_RGB_CYAN, BL_FONT_NOTE) + 6;
    }
    switch (st->item) {
    case BL_SET_ONLINE: bl_note(f, px + 44, py + ph - 100, pw - 88, bl_str(BL_S_NOTE_ONLINE), 0xA9BBDC, BL_FONT_NOTE); break;
    case BL_SET_FRAMERATE: {   /* the help of the value shown; amber when 60 was refused */
        char note[400];
        bl_settings_note(st, BL_SET_FRAMERATE, note, sizeof note);
        bl_note(f, px + 44, y + 6, pw - 88, note, st->message == BL_S_FRAMERATE_UNTESTED ? BL_RGB_AMBER : 0xA9BBDC, BL_FONT_NOTE);
        break;
    }
    case BL_SET_STATS: {
        int bx = px + 44, by = y + 6, bw = pw - 88, bh = 260;
        bl_round_rect(f, bx, by, bw, bh, 14, 0x1B2230, 0x1F1E1D, 255, 0x4696FF, 2, 0, 0);
        bl_trail(f, bx + 6, by + bh * 52 / 100, bx + bw - 8, by + bh * 52 / 100, 6, BL_RGB_TRAIL, 200, 0);
        bl_trail(f, bx + 6, by + bh * 60 / 100, bx + bw - 8, by + bh * 60 / 100, 4, 0x2A7BFF, 150, 0);
        if (c->stats != BL_STATS_OFF) {
            const char *hud = "30 FPS  33.3 ms";   /* audit 24: the product HUD ("%.0f FPS  %.1f ms") */
            int tw = bl_text_width(BL_FONT_NOTE, hud);
            bl_text(f, BL_FONT_NOTE, bx + bw - tw - 14 + 2, by + 14 + 2, hud, 0x000000);
            bl_text(f, BL_FONT_NOTE, bx + bw - tw - 14, by + 14, hud, BL_RGB_WHITE);
        }
        bl_note(f, px + 44, by + bh + 14, pw - 88, bl_str(BL_S_NOTE_STATS), 0xA9BBDC, BL_FONT_NOTE);
        break;
    }
    case BL_SET_ACCOUNT:
    {   /* where to get an account (finding 29: refer only), then where it is kept */
        char note[400];
        bl_settings_note(st, BL_SET_ACCOUNT, note, sizeof note);
        int h = bl_note(NULL, 0, 0, pw - 88, bl_str(BL_S_NOTE_ACCOUNT_CREATE), 0, BL_FONT_NOTE) + 6 + bl_note(NULL, 0, 0, pw - 88, note, 0, BL_FONT_NOTE);
        y = bl_note(f, px + 44, py + ph - 30 - h, pw - 88, bl_str(BL_S_NOTE_ACCOUNT_CREATE), 0xA9BBDC, BL_FONT_NOTE) + 6;
        bl_note(f, px + 44, y, pw - 88, note, 0xA9BBDC, BL_FONT_NOTE);
        break;
    }
    case BL_SET_SUPPORT: {
        char note[400];
        bl_settings_note(st, BL_SET_SUPPORT, note, sizeof note);
        bl_note(f, px + 44, py + ph - 130, pw - 88, note, 0xA9BBDC, BL_FONT_NOTE);
        break;
    }
    case BL_SET_LOGGING: {
        char line[96];
        bl_log_summary(line, sizeof line, st->log_count, st->log_bytes);
        bl_text(f, BL_FONT_NOTE, px + 44, y, line, 0xE0E8F8);
        char note[400];
        bl_settings_note(st, BL_SET_LOGGING, note, sizeof note);
        bl_note(f, px + 44, y + bl_font_line_height(BL_FONT_NOTE) + 10, pw - 88, note, 0xA9BBDC, BL_FONT_NOTE);
        break;
    }
    }
}

void bl_log_summary(char *out, size_t cap, int count, long long bytes)
{
    if (bytes < 1024LL * 1024LL) {
        long long kb = bytes > 0 ? (bytes + 1023) / 1024 : 0;
        snprintf(out, cap, bl_str_n(BL_S_LOG_SUMMARY_KB_ONE, BL_S_LOG_SUMMARY_KB, count), count, kb);
    } else {
        snprintf(out, cap, bl_str_n(BL_S_LOG_SUMMARY_ONE, BL_S_LOG_SUMMARY, count), count, (double)bytes / (1024.0 * 1024.0));
    }
}

BlSettingsState bl_settings_last_drawn;

/* Static parts in drawing order: background to top bar, then the panel. */
static void static_top(BlFrame *f)
{
    bl_screen_background(f, BL_BG_LIST);
    bl_fill_alpha(f, 0, 0, (int)f->width, (int)f->height, 0x05080F, 50);
    bl_top_bar_frame(f, BL_ICON_GEAR);
}

/* The pill header: after the status items, which never reach it. */
static void static_pill(BlFrame *f) { bl_pill_header(f, BL_ICON_GEAR, bl_str(BL_S_SETTINGS_TITLE)); }

static void static_panel(BlFrame *f) { bl_glass(f, PX, PY, PW, PH, 24, 0); }

/* Everything after the panel glass. */
static void after_panel(BlFrame *f, const BlSettingsState *st)
{
    draw_panel(f, st);
    bl_band(f, bl_str(ITEMS[st->item].band));
    if (st->in_panel) {
        const BlHint hints[] = { { BL_KEY_CROSS, BL_S_HINT_EDIT }, { BL_KEY_LEFTRIGHT, BL_S_HINT_CHANGE },
                                 { BL_KEY_UPDOWN, BL_S_HINT_MOVE }, { BL_KEY_CIRCLE, BL_S_HINT_BACK } };
        bl_hints(f, hints, 4);
    } else {
        const BlHint hints[] = { { BL_KEY_CROSS, BL_S_HINT_SELECT }, { BL_KEY_UPDOWN, BL_S_HINT_MOVE },
                                 { BL_KEY_CIRCLE, BL_S_HINT_SAVE_BACK } };
        bl_hints(f, hints, 3);
    }
}

void bl_settings_draw(BlFrame *f, const BlSettingsState *st)
{
    bl_settings_last_drawn = *st;
    bl_frame_key_set(f, bl_hash_bytes(bl_layer_key("settings", 0), st, sizeof *st));
    BlTopItem top[1];
    int n = 0;
    if (st->cfg.logging) top[n++] = (BlTopItem){ bl_str(BL_S_STATUS_LOGGING_ON), BL_RGB_AMBER, 2 };
    /* The list is drawn before the panel glass. The layer holds the glass;
     * a list clear of the glass (and its glow) goes on top as it is. A
     * longer one reaching into the strip under the glass's left edge: that
     * strip as it was before the glass, the list, then the glass again
     * over the strip only (the same pixels). Longer still: drawn in order. */
    enum { SX = PX - 36, SY = PY - 36, SW = 320, SH = PH + 72 };
    int right = draw_list(NULL, st) + 12;            /* room for glyphs past their advance */
    int clear = right < SX, strip = !clear && right <= SX + SW;
    uint64_t key = bl_layer_key("settings", 0);
    if (clear || strip) {
        if (!bl_layer_restore(f, key) || (strip && !bl_rect_restore(f, key, SX, SY, SW, SH))) {
            static_top(f);
            static_pill(f);
            if (!bl_rect_save(f, key, SX, SY, SW, SH)) strip = 0;
            static_panel(f);
            bl_layer_save(f, key);
            if (strip) bl_rect_restore(f, key, SX, SY, SW, SH);
        }
    }
    if (!clear && !strip) static_top(f);
    bl_top_bar_items(f, top, n);
    if (!clear && !strip) static_pill(f);
    draw_list(f, st);
    if (strip) {
        BlFrame under = { (uint8_t *)f->pixels + (size_t)SY * f->stride + (size_t)SX * 4, SW, SH, f->stride };
        bl_glass(&under, PX - SX, PY - SY, PW, PH, 24, 0);
    } else if (!clear) {
        static_panel(f);
    }
    after_panel(f, st);
}
