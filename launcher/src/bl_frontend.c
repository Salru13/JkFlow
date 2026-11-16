/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* The BLUR 1.00 front end (plan Task L9): Start, Patch (nine steps), Settings
 * (four items), Help, the start-failed screen and the Play checks. No About
 * screen (owner finding 32): credits are in the README, licences in the
 * package's THIRD_PARTY.md. */
#include "bl_frontend.h"
#include "bl_account.h"
#include "bl_amax.h"
#include "bl_clock.h"
#include "bl_config.h"
#include "bl_events.h"
#include "bl_exe.h"
#include "bl_fs.h"
#include "bl_import.h"
#include "bl_keyboard.h"
#include "bl_loader.h"
#include "bl_logs.h"
#include "bl_native.h"
#include "bl_net.h"
#include "bl_paths.h"
#include "bl_patcher.h"
#include "bl_profile.h"
#include "bl_screens.h"
#include "bl_session.h"
#include "bl_sha256.h"
#include "bl_support.h"
#include "bl_time.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FRAME_W 1920
#define FRAME_H 1080
#define TICK_US 16000u                             /* idle only: animation is paced by the flip */
#define FADE_NS 150000000ull                       /* screen change cross-fade (owner finding 34) */
#define STATUS_REFRESH_NS 2000000000ull
#define PATCH_ROOM (64LL * 1024 * 1024)            /* offline: our files and backups */
#define PATCH_ROOM_ONLINE (192LL * 1024 * 1024)    /* online: also both sets twice (stage and game folder) */
#define PATCH_ONLINE_SPARE (1024LL * 1024 * 1024)  /* audit 16: online also keeps 1 GB free */


typedef enum Screen { SCREEN_START = 0, SCREEN_PATCH, SCREEN_SETTINGS, SCREEN_ERROR, SCREEN_DIALOG, SCREEN_HELP } Screen;
static const char *const SCREEN_NAME[] = { "start", "patch", "settings", "error", "dialog", "help" };
typedef enum Ask { ASK_NONE = 0, ASK_UNPATCHED, ASK_ACCOUNT, ASK_CLOCK, ASK_IMPORT, ASK_PATCH_DONE } Ask;

typedef struct Ui {
    const BlServices *s;
    BlFrame frame;
    Screen screen, under;         /* under: the screen below a dialog */
    BlStatus status;
    BlConfig cfg;
    BlStartState start;
    BlPatchView patch;
    BlSettingsState settings;
    BlDialog dialog;
    Ask ask;
    BlPaths paths;                /* from data_root; game is the host's game_dir */
    int have_root;
    char ini[1024], state_dir[sizeof(BlPaths){0}.state];
    char cfg_warning[256];        /* shown once in settings */
    char failure[96];             /* "Error E101 - <time>" for the failed-start screen */
    char error_note[300];         /* the support bundle result on the failed-start screen */
    char account_pass[BL_ACCOUNT_MAX + 1];   /* Online account: the password while settings are open */
    int account_loaded;           /* audit 29a: account_pass read (the account panel was opened) */
    int account_warned;           /* "No AMAX account" accepted once this run */
    int exe_other;                /* Blur.exe is not the tested 1.2 build (warn, never block) */
    char game[sizeof(BlPaths){0}.game], game_other[sizeof(BlPaths){0}.game];   /* folder in use; a second spelling when several */
    int game_several;             /* several JkFlow/Blur spellings: Play and Patch stop */
    int online_state;             /* BL_ONLINE_*: the set in the game folder */
    int patch_online;             /* this Patch run: 1 for online play */
    char stage[256];              /* the checked online sets of this Patch run, or "" */
    char hash_cache[200];         /* <state>/hash-cache.txt */
    BlHelpState help;
    Screen help_back;             /* where Circle goes from Help */
    /* Our own keyboard over Settings (owner finding 12): open while kb_field
     * is not BL_FIELD_NONE. The system keyboard service is never used. */
    BlKeyboard kb;
    BlField kb_field;
    int kb_item;                  /* BlSetItem the text is for */
    /* Fluid navigation (owner finding 34). A screen change cross-fades from
     * the last frame shown (fade_px, one frame, also the scratch frame the
     * idle pre-drawing uses) to the new screen in FADE_NS. */
    int presented;                /* u->frame holds the frame on the TV */
    uint8_t *fade_px;
    int fading;
    uint64_t fade_t0;
    /* Start idle time: the Settings file reads done ahead (pre_files), and
     * the screens the next press may open drawn once off screen so their
     * cached layers are ready (pre_drawn: bit per Screen, per Start visit). */
    int pre_files, pre_log_count, pre_account_ok;
    long long pre_log_bytes;
    BlAccount pre_account;
    unsigned pre_drawn;
    int pre_focus;
} Ui;

static const BlServices *g_services;   /* for bl_free_bytes */
static long long free_bytes_service(const char *path)
{
    return g_services && g_services->free_bytes ? g_services->free_bytes(g_services->ctx, path) : -1;
}

static void logf_ui(Ui *u, const char *fmt, ...)
{
    if (!u->s->log) return;
    char line[640];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    u->s->log(u->s->ctx, line);
}

static void set_screen(Ui *u, Screen next)
{
    /* The frame on the TV is where the cross-fade starts (a change during a
     * fade starts from the mix shown). */
    if (next != u->screen && u->presented && u->fade_px) {
        memcpy(u->fade_px, u->frame.pixels, (size_t)u->frame.stride * u->frame.height);
        u->fading = 1;
        u->fade_t0 = u->s->now_ns(u->s->ctx);
    }
    if (next == SCREEN_START) u->pre_drawn = 0;     /* the next press may open another screen */
    u->screen = next;
    if (u->s->screen) u->s->screen(u->s->ctx, SCREEN_NAME[next]);
}

/* The game folder: found under game_root (/data/JkFlow/Blur, any letter case) when
 * the host gives one, else the host's game_dir. Re-checked at every status refresh. */
#define GAME(u) ((u)->game[0] ? (u)->game : NULL)
static void resolve_game(Ui *u)
{
    const BlServices *s = u->s;
    u->game_several = 0;
    u->game[0] = u->game_other[0] = 0;
    if (s->game_root) u->game_several = bl_game_dir_find(s->game_root, u->game, sizeof u->game, u->game_other, sizeof u->game_other) < 0;
    else if (s->game_dir) snprintf(u->game, sizeof u->game, "%s", s->game_dir);
    if (u->game[0]) snprintf(u->paths.game, sizeof u->paths.game, "%s", u->game);
    if (u->game_several) snprintf(bl_str_game_several, sizeof bl_str_game_several, bl_str(BL_S_GAME_SEVERAL_FMT), u->game, u->game_other);
}

static int have_dirs(const Ui *u) { return GAME(u) && u->s->package_files_dir; }
static const char *backup_root(const Ui *u) { return u->have_root ? u->paths.backup : NULL; }

static void draw(Ui *u)
{
    /* A question over Start that is already cached: no need to draw Start. */
    if (u->screen == SCREEN_DIALOG && u->under == SCREEN_START &&
        bl_dialog_draw_cached(&u->frame, &u->dialog, bl_start_key(&u->start, &u->status)))
        return;
    Screen sc = u->screen == SCREEN_DIALOG ? u->under : u->screen;
    switch (sc) {
    case SCREEN_START: bl_start_draw(&u->frame, &u->start, &u->status); break;
    case SCREEN_PATCH: bl_patch_draw(&u->frame, &u->patch); break;
    case SCREEN_SETTINGS:
        bl_settings_draw(&u->frame, &u->settings);
        if (u->kb_field != BL_FIELD_NONE) bl_keyboard_draw(&u->frame, &u->kb);
        break;
    case SCREEN_HELP: bl_help_draw(&u->frame, &u->help); break;
    case SCREEN_ERROR: case SCREEN_DIALOG:
        bl_error_draw(&u->frame, u->failure[0] ? u->failure : u->s->start_failure, u->s->start_failure_log, u->error_note);
        break;
    }
    if (u->screen == SCREEN_DIALOG) bl_dialog_draw(&u->frame, &u->dialog);
}

