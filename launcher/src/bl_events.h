/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The launcher's always-on event log (owner requirement "Logging & support",
 * point 1). On even with Logging off: a handful of lines per start, never
 * anything per frame.
 *   <root>/state/events.log       current
 *   <root>/state/events.log.1    the previous one
 * When events.log reaches BL_EVENTS_MAX bytes it becomes events.log.1 (the
 * old .1 goes), so the pair never holds more than 2 x BL_EVENTS_MAX.
 * One line per event:
 *   <YYYYMMDDTHHMMSSZ> <code> <text>
 * <code> is the code a failure screen shows (BL_E_* below), or "I" for an
 * information line. Support asks for the code and finds the line.
 */
#ifndef BL_EVENTS_H
#define BL_EVENTS_H

#include <stddef.h>

#define BL_EVENTS_FILE "events.log"
#define BL_EVENTS_MAX (32 * 1024)

/* Codes shown on screen and written to events.log. */
#define BL_E_INFO            "I"
#define BL_E_START_FAILED    "E101"   /* the last start died before the game's first frame */
#define BL_E_SESSION_ABNORMAL "E102"  /* the game ran, then did not end normally */
#define BL_E_NATIVE_FAILED   "E103"   /* the native renderer did not reach its first frames: Compatible used */
#define BL_E_NATIVE_UNARMED  "E104"   /* native chosen, but the arm files could not be written */
#define BL_E_GAME_MISSING    "E201"   /* no Blur.exe in the game folder */
#define BL_E_UNPATCHED       "E202"   /* our files missing or different */
#define BL_E_PACKAGE_BROKEN  "E203"   /* the package's own file list is damaged */
#define BL_E_LEFTOVER        "E204"   /* files from earlier experiments change how the game runs */
#define BL_E_EXE_UNSUPPORTED "E205"   /* Blur.exe is not the tested 1.2 build (warning only) */
#define BL_E_GAME_SEVERAL    "E206"   /* several /data/JkFlow/Blur spellings: the player keeps one */
#define BL_E_PATCH_FAILED    "E301"   /* patch blur stopped (file and step in the line) */
#define BL_E_RESTORE_FAILED  "E302"
#define BL_E_DISK_FULL       "E303"   /* not enough free space for the step */
#define BL_E_SAVES_FAILED    "E304"   /* save backup or restore failed */
#define BL_E_SETTINGS_FAILED "E305"   /* launcher.ini could not be written */
#define BL_E_BUNDLE_FAILED   "E306"
#define BL_E_AMAX_MISSING    "E307"   /* Patch for online play: a zip is missing, damaged or wrong, or could not be unpacked */
#define BL_E_CONFIG_WARNING  "W401"   /* launcher.ini had bad values: defaults used */

/* Where events go: state_dir = BlPaths.state (<root>/state). NULL turns logging off. */
void bl_events_open(const char *state_dir);
/* Append one line (printf format). Cheap: open, append, close. */
void bl_events_log(const char *code, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
/* The current file's path ("" when closed). */
const char *bl_events_path(void);

#endif
