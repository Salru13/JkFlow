/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Every text the launcher can show, in English, in one table (spec 6). */
#ifndef BL_STRINGS_H
#define BL_STRINGS_H

typedef enum BlStringId {
    /* Start carousel cards (lower case, as in the game's menus) */
    BL_S_CARD_SETTINGS = 0,
    BL_S_CARD_PLAY,
    BL_S_CARD_PATCH,
    /* Band sentence for the focused card */
    BL_S_BAND_SETTINGS,
    BL_S_BAND_PLAY,
    BL_S_BAND_PATCH,
    /* Why play cannot start */
    BL_S_GAME_MISSING,
    BL_S_GAME_SEVERAL,
    BL_S_GAME_SEVERAL_FMT,
    BL_S_GAME_UNPATCHED,
    /* Top bar status */
    BL_S_STATUS_FOUND,
    BL_S_STATUS_NOT_FOUND,
    BL_S_STATUS_PATCHED,
    BL_S_STATUS_NOT_PATCHED,
    BL_S_STATUS_ONLINE,
    BL_S_STATUS_OFFLINE,
    BL_S_STATUS_LOGGING_ON,
    /* Button hints */
    BL_S_HINT_SELECT,
    BL_S_HINT_BROWSE,
    BL_S_HINT_BACK,
    BL_S_HINT_BACK_TO_START,
    BL_S_HINT_PATCH,
    BL_S_HINT_RESTORE,
    BL_S_HINT_EDIT,
    BL_S_HINT_MOVE,
    BL_S_HINT_CHANGE,
    BL_S_HINT_SAVE_BACK,
    BL_S_HINT_SCROLL,
    /* Loader and start failed */
    BL_S_LOADER,
    BL_S_LOADER_PREPARE,     /* the big line while the first start prepares */
    BL_S_LOADER_FIRST_START, /* the small line under it */
    BL_S_ERROR_TITLE,
    BL_S_ERROR_BODY,
    BL_S_ERROR_LOG,          /* printf format with one %s: the log file name */
    /* patch blur */
    BL_S_PATCH_TITLE,
    BL_S_PATCH_PROGRESS,
    BL_S_PATCH_CONFIRM_RESTORE,
    BL_S_PATCH_RESTORED,
    BL_S_PATCH_NO_BACKUP,
    BL_S_PATCH_RESTORE_FAILED,
    BL_S_PATCH_COPYING,      /* %s: file name */
    BL_S_PATCH_PACKAGE_BROKEN,
    BL_S_BAND_PATCH_SCREEN,
    /* settings */
    BL_S_SETTINGS_TITLE,
    BL_S_SET_ONLINE,
    BL_S_SET_STATS,
    BL_S_SET_LOGGING,
    BL_S_PANEL_ONLINE,
    BL_S_PANEL_STATS,
    BL_S_PANEL_LOGGING,
    BL_S_ROW_MODE,
    BL_S_ROW_SERVER,
    BL_S_ROW_PORT,
    BL_S_ROW_SIGNIN,
    BL_S_ROW_USERNAME,
    BL_S_ROW_PASSWORD,
    BL_S_ROW_TEST,
    BL_S_ROW_SHOW,
    BL_S_ROW_LOGGING,
    BL_S_ROW_CLEAR,
    BL_S_OPT_OFFICIAL,
    BL_S_OPT_PRIVATE,
    BL_S_OPT_OFF,
    BL_S_OPT_ANONYMOUS,
    BL_S_OPT_ACCOUNT,
    BL_S_OPT_ON,
    BL_S_PORT_DEFAULT,
    BL_S_NOT_SET,
    BL_S_NOTE_ONLINE,
    BL_S_NOTE_STATS,
    BL_S_NOTE_LOGGING,
    BL_S_NET_FAIL,
    BL_S_NET_NO_HOST,
    BL_S_NET_UNRESOLVED,
    BL_S_INVALID_HOST,
    BL_S_INVALID_PORT,
    BL_S_INVALID_TEXT,
    BL_S_LOGS_CLEARED,       /* %u files */
    BL_S_LOG_SUMMARY,        /* %d files, %.1f MB */
    BL_S_SETTINGS_WARNING,
    BL_S_SETTINGS_SAVED,
    BL_S_SETTINGS_SAVE_FAILED,
    BL_S_BAND_ONLINE,
    BL_S_BAND_STATS,
    BL_S_BAND_LOGGING,
    BL_S_OSK_SERVER,
    BL_S_OSK_PORT,
    BL_S_OSK_USERNAME,
    BL_S_OSK_PASSWORD,
    /* production review 2026-11-14 */
    BL_S_ERROR_CODE,          /* %s code, %s time */
    BL_S_STATUS_PADS,         /* %d pads */
    BL_S_DISK_FULL,
    BL_S_SET_SUPPORT,
    BL_S_PANEL_SUPPORT,
    BL_S_BAND_SUPPORT,
    BL_S_ROW_BUNDLE,
    BL_S_BUNDLE_DONE,         /* %s folder */
    BL_S_BUNDLE_FAILED,
    BL_S_NOTE_SUPPORT,
    BL_S_SUMMARY_SUPPORT,
    BL_S_SET_ACCOUNT,
    BL_S_PANEL_ACCOUNT,
    BL_S_BAND_ACCOUNT,
    BL_S_ROW_ACCOUNT_SAVE,
    BL_S_ROW_ACCOUNT_REMOVE,
    BL_S_NOTE_ACCOUNT_CREATE,
    BL_S_NOTE_ACCOUNT_STORED,
    BL_S_OSK_ACCOUNT_USER,
    BL_S_OSK_ACCOUNT_PASS,
    BL_S_ACCOUNT_INVALID,
    BL_S_ACCOUNT_INCOMPLETE,
    BL_S_ACCOUNT_SAVED,
    BL_S_ACCOUNT_REMOVED,
    BL_S_ACCOUNT_SAVE_FAILED,
    BL_S_CONFIRM_ACCOUNT_REMOVE,
    BL_S_CLOCK_OFF,
    BL_S_CLOCK_BAD_YEAR,
    BL_S_CLOCK_PLAY_ANYWAY,
    BL_S_CLOCK_BACK,
    BL_S_GAME_UNSUPPORTED,
    BL_S_PLAY_UPDATE,
    BL_S_DIALOG_PATCH_NOW,
    BL_S_DIALOG_CANCEL,
    BL_S_PLAY_NO_ACCOUNT,
    BL_S_DIALOG_PLAY,
    BL_S_DIALOG_BACK,
    BL_S_NATIVE_PREPARE_FAILED,
    BL_S_IMPORT_CONFLICT,
    BL_S_DIALOG_KEEP,
    BL_S_DIALOG_USE_IMPORT,
    BL_S_STEP_1,
    BL_S_STEP_2,
    BL_S_STEP_3,
    BL_S_STEP_4,
    BL_S_STEP_5,
    BL_S_STEP_6,
    BL_S_STEP_7,
    BL_S_STEP_8,
    BL_S_STEP_9,
    BL_S_PATCH_ALL_DONE,
    BL_S_PATCH_EXE_TESTED,
    BL_S_PATCH_EXE_OTHER,
    BL_S_PATCH_EXE_NONE,
    BL_S_HINT_BUNDLE,
    BL_S_CLOCK_CHECKING,
    BL_S_ERROR_BUNDLE_DONE,
    BL_S_ERROR_BUNDLE_FAILED,
    BL_S_STATUS_READY_ONLINE,
    BL_S_STATUS_ONLINE_MISSING,
    BL_S_PATCH_DONE_ONLINE,
    BL_S_PATCH_OFFLINE,
    BL_S_PATCH_ONLINE,
    BL_S_PATCH_OFFLINE_NOTE,
    BL_S_PATCH_ONLINE_NOTE,
    BL_S_PATCH_ONLINE_HELP,  /* under it: where the files go is in Help */
    BL_S_PATCH_SWITCH_OFFLINE,
    BL_S_ONLINE_NO_ZIPS,
    BL_S_ONLINE_ONLY_ONE,
    BL_S_ONLINE_ZIP_BAD,
    BL_S_ONLINE_DOWNLOAD_FAILED,
    BL_S_ONLINE_UNPACK_FAILED,
    BL_S_ONLINE_MEMBER_BAD,
    BL_S_ONLINE_NO_NETWORK,  /* no network and no zips placed (owner finding 31) */
    BL_S_PATCH_HELP_MORE,    /* under an online stop; {square}: the Square key cap */
    BL_S_HINT_HELP,
    BL_S_HINT_HELP_ONLINE,   /* the Square hint on the Patch screen */
    BL_S_HINT_CHOOSE,
    BL_S_HELP_TITLE,
    BL_S_BAND_HELP,
    BL_S_HELP_A_HEAD,
    BL_S_HELP_A_BODY,
    BL_S_HELP_A_DIR,         /* owner finding 37: the folder as full paths ({game}), no tree */
    BL_S_HELP_A_LOOK,
    BL_S_HELP_A_EXE,
    BL_S_HELP_A_BINK,
    BL_S_HELP_A_REST,
    BL_S_HELP_A_AFTER,
    BL_S_HELP_B_HEAD,
    BL_S_HELP_B_BODY,
    BL_S_HELP_C_HEAD,
    BL_S_HELP_C_BODY,
    BL_S_HELP_C_ZIP1,
    BL_S_HELP_C_ZIP2,
    BL_S_HELP_C_THEN,
    BL_S_HELP_D_HEAD,
    BL_S_HELP_D_BODY,
    BL_S_HELP_S_HEAD,        /* saves from a PC (owner finding 18) */
    BL_S_HELP_S_BODY,
    BL_S_HELP_S_TO,
    BL_S_HELP_S_THEN,
    BL_S_HELP_S_BACK,
    BL_S_HELP_E_HEAD,
    BL_S_HELP_E_FOLDER,
    BL_S_HELP_E_TWO,
    BL_S_HELP_E_ONE_ZIP,
    BL_S_HELP_E_BAD_ZIP,
    BL_S_HELP_E_NO_NET,
    BL_S_HELP_E_CLOCK,
    BL_S_HELP_E_EXE,
    BL_S_HELP_E_OFFLINE,
    BL_S_HELP_E_RESTORE,
    BL_S_HELP_E_CLOSE,       /* audit 24: how to close Blur (the title combo uses the PS5 Create bit, so the PS4 text names the game menu) */
    BL_S_PATCH_DOWNLOADING,
    BL_S_ONLINE_UNPACK_MEMORY,
    BL_S_PATCH_NO_SPACE,
    BL_S_PATCH_FILE_FAILED,
    BL_S_PATCH_STILL_WRONG,
    BL_S_PATCH_IMPORT_FAILED,
    BL_S_HELP_0_HEAD,
    BL_S_HELP_0_DATA,
    BL_S_HELP_0_LEGAL,       /* the last line of "Before you start" (owner finding 32) */
    BL_S_ONLINE_ZIP_NEWER,
    BL_S_HINT_START_AGAIN,
    BL_S_MIGRATE_FAILED,     /* the title: the old root could not move (%s old root, %s System root) */
    BL_S_TOO_MANY_SETTINGS,  /* the title: a launcher env line did not fit, Blur not started */
    BL_S_HINT_CLOSE_NOTICE,  /* under those two */
    BL_S_SET_FRAMERATE,      /* Settings > Frame rate (owner 2026-11-16) */
    BL_S_PANEL_FRAMERATE,
    BL_S_BAND_FRAMERATE,
    BL_S_ROW_FRAMERATE,
    BL_S_OPT_FPS30,
    BL_S_OPT_FPS60,
    BL_S_NOTE_FPS30,
    BL_S_NOTE_FPS60,
    BL_S_FRAMERATE_UNTESTED, /* not the tested Blur.exe: 30 FPS only */
    BL_S_GAME_NOT_FOUND,     /* audit 5: Start band with no game; {square} is the Help key cap */
    BL_S_HINT_HELP_FOLDER,   /* audit 24: Square on the offline Patch choice */
    BL_S_DL_NO_CONNECTION,   /* audit 8: download results; {square} {cross} are key caps */
    BL_S_DL_INTERRUPTED,
    BL_S_DL_SERVER_ERROR,
    BL_S_DL_MISMATCH,
    BL_S_CLOCK_TLS,          /* audit 9: %s = BL_S_CLOCK_GOLDHEN; {square} is the Help key cap */
    BL_S_CLOCK_GOLDHEN,      /* the GoldHEN phrase alone (wording confirmed before freeze) */
    BL_S_PATCH_NO_SPACE_GB,  /* audit 16: online Patch below its room + 1 GB; %d = GB to free, rounded up */
    BL_S_PATCH_TOO_MANY,     /* audit 21: %s = the folder, %d = BL_PATCH_MAX_PARK */
    BL_S_PATCH_CONNECTING,   /* audit 17: before the first byte of a download */
    BL_S_STATUS_PAD_ONE,     /* audit 19: the singular of BL_S_STATUS_PADS, BL_S_LOGS_CLEARED, BL_S_LOG_SUMMARY */
    BL_S_LOGS_CLEARED_ONE,
    BL_S_LOG_SUMMARY_ONE,
    BL_S_PATCH_NO_SPACE_BEFORE, /* audit 22: the space check before any change */
    BL_S_PATCH_UNREADABLE,   /* audit 22: %s = the file */
    BL_S_PLAY_FILES_BROKEN,  /* audit 22: Play, the profile could not be made */
    BL_S_LOG_SUMMARY_KB,     /* owner finding 40: under 1 MB, %d files, %lld KB (rounded up) */
    BL_S_LOG_SUMMARY_KB_ONE,
    BL_S_PATCH_DONE_ASK,     /* owner finding 38: the one-button dialog after a successful Patch */
    BL_S_PATCH_DONE_ASK_ONLINE,
    BL_S_DIALOG_BACK_TO_START,
    BL_S_COUNT
} BlStringId;

const char *bl_str(BlStringId id);
/* one when n == 1, else many (audit 19: "1 controller", "2 controllers"). */
const char *bl_str_n(BlStringId one, BlStringId many, long long n);
/* The text of BL_S_GAME_SEVERAL (bl_frontend formats it with the two names). */
extern char bl_str_game_several[600];

#endif