/* Put the current screen on the TV right away, before a call that may block. */
static void show_now(Ui *u)
{
    draw(u);
    u->s->present(u->s->ctx, &u->frame);
    u->presented = 1;
}

static void ask(Ui *u, Ask what, int body, int first, int second, int circle)
{
    u->ask = what;
    bl_dialog_init(&u->dialog, body, first, second, circle);
    if (u->screen != SCREEN_DIALOG) u->under = u->screen;
    set_screen(u, SCREEN_DIALOG);
}

static void close_dialog(Ui *u)
{
    u->ask = ASK_NONE;
    set_screen(u, u->under);
}

/* ---- readiness ---- */

static void zip_dir(const Ui *u, char *out, size_t cap);

static void open_help(Ui *u, BlHelpSection at)
{
    bl_help_init(&u->help, at);
    if (u->game[0]) {                         /* the folders the text names */
        snprintf(u->help.game_dir, sizeof u->help.game_dir, "%s", u->game);
        zip_dir(u, u->help.zip_dir, sizeof u->help.zip_dir);
    }
    if (u->have_root) {
        snprintf(u->help.import_dir, sizeof u->help.import_dir, "%s", u->paths.import);
        snprintf(u->help.saves_dir, sizeof u->help.saves_dir, "%s", u->paths.saves);
    }
    u->help_back = u->screen == SCREEN_DIALOG ? u->under : u->screen;
    set_screen(u, SCREEN_HELP);
}

/* The folder the player puts the two zips in: the parent of the game folder
 * (/data/JkFlow). */
static void zip_dir(const Ui *u, char *out, size_t cap)
{
    snprintf(out, cap, "%s", u->game);
    char *s = strrchr(out, '/');
    if (s && s != out) *s = 0;
}

/* with_plan: re-check the game folder (Blur.exe gate, our files). Only at
 * start, after Patch and before Play; the 2 s timer refreshes the rest. */
static void refresh_status(Ui *u, int with_plan)
{
    const BlServices *s = u->s;
    BlStatus *st = &u->status;
    if (with_plan) resolve_game(u);              /* the player may have copied it over FTP meanwhile */
    if (with_plan && u->game_several) {
        st->readiness = BL_READY_NO_GAME;
        u->exe_other = 0;
        u->online_state = st->online = BL_ONLINE_NONE;
    } else if (with_plan && have_dirs(u)) {
        BlExeVerdict v = bl_exe_verify(&u->paths, NULL);
        u->exe_other = v == BL_EXE_UNSUPPORTED;
        u->online_state = st->online = BL_ONLINE_NONE;
        if (v == BL_EXE_MISSING) st->readiness = BL_READY_NO_GAME;
        else {
            /* The set the folder holds decides what "complete" means. */
            u->online_state = st->online = bl_patch_online_state(GAME(u), s->package_files_dir);
            const char *mode = u->online_state == BL_ONLINE_READY ? BL_PATCH_IN_PLACE : NULL;
            BlPatchPlan *plan = malloc(sizeof *plan);
            if (!plan || bl_patch_plan(GAME(u), s->package_files_dir, mode, plan) != 0) st->readiness = BL_READY_UNPATCHED;
            else st->readiness = plan->pending ? BL_READY_UNPATCHED : BL_READY_OK;
            free(plan);
            /* An online run that stopped half way may have parked the
             * player's originals (gamedata.pak): never Ready until a Patch
             * put them back. */
            if (bl_patch_unfinished(GAME(u), backup_root(u))) {
                st->readiness = BL_READY_UNPATCHED;
                u->online_state = st->online = BL_ONLINE_PARTIAL;
            }
        }
    } else if (with_plan) {
        st->readiness = s->game_state ? s->game_state(s->ctx) : BL_READY_NO_GAME;
        if (st->readiness < BL_READY_NO_GAME || st->readiness > BL_READY_UNSUPPORTED) st->readiness = BL_READY_NO_GAME;
    }
    st->ipv4[0] = 0;
    st->net_up = s->net_status && s->net_status(s->ctx, st->ipv4, sizeof st->ipv4) == 0;
    snprintf(st->build, sizeof st->build, "%s", s->build_id ? s->build_id : "");
    st->logging = u->cfg.logging;
    int pads = s->pads ? s->pads(s->ctx) : -1;
    st->pads_known = pads >= 0;
    st->pads = pads >= 0 ? pads : 0;
}

/* The band that says why Play cannot start (plan L9 priority order). */
static int readiness_band(const Ui *u)
{
    switch (u->status.readiness) {
    case BL_READY_NO_GAME: return u->game_several ? BL_S_GAME_SEVERAL : BL_S_GAME_NOT_FOUND;
    case BL_READY_UNSUPPORTED: return BL_S_GAME_UNSUPPORTED;
    case BL_READY_UNPATCHED: return BL_S_GAME_UNPATCHED;
    default: return -1;
    }
}

/* ---- support bundle ---- */

static int save_bundle(Ui *u, char *dir, size_t cap)
{
    const BlServices *s = u->s;
    BlSupportInfo in = { s->data_root, GAME(u), s->package_files_dir, s->build_id, s->session_log, "native",
                         u->ini[0] ? u->ini : NULL };
    int r = bl_support_bundle(&in, dir, cap);
    bl_events_log(r == 0 ? BL_E_INFO : BL_E_BUNDLE_FAILED, "support bundle: %s", r == 0 ? dir : "failed");
    return r;
}

/* ---- Patch: nine steps ---- */

static void patch_progress(void *ctx, const char *name, unsigned done, unsigned total)
{
    Ui *u = ctx;
    snprintf(u->patch.progress_name, sizeof u->patch.progress_name, "%s", name ? name : "");
    u->patch.done = done;
    u->patch.total = total;
    show_now(u);
}

/* Download progress (bl_online_prepare): the zip, bytes done and total. */
static void download_progress(void *ctx, long long done, long long total)
{
    Ui *u = ctx;
    u->patch.downloading = 1;
    snprintf(u->patch.progress_name, sizeof u->patch.progress_name, "%s", bl_online_downloading);
    u->patch.done = done > 0 ? (unsigned long long)done : 0;
    u->patch.total = total > 0 ? (unsigned long long)total : 0;
    show_now(u);
}

static void step_start(Ui *u, int i)
{
    u->patch.step = i;
    u->patch.steps[i] = BL_STEP_RUN;
    u->patch.done = u->patch.total = 0;
    u->patch.progress_name[0] = 0;
    show_now(u);
}

/* A failed step: the player sees one plain sentence with the next action
 * (spec 5); the code and the technical detail go to events.log only. */
static void step_fail(Ui *u, int i, const char *code, const char *sentence, const char *detail)
{
    u->patch.steps[i] = BL_STEP_FAIL;
    u->patch.busy = 0;
    if (u->stage[0] && u->have_root) bl_online_unstage(&u->paths);
    u->stage[0] = 0;
    snprintf(u->patch.message_text, sizeof u->patch.message_text, "%s", sentence);
    bl_events_log(code, "patch: step %d failed: %s [%s]", i + 1, sentence, detail && detail[0] ? detail : "-");
    refresh_status(u, 1);
}

/* The sentence for a failed install or restore (bl_patch_error_name/errno). */
static void file_sentence(char *out, size_t cap, BlStringId fmt)
{
    if (bl_patch_errno == ENOSPC || bl_patch_errno == EDQUOT) snprintf(out, cap, "%s", bl_str(BL_S_PATCH_NO_SPACE));
    else snprintf(out, cap, bl_str(fmt), bl_patch_error_name[0] ? bl_patch_error_name : "a file");
}

