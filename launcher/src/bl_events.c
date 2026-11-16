/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
#define _POSIX_C_SOURCE 200809L
#include "bl_events.h"
#include "bl_fs.h"
#include "bl_time.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_path[1024];

void bl_events_open(const char *state_dir)
{
    g_path[0] = 0;
    if (!state_dir || !state_dir[0] || bl_fs_mkdirs(state_dir) != 0) return;
    if (snprintf(g_path, sizeof g_path, "%s/%s", state_dir, BL_EVENTS_FILE) >= (int)sizeof g_path) g_path[0] = 0;
}

const char *bl_events_path(void) { return g_path; }

void bl_events_log(const char *code, const char *fmt, ...)
{
    if (!g_path[0]) return;
    char line[700], stamp[32];
    bl_utc_stamp(stamp);
    int n = snprintf(line, sizeof line, "%s %s ", stamp, code ? code : BL_E_INFO);
    va_list ap;
    va_start(ap, fmt);
    if (n > 0 && n < (int)sizeof line) vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    va_end(ap);
    /* One line, no control characters (paths and names come from the disk). */
    for (char *p = line; *p; p++) if ((unsigned char)*p < 0x20) *p = ' ';
    size_t len = strlen(line);
    struct stat st;
    if (stat(g_path, &st) == 0 && (size_t)st.st_size + len + 1 > BL_EVENTS_MAX) {
        char old[1100];
        if (snprintf(old, sizeof old, "%s.1", g_path) < (int)sizeof old) (void)rename(g_path, old);
    }
    FILE *f = fopen(g_path, "a");
    if (!f) return;
    fprintf(f, "%s\n", line);
    /* On disk before the next step: a later crash (no normal exit) still
     * leaves the lines that led to it. A handful of lines per start. */
    fflush(f);
    (void)fsync(fileno(f));
    fclose(f);
}
