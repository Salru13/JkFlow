/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The JkFlow data layout (owner findings 4+5, option B). Everything lives in
 * one folder beside the player's game, /data/JkFlow (any letter case):
 *   <jkflow>/Blur          the player's Blur (bl_game_dir_find)
 *   <jkflow>/System        the root: everything the launcher and title keep
 *     game                 unused default
 *     backup               files Patch replaced or parked, imported-save backups
 *     prefix               the Wine prefix (saves in users/BLUR/...)
 *     cache                DXVK and shader caches, dxvk.conf
 *     state                events.log, session.txt, launcher.ini, account.cfg, ...
 *     import               files the player drops in over FTP (blursave, AMAX zip)
 *   <jkflow>/Logs          session and engine logs (BLUR_LOG_DIR)
 *     support              support bundles
 * A root whose last folder is not "System" (host tests) keeps logs/ and
 * support/ inside it. bl_paths_make does not create the game folder.
 */
#ifndef BL_PATHS_H
#define BL_PATHS_H

#include <stddef.h>

#define BL_DATA_ROOT "/data/JkFlow/System"
#define BL_LOGS_ROOT "/data/JkFlow/Logs"
#define BL_WINE_GAME_DIR "Z:\\data\\JkFlow\\Blur"
/* Where 1.00 test builds kept everything; moved once by bl_paths_migrate. */
#define BL_OLD_ROOT "/data/BLUR"

typedef struct BlPaths {
    char root[64], game[96], backup[96], prefix[96], cache[96], logs[96],
         support[96], state[96], import[96], saves[256];   /* saves = Blur save folder in the prefix */
} BlPaths;

/* root NULL -> the default root (bl_paths_set_default_root, else
 * BL_DATA_ROOT). A root too long for the fields leaves every field empty
 * (never a truncated path); bl_paths_make then fails. */
void bl_paths_init(BlPaths *p, const char *root);
/* The title sets the System root it found (the player's JkFlow spelling) so
 * that every bl_paths_init(NULL) agrees with it. NULL or too long: back to
 * BL_DATA_ROOT. */
void bl_paths_set_default_root(const char *root);
/* mkdir -p each folder except game and saves. 0 ok (also when they exist). */
int  bl_paths_make(const BlPaths *p);

/* The player's game folder (owner decision 2026-11-15): <data>/JkFlow/Blur,
 * letter case ignored at both levels. 1: found, out = its spelling on disk.
 * 0: none, out = <data>/JkFlow/Blur. -1: several spellings, out and other
 * name two of them (the launcher shows both instead of guessing). */
#define BL_GAME_ROOT "/data"
#define BL_GAME_PARENT "JkFlow"
#define BL_GAME_LEAF "Blur"
int bl_game_dir_find(const char *data, char *out, size_t cap, char *other, size_t ocap);

/* The JkFlow folder in <data>, looked up before anything is created: the
 * spelling on disk, letter case ignored. Several: the only one holding a
 * Blur folder, else exactly "JkFlow", else the first in strcmp order.
 * 1: found. 0: none; out = <data>/JkFlow, created when create is set.
 * -1: out too small or create failed. */
int bl_jkflow_dir(const char *data, char *out, size_t cap, int create);

/* One-time move of an old root (BL_OLD_ROOT) into p's layout: logs -> p->logs,
 * support -> p->support, every other entry -> <p->root>/<name>. rename()
 * only (same filesystem, atomic); a target that exists is never
 * overwritten: the old entry is kept beside it as <target>.old-BLUR (or
 * .old-BLUR2..9) and the log says so. An EMPTY folder at the target is not
 * the player's data (an earlier build made it fresh): it is replaced. Any
 * number of entries. The old root is removed once empty.
 * Audit 5: an old root holding Blur.exe directly (any letter case) is the
 * player's game, not old data: it is renamed whole to <JkFlow>/BLUR (the folder
 * above p->root) when no game is there (missing or empty, any letter case),
 * else left alone ("kept (game already present)"); never merged into System.
 * Safe to repeat. Returns how many entries moved, 0 when there is no old
 * root, -1 when one could not be moved (the old root then stays). log may be
 * NULL; it gets one line per move and per problem. While the old root still
 * holds anything (bl_paths_pending), the title must not create fresh data in
 * p's folders: the next start would then keep the old data only beside it. */
typedef void (*BlPathsLog)(void *ctx, const char *line);
int bl_paths_migrate(const char *old_root, const BlPaths *p, BlPathsLog log, void *ctx);
/* 1 when old_root is a folder that still holds any entry (or cannot be read). */
int bl_paths_pending(const char *old_root);

#endif