static void patch_view(const Ui *u, BlPatchView *v);

static void open_patch(Ui *u)
{
    patch_view(u, &u->patch);
    set_screen(u, SCREEN_PATCH);
}

/* Steps 7-9 (index 6-8), from the save import on. replace: -1 ask on a conflict. */
static void patch_finish(Ui *u, int replace)
{
    const BlServices *s = u->s;
    /* 7. Save import (never silently over a save). */
    if (u->have_root) {
        BlImportState st = bl_import_check(&u->paths);
        if (st == BL_IMPORT_CONFLICT && replace < 0) {
            ask(u, ASK_IMPORT, BL_S_IMPORT_CONFLICT, BL_S_DIALOG_KEEP, BL_S_DIALOG_USE_IMPORT, 1);
            snprintf(u->dialog.body_text, sizeof u->dialog.body_text, bl_str(BL_S_IMPORT_CONFLICT), u->paths.import);
            return;                                      /* resumed by the answer */
        }
        if (st == BL_IMPORT_INVALID) bl_events_log(BL_E_INFO, "patch: import/blursave is not a Blur save: left alone");
        else if (st != BL_IMPORT_NONE) {
            char b[300];
            if (bl_import_apply(&u->paths, st == BL_IMPORT_READY ? 0 : replace, b, sizeof b) < 0) {
                char sentence[300];
                snprintf(sentence, sizeof sentence, bl_str(BL_S_PATCH_IMPORT_FAILED), u->paths.import);
                step_fail(u, 6, BL_E_SAVES_FAILED, sentence, "save import");
                return;
            }
            bl_events_log(BL_E_INFO, "patch: save import %s%s", st == BL_IMPORT_READY ? "applied" : replace ? "used, current save backed up to " : "kept the current save", b);
        }
    }
    u->patch.steps[6] = BL_STEP_DONE;
    /* 8. Check everything again. */
    step_start(u, 7);
    BlPatchPlan *plan = malloc(sizeof *plan);
    const char *amax = !u->patch_online ? NULL : u->stage[0] ? u->stage : BL_PATCH_IN_PLACE;
    int ok = plan && bl_patch_plan(GAME(u), s->package_files_dir, amax, plan) == 0 && plan->game == BL_GAME_FOUND && plan->pending == 0;
    free(plan);
    if (u->stage[0] && u->have_root) bl_online_unstage(&u->paths);     /* about 44 MB: not kept */
    u->stage[0] = 0;
    if (!ok) { step_fail(u, 7, BL_E_PATCH_FAILED, bl_str(BL_S_PATCH_STILL_WRONG), "files still differ"); return; }
    u->patch.steps[7] = BL_STEP_DONE;
    /* 9. The patch record. */
    step_start(u, 8);
    if (u->have_root) {
        char man[1300], sha[65] = "-", stamp[BL_STAMP_LEN], text[400], rec[300];
        if (snprintf(man, sizeof man, "%s/manifest.txt", s->package_files_dir) < (int)sizeof man) bl_fs_sha256(man, sha, NULL);
        bl_utc_stamp(stamp);
        int n = snprintf(text, sizeof text, "build_id=%s\nmanifest_sha=%s\nstamp=%s\nmode=%s\n", s->build_id ? s->build_id : "-", sha, stamp,
                         u->patch_online ? "online" : "offline");
        if (snprintf(rec, sizeof rec, "%s/patch.txt", u->paths.state) >= (int)sizeof rec || n <= 0 || n >= (int)sizeof text
            || bl_fs_write_atomic(rec, text, (size_t)n, 0644) != 0) {
            char sentence[300];
            snprintf(sentence, sizeof sentence, bl_str(errno == ENOSPC ? BL_S_PATCH_NO_SPACE : BL_S_PATCH_FILE_FAILED), "patch.txt");
            step_fail(u, 8, BL_E_PATCH_FAILED, sentence, "patch.txt");
            return;
        }
    }
    u->patch.steps[8] = BL_STEP_DONE;
    u->patch.busy = 0;
    u->patch.message = u->patch_online ? BL_S_PATCH_DONE_ONLINE : BL_S_PATCH_ALL_DONE;
    u->patch.message_text[0] = 0;
    bl_events_log(BL_E_INFO, "patch: done (%s)", u->patch_online ? "online" : "offline");
    refresh_status(u, 1);
    u->patch.online = u->online_state;
    /* Owner finding 38: say it is done and where to go next (one button). */
    ask(u, ASK_PATCH_DONE, u->patch_online ? BL_S_PATCH_DONE_ASK_ONLINE : BL_S_PATCH_DONE_ASK, BL_S_DIALOG_BACK_TO_START, -1, 1);
}

/* Steps 1-6. Nothing is written into the game folder before the exe gate,
 * the space check and the online files pass. online: Patch for online play. */
/* The console clock against amax-emu.com's (one request, 3 s at most).
 * *offset = console - server when the server answered. */
static BlClock clock_check(Ui *u, long long *offset)
{
    const BlServices *s = u->s;
    long long now = bl_clock_console_now(), server = 0;
    int have = 0;
    *offset = 0;
    if (s->http_date) have = s->http_date(s->ctx, BL_CLOCK_HOST, &server) == 0;
    BlClock c = bl_clock_judge(now, have, server, offset);
    if (have) bl_events_log(BL_E_INFO, "clock offset=%lld", *offset);
    return c;
}

