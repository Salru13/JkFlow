/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#define _POSIX_C_SOURCE 200809L
#include "bl_session.h"
#include "bl_fs.h"
#include "bl_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char g_state[1024];
static int g_first_done, g_end_done;

static int join(char *out, size_t cap, const char *dir, const char *name)
{
    return dir && dir[0] && snprintf(out, cap, "%s/%s", dir, name) < (int)cap ? 0 : -1;
}

static void stamp(char out[32])
{
    bl_utc_stamp(out);
}

static int write_record(const char *state_dir, const char *when, int first_frame)
{
    char path[1100], line[128];
    if (join(path, sizeof path, state_dir, BL_SESSION_FILE)) return -1;
    int n = snprintf(line, sizeof line, "blur-launcher session 1 %s first_frame=%d\n", when, first_frame);
    return bl_fs_mkdirs(state_dir) == 0 && bl_fs_write_atomic(path, line, (size_t)n, 0644) == 0 ? 0 : -1;
}

static int read_count(const char *state_dir)
{
    char path[1100], text[32];
    if (join(path, sizeof path, state_dir, BL_SESSION_COUNT_FILE) || bl_fs_read(path, text, sizeof text) < 0) return 0;
    int v = atoi(text);
    return v < 0 ? 0 : v > 1000 ? 1000 : v;
}

static void write_count(const char *state_dir, int v)
{
    char path[1100], text[32];
    if (join(path, sizeof path, state_dir, BL_SESSION_COUNT_FILE)) return;
    if (v == 0) { unlink(path); return; }
    int n = snprintf(text, sizeof text, "%d\n", v);
    (void)bl_fs_write_atomic(path, text, (size_t)n, 0644);
}

BlSessionEnd bl_session_check(const char *state_dir, int *abnormal, char *when, size_t cap)
{
    if (abnormal) *abnormal = 0;
    if (when && cap) when[0] = 0;
    if (!state_dir || !state_dir[0]) return BL_SESSION_CLEAN;
    char path[1100], text[160] = "";
    int count = read_count(state_dir);
    BlSessionEnd end = BL_SESSION_CLEAN;
    if (!join(path, sizeof path, state_dir, BL_SESSION_FILE) && bl_fs_read(path, text, sizeof text) >= 0) {
        char w[40] = "";
        int first = 0;
        if (sscanf(text, "blur-launcher session 1 %39s first_frame=%d", w, &first) == 2 && when) snprintf(when, cap, "%s", w);
        end = first ? BL_SESSION_ABNORMAL : BL_SESSION_NO_FIRST_FRAME;
        unlink(path);
    }
    if (end == BL_SESSION_CLEAN) count = 0;
    else if (end == BL_SESSION_ABNORMAL) count++;
    /* A failed start is shown on its own screen and handled by the native
     * guard; it neither adds to nor clears the count of abnormal ends. */
    write_count(state_dir, count);
    if (abnormal) *abnormal = count;
    return end;
}

int bl_session_begin(const char *state_dir)
{
    g_state[0] = 0;
    g_first_done = g_end_done = 0;
    char when[32];
    stamp(when);
    if (!state_dir || write_record(state_dir, when, 0) != 0) return -1;
    snprintf(g_state, sizeof g_state, "%s", state_dir);
    return 0;
}

void bl_session_first_frame(void)
{
    if (!g_state[0] || g_first_done || g_end_done) return;
    g_first_done = 1;
    char when[32];
    stamp(when);
    (void)write_record(g_state, when, 1);
}

void bl_session_end_clean(void)
{
    if (!g_state[0] || g_end_done) return;
    g_end_done = 1;
    char path[1100];
    if (!join(path, sizeof path, g_state, BL_SESSION_FILE)) unlink(path);
}

int bl_session_reset_count(const char *state_dir)
{
    if (!state_dir) return -1;
    write_count(state_dir, 0);
    return 0;
}
