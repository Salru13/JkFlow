/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Loader shown from "play" until Blur's first frame (spec 9, option A):
 * the original logo with a slow glow pulse, red and blue light trails
 * sweeping past, "Starting Blur…". No ring, no dots, no percentage.
 * Also the "Start failed" screen that replaces it when the game stops
 * before its first frame.
 */
#ifndef BL_LOADER_H
#define BL_LOADER_H

#include "bl_ui.h"

/* Deterministic for a given t_ms (time since play). Draws the whole frame.
 * Meant to be called at most every 33 ms (30 frames per second). */
void bl_loader_draw(BlFrame *f, uint64_t t_ms);
/* The same with another big line (NULL: "Starting Blur..."). */
void bl_loader_draw_text(BlFrame *f, uint64_t t_ms, const char *big);
/* Forget which buffers hold a previous loader frame (the next draw into any
 * buffer is a full one). Drawing notices foreign content by itself; this is
 * for hosts that reuse a buffer and want to be certain. */
void bl_loader_reset(void);
/* Free the enlarged logo (call once the game shows its first frame). */
void bl_loader_release(void);

/* The loader with a progress bar and a status line under the logo: the
 * title's first-start import (T1), drawn before bl_frontend_run. big: the
 * big line (BL_S_LOADER_PREPARE there; NULL: "Starting Blur..."). total 0:
 * no bar. status may be NULL. */
void bl_loader_draw_progress(BlFrame *f, uint64_t t_ms, const char *big, unsigned done, unsigned total, const char *status);

/* "blur didn't start". what: optional short reason (may be NULL, never a
 * path); log_name: the session log's file name only (may be NULL); note: a
 * line under them (the support bundle result), may be NULL. Square saves a
 * support bundle. */
void bl_error_draw(BlFrame *f, const char *what, const char *log_name, const char *note);

/* "blur didn't start" with the title's one-sentence reason (wrapped in a
 * 1600-pixel column) and a hint line under it: the title's own stop screens
 * (old data that could not move, too many settings), drawn without the
 * launcher running. Returns the y below the hint. */
int bl_notice_draw(BlFrame *f, const char *text, const char *hint);

#endif