static void patch_run(Ui *u, int online)
{
    const BlServices *s = u->s;
    memset(u->patch.steps, 0, sizeof u->patch.steps);
    u->patch.help_line = 0;
    u->patch.message = -1;
    u->patch.message_text[0] = 0;
    u->stage[0] = 0;
    u->patch_online = online;
    resolve_game(u);
    char missing[400];                        /* audit 22/24: the real folder, nothing changed, the next step */
    snprintf(missing, sizeof missing, bl_str(BL_S_GAME_MISSING), u->game[0] ? u->game : "/data/JkFlow/Blur");
    if (!have_dirs(u)) { step_fail(u, 0, BL_E_PACKAGE_BROKEN, missing, "no game or package folder"); return; }
    if (u->have_root) {                       /* leftovers of an interrupted run or download */
        char zdir[700];
        zip_dir(u, zdir, sizeof zdir);
        bl_online_cleanup(&u->paths, zdir);
    }
    u->patch.busy = 1;
    /* 1. The one game folder, /data/JkFlow/Blur in any letter case (owner
     * decision 2026-11-15): no search, no copy-in. Patch works in place. */
    step_start(u, 0);
    if (u->game_several) { step_fail(u, 0, BL_E_GAME_SEVERAL, bl_str(BL_S_GAME_SEVERAL), "several game folders"); return; }
    char exe_name[64] = "";
    BlExeVerdict v = bl_exe_verify(&u->paths, exe_name);
    if (v == BL_EXE_MISSING) { step_fail(u, 0, BL_E_GAME_MISSING, missing, "no Blur.exe"); return; }
    {
        char exe[700], sha[65] = "";
        snprintf(exe, sizeof exe, "%s/%s", GAME(u), exe_name);
        bl_fs_sha256(exe, sha, NULL);
        bl_events_log(BL_E_INFO, "patch: game folder %s, Blur.exe sha256 %s", GAME(u), sha[0] ? sha : "?");
    }
    u->patch.steps[0] = BL_STEP_DONE;
    /* 2. The player's Blur.exe: any version (ruling 2026-11-15). It is never
     * replaced; another version than the tested 1.2 is a warning only. */
    step_start(u, 1);
    u->patch.exe_verdict = v;
    if (v != BL_EXE_TESTED) bl_events_log(BL_E_EXE_UNSUPPORTED, "patch: Blur.exe is not the tested 1.2 build (warning only)");
    u->patch.steps[1] = BL_STEP_DONE;
    /* 3. Free space for our files, the online sets and backups. */
    step_start(u, 2);
    if (online && bl_free_bytes) {             /* audit 16: room + 1 GB, before any download; unknown (<0): as before */
        const long long GB = 1024LL * 1024 * 1024, need = PATCH_ROOM_ONLINE + PATCH_ONLINE_SPARE;
        long long free_b = bl_free_bytes(GAME(u));
        if (free_b >= 0 && free_b < need) {
            char sentence[160];
            snprintf(sentence, sizeof sentence, bl_str(BL_S_PATCH_NO_SPACE_GB), (int)((need - free_b + GB - 1) / GB));
            step_fail(u, 2, BL_E_DISK_FULL, sentence, "free space check (online)");
            return;
        }
    }
    if (!bl_space_ok(GAME(u), online ? PATCH_ROOM_ONLINE : PATCH_ROOM)) { step_fail(u, 2, BL_E_DISK_FULL, bl_str(BL_S_PATCH_NO_SPACE_BEFORE), "free space check"); return; }
    u->patch.steps[2] = BL_STEP_DONE;
    /* 4. The online files. Online: the two zips (placed in /data/JkFlow, or
     * downloaded when the title can), checked before anything changes;
     * neither there: the instructions, not a failure. Offline over an online
     * folder: every Patch change is undone first (the AMAX files go, the
     * player's own Blur.exe and game data come back), then the offline set. */
    step_start(u, 3);
    int state = bl_patch_online_state(GAME(u), s->package_files_dir);
    if (online && state != BL_ONLINE_READY) {
        char err[300], zdir[700];
        BlOnlineSource src;
        zip_dir(u, zdir, sizeof zdir);
        int net = s->net_status && s->net_status(s->ctx, (char[32]){ 0 }, 32) == 0;
        /* Audit 9: a download is HTTPS and needs the right clock; with a wrong
         * one nothing is downloaded (the zips placed by hand still work). */
        char z1[800], z2[800];
        long long offset;
        int fetch = u->have_root && s->https_get && net &&
                    !(bl_online_find_zip(zdir, BL_AMAX_ZIP_NAME, z1, sizeof z1) && bl_online_find_zip(zdir, BL_BLUR12_ZIP_NAME, z2, sizeof z2));
        if (fetch) download_progress(u, 0, 0);       /* audit 17: "Connecting to amax-emu.com", not "Installing" */
        if (fetch && clock_check(u, &offset) != BL_CLOCK_OK) {
            u->stage[0] = 0;
            u->patch.downloading = 0;
            u->patch.steps[3] = BL_STEP_FAIL;
            u->patch.busy = 0;
            u->patch.help_line = 1;
            snprintf(u->patch.message_text, sizeof u->patch.message_text, bl_str(BL_S_CLOCK_TLS), bl_str(BL_S_CLOCK_GOLDHEN));
            bl_events_log(BL_E_INFO, "patch: online: the console clock is wrong (offset %lld s): no download", offset);
            return;
        }
        int r = u->have_root ? bl_online_prepare(&u->paths, s->package_files_dir, zdir, s->https_get, s->ctx, net,
                                                 download_progress, u, u->stage, &src, err, sizeof err)
                             : BL_ONLINE_NO_ZIPS;
        u->patch.downloading = 0;
        u->patch.done = u->patch.total = 0;
        u->patch.progress_name[0] = 0;
        if (r == BL_ONLINE_NO_ZIPS && !u->have_root)
            snprintf(err, sizeof err, bl_str(net ? BL_S_ONLINE_NO_ZIPS : BL_S_ONLINE_NO_NETWORK), zdir);
        if (r == BL_ONLINE_NO_ZIPS || r == BL_ONLINE_DOWNLOAD_ERR) {
            /* No network, or the download failed (owner finding 31): stay
             * on Patch, step 4 amber with the cause and "Press Square Help"
             * under it; Square opens Help at the online section. Nothing in
             * the game folder changed. */
            u->stage[0] = 0;
            u->patch.steps[3] = BL_STEP_FAIL;
            u->patch.busy = 0;
            u->patch.help_line = 1;
            snprintf(u->patch.message_text, sizeof u->patch.message_text, "%s", err);
            bl_events_log(r == BL_ONLINE_NO_ZIPS ? BL_E_INFO : BL_E_AMAX_MISSING, "patch: online: %s", err);
            return;
        }
        if (r != 0) {
            /* Not a failure of Patch: the placed zips are wrong. Help at
             * the online section says how, with the cause on top; nothing in
             * the game folder changed. */
            u->stage[0] = 0;
            u->patch.steps[3] = BL_STEP_WAIT;
            u->patch.busy = 0;
            snprintf(u->patch.message_text, sizeof u->patch.message_text, "%s", err);
            bl_events_log(r == BL_ONLINE_NO_ZIPS ? BL_E_INFO : BL_E_AMAX_MISSING, "patch: online: %s: help shown", err);
            open_help(u, BL_HELP_ONLINE);
            snprintf(u->help.note, sizeof u->help.note, "%s", err);
            return;
        }
        bl_events_log(BL_E_INFO, "patch: online files from %s", src == BL_ONLINE_SRC_PLACED ? zdir : src == BL_ONLINE_SRC_DOWNLOAD ? "a download" : "the zip folder and a download");
    }
    /* An online run that stopped half way (power cut, full disk): its parked
     * originals come back first, whatever this Patch is for. Offline over an
     * online folder: the same, then the offline set. */
    int unfinished = bl_patch_unfinished(GAME(u), backup_root(u));
    if (unfinished || (!online && state != BL_ONLINE_NONE)) {
        int r = bl_patch_restore(GAME(u), backup_root(u));
        if (r < 0) {
            char text[300];
            file_sentence(text, sizeof text, BL_S_PATCH_RESTORE_FAILED);
            step_fail(u, 3, BL_E_PATCH_FAILED, text, bl_patch_error);
            return;
        }
        bl_events_log(BL_E_INFO, "patch: %s: %s", unfinished ? "unfinished online run" : "back to offline",
                      r == 0 ? "the backups were put back" : "no backup to put back");
    }
    u->patch.steps[3] = BL_STEP_DONE;
    /* 5. Plan and install (park, ours, AMAX). */
    step_start(u, 4);
    BlPatchPlan *plan = malloc(sizeof *plan);
    const char *amax = !online ? NULL : u->stage[0] ? u->stage : BL_PATCH_IN_PLACE;
    if (!plan || bl_patch_plan(GAME(u), s->package_files_dir, amax, plan) != 0) {
        free(plan);
        step_fail(u, 4, BL_E_PACKAGE_BROKEN, bl_str(BL_S_PATCH_PACKAGE_BROKEN), "manifest");
        return;
    }
    if (plan->park_total > BL_PATCH_MAX_PARK) {   /* audit 21: refused before any change */
        char folder[800], sentence[1000];
        const char *slash = strrchr(plan->park_over, '/');
        snprintf(folder, sizeof folder, "%s%s%.*s", GAME(u), slash ? "/" : "", slash ? (int)(slash - plan->park_over) : 0, plan->park_over);
        snprintf(sentence, sizeof sentence, bl_str(BL_S_PATCH_TOO_MANY), folder, BL_PATCH_MAX_PARK);
        bl_events_log(BL_E_INFO, "patch: %u other files to park", plan->park_total);
        free(plan);
        step_fail(u, 4, BL_E_PATCH_FAILED, sentence, "too many other files to park");
        return;
    }
    for (unsigned i = 0; i < plan->count; i++)
        if (plan->items[i].state != BL_FILE_OK)
            bl_events_log(BL_E_UNPATCHED, "patch: %s %s%s", plan->items[i].name, plan->items[i].state == BL_FILE_MISSING ? "missing" : "different",
                          plan->items[i].foreign ? " (not ours: parked)" : "");
    /* The first 32 by name, then a count: events.log (32 KiB x 2) keeps its history. */
    for (unsigned i = 0; i < plan->park_count && i < 32; i++) bl_events_log(BL_E_INFO, "patch: park %s", plan->park[i]);
    if (plan->park_count > 32) bl_events_log(BL_E_INFO, "patch: park %u more", plan->park_count - 32);
    int r = bl_patch_apply(GAME(u), s->package_files_dir, amax, backup_root(u), plan, patch_progress, u);
    free(plan);
    if (r != 0) {
        char text[300];
        if (!strncmp(bl_patch_error, "unreadable ", 11))          /* audit 22: stopped before any move */
            snprintf(text, sizeof text, bl_str(BL_S_PATCH_UNREADABLE), bl_patch_error_name[0] ? bl_patch_error_name : "a file");
        else file_sentence(text, sizeof text, BL_S_PATCH_FILE_FAILED);
        step_fail(u, 4, BL_E_PATCH_FAILED, text, bl_patch_error);
        return;
    }
    u->patch.steps[4] = BL_STEP_DONE;
    /* 6. The shader cache seed ("cache" lines) into the cache folder (bl_paths), where the
     * engine reads it (DXVK_STATE_CACHE_PATH). A failure only costs warm-up
     * stutter, so it is logged and Patch goes on. */
    step_start(u, 5);
    if (u->have_root) {
        int n = bl_patch_seed_cache(s->package_files_dir, u->paths.cache);
        if (n < 0) bl_events_log(BL_E_INFO, "patch: shader cache seed not installed: %s", bl_patch_error);
        else if (n > 0) bl_events_log(BL_E_INFO, "patch: shader cache seed installed (%d)", n);
    }
    u->patch.steps[5] = BL_STEP_DONE;
    step_start(u, 6);
    patch_finish(u, -1);
}

