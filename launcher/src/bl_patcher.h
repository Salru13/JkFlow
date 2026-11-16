/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Patch (spec 8.5, BLUR 1.00 plan Task L3): bring the player's Blur folder
 * to our known-good set of files, safely and repeatably.
 *
 * Package side, files_dir/manifest.txt. Names are paths relative to the game
 * folder and may hold "/" subfolders; a name with "..", a leading "/", an
 * empty segment, a segment starting with a dot, a backslash, or a segment
 * "Blur.exe" makes the whole manifest invalid.
 *   file  <rel> <sha256> <size> [src=<package rel>]   our file (from files_dir/<src or rel>), offline and online
 *   online <rel> <sha256> <size> [src=<package rel>]  our file that only makes sense with AMAX (online only)
 *   amax  <rel> <sha256> <size> [edit=<sha256>]       a member of the player's amax_client_files.zip
 *                                                     (online only, from amax_dir/<rel>); with edit=,
 *                                                     AMAX's plugins.lua: installed through
 *                                                     bl_patch_plugins_edit, the result must have the
 *                                                     edit hash; a different file there is parked
 *                                                     (Restore puts AMAX's own list back), except an
 *                                                     older list of ours (a known hash, PKG 286's): it
 *                                                     is replaced and AMAX's own list from amax_dir goes
 *                                                     into the backup instead, so Restore brings that
 *   blur12 <rel> <sha256> <size>                      a member of the player's blur_12.zip (online only,
 *                                                     from amax_dir/<rel>); may be the top-level Blur.exe;
 *                                                     installed over the spelling on disk (any letter case)
 * Online, an amax or blur12 line takes over a file line of the same name
 * (the root d3d9.dll: our V7 offline, the AMAX proxy online).
 *   seed  <rel> <sha256> <size>                       put in place only when missing or header-only
 *                                                     (at most BL_SEED_HEADER_BYTES): never overwritten,
 *                                                     backed up or removed by restore
 *   known <sha256>                                    an older version of one of our files
 *   park  <glob>                                      top-level names (any letter case, * and ?) that are
 *                                                     parked unless they belong to the set; a matching
 *                                                     folder is walked and every file in it that is not
 *                                                     in the set is parked
 *   tested <sha256>                                   (information for bl_support)
 * A file of the set that is present, different and not ours (no current or
 * known hash) is parked too before ours goes in. Never parked: the launcher's
 * amax/plugins/account_cfg.lua and AMAX's runtime logs in amax/log/.
 *
 * Parking MOVES the file (same volume; Blur.exe is copied, so the folder
 * never lacks it) to
 *   <backup_root>/<NNNNNN>-<YYYYMMDDTHHMMSSZ>/<rel>      with backup.txt
 * backup.txt ("parked <rel> <sha> <size>", "added <rel>") is complete before
 * the first move, so an interrupted Patch never loses a file: each one is in
 * place or in a backup, and running Patch again finishes the job. NNNNNN
 * counts up, so the order of backups holds even if the clock is wrong.
 * Installs are atomic (bl_fs): temp, verify, rename.
 */
#ifndef BL_PATCHER_H
#define BL_PATCHER_H

#include <stddef.h>

typedef enum BlGameState { BL_GAME_MISSING = 0, BL_GAME_FOUND } BlGameState;
typedef enum BlFileState { BL_FILE_OK = 0, BL_FILE_MISSING, BL_FILE_DIFFERENT } BlFileState;

/* A DXVK state cache header (magic, version, entry size): a file this small
 * holds no entries yet. */
#define BL_SEED_HEADER_BYTES 12
#define BL_PATCH_MAX_ITEMS 64
#define BL_PATCH_MAX_PARK 4096        /* audit 21: foreign files one Patch parks (a plan is about 0.5 MB: heap only) */

typedef struct BlPlanItem {
    char name[128];           /* relative to the game folder */
    BlFileState state;
    int seed;                 /* seed class: installed only when missing or header-only */
    int foreign;              /* present, different, and not any of our known hashes: parked first */
    int amax;                 /* from the extracted zips (amax_dir), not from blur-files */
    int online;               /* an online-only line (online, amax, blur12) */
    unsigned entry;           /* its manifest line */
} BlPlanItem;

typedef struct BlPatchPlan {
    BlGameState game;
    char exe_name[64];        /* as spelled in the folder: "Blur.exe", "BLUR.EXE", ... */
    unsigned long long exe_size;
    char last_backup[64];     /* unused since L3 (see bl_patch_last_backup) */
    BlPlanItem items[BL_PATCH_MAX_ITEMS];
    unsigned count, pending;  /* pending: items not OK plus entries to park */
    char park[BL_PATCH_MAX_PARK][128];   /* relative paths to park (the first BL_PATCH_MAX_PARK) */
    unsigned park_count;
    unsigned park_total;      /* audit 21: every file to park; above BL_PATCH_MAX_PARK Patch refuses */
    char park_over[128];      /* the first one that did not fit ("" none) */
} BlPatchPlan;

/* amax_dir NULL: the offline set (file and seed lines). Otherwise the online
 * set, with amax_dir the folder holding the extracted, hash-checked zips
 * (bl_online_prepare), or BL_PATCH_IN_PLACE when there is none (zip items
 * not already in place then cannot be installed).
 * 0 ok; <0 when the package manifest is missing, unreadable or tampered. */
#define BL_PATCH_IN_PLACE ""
int bl_patch_plan(const char *game_dir, const char *files_dir, const char *amax_dir, BlPatchPlan *plan);
/* Which set the game folder holds: NONE (no AMAX file of ours in place; the
 * player's own 1.2 Blur.exe does not count), READY (every online line in
 * place) or PARTIAL (some AMAX files, not all). */
enum { BL_ONLINE_NONE = 0, BL_ONLINE_READY, BL_ONLINE_PARTIAL };
int bl_patch_online_state(const char *game_dir, const char *files_dir);
int bl_patch_online_in_place(const char *game_dir, const char *files_dir);   /* state == READY */
/* A file that caches the hashes of big files for bl_patch_plan
 * (bl_fs_sha256_cached); NULL: none. */
extern const char *bl_patch_hash_cache;
/* Park, install file and amax items, seed, then plan again. backup_root
 * NULL -> <game_dir>/.blur-launcher/backup. 0 ok (nothing pending
 * afterwards), <0 on failure (run again: nothing is lost). */
int bl_patch_apply(const char *game_dir, const char *files_dir, const char *amax_dir, const char *backup_root,
                   const BlPatchPlan *plan, void (*progress)(void *ctx, const char *name, unsigned done, unsigned total), void *ctx);
/* The manifest's "cache <rel> <sha256> <size>" lines (the engine's state
 * cache seed, read from DXVK_STATE_CACHE_PATH): files_dir/<rel> goes to
 * cache_dir/<rel> only when that is missing or header-only
 * (<= BL_SEED_HEADER_BYTES), source and copy hash-checked. bl_patch_plan and
 * bl_patch_apply ignore these lines. Returns how many went in, <0 on failure
 * (bl_patch_error says why; a refused copy leaves cache_dir as it was). */
int bl_patch_seed_cache(const char *files_dir, const char *cache_dir);
/* Undo every patch run since the last restore, newest first: parked files
 * go back and files patch added go. 0 ok, 1 nothing to restore, <0 failure. */
int bl_patch_restore(const char *game_dir, const char *backup_root);
/* 1 when a Patch for online play stopped before it finished and its backup
 * is not restored yet (backup.txt says "run online" and the run has no
 * "done" file): originals such as cache/gamedata.pak may be parked. Every
 * Patch restores first; Start is never Ready meanwhile. 0 otherwise. */
int bl_patch_unfinished(const char *game_dir, const char *backup_root);
/* The newest patch backup not yet restored into name. 1 found, 0 none. */
int bl_patch_last_backup(const char *backup_root, char *name, size_t cap);
/* A new, empty backup folder <backup_root>/<NNNNNN>-<stamp> whose counter is
 * above every folder there (shared with the save import, L6). 0 ok. */
int bl_backup_new_dir(const char *backup_root, char *out, size_t cap);
/* "2026-11-13 12:00 UTC" from a backup name, for the patch screen. */
void bl_backup_label(const char *name, char *out, unsigned cap);

/* AMAX's plugin list (amax/plugins/plugins.lua) as Patch installs it (owner
 * finding 25: JkFlow ships no AMAX file): every line that starts with
 * require("solo.solo") or require("fps.fps") is commented out ("-- " after
 * its indent), and require("account.account"), is added before the last
 * line that starts with "}" (indent of the first require line, else a tab;
 * the file's own line ends), unless it is already there. Already edited text
 * comes back unchanged. tools/make_manifest.py --edit-plugins does the same.
 * Returns the length written to out, or -1 (no closing "}", or out too small). */
long bl_patch_plugins_edit(const char *in, size_t n, char *out, size_t cap);

/* How many times bl_patch_plan ran (each one hashes our installed files); for tests. */
extern unsigned bl_patcher_plan_count;
/* Why the last bl_patch_apply or bl_patch_restore failed:
 * "<step> <file> errno=<n>" (for the event log), "" after a success. */
extern char bl_patch_error[200];
/* The same failure for the player's sentence: errno and the file's name
 * (relative to the game folder, "" when none). */
extern int bl_patch_errno;
extern char bl_patch_error_name[128];

#endif
