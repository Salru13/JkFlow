/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * Optional save import (BLUR 1.00 plan Task L6, decision D20). A player may
 * drop a Blur save into <root>/import/blursave over FTP. Patch applies it
 * once; it is never applied silently over an existing save:
 *   no current save  -> applied (READY)
 *   current save     -> CONFLICT: the player chooses Keep (default) or Use
 *                       import; Use import backs the current save up first
 *                       to <backup>/<NNNNNN>-<stamp>/save/blursave
 * Afterwards the import file is renamed (blursave.imported-<stamp> or
 * blursave.kept-<stamp>), so it is never applied twice. Only blursave is
 * touched; settings.xml never.
 * The current save is <saves>/save/blursave (BlPaths.saves).
 */
#ifndef BL_IMPORT_H
#define BL_IMPORT_H

#include <stddef.h>

#include "bl_paths.h"

#define BL_IMPORT_FILE "blursave"
#define BL_SAVE_MAGIC 0xABCDE337u

typedef enum BlImportState { BL_IMPORT_NONE = 0, BL_IMPORT_READY, BL_IMPORT_CONFLICT, BL_IMPORT_INVALID } BlImportState;

/* <import>/blursave. Valid = at least 12 bytes, u32 LE magic 0xABCDE337 at offset 0. */
BlImportState bl_import_check(const BlPaths *p);
/* replace: 0 keep the current save, 1 use the import (READY applies either
 * way). backup_out gets the backup folder when one was made, else "".
 * 0 ok, 1 nothing to import, <0 error (invalid file, I/O). */
int bl_import_apply(const BlPaths *p, int replace, char *backup_out, size_t cap);

#endif