static void patch_event(Ui *u, BlPatchEvent ev)
{
    if (ev == BL_PATCH_EV_BACK) {
        refresh_status(u, 0);
        int b = readiness_band(u);
        u->start.band = b >= 0 ? b : u->start.focus == BL_CARD_PATCH ? BL_S_BAND_PATCH : BL_S_BAND_PLAY;
        set_screen(u, SCREEN_START);
        return;
    }
    if (ev == BL_PATCH_EV_HELP) {
        /* After an online stop: the online section, the cause on top. */
        open_help(u, u->patch.choice || u->patch.help_line ? BL_HELP_ONLINE : BL_HELP_FOLDER);
        if (u->patch.help_line) snprintf(u->help.note, sizeof u->help.note, "%s", u->patch.message_text);
        return;
    }
    if (ev == BL_PATCH_EV_APPLY) { patch_run(u, u->patch.choice); return; }
    if (ev == BL_PATCH_EV_RESTORE) {
        u->patch.message = -1;
        u->patch.message_text[0] = 0;
        u->patch.help_line = 0;
        int r = u->game[0] ? bl_patch_restore(u->game, backup_root(u)) : 1;
        if (r < 0) file_sentence(u->patch.message_text, sizeof u->patch.message_text, BL_S_PATCH_RESTORE_FAILED);
        else u->patch.message = r == 0 ? BL_S_PATCH_RESTORED : BL_S_PATCH_NO_BACKUP;
        bl_events_log(r < 0 ? BL_E_PATCH_FAILED : BL_E_INFO, "patch: restore %s", r < 0 ? bl_patch_error : r == 0 ? "done" : "nothing to restore");
        refresh_status(u, 1);
        u->patch.online = u->online_state;
        u->patch.choice = u->online_state != BL_ONLINE_NONE;
    }
}

/* ---- settings ---- */

static void count_logs(Ui *u)
{
    u->settings.log_count = bl_logs_summary(GAME(u), u->s->data_root, u->s->session_log, &u->settings.log_bytes);
}

/* The Settings file reads (log folder scan, account file), in Start idle
 * time (owner finding 34): opening Settings then reads nothing. */
static void prefetch_files(Ui *u)
{
    u->pre_log_count = bl_logs_summary(GAME(u), u->s->data_root, u->s->session_log, &u->pre_log_bytes);
    memset(&u->pre_account, 0, sizeof u->pre_account);
    /* Audit 29a: the user name only; the password is read when the account panel opens. */
    u->pre_account_ok = u->have_root && bl_account_user(&u->paths, u->pre_account.user, sizeof u->pre_account.user) == 0;
    u->pre_files = 1;
}

static void prefetch_forget(Ui *u)
{
    memset(&u->pre_account, 0, sizeof u->pre_account);
    u->pre_account_ok = u->pre_files = 0;
}

/* The settings state as opening Settings sets it (no file read). */
static void settings_view(const Ui *u, BlSettingsState *st)
{
    bl_settings_init(st, &u->cfg);
    if (u->have_root) {                       /* the folders the texts name */
        snprintf(st->logs_dir, sizeof st->logs_dir, "%s", u->paths.logs);
        snprintf(st->support_dir, sizeof st->support_dir, "%s", u->paths.support);
        snprintf(st->state_dir, sizeof st->state_dir, "%s", u->paths.state);
    }
    if (!u->have_root) bl_settings_hide(st, BL_SET_ACCOUNT);
    /* Frame rate: 60 only for the tested Blur.exe (the check is cached). */
    st->exe_untested = have_dirs(u) && bl_exe_verify(&u->paths, NULL) == BL_EXE_UNSUPPORTED;
}

static void open_settings(Ui *u)
{
    settings_view(u, &u->settings);
    if (!u->pre_files) prefetch_files(u);     /* pressed before any idle time */
    u->settings.log_count = u->pre_log_count;
    u->settings.log_bytes = u->pre_log_bytes;
    memset(u->account_pass, 0, sizeof u->account_pass);
    u->account_loaded = 0;
    if (u->have_root && u->pre_account_ok) {
        snprintf(u->settings.account_user, sizeof u->settings.account_user, "%s", u->pre_account.user);
        u->settings.account_pass_set = 1;
    }
    prefetch_forget(u);                       /* Settings may change both: read again next time */
    if (u->cfg_warning[0]) {
        snprintf(u->settings.message_text, sizeof u->settings.message_text, "%.127s", u->cfg_warning);
        u->cfg_warning[0] = 0;
    }
}

/* Audit 29a: the password, read when Settings > Online account opens (never
 * in the Start idle prefetch); wiped when Settings closes. */
static void load_account_pass(Ui *u)
{
    BlAccount a;
    u->account_loaded = 1;
    if (u->have_root && bl_account_load(&u->paths, &a) == 0) memcpy(u->account_pass, a.pass, sizeof u->account_pass);
    memset(&a, 0, sizeof a);
}

/* Our keyboard for one settings field; the initial text when it fits. */
static void open_keyboard(Ui *u, BlField field, const char *title, int secret, const char *initial)
{
    char text[BL_KB_MAX_LEN + 1] = "";
    if (initial && strlen(initial) <= BL_KB_MAX_LEN) snprintf(text, sizeof text, "%s", initial);
    bl_keyboard_init(&u->kb, title, secret, text, BL_KB_MAX_LEN);
    memset(text, 0, sizeof text);
    u->kb_field = field;
    u->kb_item = u->settings.item;
    if (u->s->screen) u->s->screen(u->s->ctx, "keyboard");
}

/* A press while the keyboard is open. OK stores the text in the field
 * (checked as before); Cancel leaves the field as it was. */
