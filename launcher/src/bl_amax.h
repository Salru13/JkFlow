/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The online set for "Patch for online play" (owner spec 2026-11-15 v3).
 * JkFlow never ships an AMAX or Activision file. The player brings two
 * zips from amax-emu.com (How to play):
 *   amax_client_files.zip   the AMAX client set
 *   blur_12.zip             Blur.exe 1.2, cache/gamedata.pak, cache/gamedata.sig
 * Source order, for each zip on its own:
 *   1. placed by the player in zip_dir (/data/JkFlow, the parent of the game
 *      folder; the name in any letter case)
 *   2. when it is missing or wrong: a download of its URL with https_get, when
 *      the title has it and the network is up, to zip_dir/<name> (the final
 *      path: the service writes <final>.part and renames it only when the
 *      download completed), at most the pinned size + BL_ONLINE_DL_MARGIN
 *      bytes; the service reports a stalled download (over 30 s) as a failure.
 *      A downloaded zip stays there, so a later Patch reuses it; one with the
 *      wrong size or hash is deleted (it never keeps the final name).
 * A zip must have the pinned size and SHA-256. Both are unpacked (streamed,
 * bl_zip.h) into <state>/online-stage/ and every "amax" and "blur12" line of
 * manifest.txt is checked against its member before Patch may use the folder.
 * Nothing in the game folder changes here.
 */
#ifndef BL_AMAX_H
#define BL_AMAX_H

#include <stddef.h>

#include "bl_paths.h"

#define BL_AMAX_ZIP_NAME "amax_client_files.zip"
#define BL_AMAX_ZIP_URL "https://cs.amax-emu.com/amax_client_files.zip"
#define BL_AMAX_ZIP_SHA "4ee93bc7676e4f9f03d2bd657300c47594fad5265fc15c2cd911f190567425c7"
#define BL_AMAX_ZIP_SIZE 3300467LL
#define BL_BLUR12_ZIP_NAME "blur_12.zip"
#define BL_BLUR12_ZIP_URL "https://cs.amax-emu.com/blur_12.zip"
#define BL_BLUR12_ZIP_SHA "bfebfba6d5f459bfa6cb64278f874cc49c76663685815520e534a51bdd398d9a"
#define BL_BLUR12_ZIP_SIZE 27496856LL
#define BL_ONLINE_STAGE "online-stage"
#define BL_HASH_CACHE_FILE "hash-cache.txt"     /* in <state>: hashes of big files (bl_fs_sha256_cached) */
#define BL_ONLINE_MAX_TOTAL (64LL * 1024 * 1024)  /* uncompressed, both zips: the real sets are about 7 and 37 MB */

typedef enum BlOnlineSource { BL_ONLINE_SRC_NONE = 0, BL_ONLINE_SRC_PLACED, BL_ONLINE_SRC_DOWNLOAD, BL_ONLINE_SRC_MIXED } BlOnlineSource;

typedef int (*BlHttpsGet)(void *ctx, const char *url, const char *dest, long long max_bytes,
                          void (*prog)(void *, long long, long long), void *pctx);

#define BL_ONLINE_NO_ZIPS (-2)
#define BL_ONLINE_DOWNLOAD_ERR (-3)
#define BL_ONLINE_DL_MARGIN (64LL * 1024)
/* https_get results (the title's blur_host_https_get, audit 8). The service
 * keeps <dest>.part on NO_CONNECTION, INTERRUPTED and CERT_ERROR so the next
 * Patch resumes it; it removes it on SERVER_ERROR and RESUME_MISMATCH. */
#define BL_DL_FAILED (-1)
#define BL_DL_NO_CONNECTION (-2)
#define BL_DL_INTERRUPTED (-3)
#define BL_DL_SERVER_ERROR (-4)
#define BL_DL_HASH_MISMATCH (-5)          /* the launcher's own check; the title never returns it */
#define BL_DL_CERT_ERROR (-6)
#define BL_DL_RESUME_MISMATCH (-7)

/* 0 ok: stage_out is the folder with both checked sets. BL_ONLINE_NO_ZIPS:
 * neither zip is placed and none can be downloaded (the player needs the
 * instructions; err says so). BL_ONLINE_DOWNLOAD_ERR: a download failed or
 * arrived wrong (err says which and what to do; the partial file is gone).
 * -1: err holds one sentence with the exact cause and the next action (which
 * placed zip, what is wrong). prog/pctx: download progress (done, total
 * bytes), may be NULL. Nothing in the game folder changes. */
int bl_online_prepare(const BlPaths *p, const char *files_dir, const char *zip_dir, BlHttpsGet https_get, void *ctx,
                      int net_up, void (*prog)(void *, long long, long long), void *pctx,
                      char stage_out[256], BlOnlineSource *src, char *err, size_t cap);
/* The zip being downloaded (for the progress text), "" otherwise. */
extern const char *bl_online_downloading;
/* Remove <state>/online-stage (after Patch). */
void bl_online_unstage(const BlPaths *p);
/* At every Patch start: the stage. An unfinished download (<name>.part in
 * zip_dir) stays for the service to resume (audit 8). */
void bl_online_cleanup(const BlPaths *p, const char *zip_dir);
/* The zip placed in zip_dir under name in any letter case: 1 found (path in out). */
int bl_online_find_zip(const char *zip_dir, const char *name, char *out, size_t cap);

/* Test hooks: the pinned zip hashes and sizes. */
extern const char *bl_amax_zip_sha;
extern long long bl_amax_zip_size;
extern const char *bl_blur12_zip_sha;
extern long long bl_blur12_zip_size;

#endif
