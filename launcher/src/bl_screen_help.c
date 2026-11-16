/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* "help" screen (owner spec 2026-11-15 v3): how to set up Blur for offline
 * and online play, and what to do when something goes wrong. A simple page
 * of paragraphs; Up/Down scroll by paragraph. Patch opens
 * it at the online section when the two zips are not there. */
#include "bl_screens.h"

#include "bl_paths.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef enum Kind { HEAD = 0, BODY, PATH, ITEM } Kind;
typedef struct Para { BlStringId text; Kind kind; } Para;

static const Para PARAS[] = {
    { BL_S_HELP_0_HEAD, HEAD }, { BL_S_HELP_0_DATA, BODY }, { BL_S_HELP_0_LEGAL, BODY },
    { BL_S_HELP_A_HEAD, HEAD }, { BL_S_HELP_A_BODY, BODY }, { BL_S_HELP_A_DIR, PATH }, { BL_S_HELP_A_LOOK, BODY },
    { BL_S_HELP_A_EXE, PATH }, { BL_S_HELP_A_BINK, PATH }, { BL_S_HELP_A_REST, PATH }, { BL_S_HELP_A_AFTER, BODY },
    { BL_S_HELP_B_HEAD, HEAD }, { BL_S_HELP_B_BODY, BODY },
    { BL_S_HELP_C_HEAD, HEAD }, { BL_S_HELP_C_BODY, BODY }, { BL_S_HELP_C_ZIP1, PATH }, { BL_S_HELP_C_ZIP2, PATH },
    { BL_S_HELP_C_THEN, BODY },
    { BL_S_HELP_D_HEAD, HEAD }, { BL_S_HELP_D_BODY, BODY },
    { BL_S_HELP_S_HEAD, HEAD }, { BL_S_HELP_S_BODY, BODY }, { BL_S_HELP_S_TO, PATH }, { BL_S_HELP_S_THEN, BODY },
    { BL_S_HELP_S_BACK, BODY },
    { BL_S_HELP_E_HEAD, HEAD }, { BL_S_HELP_E_FOLDER, ITEM }, { BL_S_HELP_E_TWO, ITEM }, { BL_S_HELP_E_ONE_ZIP, ITEM },
    { BL_S_HELP_E_BAD_ZIP, ITEM }, { BL_S_HELP_E_NO_NET, ITEM }, { BL_S_HELP_E_CLOCK, ITEM }, { BL_S_HELP_E_EXE, ITEM },
    { BL_S_HELP_E_OFFLINE, ITEM }, { BL_S_HELP_E_RESTORE, ITEM }, { BL_S_HELP_E_CLOSE, ITEM },
};
#define COUNT ((int)(sizeof PARAS / sizeof PARAS[0]))
static const BlStringId SECTION_HEAD[] = { BL_S_HELP_0_HEAD, BL_S_HELP_A_HEAD, BL_S_HELP_B_HEAD, BL_S_HELP_C_HEAD, BL_S_HELP_D_HEAD, BL_S_HELP_S_HEAD, BL_S_HELP_E_HEAD };

BlHelpState bl_help_last_drawn;

int bl_help_count(void) { return COUNT; }

int bl_help_para_id(int i) { return i >= 0 && i < COUNT ? (int)PARAS[i].text : -1; }

/* {game}, {zips}, {import} and {saves} replaced by the folders in st. */
void bl_help_text(const BlHelpState *st, int i, char *out, size_t cap)
{
    if (!out || !cap) return;
    out[0] = 0;
    if (i < 0 || i >= COUNT) return;
    static const struct { const char *name; size_t off; } VARS[] = {
        { "{game}", offsetof(BlHelpState, game_dir) }, { "{zips}", offsetof(BlHelpState, zip_dir) },
        { "{import}", offsetof(BlHelpState, import_dir) }, { "{saves}", offsetof(BlHelpState, saves_dir) },
    };
    size_t n = 0;
    for (const char *p = bl_str(PARAS[i].text); *p && n + 1 < cap;) {
        const char *val = NULL;
        size_t skip = 0;
        for (size_t v = 0; v < sizeof VARS / sizeof VARS[0]; v++)
            if (!strncmp(p, VARS[v].name, strlen(VARS[v].name))) { val = (const char *)st + VARS[v].off; skip = strlen(VARS[v].name); break; }
        if (val) {
            for (; *val && n + 1 < cap; val++) out[n++] = *val;
            p += skip;
        } else {
            out[n++] = *p++;
        }
    }
    out[n] = 0;
}

