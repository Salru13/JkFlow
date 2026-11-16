/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * How the last game session ended (gap items 5 and 13), without any help
 * from the game itself:
 *   play           bl_session_begin() writes <state_dir>/session.txt
 *                  ("blur-launcher session 1 <stamp> first_frame=0")
 *   first frame    the title calls bl_session_first_frame() where it drops
 *                  the loader (first_frame=1)
 *   normal exit    the title calls bl_session_end_clean() when Wine exits
 *                  (the file is removed)
 * At the next launcher start bl_session_check() reads what is left:
 *   no file                -> clean (or never played): the count goes to 0
 *   first_frame=0          -> the start failed before the game's first frame
 *   first_frame=1          -> the session ended abnormally (crash, hang, or
 *                             closed from the PS menu): the count goes up
 * The launcher only suggests Compatible after BL_SESSION_SUGGEST abnormal
 * ends in a row; it never relaunches anything by itself.
 */
#ifndef BL_SESSION_H
#define BL_SESSION_H

#include <stddef.h>

#define BL_SESSION_FILE "session.txt"
#define BL_SESSION_COUNT_FILE "abnormal-ends.txt"
#define BL_SESSION_SUGGEST 2

typedef enum BlSessionEnd {
    BL_SESSION_CLEAN = 0,          /* or no session on record */
    BL_SESSION_NO_FIRST_FRAME,     /* the start failed */
    BL_SESSION_ABNORMAL            /* the game ran, then the session did not end normally */
} BlSessionEnd;

/* At launcher start. Consumes the record; *abnormal gets the number of
 * abnormal ends in a row (updated and stored). when (may be NULL) gets the
 * record's stamp. state_dir NULL: BL_SESSION_CLEAN, *abnormal 0. */
BlSessionEnd bl_session_check(const char *state_dir, int *abnormal, char *when, size_t cap);
/* At play. Remembers state_dir for the two calls below. 0 ok. */
int bl_session_begin(const char *state_dir);
/* Called by the title (no arguments: it has no launcher state). No-ops
 * before bl_session_begin. Each runs at most once per session. */
void bl_session_first_frame(void);
void bl_session_end_clean(void);
/* Settings: forget the abnormal ends (after the player chose). 0 ok. */
int bl_session_reset_count(const char *state_dir);

#endif