static void keyboard_input(Ui *u, uint32_t pressed)
{
    int r = bl_keyboard_input(&u->kb, pressed);
    if (r == BL_KB_EDITING) return;
    BlSettingsState *st = &u->settings;
    BlField field = u->kb_field;
    if (r == BL_KB_DONE) {
        const char *text = bl_keyboard_text(&u->kb);
        if (bl_settings_set_text(st, field, text) == 0 && u->kb_item == BL_SET_ACCOUNT && field == BL_FIELD_PASSWORD)
            snprintf(u->account_pass, sizeof u->account_pass, "%s", text);     /* valid: at most 31 bytes */
    }
    bl_keyboard_wipe(&u->kb);
    bl_keyboard_release();
    u->kb_field = BL_FIELD_NONE;
    if (u->s->screen) u->s->screen(u->s->ctx, SCREEN_NAME[SCREEN_SETTINGS]);
}

static void settings_event(Ui *u, BlSetEvent ev, BlField field)
{
    BlSettingsState *st = &u->settings;
    const BlServices *s = u->s;
    if (ev == BL_SET_EV_BACK) {
        memset(u->account_pass, 0, sizeof u->account_pass);
        u->account_loaded = 0;
        u->cfg = st->cfg;
        int saved = u->state_dir[0] && bl_fs_mkdirs(u->state_dir) == 0 && bl_config_save(u->ini, &u->cfg) == 0;
        u->start.band = saved ? BL_S_SETTINGS_SAVED : BL_S_SETTINGS_SAVE_FAILED;
        if (!saved) bl_events_log(BL_E_SETTINGS_FAILED, "settings: launcher.ini could not be written");
        else bl_events_log(BL_E_INFO, "settings: logging=%d fps=%d frame_rate=%d", u->cfg.logging, u->cfg.stats != BL_STATS_OFF,
                                 u->cfg.frame_rate == BL_FPS_60 ? 60 : 30);
        refresh_status(u, 0);
        set_screen(u, SCREEN_START);
    } else if (ev == BL_SET_EV_EDIT && st->item == BL_SET_ACCOUNT) {
        int pass = field == BL_FIELD_PASSWORD;
        open_keyboard(u, field, bl_str(pass ? BL_S_OSK_ACCOUNT_PASS : BL_S_OSK_ACCOUNT_USER), pass,
                      pass ? u->account_pass : st->account_user);
    } else if (ev == BL_SET_EV_ACCOUNT_SAVE) {
        st->message_text[0] = 0;
        BlAccount a;
        memset(&a, 0, sizeof a);
        snprintf(a.user, sizeof a.user, "%s", st->account_user);
        memcpy(a.pass, u->account_pass, sizeof a.pass);
        if (!a.user[0] || !a.pass[0]) st->message = BL_S_ACCOUNT_INCOMPLETE;
        else st->message = bl_account_save(&u->paths, &a) == 0 ? BL_S_ACCOUNT_SAVED : BL_S_ACCOUNT_SAVE_FAILED;
        memset(&a, 0, sizeof a);
    } else if (ev == BL_SET_EV_ACCOUNT_REMOVE) {
        st->message_text[0] = 0;
        int r = bl_account_clear(&u->paths);
        st->account_user[0] = 0;
        st->account_pass_set = 0;
        memset(u->account_pass, 0, sizeof u->account_pass);
        st->message = r == 0 ? BL_S_ACCOUNT_REMOVED : BL_S_SETTINGS_SAVE_FAILED;
    } else if (ev == BL_SET_EV_EDIT) {
        /* The hidden private-server item (BL_ONLINE_UI builds only). */
        static const BlStringId titles[] = { BL_S_OSK_SERVER, BL_S_OSK_SERVER, BL_S_OSK_PORT, BL_S_OSK_USERNAME, BL_S_OSK_PASSWORD };
        if ((unsigned)field < sizeof titles / sizeof titles[0])
            open_keyboard(u, field, bl_str(titles[field]), field == BL_FIELD_PASSWORD,
                          field == BL_FIELD_SERVER ? st->cfg.host : field == BL_FIELD_USERNAME ? st->cfg.username : "");
    } else if (ev == BL_SET_EV_CLEAR_LOGS) {
        int n = bl_logs_clear(GAME(u), s->data_root, s->session_log);
        snprintf(st->message_text, sizeof st->message_text, bl_str_n(BL_S_LOGS_CLEARED_ONE, BL_S_LOGS_CLEARED, n), (unsigned)n);
        count_logs(u);
    } else if (ev == BL_SET_EV_BUNDLE) {
        char dir[1200];
        st->message = -1;
        st->message_text[0] = 0;
        int r = save_bundle(u, dir, sizeof dir);
        if (r == 0) snprintf(st->message_text, sizeof st->message_text, bl_str(BL_S_BUNDLE_DONE), dir);
        else st->message = r == -2 ? BL_S_DISK_FULL : BL_S_BUNDLE_FAILED;
    }
}

/* ---- fluid navigation (owner finding 34) ---- */

/* The patch view as opening Patch sets it. */
static void patch_view(const Ui *u, BlPatchView *v)
{
    memset(v, 0, sizeof *v);
    v->message = -1;
    v->exe_verdict = have_dirs(u) ? (int)bl_exe_verify(&u->paths, NULL) : BL_EXE_MISSING;
    snprintf(v->game_dir, sizeof v->game_dir, "%s", u->game[0] ? u->game : "/data/JkFlow/Blur");
    /* The choice that keeps what the folder has: online once AMAX is in. */
    v->online = u->online_state;
    v->choice = u->online_state != BL_ONLINE_NONE;
}

/* Draw a screen the next press may open once, off screen, so its cached
 * layer is ready (a cold first draw costs a frame of 35-70 ms on the PS4).
 * The frames last drawn the tests read are kept. */
static void predraw(Ui *u, Screen sc)
{
    BlFrame scratch = { u->fade_px, u->frame.width, u->frame.height, u->frame.stride };
    BlPatchView pv = bl_patch_last_drawn;
    BlSettingsState sv = bl_settings_last_drawn;
    BlHelpState hv = bl_help_last_drawn;
    if (sc == SCREEN_SETTINGS) {
        static BlSettingsState st;
        settings_view(u, &st);
        bl_settings_draw(&scratch, &st);
    } else if (sc == SCREEN_PATCH) {
        static BlPatchView v;
        patch_view(u, &v);
        bl_patch_draw(&scratch, &v);
    } else if (sc == SCREEN_HELP) {
        static BlHelpState h;
        bl_help_init(&h, BL_HELP_BEFORE);
        bl_help_draw(&scratch, &h);
    }
    bl_patch_last_drawn = pv;
    bl_settings_last_drawn = sv;
    bl_help_last_drawn = hv;
}

/* One piece of Start idle work; 1 when there was one (call again later). */
static int idle_work(Ui *u)
{
    if (!u->pre_files) { prefetch_files(u); return 1; }
    if (bl_start_prepare(&u->start)) return 1;          /* the next slide's card images */
    if (!u->fade_px || u->fading || u->start.pos != (float)u->start.focus) return 0;
    if (u->pre_focus != u->start.focus) { u->pre_focus = u->start.focus; u->pre_drawn = 0; }
    /* The focused card's screen, then Help (Square); two layers are cached. */
    Screen want[2] = { u->start.focus == BL_CARD_SETTINGS ? SCREEN_SETTINGS : u->start.focus == BL_CARD_PATCH ? SCREEN_PATCH : SCREEN_START,
                       SCREEN_HELP };
    for (int i = 0; i < 2; i++) {
        if (want[i] == SCREEN_START || (u->pre_drawn >> want[i] & 1)) continue;
        u->pre_drawn |= 1u << want[i];
        predraw(u, want[i]);
        return 1;
    }
    return 0;
}

/* ---- play ---- */

