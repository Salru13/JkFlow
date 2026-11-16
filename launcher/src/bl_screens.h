/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Screens and the frame every screen shares (spec 6, 7): top bar with badge,
 * logo and status; lower-case title; soft description band; button hints
 * bottom left. This file has the Start carousel; later screens (patch blur,
 * settings, help) reuse the shared parts.
 */
#ifndef BL_SCREENS_H
#define BL_SCREENS_H

#include "bl_frontend.h"
#include "bl_strings.h"
#include "bl_ui.h"

/* ---- shared parts ---- */

typedef enum BlIcon { BL_ICON_PLAY = 0, BL_ICON_WRENCH, BL_ICON_GEAR, BL_ICON_INFO,
                      BL_ICON_GLOBE, BL_ICON_USER, BL_ICON_CONSOLE, BL_ICON_CHART, BL_ICON_LOG, BL_ICON_DISPLAY } BlIcon;
typedef enum BlKey { BL_KEY_CROSS = 0, BL_KEY_CIRCLE, BL_KEY_TRIANGLE, BL_KEY_SQUARE,
                     BL_KEY_LEFTRIGHT, BL_KEY_UPDOWN } BlKey;
typedef struct BlHint { BlKey key; BlStringId text; } BlHint;
typedef struct BlTopItem { const char *text; uint32_t rgb; int tick; /* 0 none, 1 cyan, 2 amber */ } BlTopItem;

/* Pictogram drawn in a size x size box centred on (cx, cy). */
void bl_icon(BlFrame *f, BlIcon icon, int cx, int cy, int size, uint32_t rgb);
void bl_top_bar(BlFrame *f, BlIcon badge, const BlTopItem *items, int count);
/* bl_top_bar in two parts: the bar with the badge, then the status items. */
void bl_top_bar_frame(BlFrame *f, BlIcon badge);
void bl_top_bar_items(BlFrame *f, const BlTopItem *items, int count);
/* One part of the top bar alone (tests: the parts never overlap, owner
 * finding 33): the badge (disc, glow, pictogram), the JkFlow logo, the bar. */
typedef enum BlTopPart { BL_TOP_BADGE = 0, BL_TOP_LOGO, BL_TOP_BAR } BlTopPart;
void bl_top_bar_part(BlFrame *f, BlTopPart part, BlIcon badge);
void bl_band(BlFrame *f, const char *text);
/* bl_band in two parts: the soft band, then its text. */
void bl_band_fill(BlFrame *f);
void bl_band_text(BlFrame *f, const char *text);
/* The y below the band text (it wraps when long). */
int bl_band_text_bottom(const BlFrame *f, const char *text);

/* Screen layers: the parts of a screen that do not change, cached under a
 * key (see bl_screens.c). restore: 1 when f now holds the layer. */
uint64_t bl_layer_key(const char *screen, uint64_t extra);
int bl_layer_restore(BlFrame *f, uint64_t key);
void bl_layer_save(const BlFrame *f, uint64_t key);
/* One small rectangle kept under a key (what lies under a panel). 1: done. */
int bl_rect_save(const BlFrame *f, uint64_t key, int x, int y, int w, int h);
int bl_rect_restore(BlFrame *f, uint64_t key, int x, int y, int w, int h);
/* What a screen just drew into f, for the question drawn over it. */
uint64_t bl_hash_bytes(uint64_t h, const void *p, size_t n);
void bl_frame_key_set(const BlFrame *f, uint64_t key);
/* 1 when the layer under key is cached (bl_layer_restore would succeed for
 * a frame of that size). */
int bl_layer_cached(uint64_t key, const BlFrame *f);
/* Cross-fade (owner finding 34): f += (from - f) * w / 128 per channel,
 * from a frame of f's size and stride; w 0..128 (128: f becomes from). */
