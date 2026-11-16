/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * The player's settings: <root>/state/launcher.ini (BlPaths.state) (spec 8.1).
 * INI, hand-editable over FTP. Unknown sections, keys and comments survive a
 * save; invalid values fall back to defaults with a warning; a corrupt file
 * gives defaults and a warning, never a crash. The password is kept apart
 * in online.secret (mode 0600) and never written to launcher.ini.
 */
#ifndef BL_CONFIG_H
#define BL_CONFIG_H

#include <stddef.h>

typedef enum BlModel { BL_MODEL_AUTO = 0, BL_MODEL_BASE, BL_MODEL_PRO } BlModel;
typedef enum BlOnlineMode { BL_ONLINE_OFFICIAL = 0, BL_ONLINE_PRIVATE, BL_ONLINE_OFF } BlOnlineMode;
typedef enum BlStats { BL_STATS_OFF = 0, BL_STATS_FPS, BL_STATS_FULL } BlStats;
/* Native: the native DXVK device (armed per start, falls back to compatible
 * after a failed native start). Compatible: the PE DXVK device. */
typedef enum BlRenderer { BL_RENDERER_NATIVE = 0, BL_RENDERER_COMPATIBLE } BlRenderer;
/* Settings > Frame rate ([game] frame_rate): 30 (default) or 60. The race
 * lock the engine gets at Play follows it for the tested Blur.exe only. */
typedef enum BlFrameRate { BL_FPS_30 = 0, BL_FPS_60 } BlFrameRate;

typedef struct BlConfig {
    int schema;
    BlModel model;
    BlOnlineMode online_mode;
    char host[128], auth_host[128], lobby_host[128];
    int port;                 /* 0 = the game's default ports */
    int login_account;        /* 0 anonymous, 1 account */
    char username[64];
    int logging;              /* 0 off, 1 on */
    BlStats stats;
    BlRenderer renderer;
    BlFrameRate frame_rate;
} BlConfig;

void bl_config_defaults(BlConfig *c);
/* 0 ok (also when the file does not exist yet), 1 ok with a warning in
 * warning (defaults used where values were bad), <0 I/O error. */
int bl_config_load(const char *path, BlConfig *c, char *warning, size_t cap);
/* Atomic; keeps unknown keys and comments; the previous file becomes
 * <path>.bak. 0 ok, <0 error (the old file is then unchanged). */
int bl_config_save(const char *path, const BlConfig *c);

/* 0 ok, 1 no password stored (password = ""), <0 error. */
int bl_secret_load(const char *path, char *password, size_t cap);
/* Mode 0600; an empty password deletes the file. 0 ok. */
int bl_secret_save(const char *path, const char *password);

#endif
