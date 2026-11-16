/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The log files settings › Logging shows and "clear logs" deletes (spec 10).
 * Only these known names are ever touched:
 *   <game_dir>/Blur_d3d9.log (and .old), every file in the logs folder of
 *   data_root (BlPaths.logs: /data/JkFlow/Logs; session, engine logs)
 * keep: the file name of this run's own session log in the logs folder
 * (still being written), left out of the count and never deleted. May be NULL.
 */
#ifndef BL_LOGS_H
#define BL_LOGS_H

typedef struct BlLogFile { char path[512]; long long size; } BlLogFile;

/* Existing log files, at most max. Returns how many were stored. */
int bl_logs_list(const char *game_dir, const char *data_root, BlLogFile *out, int max);
/* How many log files there are and their total size, without any limit. */
int bl_logs_summary(const char *game_dir, const char *data_root, const char *keep, long long *bytes);
/* Delete them all except keep. Returns how many were deleted. */
int bl_logs_clear(const char *game_dir, const char *data_root, const char *keep);

#endif
