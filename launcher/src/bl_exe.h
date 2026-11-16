/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The tested-exe check (ruling 2026-11-15, replaces decision 31). BLUR is tested with one
 * Blur.exe (1.2, below); any other is accepted with a warning. It is never shipped, downloaded
 * or replaced; the launcher only checks it.
 * A file of another size is UNSUPPORTED (= not tested) without hashing. The hash of a file
 * of the right size is cached in <state>/exe-check.txt, keyed by size and
 * mtime, so later starts never hash again (hashing takes seconds on the
 * console).
 */
#ifndef BL_EXE_H
#define BL_EXE_H

#include "bl_paths.h"

#define BL_TESTED_EXE_SHA "dba0264b7808a5e38d99c8c162ecd71ab06a48fbc424feb1b9a3bc1bff2fbf3f"
#define BL_TESTED_EXE_SIZE 13738496ULL
#define BL_EXE_CHECK_FILE "exe-check.txt"

typedef enum BlExeVerdict { BL_EXE_MISSING = 0, BL_EXE_TESTED, BL_EXE_UNSUPPORTED } BlExeVerdict;

/* Finds Blur.exe in p->game in any letter case (name_out gets its spelling,
 * "" when missing; may be NULL). */
BlExeVerdict bl_exe_verify(const BlPaths *p, char name_out[64]);

/* Test hooks: the expected hash (default BL_TESTED_EXE_SHA) and how many
 * times Blur.exe was hashed. */
extern const char *bl_exe_expected_sha;
extern unsigned bl_exe_hash_count;

#endif