void bl_frame_mix(BlFrame *f, const uint8_t *from, int w);
/* Tests: 0 draws every frame directly (no caches), as before 2026-11-15. */
void bl_screens_set_cache(int on);
/* Free the screen layers and card images but keep the backgrounds. */
void bl_screens_release_layers(void);
void bl_hints(BlFrame *f, const BlHint *hints, int count);
/* The key cap the hints draw, top left at (x, y), 36 px high. Returns its width. */
int bl_key_cap(BlFrame *f, BlKey key, int x, int y);
/* Whole-frame background from a cache (computed once per kind and size). */
void bl_screen_background(BlFrame *f, BlBackground kind);
/* Blue pill header with a ring icon, top left under the top bar (list screens). */
void bl_pill_header(BlFrame *f, BlIcon icon, const char *title);
/* Panel content helpers (y is the top of the row). */
void bl_panel_title(BlFrame *f, int x, int y, const char *title);
void bl_row(BlFrame *f, int x, int y, int w, const char *label, int focused);
/* Segmented value, right-aligned to right_x. Returns its left edge. */
int bl_segments(BlFrame *f, int right_x, int y, const BlStringId *options, int count, int selected);
/* The same with the options in `greyed` (bit per option) drawn as not available. */
int bl_segments_greyed(BlFrame *f, int right_x, int y, const BlStringId *options, int count, int selected, unsigned greyed);
/* Text field, right-aligned to right_x. */
void bl_field(BlFrame *f, int right_x, int y, int w, const char *text, int dim);
/* Plain table: header row plus rows of up to 4 cells; colours per cell (0 = default). */
int bl_table(BlFrame *f, int x, int y, int width, const int *col_x, int cols, const char *const *cells,
             const uint32_t *colours, int rows, BlFont font);
/* Wrapped note text, returns the y below it. */
int bl_note(BlFrame *f, int x, int y, int w, const char *text, uint32_t rgb, BlFont font);
/* The same with {square} and {cross} drawn as key caps. Returns the y below it. */
int bl_note_keys(BlFrame *f, int x, int y, int w, const char *text, uint32_t rgb, BlFont font);
void bl_button(BlFrame *f, int x, int y, const char *text, int focused);

/* ---- Start carousel ---- */

/* Owner finding 32 (2026-11-15): no About card (credits in the README). */
typedef enum BlCard { BL_CARD_SETTINGS = 0, BL_CARD_PLAY, BL_CARD_PATCH, BL_CARD_COUNT } BlCard;

/* A slide takes BL_SLIDE_NS, eased out (owner finding 34: at least 8 frames
 * at 60 Hz); a press during a slide turns it from where it is. */
#define BL_SLIDE_NS 150000000ull

typedef struct BlStartState {
    int focus;          /* BlCard */
    float pos;          /* animated position of the carousel, moves toward focus */
    int band;           /* BlStringId shown in the band */
    int target;         /* the focus the running slide heads for */
    float from;         /* where it started */
    uint64_t t0;        /* when (ns) */
} BlStartState;

typedef struct BlStatus {
    int readiness;      /* BlReadiness */
    int net_up;
    char ipv4[32];
    char build[48];     /* e.g. "BLUR-1.00", may be empty; never drawn (events.log and support bundle only) */
    int logging;        /* "Logging on" in amber when set */
    int pads_known;     /* the title reports controllers (else nothing is shown) */
    int pads;           /* controllers connected */
    int online;         /* BL_ONLINE_NONE / READY / PARTIAL (bl_patch_online_state) */
} BlStatus;

typedef enum BlStartEvent { BL_START_NONE = 0, BL_START_PLAY, BL_START_OPEN } BlStartEvent;

void bl_start_init(BlStartState *st);
/* Apply newly pressed buttons. BL_START_PLAY: start the game now. When play
 * is pressed but the game is not ready, the band shows why and focus moves
 * to patch blur. Other cards open screens of later plan tasks; until then
 * the band says so. */
BlStartEvent bl_start_input(BlStartState *st, uint32_t pressed, int readiness);
/* The slide at time now_ns (any monotonic clock): a new focus starts a
 * slide from the current position. 1: pos changed (draw it), 0: still. */
int bl_start_animate(BlStartState *st, uint64_t now_ns);
void bl_start_draw(BlFrame *f, const BlStartState *st, const BlStatus *status);
/* Idle time on Start: build one card image the next slide will need, so the
 * slide itself only moves images. 1: built one (call again), 0: all ready. */
int bl_start_prepare(const BlStartState *st);
/* What a Start frame with this state shows (for bl_dialog_draw_cached). */
uint64_t bl_start_key(const BlStartState *st, const BlStatus *status);
/* The band string id of the last Start frame drawn (for tests). */
extern int bl_start_last_band;
/* The status of the last Start frame drawn (for tests). */
extern BlStatus bl_start_last_status;
/* The carousel position of the last Start frame drawn (for tests). */
extern float bl_start_last_pos;
/* Free every cache: backgrounds, screen layers, card images, small logo,
 * text halos (at most 48 MB, each piece at most one frame); they are
 * rebuilt on demand. bl_frontend_run calls this before it returns. */
