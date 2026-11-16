/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Settings -> what the game is started with (spec 8.2): the game profile
 * text, dxvk.conf and the environment. Nothing else may add environment
 * variables, so a run is fully described by launcher.ini + package version.
 *
 * Console presets live in the package (files_dir): model-base.ini (PS4 /
 * Slim, the baseline) and model-pro.ini (optional; overrides only the keys
 * it lists). Sections other than [env] and [dxvk.conf] form the profile.
 */
#ifndef BL_PROFILE_H
#define BL_PROFILE_H

#include "bl_config.h"
#include "bl_frontend.h"

/* detected_pro: 1 on a PS4 Pro, 0 otherwise or unknown. 0 ok, <0 when a
 * preset is missing or the result does not fit BlDecision. */
int bl_profile_compose(const BlConfig *c, int detected_pro, const char *files_dir, BlDecision *out);

/* Points the profile's executable and working_directory at game_dir (an
 * absolute console path, e.g. the /data/JkFlow/Blur the launcher found).
 * 0 ok; -1 bad input or no room (the profile is then unchanged). */
int bl_profile_set_game(BlDecision *d, const char *game_dir);
/* Set or replace one environment variable in the decision. <0 when full. */
int bl_decision_setenv(BlDecision *d, const char *name, const char *value);
/* Remove one environment variable (nothing when it is not set). */
void bl_decision_unsetenv(BlDecision *d, const char *name);

#endif