/* The last step: pre-fill file, arm file, profile, session record. 0 started. */
static int compose(Ui *u, BlDecision *out)
{
    const BlServices *s = u->s;
    if (s->package_files_dir && bl_profile_compose(&u->cfg, 0, s->package_files_dir, out) != 0) {
        /* Audit 22: a full environment is too many settings, not a damaged package. */
        u->start.band = out->env_count >= sizeof out->env / sizeof out->env[0] ? BL_S_TOO_MANY_SETTINGS : BL_S_PLAY_FILES_BROKEN;
        return -1;
    }
    if (GAME(u) && bl_profile_set_game(out, GAME(u)) != 0) {     /* Blur starts from the folder in use */
        u->start.band = BL_S_PLAY_FILES_BROKEN;
        return -1;
    }
    if (u->have_root && bl_account_apply(&u->paths) != 0) bl_events_log(BL_E_INFO, "start: the account pre-fill file could not be written");
    if (GAME(u) && bl_native_prepare(GAME(u), 1) != 0) {
        u->start.band = BL_S_NATIVE_PREPARE_FAILED;
        bl_events_log(BL_E_NATIVE_UNARMED, "start: %s/%s could not be written", GAME(u), BL_NATIVE_ARM_FILE);
        return -1;
    }
    logf_ui(u, "renderer: native (%s/%s written)", u->game[0] ? u->game : "-", BL_NATIVE_ARM_FILE);
    /* Settings > Frame rate (owner 2026-11-16) through the c988 race lock:
     * unset = races 30 and menus 60, 60 = races free (split screen 30).
     * Both read Blur's screen state and are safe only on the tested exe;
     * any other Blur.exe gets force (30 everywhere) from the first frame,
     * whatever the setting (owner finding 11). One env line at most. */
    {
        int untested = GAME(u) && bl_exe_verify(&u->paths, NULL) == BL_EXE_UNSUPPORTED;
        const char *lock = untested ? "force" : u->cfg.frame_rate == BL_FPS_60 ? "60" : NULL;
        if (lock && bl_decision_setenv(out, "DXVK_PS4_RACE_LOCK", lock) != 0) {
            u->start.band = BL_S_TOO_MANY_SETTINGS;
            return -1;
        }
        bl_events_log(BL_E_INFO, "start: frame rate %s", untested ? "30 (untested exe)" : lock ? "60" : "30");
    }
    /* Finding 19 L6: the native client's dxn_stage.txt (game folder) only
     * grows; each start begins it again. dxn_arm.txt is a different file. */
    if (GAME(u)) {
        char stage[700];
        if (snprintf(stage, sizeof stage, "%s/dxn_stage.txt", GAME(u)) < (int)sizeof stage) unlink(stage);
    }
    int rotated = bl_logs_rotate(GAME(u), s->data_root, s->session_log);
    if (bl_session_begin(u->state_dir[0] ? u->state_dir : NULL) != 0) bl_events_log(BL_E_INFO, "start: no session record (no data folder)");
    bl_events_log(BL_E_INFO, "start: play build=%s logging=%d fps=%d env=%u rotated=%d", s->build_id ? s->build_id : "-",
                  u->cfg.logging, u->cfg.stats != BL_STATS_OFF, out->env_count, rotated);
    out->action = BL_START_GAME;
    return 0;
}

/* The Play checks from step `from` on (0 readiness, 1 account, 2 clock,
 * 3 start). 1: the game starts; 0: a question is open or Play stopped. */
static int play(Ui *u, BlDecision *out, int from)
{
    const BlServices *s = u->s;
    if (from <= 0) {
        refresh_status(u, 1);                    /* the folder may have changed over FTP */
        if (u->status.readiness == BL_READY_UNPATCHED && have_dirs(u)) {
            ask(u, ASK_UNPATCHED, BL_S_PLAY_UPDATE, BL_S_DIALOG_PATCH_NOW, BL_S_DIALOG_CANCEL, 2);
            return 0;
        }
        if (u->status.readiness != BL_READY_OK) {
            bl_start_init(&u->start);
            bl_start_input(&u->start, BL_PAD_CROSS, u->status.readiness);   /* shows the reason */
            int band = readiness_band(u);
            if (band >= 0) u->start.band = band;
            return 0;
        }
        /* Another Blur.exe: no question at Play (owner finding 7, 2026-11-15);
         * the Patch screen line and the E205 logged at Patch say it. */
    }
    /* The AMAX account matters only for a game patched for online play
     * (owner finding 8): an offline-patched game never asks. */
    if (from <= 1 && u->have_root && !u->account_warned && u->online_state == BL_ONLINE_READY) {
        BlAccount a;
        int r = bl_account_load(&u->paths, &a);
        memset(&a, 0, sizeof a);
        if (r != 0) {                             /* D6: warn once per run, then play */
            ask(u, ASK_ACCOUNT, BL_S_PLAY_NO_ACCOUNT, BL_S_DIALOG_PLAY, BL_S_DIALOG_BACK, 2);
            return 0;
        }
    }
    /* The clock matters only for online play (audit 14): an offline-patched
     * game makes no request and asks nothing. */
    if (from <= 2 && u->online_state == BL_ONLINE_READY) {
        long long offset = 0;
        if (s->http_date) {
            int band = u->start.band;
            u->start.band = BL_S_CLOCK_CHECKING;
            show_now(u);                          /* one request, 3 s at most */
            u->start.band = band;
        }
        BlClock c = clock_check(u, &offset);
        if (c != BL_CLOCK_OK) {
            ask(u, ASK_CLOCK, c == BL_CLOCK_OFF ? BL_S_CLOCK_OFF : BL_S_CLOCK_BAD_YEAR, BL_S_CLOCK_PLAY_ANYWAY, BL_S_CLOCK_BACK, 2);
            if (c == BL_CLOCK_OFF) snprintf(u->dialog.body_text, sizeof u->dialog.body_text, bl_str(BL_S_CLOCK_OFF), bl_clock_minutes(offset));
            return 0;
        }
    }
    return compose(u, out) == 0;
}

/* An answer to the open question. 1: the game starts. */
static int answer(Ui *u, int choice, BlDecision *out)
{
    Ask what = u->ask;
    if (what == ASK_PATCH_DONE) {                         /* straight to Start, not back to Patch */
        u->ask = ASK_NONE;
        patch_event(u, BL_PATCH_EV_BACK);
        return 0;
    }
    close_dialog(u);
    switch (what) {
    case ASK_UNPATCHED:
        if (choice == 1) { open_patch(u); patch_run(u, u->patch.choice); }
        return 0;
    case ASK_ACCOUNT:
        if (choice != 1) return 0;
        u->account_warned = 1;
        return play(u, out, 2);
    case ASK_CLOCK:
        return choice == 1 ? play(u, out, 3) : 0;
    case ASK_IMPORT:
        patch_finish(u, choice == 2);
        return 0;
    case ASK_PATCH_DONE: case ASK_NONE: break;
    }
    return 0;
}

