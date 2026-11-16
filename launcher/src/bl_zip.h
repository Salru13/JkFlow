/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/*
 * A streaming zip reader for the player's two zips (amax_client_files.zip,
 * blur_12.zip). Stored and deflated members only; no zip64, no encryption;
 * data descriptors are fine (the central directory has the sizes).
 *
 * Nothing is read whole: the end record and the central directory are read
 * by seek, then each member's data in 256 KB pieces, decoded through a 32 KB
 * window (bl_inflate_back) straight into "<dest>/<name>.zpart", with its
 * CRC-32 and size checked on the way. Only when every member passed (and the
 * optional check callback accepted it) are the .zpart files renamed to their
 * names; on any failure every .zpart is deleted, so a bad zip writes nothing.
 * No allocation is above 4 MB (bl_zip_malloc).
 */
#ifndef BL_ZIP_H
#define BL_ZIP_H

#define BL_ZIP_MAX_FILE (64LL * 1024 * 1024)   /* zip files larger than this are refused */
#define BL_ZIP_MAX_MEMBERS 1024

enum {
    BL_ZIP_DAMAGED = -1,      /* unreadable, not a zip, a bad CRC/size, a cut stream */
    BL_ZIP_UNSAFE = -2,       /* a name with "..", a leading "/", a drive letter or a backslash */
    BL_ZIP_TOO_BIG = -3,      /* more than max_total bytes uncompressed */
    BL_ZIP_NOMEM = -4,        /* an allocation failed */
    BL_ZIP_WRITE = -5,        /* a file could not be written (full disk) */
    BL_ZIP_CHECK = -6,        /* the check callback refused a member */
};

/* Called with each member's name and its finished .zpart path before any
 * member is renamed into place: 0 accept, anything else refuse. */
typedef int (*BlZipCheck)(void *ctx, const char *name, const char *path);

/* Extract zip into dest_dir (created). 0 ok, or one of the errors above. */
int bl_zip_extract(const char *zip, const char *dest_dir, long long max_total);
int bl_zip_extract_checked(const char *zip, const char *dest_dir, long long max_total, BlZipCheck check, void *ctx);
/* 1: zip is a whole zip file (its end record and central directory read
 * and list at least one member we could unpack); 0: not a zip, cut short or
 * damaged. Reads only the end of the file (finding 22: a whole zip with an
 * unknown hash is a newer release, not a damaged download). */
int bl_zip_is_whole(const char *zip);

#endif
