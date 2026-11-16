/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The single seam between the BLUR launcher UI and the title that hosts it.
 *
 * The host fills BlServices with callbacks (video out, pad, clock,
 * network, game state) and calls bl_frontend_run(). The UI calls no PS4 API
 * itself, so every screen runs in host tests. The host acts on the returned
 * BlDecision: start the game, or leave to home.
 */
#ifndef BL_FRONTEND_H
#define BL_FRONTEND_H

#include <stddef.h>
#include <stdint.h>

#include "bl_ui.h"

/* Pad buttons as scePadRead reports them (same values as pw_game_menu.h).
 * The host maps the left stick onto the D-pad bits. */
enum {
    BL_PAD_L3 = 0x2u, BL_PAD_R3 = 0x4u, BL_PAD_OPTIONS = 0x8u,
    BL_PAD_UP = 0x10u, BL_PAD_RIGHT = 0x20u, BL_PAD_DOWN = 0x40u, BL_PAD_LEFT = 0x80u,
    BL_PAD_L1 = 0x400u, BL_PAD_R1 = 0x800u,
    BL_PAD_TRIANGLE = 0x1000u, BL_PAD_CIRCLE = 0x2000u, BL_PAD_CROSS = 0x4000u, BL_PAD_SQUARE = 0x8000u
};

/* Whether Play may start the game. */
typedef enum BlReadiness {
    BL_READY_NO_GAME = 0,       /* no Blur.exe in the game folder */
    BL_READY_OK,
    BL_READY_UNPATCHED,         /* BLUR files missing or different */
    BL_READY_UNSUPPORTED        /* Blur.exe is not the tested 1.2 build (warning only) */
} BlReadiness;

typedef struct BlServices {
    void *ctx;
    /* Show a finished frame (1920x1080 BGRX). <0: display lost, the run ends.
     * While something animates the loop calls it back to back without
     * sleeping (owner finding 34): it should wait for a free buffer (the
     * title's flip waits while two flips are pending). */
    int  (*present)(void *ctx, const BlFrame *frame);
    /* Buttons held and newly pressed since the last call. <0: no controller yet. May be NULL. */
    int  (*pad)(void *ctx, uint32_t *held, uint32_t *pressed);
    /* The PS4 system keyboard. NOT USED (owner finding 12: it opened in the
     * top left corner and could not be used); the launcher has its own
     * keyboard (bl_keyboard). Kept so the title still compiles; may be NULL. */
    int  (*osk)(void *ctx, const char *title, int secret, char *out, size_t cap);
    /* 1 when the system asked the app to close: the run ends with
     * BL_LEAVE_HOME. May be NULL. */
    int  (*should_quit)(void *ctx);
    uint64_t (*now_ns)(void *ctx);
    void (*sleep_us)(void *ctx, uint32_t us);
    /* 0: link up, console IPv4 in ipv4. May be NULL (shown as offline). */
    int  (*net_status)(void *ctx, char *ipv4, size_t cap);
    /* Resolve host and TCP-connect (5 s). Kept for diagnostics. May be NULL. */
    int  (*net_probe)(void *ctx, const char *host, int port, char *resolved, size_t cap, int *ms);
    /* One "HEAD / HTTP/1.0" to host:80, 3 s in all; *epoch = its Date
     * header. 0 ok, <0 no answer. May be NULL (the server time is unknown). */
    int  (*http_date)(void *ctx, const char *host, long long *epoch);
    /* Download url to dest (at most max_bytes). 0 ok. May be NULL: Patch then
     * takes the AMAX client files only from the package or import/ (D2). */
    int  (*https_get)(void *ctx, const char *url, const char *dest, long long max_bytes,
                      void (*prog)(void *, long long, long long), void *pctx);
    /* BlReadiness of the game folder. Used only when game_dir or
     * package_files_dir is NULL. May be NULL (treated as no game). */
    int  (*game_state)(void *ctx);
    /* Told the name of each screen when it changes ("start", "patch",
     * "settings", "help", "error", "dialog", "keyboard"). May be NULL. */
    void (*screen)(void *ctx, const char *name);
    /* data_root: the JkFlow root (/data/JkFlow/System, bl_paths.h): state/, backup/,
     * import/ ... (logs: BlPaths.logs) game_dir: the Blur folder (<root>/game).
     * package_files_dir: the package's blur-files/ (manifest.txt, model-*.ini).
     * Any may be NULL. */
    const char *data_root, *game_dir, *package_files_dir;
    /* When set (the title: "/data"), the game folder is <game_root>/JkFlow/Blur in
     * any letter case (bl_game_dir_find), re-checked at start, Play and Patch;
     * game_dir is then not used. Several spellings stop Play and Patch. */
    const char *game_root;
    /* Build id shown bottom right, e.g. "BLUR-1.00". May be NULL. */
    const char *build_id;
    /* Set when the previous start failed before the game's first frame: the
     * run opens on the "Start failed" screen. Never a path in start_failure;
     * start_failure_log is the session log's file name only. */
    const char *start_failure, *start_failure_log;
    /* File name (no path) of this run's own session log in <root>/logs. May be NULL. */
    const char *session_log;
    /* One line for this run's session log. No newline at the end. May be NULL. */
    void (*log)(void *ctx, const char *line);
    /* Controllers connected now, or <0 unknown. May be NULL. */
    int  (*pads)(void *ctx);
    /* Free bytes on the volume holding path, <0 unknown. May be NULL: the
     * disk-space checks are then skipped. */
    long long (*free_bytes)(void *ctx, const char *path);
    /* The PS4 local user name, masked as <ps4-user> in a support bundle
     * (audit 26). May be NULL: not masked. */
    const char *local_user;
} BlServices;

typedef enum BlAction { BL_START_GAME = 1, BL_LEAVE_HOME } BlAction;

typedef struct BlDecision {
    BlAction action;
    char profile_text[4096];          /* effective game profile */
    char dxvk_conf[512];
    struct { char name[48], value[512]; } env[24];
    unsigned env_count;
    char remote_profile[64];          /* unused by BLUR (always "") */
} BlDecision;

/* Runs the launcher UI until the player starts the game or the system asks
 * to close. 0: *out holds the decision. <0: bad arguments, out of memory, or
 * the display was lost. */
int bl_frontend_run(const BlServices *s, BlDecision *out);

#endif
