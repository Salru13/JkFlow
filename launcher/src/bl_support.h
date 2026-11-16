/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Support and housekeeping (production audit 2026-11-14 and gap items 10,
 * 11, 12, 14, 15, 16, 17): the fixed log file set and its bounds, the
 * support bundle and Blur.exe information.
 *
 * Folders: data_root = /data/<runtime>/ (settings and runtime logs),
 * state_dir = <root>/state, game_dir = <root>/game (bl_paths.h).
 */
#ifndef BL_SUPPORT_H
#define BL_SUPPORT_H

#include <stddef.h>

/* ---- the fixed log file set (PROD_AUDIT.md "Logging & support") ---- */

typedef enum BlLogWhere { BL_LOG_GAME = 0, BL_LOG_DATA, BL_LOG_STATE } BlLogWhere;
typedef struct BlLogSpec {
    BlLogWhere where;
    const char *name;         /* relative to its folder; the logs folder and the AMAX log folder are patterns */
    int always;               /* written with Logging off too */
    int rotate;               /* the launcher renames it to <name>.old above BL_LOG_ROTATE_BYTES (bundled too) */
    const char *writer, *answers;
} BlLogSpec;

#define BL_LOG_ROTATE_BYTES (4LL * 1024 * 1024)
#define BL_LOG_SESSION_KEEP 16

extern const BlLogSpec bl_log_specs[];
extern const int bl_log_spec_count;

/* Before every start: logs above BL_LOG_ROTATE_BYTES become <name>.old
 * (replacing the older .old), and only the newest BL_LOG_SESSION_KEEP
 * session logs are kept (keep: this run's own, never touched). Returns how
 * many files were rotated or removed. */
int bl_logs_rotate(const char *game_dir, const char *data_root, const char *keep);

/* Logging is passed to the game through the environment only (BLUR_LOGS,
 * BLUR_LOG_DIR; design 7): there are no switch files. */

/* ---- disk space ---- */

/* Free bytes on the volume holding path, or -1 unknown. NULL by default:
 * the host sets it (the PS4 libc's statfs is a new import, so the title
 * decides). Checks are skipped while it is unknown. */
extern long long (*bl_free_bytes)(const char *path);
/* 1 when need bytes (plus a 16 MiB margin) fit or the space is unknown. */
int bl_space_ok(const char *path, long long need);

/* ---- Blur.exe information (information only; any copy is accepted) ---- */

typedef struct BlExeInfo {
    int found;
    char name[64];
    unsigned long long size;
    char sha[65];
    int tested;               /* the hash is a "tested <sha256>" line of manifest.txt */
} BlExeInfo;
/* Hashes Blur.exe (a few seconds on the console: call it on the patch
 * screen, not every frame). 0 ok (also when not found). */
int bl_exe_info(const char *game_dir, const char *files_dir, BlExeInfo *out);

/* ---- support bundle ---- */

typedef struct BlSupportInfo {
    const char *data_root, *game_dir, *files_dir, *build_id, *session_log;
    const char *renderer;     /* "native", "compatible", "compatible (native failed ...)" */
    const char *config_path;  /* launcher.ini */
} BlSupportInfo;
/* Audit 26: the PS4 local user name, masked as <ps4-user> in the copied text
 * files (NULL or "": none). bl_frontend sets it from BlServices.local_user. */
extern const char *bl_support_local_user;
/* Copies the fixed set (bl_log_specs) into <data_root>/support/<stamp>/
 * with MANIFEST.txt (path, size, sha256 of each file; build id; time; the
 * Blur.exe verdict; the package manifest's hash; installed files) and
 * README.txt (what each file answers). account.cfg, account_cfg.lua,
 * blursave and online.secret are never copied. out gets the folder.
 * 0 ok, -2 not enough free space, <0 error (also without a data folder). */
int bl_support_bundle(const BlSupportInfo *in, char *out, size_t cap);

#endif
