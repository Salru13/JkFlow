/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Save-game backup and restore. Blur keeps its saves in the Wine prefix,
 * which the console's own save backup does not cover:
 *   <root>/prefix/drive_c/users/BLUR/AppData/Roaming/Bizarre Creations/Blur
 * (BlPaths.saves). Backups go to <root>/backup/saves/<NNNNNN>-<YYYYMMDDTHHMMSSZ>/
 * with the same tree and a files.txt ("<sha256> <size> <relative path>",
 * sorted by path). A backup equal to the newest one is not made again, and
 * only the newest BL_SAVES_KEEP backups are kept.
 */
#ifndef BL_SAVES_H
#define BL_SAVES_H

#include <stddef.h>

#include "bl_paths.h"

#define BL_SAVES_KEEP 10
#define BL_SAVES_MAX_FILES 256

/* The save folder (p->saves). 0 found, -1 not there (no save yet). */
int bl_saves_find2(const BlPaths *p, char *out, size_t cap);
/* 0 made, 1 nothing to do (no saves, or equal to the newest backup), <0 error. */
int bl_saves_backup(const BlPaths *p);
/* Put the newest backup back: the current saves are backed up first, then
 * the folder is made equal to that backup. 0 ok, 1 no backup, <0 error. */
int bl_saves_restore(const BlPaths *p);
/* How many backups there are; newest (may be NULL) gets its folder name. */
int bl_saves_count(const BlPaths *p, char *newest, size_t cap);
/* Bytes the save folder holds (for the disk-space check), 0 when none. */
long long bl_saves_bytes(const BlPaths *p);

#endif