int bl_frontend_run(const BlServices *s, BlDecision *out)
{
    if (!s || !out || !s->present || !s->now_ns || !s->sleep_us) return -1;
    memset(out, 0, sizeof *out);
    Ui *u = calloc(1, sizeof *u);
    if (!u) return -1;
    u->s = s;
    u->frame = (BlFrame){ malloc((size_t)FRAME_W * FRAME_H * 4), FRAME_W, FRAME_H, FRAME_W * 4 };
    if (!u->frame.pixels) { free(u); return -1; }
    u->fade_px = malloc((size_t)FRAME_W * FRAME_H * 4);     /* none: screens change at once */
    u->pre_focus = -1;

    bl_config_defaults(&u->cfg);
    if (s->data_root) {
        /* data_root is the BLUR root: settings and state in <root>/state. */
        bl_paths_init(&u->paths, s->data_root);
        u->have_root = u->paths.root[0] != 0;
        snprintf(u->state_dir, sizeof u->state_dir, "%s", u->paths.state);
        if (u->have_root && snprintf(u->hash_cache, sizeof u->hash_cache, "%s/%s", u->paths.state, BL_HASH_CACHE_FILE) < (int)sizeof u->hash_cache)
            bl_patch_hash_cache = u->hash_cache;
    }
    resolve_game(u);
    if (u->state_dir[0]) {
        snprintf(u->ini, sizeof u->ini, "%s/launcher.ini", u->state_dir);
        char warning[256];
        if (bl_config_load(u->ini, &u->cfg, warning, sizeof warning) == 1)
            snprintf(u->cfg_warning, sizeof u->cfg_warning, "%s", warning);
    }
    g_services = s;
    bl_free_bytes = s->free_bytes ? free_bytes_service : NULL;
    bl_support_local_user = s->local_user;    /* audit 26: masked in a support bundle */
    bl_events_open(u->state_dir[0] ? u->state_dir : NULL);
    bl_events_log(BL_E_INFO, "launcher: open build=%s", s->build_id ? s->build_id : "-");
    /* Owner finding 27: the pre-fill file of the last Play (password in plain
     * text) never outlives the game; the plugin deletes it once read. */
    if (u->have_root && u->game[0]) bl_account_remove_prefill(&u->paths);
    if (u->cfg_warning[0]) bl_events_log(BL_E_CONFIG_WARNING, "launcher.ini: %s", u->cfg_warning);
    {   /* How the last session ended (bl_session.h). Only a start that died
         * before its first frame gets a screen (decision 25). */
        char when[40] = "", label[64] = "";
        int abnormal = 0;
        BlSessionEnd end = bl_session_check(u->state_dir[0] ? u->state_dir : NULL, &abnormal, when, sizeof when);
        char name[64];
        snprintf(name, sizeof name, "000000-%.40s", when);
        bl_backup_label(name, label, sizeof label);
        if (end == BL_SESSION_NO_FIRST_FRAME) {
            snprintf(u->failure, sizeof u->failure, bl_str(BL_S_ERROR_CODE), BL_E_START_FAILED, label[0] ? label : "-");
            bl_events_log(BL_E_START_FAILED, "session started %s ended before the first frame", when);
        } else if (end == BL_SESSION_ABNORMAL) {
            bl_events_log(BL_E_SESSION_ABNORMAL, "session first frame %s did not end normally (%d in a row)", when, abnormal);
        }
    }
    refresh_status(u, 1);
    uint64_t last_refresh = s->now_ns(s->ctx);
    bl_start_init(&u->start);
    if (u->cfg_warning[0]) u->start.band = BL_S_SETTINGS_WARNING;
    else if (readiness_band(u) >= 0) u->start.band = readiness_band(u);
    set_screen(u, s->start_failure || u->failure[0] ? SCREEN_ERROR : SCREEN_START);

    int dirty = 1, result = 0;
    for (;;) {
        if (s->should_quit && s->should_quit(s->ctx)) { out->action = BL_LEAVE_HOME; break; }

        uint32_t held = 0, pressed = 0;
        if (s->pad && s->pad(s->ctx, &held, &pressed) < 0) pressed = 0;

        if (pressed) {
            dirty = 1;
            switch (u->screen) {
            case SCREEN_DIALOG: {
                int choice = bl_dialog_input(&u->dialog, pressed);
                if (choice && answer(u, choice, out)) goto done;
                break;
            }
            case SCREEN_ERROR:
                /* Start again (F1 request 3, finding 13): the same Play checks. */
                if (pressed & BL_PAD_CROSS) {
                    if (play(u, out, 0)) goto done;
                    if (u->screen == SCREEN_ERROR) set_screen(u, SCREEN_START);   /* the band says why not */
                }
                else if (pressed & BL_PAD_CIRCLE) set_screen(u, SCREEN_START);
                else if (pressed & BL_PAD_SQUARE) {
                    char dir[1200];
                    if (save_bundle(u, dir, sizeof dir) == 0) snprintf(u->error_note, sizeof u->error_note, bl_str(BL_S_ERROR_BUNDLE_DONE), dir);
                    else snprintf(u->error_note, sizeof u->error_note, "%s", bl_str(BL_S_ERROR_BUNDLE_FAILED));
                }
                break;
            case SCREEN_HELP:
                if (bl_help_input(&u->help, pressed)) set_screen(u, u->help_back);
                break;
            case SCREEN_START: {
                if (pressed & BL_PAD_SQUARE) {         /* no game found: straight to where it goes (audit 5) */
                    open_help(u, u->status.readiness == BL_READY_NO_GAME && !u->game_several ? BL_HELP_FOLDER : BL_HELP_BEFORE);
                    break;
                }
                BlStartEvent ev = bl_start_input(&u->start, pressed, BL_READY_OK);   /* Play checks readiness itself */
                if (ev == BL_START_PLAY && play(u, out, 0)) goto done;
                if (ev == BL_START_OPEN) {
                    if (u->start.focus == BL_CARD_PATCH) open_patch(u);
                    else if (u->start.focus == BL_CARD_SETTINGS) { open_settings(u); set_screen(u, SCREEN_SETTINGS); }
                }
                break;
            }
            case SCREEN_PATCH:
                patch_event(u, bl_patch_input(&u->patch, pressed));
                break;
            case SCREEN_SETTINGS: {
                if (u->kb_field != BL_FIELD_NONE) { keyboard_input(u, pressed); break; }
                BlField field;
                BlSetEvent ev = bl_settings_input(&u->settings, pressed, &field);
                if (u->settings.in_panel && u->settings.item == BL_SET_ACCOUNT && !u->account_loaded) load_account_pass(u);
                if (ev != BL_SET_EV_NONE) settings_event(u, ev, field);
                break;
            }
            }
        }

        uint64_t now = s->now_ns(s->ctx);
        if (now - last_refresh >= STATUS_REFRESH_NS && u->screen == SCREEN_START) {
            BlStatus before = u->status;
            refresh_status(u, 0);
            if (memcmp(&before, &u->status, sizeof u->status) != 0) dirty = 1;
            last_refresh = now;
        }
        /* Owner finding 34: while anything moves the loop does not sleep;
         * the present paces it (the flip waits while two are pending). */
        int animating = 0;
        if (u->screen == SCREEN_START && bl_start_animate(&u->start, now)) animating = dirty = 1;
        int fade_w = 0;                            /* weight of the old frame, 0..128 */
        if (u->fading) {
            uint64_t t = now - u->fade_t0;
            if (t >= FADE_NS) u->fading = 0;
            else {
                float x = 1.0f - (float)t / (float)FADE_NS;       /* eased out like the slide */
                fade_w = (int)(x * x * x * 128.0f + 0.5f);
            }
            animating = dirty = 1;                 /* the last pass draws the new screen alone */
        }

        if (dirty) {
            draw(u);
            if (fade_w > 0) bl_frame_mix(&u->frame, u->fade_px, fade_w);
            if (s->present(s->ctx, &u->frame) < 0) { result = -1; break; }
            u->presented = 1;
            dirty = 0;
        } else if (u->screen == SCREEN_START) {
            idle_work(u);                          /* file reads, card images, screens drawn ahead */
        }
        if (!animating) s->sleep_us(s->ctx, TICK_US);
    }
done:
    bl_patch_hash_cache = NULL;
    free(u->frame.pixels);
    free(u->fade_px);
    bl_screens_release();
    bl_keyboard_wipe(&u->kb);
    bl_keyboard_release();
    memset(u, 0, sizeof *u);                      /* the account password with it */
    free(u);
    return result;
}