void bl_screens_release(void);
size_t bl_screens_cache_bytes(void);

/* ---- Patch (BLUR 1.00 plan Task L9): nine steps, one button ---- */

#include "bl_patcher.h"

#define BL_PATCH_STEPS 9
typedef enum BlStepState { BL_STEP_WAIT = 0, BL_STEP_RUN, BL_STEP_DONE, BL_STEP_FAIL } BlStepState;

typedef struct BlPatchView {
    int step;                 /* the step running or last run, 0..8 */
    BlStepState steps[BL_PATCH_STEPS];
    int busy;                 /* Patch running: progress shown, input ignored */
    int message;              /* BlStringId, or -1 */
    char message_text[320];   /* formatted message (overrides message when set) */
    char progress_name[96];
    unsigned long long done, total;   /* progress of the running step */
    int exe_verdict;          /* BlExeVerdict of the game folder (information) */
    int choice;               /* 0 Patch for offline play, 1 Patch for online play (AMAX) */
    int online;               /* BL_ONLINE_*: the set in the game folder (the offline note says "back to offline") */
    int confirm_restore;      /* Triangle pressed once: the next Triangle restores */
    int downloading;          /* busy with a download: "Downloading <progress_name>" */
    int help_line;            /* an online Patch stopped (no network, download): "Press Square Help" under the cause */
    char game_dir[128];       /* audit 24: the game folder as spelled on disk ("No Blur.exe in <it> yet") */
} BlPatchView;

/* Up/Down choose, Cross applies the choice, Square Help, Triangle twice
 * Restore, Circle back. */
typedef enum BlPatchEvent { BL_PATCH_EV_NONE = 0, BL_PATCH_EV_APPLY, BL_PATCH_EV_BACK, BL_PATCH_EV_HELP,
                            BL_PATCH_EV_RESTORE } BlPatchEvent;

BlPatchEvent bl_patch_input(BlPatchView *v, uint32_t pressed);
void bl_patch_draw(BlFrame *f, const BlPatchView *v);
/* The line over the progress bar (the running file, or the result). */
void bl_patch_status_line(const BlPatchView *v, char *out, size_t cap);
/* A copy of the view of the last Patch frame drawn (for tests). */
extern BlPatchView bl_patch_last_drawn;

/* ---- a question over the current screen ---- */

typedef struct BlDialog {
    int body;                 /* BlStringId of the question */
    char body_text[480];      /* formatted question (overrides body when set) */
    int first, second;        /* BlStringId of the two buttons */
    int focus;                /* 0 first, 1 second */
    int circle;               /* what Circle answers: 1 or 2 */
} BlDialog;

/* Focus starts on the first button. second < 0: one button only. */
void bl_dialog_init(BlDialog *d, int body, int first, int second, int circle);
/* 1 first, 2 second, 0 nothing yet. Left/Right move, Cross picks, Circle answers d->circle. */
int bl_dialog_input(BlDialog *d, uint32_t pressed);
/* Dims the frame and draws the question on top. */
void bl_dialog_draw(BlFrame *f, const BlDialog *d);
/* The question over a screen whose key is under (bl_start_key), from the
 * cache without drawing that screen first. 0: not cached, draw both. */
int bl_dialog_draw_cached(BlFrame *f, const BlDialog *d, uint64_t under);
extern BlDialog bl_dialog_last_drawn;

/* ---- settings (mockup frames 4-6) ---- */

#include "bl_config.h"
#include "bl_features.h"

/* BLUR 1.00: Frame rate, Show FPS / ms, Enable logs, Online account, Save
 * support bundle. The old private-server item stays hidden while BL_ONLINE_UI is 0. */