int bl_help_section(BlHelpSection s)
{
    if ((unsigned)s >= sizeof SECTION_HEAD / sizeof SECTION_HEAD[0]) return 0;
    for (int i = 0; i < COUNT; i++) if (PARAS[i].text == SECTION_HEAD[s]) return i;
    return 0;
}

void bl_help_init(BlHelpState *st, BlHelpSection at)
{
    memset(st, 0, sizeof *st);
    st->scroll = bl_help_section(at);
    BlPaths p;                                 /* the default layout; the frontend sets its own */
    bl_paths_init(&p, NULL);
    snprintf(st->game_dir, sizeof st->game_dir, "%s/%s/%s", BL_GAME_ROOT, BL_GAME_PARENT, BL_GAME_LEAF);
    snprintf(st->zip_dir, sizeof st->zip_dir, "%s/%s", BL_GAME_ROOT, BL_GAME_PARENT);
    snprintf(st->import_dir, sizeof st->import_dir, "%s", p.import);
    snprintf(st->saves_dir, sizeof st->saves_dir, "%s", p.saves);
}

int bl_help_input(BlHelpState *st, uint32_t pressed)
{
    if (pressed & BL_PAD_CIRCLE) return 1;
    if (pressed & BL_PAD_DOWN && st->scroll < COUNT - 1) st->scroll++;
    if (pressed & BL_PAD_UP && st->scroll > 0) st->scroll--;
    return 0;
}

/* One paragraph at y; returns the y below it. f NULL: measure only. */
static int para(BlFrame *f, int x, int y, int w, const BlHelpState *st, int i)
{
    const Para *p = &PARAS[i];
    char s[600];
    bl_help_text(st, i, s, sizeof s);
    switch (p->kind) {
    case HEAD:
        y += 6;
        bl_text(f, BL_FONT_BODY, x, y, s, BL_RGB_SUBTITLE);
        return y + bl_font_line_height(BL_FONT_BODY);
    case PATH:
        bl_text(f, BL_FONT_NOTE, x + 40, y, s, BL_RGB_CREAM);
        return y + bl_font_line_height(BL_FONT_NOTE);
    case ITEM:
        bl_text(f, BL_FONT_NOTE, x + 4, y, "\xE2\x80\xA2", 0xA9BBDC);
        return bl_note(f, x + 30, y, w - 30, s, BL_RGB_WHITE, BL_FONT_NOTE) + 2;
    case BODY: default:
        return bl_note(f, x, y, w, s, BL_RGB_WHITE, BL_FONT_NOTE) + 4;
    }
}

void bl_help_draw(BlFrame *f, const BlHelpState *st)
{
    bl_help_last_drawn = *st;
    bl_frame_key_set(f, bl_hash_bytes(bl_layer_key("help", 0), st, sizeof *st));
    const int px = 150, py = 290, pw = (int)f->width - 300, ph = 560;
    uint64_t key = bl_layer_key("help", 0);
    if (!bl_layer_restore(f, key)) {                 /* the parts that never change come first */
        bl_screen_background(f, BL_BG_LIST);
        bl_fill_alpha(f, 0, 0, (int)f->width, (int)f->height, 0x05080F, 80);
        bl_top_bar_frame(f, BL_ICON_INFO);
        bl_pill_header(f, BL_ICON_INFO, bl_str(BL_S_HELP_TITLE));
        bl_glass(f, px, py, pw, ph, 24, 0);
        bl_layer_save(f, key);
    }
    int x = px + 44, w = pw - 88, y = py + 26, bottom = py + ph - 24;
    if (st->note[0]) y = bl_note_keys(f, x, y, w, st->note, BL_RGB_AMBER, BL_FONT_NOTE) + 8;   /* a download result may name a key */
    int first = st->scroll < 0 ? 0 : st->scroll >= COUNT ? COUNT - 1 : st->scroll;
    for (int i = first; i < COUNT; i++) {
        if (para(NULL, x, y, w, st, i) > bottom) break;         /* only whole paragraphs */
        y = para(f, x, y, w, st, i);
    }

    bl_band(f, bl_str(BL_S_BAND_HELP));
    const BlHint hints[] = { { BL_KEY_UPDOWN, BL_S_HINT_SCROLL }, { BL_KEY_CIRCLE, BL_S_HINT_BACK } };
    bl_hints(f, hints, 2);
}