typedef enum BlSetItem { BL_SET_ONLINE = 0, BL_SET_FRAMERATE, BL_SET_STATS, BL_SET_LOGGING, BL_SET_ACCOUNT, BL_SET_SUPPORT, BL_SET_COUNT } BlSetItem;
#define BL_SET_FIRST (BL_ONLINE_UI ? BL_SET_ONLINE : BL_SET_STATS)
typedef enum BlField { BL_FIELD_NONE = 0, BL_FIELD_SERVER, BL_FIELD_PORT, BL_FIELD_USERNAME, BL_FIELD_PASSWORD } BlField;
typedef enum BlSetEvent { BL_SET_EV_NONE = 0, BL_SET_EV_BACK, BL_SET_EV_EDIT, BL_SET_EV_TEST, BL_SET_EV_CLEAR_LOGS,
                          BL_SET_EV_BUNDLE, BL_SET_EV_ACCOUNT_SAVE, BL_SET_EV_ACCOUNT_REMOVE } BlSetEvent;

typedef struct BlSettingsState {
    int item;                 /* BlSetItem */
    unsigned hidden;          /* bit per BlSetItem not listed (a service the title does not offer) */
    int confirm_row;          /* row waiting for a second Cross, or -1 */
    int in_panel, row;
    BlConfig cfg;
    int password_set;         /* hidden private-server item only */
    int message;              /* BlStringId, or -1 */
    char message_text[200];   /* formatted message (overrides message when set) */
    int log_count;
    long long log_bytes;
    char account_user[32];    /* Online account (AMAX): username, 1..31 bytes, or "" */
    int account_pass_set;     /* a password is entered (the text itself stays with the frontend) */
    char logs_dir[96], support_dir[96];   /* where logs and support bundles go (BlPaths) */
    char state_dir[96];       /* where the account is kept (BlPaths state) */
    int exe_untested;         /* Blur.exe is not the tested one: Frame rate stays at 30 FPS */
} BlSettingsState;

void bl_settings_init(BlSettingsState *st, const BlConfig *cfg);
/* "7 log files · 41 KB": whole KB (rounded up) under 1 MB, else "· 2.0 MB" (owner finding 40). */
void bl_log_summary(char *out, size_t cap, int count, long long bytes);
/* Take an item off the list (focus moves to the first listed item). */
void bl_settings_hide(BlSettingsState *st, int item);
int bl_settings_listed(const BlSettingsState *st, int item);
/* *field is set for BL_SET_EV_EDIT. */
BlSetEvent bl_settings_input(BlSettingsState *st, uint32_t pressed, BlField *field);
/* Validate and store text typed for a field. 0 ok; <0 rejected (message set). */
int bl_settings_set_text(BlSettingsState *st, BlField field, const char *text);
int bl_settings_rows(const BlSettingsState *st, int item);
/* The text a field row shows (passwords as stars, never the text). */
void bl_settings_field_text(const BlSettingsState *st, int item, int row, char *out, size_t cap);
void bl_settings_draw(BlFrame *f, const BlSettingsState *st);
/* The panel note of an item with the real folders filled in ("" none). */
void bl_settings_note(const BlSettingsState *st, int item, char *out, size_t cap);
/* A copy of the state of the last settings frame drawn (for tests). */
extern BlSettingsState bl_settings_last_drawn;

/* ---- help: setup instructions (owner spec 2026-11-15 v3) ---- */

typedef enum BlHelpSection { BL_HELP_BEFORE = 0, BL_HELP_FOLDER, BL_HELP_OFFLINE, BL_HELP_ONLINE, BL_HELP_ACCOUNT, BL_HELP_SAVES, BL_HELP_TROUBLE } BlHelpSection;
typedef struct BlHelpState {
    int scroll;               /* the first paragraph shown */
    char note[320];           /* why Help opened (a failed download), shown on top in amber; "" none */
    /* The folders the text names (bl_help_init: the default layout; the
     * frontend sets its own): game, the zips (its parent), save import,
     * Blur's save folder in the prefix. */
    char game_dir[96], zip_dir[96], import_dir[96], saves_dir[256];
} BlHelpState;

void bl_help_init(BlHelpState *st, BlHelpSection at);
int bl_help_input(BlHelpState *st, uint32_t pressed);     /* 1: back */
void bl_help_draw(BlFrame *f, const BlHelpState *st);
int bl_help_section(BlHelpSection s);                     /* its first paragraph */
int bl_help_count(void);                                  /* paragraphs */
int bl_help_para_id(int i);                               /* BlStringId of paragraph i, or -1 */
/* Paragraph i with the folders filled in. */
void bl_help_text(const BlHelpState *st, int i, char *out, size_t cap);
extern BlHelpState bl_help_last_drawn;

#endif
