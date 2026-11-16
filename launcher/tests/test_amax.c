/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests for the AMAX client source and the zip reader (BLUR 1.00 plan
 * Task L4). Zips are built at run time by python3's zipfile from known bytes
 * (never the real AMAX zip). */
#define _POSIX_C_SOURCE 200809L
#include "bl_amax.h"
#include "bl_events.h"
#include "bl_fs.h"
#include "bl_inflate.h"
#include "bl_sha256.h"
#include "bl_strings.h"
#include "bl_test.h"
#include "bl_zip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[256], files[400], fx[400];
static BlPaths P;

static void sh(const char *cmd) { if (system(cmd) != 0) { fprintf(stderr, "  command failed: %s\n", cmd); } }

static void put(const char *dir, const char *name, const char *text)
{
    char p[900]; snprintf(p, sizeof p, "%s/%s", dir, name);
    char d[900]; snprintf(d, sizeof d, "%s", p);
    char *s = strrchr(d, '/'); if (s) { *s = 0; bl_fs_mkdirs(d); }
    FILE *f = fopen(p, "wb"); fputs(text, f); fclose(f);
}

static int has_text(const char *dir, const char *name, const char *text)
{
    char p[900], h[65], w[65];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    if (bl_fs_sha256(p, h, NULL) != 0) return 0;
    bl_sha256_hex(text, strlen(text), w);
    return !strcmp(h, w);
}

/* python3 builds zips: each member is "name=text" with an optional method
 * prefix "S:" (stored) or "D:" (deflated, the default). */
static void make_zip(const char *zip, const char *spec)
{
    char cmd[4096];
    snprintf(cmd, sizeof cmd,
             "python3 -I -c 'import sys,zipfile\n"
             "z=zipfile.ZipFile(sys.argv[1],\"w\")\n"
             "for m in sys.argv[2].split(\"|\"):\n"
             "  meth=zipfile.ZIP_DEFLATED\n"
             "  if m.startswith(\"S:\"): meth=zipfile.ZIP_STORED; m=m[2:]\n"
             "  elif m.startswith(\"D:\"): m=m[2:]\n"
             "  n,t=(m.split(\"=\",1)+[\"\"])[:2]\n"
             "  if t.startswith(\"@big\"): t=\"\".join(\"line %%d of a long and repetitive text for the dynamic huffman blocks\\n\" %% i for i in range(int(t[4:])))\n"
             "  if t.startswith(\"@zeros\"): t=\"\\0\"*int(t[6:])\n"
             "  z.writestr(zipfile.ZipInfo(n) if n.endswith(\"/\") else n, t, compress_type=meth) if not n.endswith(\"/\") else z.writestr(zipfile.ZipInfo(n), \"\")\n"
             "z.close()' '%s' '%s'", zip, spec);
    sh(cmd);
}

static void fresh(void)
{
    char cmd[700];
    if (root[0]) { snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); sh(cmd); }
    snprintf(root, sizeof root, "/tmp/bl-amax-XXXXXX");
    if (!mkdtemp(root)) exit(1);
    bl_paths_init(&P, root);
    bl_paths_make(&P);
    snprintf(files, sizeof files, "%s/blur-files", root);
    snprintf(fx, sizeof fx, "%s/fx", root);
    bl_fs_mkdirs(files); bl_fs_mkdirs(fx);
}

static void *tiny_malloc(size_t n) { return n > 16 * 1024 ? NULL : malloc(n); }
static int g_get_calls;
static int fake_get(void *ctx, const char *url, const char *dest, long long max_bytes,
                    void (*prog)(void *, long long, long long), void *pctx);

static void test_inflate_raw(void)
{
    /* zlib.compress level 9 of a repetitive text, minus the 2-byte header and
     * 4-byte trailer: a raw deflate stream (python3 makes it). */
    fresh();
    char cmd[1200], raw[600];
    snprintf(raw, sizeof raw, "%s/raw.bin", fx);
    snprintf(cmd, sizeof cmd, "python3 -I -c 'import zlib,sys; d=(\"abcabcabd\"*500).encode(); open(sys.argv[1],\"wb\").write(zlib.compress(d,9)[2:-4])' '%s'", raw);
    sh(cmd);
    static unsigned char in[8192], out[8192];
    FILE *f = fopen(raw, "rb");
    size_t n = f ? fread(in, 1, sizeof in, f) : 0;
    if (f) fclose(f);
    size_t got = 0;
    CHECK_EQ(bl_inflate(in, n, out, sizeof out, &got), 0);
    CHECK_EQ(got, 4500);
    CHECK(got == 4500 && !memcmp(out, "abcabcabdabcabcabd", 18) && !memcmp(out + 4491, "abcabcabd", 9));
    /* output larger than the room given: refused, never written past it */
    CHECK(bl_inflate(in, n, out, 4499, &got) < 0);
    /* garbage and a truncated stream are refused */
    CHECK(bl_inflate(in, n / 2, out, sizeof out, &got) < 0);
    unsigned char junk[16] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    CHECK(bl_inflate(junk, sizeof junk, out, sizeof out, &got) < 0);
    CHECK_EQ(bl_crc32((const unsigned char *)"123456789", 9), 0xCBF43926u);
}

static void test_zip_stored_and_deflate(void)
{
    fresh();
    char zip[600], dest[600];
    snprintf(zip, sizeof zip, "%s/mini.zip", fx);
    snprintf(dest, sizeof dest, "%s/out", root);
    make_zip(zip, "S:stored.txt=kept as is|D:amax/dlls/a.asi=deflated text deflated text deflated text|amax/log/|big.txt=@big2000|S:empty.bin=");
    CHECK_EQ(bl_zip_extract(zip, dest, 10LL * 1024 * 1024), 0);
    CHECK(has_text(dest, "stored.txt", "kept as is"));
    CHECK(has_text(dest, "amax/dlls/a.asi", "deflated text deflated text deflated text"));
    char p[800]; struct stat st;
    snprintf(p, sizeof p, "%s/amax/log", dest);
    CHECK(stat(p, &st) == 0 && S_ISDIR(st.st_mode));
    snprintf(p, sizeof p, "%s/big.txt", dest);
    CHECK(stat(p, &st) == 0 && st.st_size > 100000);
    char want[80], got[65];
    snprintf(p, sizeof p, "python3 -I -c 'import hashlib; print(hashlib.sha256(\"\".join(\"line %%d of a long and repetitive text for the dynamic huffman blocks\\n\" %% i for i in range(2000)).encode()).hexdigest())' > '%s/big.sha'", fx);
    sh(p);
    snprintf(p, sizeof p, "%s/big.sha", fx);
    long n = bl_fs_read(p, want, sizeof want);
    CHECK(n >= 64);
    want[64] = 0;
    snprintf(p, sizeof p, "%s/big.txt", dest);
    CHECK_EQ(bl_fs_sha256(p, got, NULL), 0);
    CHECK(!strcmp(want, got));
    snprintf(p, sizeof p, "%s/empty.bin", dest);
    CHECK(stat(p, &st) == 0 && st.st_size == 0);
    /* not a zip */
    put(fx, "not.zip", "PK but not really a zip file");
    snprintf(p, sizeof p, "%s/not.zip", fx);
    CHECK(bl_zip_extract(p, dest, 1 << 20) < 0);
}

static void test_zip_refuses_traversal(void)
{
    const char *bad[] = { "../evil.txt=x", "a/../../evil.txt=x", "/abs.txt=x", "c:/win.txt=x", "a\\\\b.txt=x" };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        fresh();
        char zip[600], dest[600], spec[200];
        snprintf(zip, sizeof zip, "%s/bad.zip", fx);
        snprintf(dest, sizeof dest, "%s/out", root);
        snprintf(spec, sizeof spec, "ok.txt=fine|%s", bad[i]);
        make_zip(zip, spec);
        CHECK(bl_zip_extract(zip, dest, 1 << 20) < 0);
        char p[700]; snprintf(p, sizeof p, "%s/evil.txt", root);
        CHECK(!bl_fs_exists(p));
        CHECK(!has_text(dest, "ok.txt", "fine"));          /* nothing is written from a bad zip */
    }
}

static void test_zip_bomb_limit(void)
{
    fresh();
    char zip[600], dest[600];
    snprintf(zip, sizeof zip, "%s/bomb.zip", fx);
    snprintf(dest, sizeof dest, "%s/out", root);
    make_zip(zip, "zeros.bin=@zeros5000000");
    struct stat st;
    CHECK(stat(zip, &st) == 0 && st.st_size < 100000);    /* small on disk */
    CHECK(bl_zip_extract(zip, dest, 1024 * 1024) < 0);    /* 5 MB inflated is over the limit */
    char p[700]; snprintf(p, sizeof p, "%s/zeros.bin", dest);
    CHECK(!bl_fs_exists(p));
    CHECK_EQ(bl_zip_extract(zip, dest, 8LL * 1024 * 1024), 0);
}

/* Fake zips: an AMAX-like set and a blur_12-like set, and a manifest with
 * their amax and blur12 lines (never the real zips). */
static const char *M_PROXY = "proxy d3d9 bytes", *M_AUTH = "amax_auth.asi bytes", *M_LUA = "plugins list";
static const char *M_EXE = "MZ Blur.exe 1.2", *M_PAK = "gamedata.pak 1.2", *M_SIG = "gamedata.sig 1.2";
static char amax_sha[65], blur_sha[65], amax_zip[700], blur_zip[700];
static long long amax_size, blur_size;

static void zip_pin(const char *zip, char sha[65], long long *size)
{
    uint64_t s = 0;
    bl_fs_sha256(zip, sha, &s);
    *size = (long long)s;
}

/* Both zips built in fx (not placed); bad_member: the zip's own hash is
 * pinned but one member is not what the manifest says. */
static void online_fixture(int bad_member)
{
    char h1[65], h2[65], h3[65], h4[65], h5[65], h6[65], man[2000], spec[400];
    snprintf(amax_zip, sizeof amax_zip, "%s/amax_client_files.zip", fx);
    snprintf(blur_zip, sizeof blur_zip, "%s/blur_12.zip", fx);
    snprintf(spec, sizeof spec, "d3d9.dll=%s|amax/|amax/dlls/amax_auth.asi=%s|S:amax/plugins/plugins.lua=%s|amax/log/.gitkeep=..",
             M_PROXY, M_AUTH, M_LUA);
    make_zip(amax_zip, spec);
    snprintf(spec, sizeof spec, "Blur.exe=%s|cache/|cache/gamedata.pak=%s|S:cache/gamedata.sig=%s",
             bad_member ? "MZ another exe" : M_EXE, M_PAK, M_SIG);
    make_zip(blur_zip, spec);
    zip_pin(amax_zip, amax_sha, &amax_size);
    zip_pin(blur_zip, blur_sha, &blur_size);
    bl_amax_zip_sha = amax_sha; bl_amax_zip_size = amax_size;
    bl_blur12_zip_sha = blur_sha; bl_blur12_zip_size = blur_size;
    bl_sha256_hex(M_PROXY, strlen(M_PROXY), h1);
    bl_sha256_hex(M_AUTH, strlen(M_AUTH), h2);
    bl_sha256_hex(M_LUA, strlen(M_LUA), h3);
    bl_sha256_hex(M_EXE, strlen(M_EXE), h4);
    bl_sha256_hex(M_PAK, strlen(M_PAK), h5);
    bl_sha256_hex(M_SIG, strlen(M_SIG), h6);
    snprintf(man, sizeof man, "# m\nfile d3d9_blur_dxvk.dll %064d 2\namax d3d9.dll %s %zu\namax amax/dlls/amax_auth.asi %s %zu\n"
             "amax amax/plugins/plugins.lua %s %zu\nblur12 Blur.exe %s %zu\nblur12 cache/gamedata.pak %s %zu\nblur12 cache/gamedata.sig %s %zu\n",
             0, h1, strlen(M_PROXY), h2, strlen(M_AUTH), h3, strlen(M_LUA), h4, strlen(M_EXE), h5, strlen(M_PAK), h6, strlen(M_SIG));
    put(files, "manifest.txt", man);
}

static char jk[300];                                   /* the zip folder: the parent of the game folder */
static void place(const char *zip, const char *as)
{
    char cmd[2000];
    bl_fs_mkdirs(jk);
    snprintf(cmd, sizeof cmd, "cp '%s' '%s/%s'", zip, jk, as);
    sh(cmd);
}

static void fresh_online(int bad_member)
{
    fresh();
    snprintf(jk, sizeof jk, "%s/JkFlow", root);
    bl_fs_mkdirs(jk);
    online_fixture(bad_member);
}

static int staged(void)
{
    char p[700]; snprintf(p, sizeof p, "%s/%s", P.state, BL_ONLINE_STAGE);
    return bl_fs_exists(p);
}

static void test_urls_and_pins(void)
{
    CHECK(!strcmp(BL_AMAX_ZIP_URL, "https://cs.amax-emu.com/amax_client_files.zip"));
    CHECK(!strcmp(BL_BLUR12_ZIP_URL, "https://cs.amax-emu.com/blur_12.zip"));
    CHECK(!strcmp(BL_BLUR12_ZIP_SHA, "bfebfba6d5f459bfa6cb64278f874cc49c76663685815520e534a51bdd398d9a"));
    CHECK_EQ(BL_BLUR12_ZIP_SIZE, 27496856LL);
    CHECK(!strcmp(BL_AMAX_ZIP_SHA, "4ee93bc7676e4f9f03d2bd657300c47594fad5265fc15c2cd911f190567425c7"));
    CHECK_EQ(BL_AMAX_ZIP_SIZE, 3300467LL);
}

/* No zip placed and no internet: the player needs the instructions. */
static void test_no_zips_no_internet(void)
{
    fresh_online(0);
    char stage[256], err[300];
    BlOnlineSource src = BL_ONLINE_SRC_PLACED;
    CHECK_EQ(bl_online_prepare(&P, files, jk, NULL, NULL, 1, NULL, NULL, stage, &src, err, sizeof err), BL_ONLINE_NO_ZIPS);
    CHECK_EQ(src, BL_ONLINE_SRC_NONE);
    CHECK(!staged());
    g_get_calls = 0;
    CHECK_EQ(bl_online_prepare(&P, files, jk, fake_get, NULL, 0, NULL, NULL, stage, &src, err, sizeof err), BL_ONLINE_NO_ZIPS);
    CHECK_EQ(g_get_calls, 0);                          /* no network: no download tried */
    CHECK(!strncmp(err, "No network. Nothing was changed. Connect the PS4 to the internet", 64));   /* owner finding 31 */
    CHECK(strstr(err, jk) != NULL);
}

/* Both placed, names in any letter case: extracted and checked. */
static void test_both_placed(void)
{
    fresh_online(0);
    place(amax_zip, "AMAX_Client_Files.ZIP");
    place(blur_zip, "Blur_12.zip");
    char stage[256], err[300];
    BlOnlineSource src;
    CHECK_EQ(bl_online_prepare(&P, files, jk, NULL, NULL, 0, NULL, NULL, stage, &src, err, sizeof err), 0);
    CHECK_EQ(src, BL_ONLINE_SRC_PLACED);
    CHECK(has_text(stage, "d3d9.dll", M_PROXY));
    CHECK(has_text(stage, "amax/dlls/amax_auth.asi", M_AUTH));
    CHECK(has_text(stage, "Blur.exe", M_EXE));
    CHECK(has_text(stage, "cache/gamedata.sig", M_SIG));
    CHECK(strstr(stage, "/state/online-stage") != NULL);
    /* the placed zips stay where the player put them */
    char p[900]; snprintf(p, sizeof p, "%s/Blur_12.zip", jk);
    CHECK(bl_fs_exists(p));
    bl_online_unstage(&P);
    CHECK(!staged());
}

/* Only one zip: the message names the one that is there and the missing one. */
static void test_one_zip(void)
{
    fresh_online(0);
    place(amax_zip, "amax_client_files.zip");
    char stage[256], err[300], want[1000];
    BlOnlineSource src;
    CHECK_EQ(bl_online_prepare(&P, files, jk, NULL, NULL, 1, NULL, NULL, stage, &src, err, sizeof err), -1);
    snprintf(want, sizeof want, "Only amax_client_files.zip is in %s. Nothing was changed. Put blur_12.zip there too, then press Patch for online play.", jk);
    CHECK(!strcmp(err, want));
    if (strcmp(err, want)) fprintf(stderr, "  got: %s\n", err);
    CHECK(!staged());
    fresh_online(0);
    place(blur_zip, "blur_12.zip");
    CHECK_EQ(bl_online_prepare(&P, files, jk, NULL, NULL, 1, NULL, NULL, stage, &src, err, sizeof err), -1);
    CHECK(strstr(err, "Only blur_12.zip is in ") && strstr(err, "Put amax_client_files.zip there too"));
}

/* A damaged or wrong placed zip, no download possible: named, with the next
 * action; nothing staged; the player's file is left alone. */
static void test_bad_zip(void)
{
    fresh_online(0);
    place(amax_zip, "amax_client_files.zip");
    put(jk, "blur_12.zip", "PK not the blur_12 zip");
    char stage[256], err[300], want[1000];
    BlOnlineSource src;
    CHECK_EQ(bl_online_prepare(&P, files, jk, NULL, NULL, 1, NULL, NULL, stage, &src, err, sizeof err), -1);
    snprintf(want, sizeof want, "blur_12.zip in %s is damaged. Nothing was changed. Download it again from amax-emu.com, then press Patch again.", jk);
    CHECK(!strcmp(err, want));
    if (strcmp(err, want)) fprintf(stderr, "  got: %s\n", err);
    CHECK(!staged());
    CHECK(has_text(jk, "blur_12.zip", "PK not the blur_12 zip"));
    /* a cut download (the zip's end is missing): damaged too */
    fresh_online(0);
    place(amax_zip, "amax_client_files.zip");
    char cut[2000];
    snprintf(cut, sizeof cut, "head -c %lld '%s' > '%s/blur_12.zip'", blur_size / 2, blur_zip, jk);
    sh(cut);
    CHECK_EQ(bl_online_prepare(&P, files, jk, NULL, NULL, 1, NULL, NULL, stage, &src, err, sizeof err), -1);
    CHECK(strstr(err, "blur_12.zip in ") && strstr(err, "is damaged. Nothing was changed. Download it again"));
    CHECK(!staged());
}

/* The zip's hash is pinned but a member is not what the package expects. */
static void test_member_mismatch(void)
{
    fresh_online(1);
    place(amax_zip, "amax_client_files.zip");
    place(blur_zip, "blur_12.zip");
    char stage[256], err[300];
    BlOnlineSource src;
    CHECK_EQ(bl_online_prepare(&P, files, jk, NULL, NULL, 1, NULL, NULL, stage, &src, err, sizeof err), -1);
    CHECK(!strcmp(err, "Blur.exe from blur_12.zip is not the expected file. Nothing was changed. Download the zip again from amax-emu.com, then press Patch again."));
    if (!strstr(err, "Blur.exe from blur_12.zip")) fprintf(stderr, "  got: %s\n", err);
    CHECK(!staged());
}

/* Out of memory while unpacking is its own error, not "damaged". */
static void test_unpack_out_of_memory(void)
{
    fresh_online(0);
    place(amax_zip, "amax_client_files.zip");
    place(blur_zip, "blur_12.zip");
    char stage[256], err[300];
    BlOnlineSource src;
    bl_zip_malloc = tiny_malloc;
    int r = bl_online_prepare(&P, files, jk, NULL, NULL, 1, NULL, NULL, stage, &src, err, sizeof err);
    bl_zip_malloc = malloc;
    CHECK_EQ(r, -1);
    CHECK(strstr(err, "Not enough memory to unpack amax_client_files.zip. Nothing was changed. Restart the PS4, then press Patch for online play again.") != NULL);
    if (!strstr(err, "Not enough memory")) fprintf(stderr, "  got: %s\n", err);
    CHECK(!staged());
}

/* The title's service (fake, result codes as blur_host_https_get, 003f937):
 * it writes <dest>.part and renames it to dest only on success.
 *   0 the right zip, 1 -1 FAILED (nothing written), 2 another file (complete:
 *   the launcher's hash check catches it), 3 too big: -4 SERVER_ERROR (.part
 *   removed by the service), 4 a larger file it did not stop (the size check),
 *   10 -2 NO_CONNECTION, 11 -3 INTERRUPTED, 13 -6 CERT_ERROR (.part kept by
 *   the service in all three), 14 a resumed download: the first progress is
 *   the kept .part's size, then the right zip. */
static int g_get_fail, g_prog_calls;
static long long g_first_done = -1, g_resume_at = 1000;
static int fake_get(void *ctx, const char *url, const char *dest, long long max_bytes,
                    void (*prog)(void *, long long, long long), void *pctx)
{
    (void)ctx;
    g_get_calls++;
    const char *from = NULL, *name = NULL;
    if (!strcmp(url, BL_AMAX_ZIP_URL)) { from = amax_zip; name = "amax_client_files.zip"; CHECK_EQ(max_bytes, bl_amax_zip_size + BL_ONLINE_DL_MARGIN); }
    else if (!strcmp(url, BL_BLUR12_ZIP_URL)) { from = blur_zip; name = "blur_12.zip"; CHECK_EQ(max_bytes, bl_blur12_zip_size + BL_ONLINE_DL_MARGIN); }
    CHECK(from != NULL);
    char low[800]; snprintf(low, sizeof low, "%s", dest);
    for (char *c = low; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
    CHECK(name && strstr(dest, jk) == dest && strstr(low, name));           /* the player's zip folder, the final name (any case) */
    CHECK(strstr(dest, ".part") == NULL);
    char part[800]; snprintf(part, sizeof part, "%s.part", dest);
    if (prog) { prog(pctx, g_get_fail == 14 ? g_resume_at : 0, max_bytes); g_prog_calls++; }
    if (!from || g_get_fail == 1) return -1;
    char cmd[2500];
    if (g_get_fail >= 10 && g_get_fail <= 13) {
        snprintf(cmd, sizeof cmd, "head -c 1000 '%s' > '%s'", from, part);
        sh(cmd);
        return g_get_fail == 10 ? -2 : g_get_fail == 11 ? -3 : g_get_fail == 12 ? -4 : -6;
    }
    if (g_get_fail == 2) snprintf(cmd, sizeof cmd, "printf 'PK another file' > '%s'", part);
    else if (g_get_fail == 3 || g_get_fail == 4) snprintf(cmd, sizeof cmd, "head -c %lld /dev/zero > '%s'", max_bytes + (g_get_fail == 3 ? 1 : -8), part);
    else snprintf(cmd, sizeof cmd, "cp '%s' '%s'", from, part);
    sh(cmd);
    if (g_get_fail == 3) { unlink(part); return -4; }
    if (rename(part, dest) != 0) return -1;
    if (prog) prog(pctx, max_bytes, max_bytes);
    return 0;
}

static long long g_last_done;
static void count_prog(void *ctx, long long done, long long total)
{
    (void)ctx;
    if (g_first_done < 0 && total > 0) g_first_done = done;   /* not the "connecting" signal (0, 0), audit 17 */
    g_last_done = done;
}

static int leftovers(void)
{
    char p[800]; int n = 0;
    const char *names[] = { "amax_client_files.zip.part", "blur_12.zip.part" };
    for (int i = 0; i < 2; i++) { snprintf(p, sizeof p, "%s/%s", jk, names[i]); n += bl_fs_exists(p); }
    return n;
}

static void test_download(void)
{
    fresh_online(0);
    place(amax_zip, "amax_client_files.zip");          /* one placed, one downloaded */
    char stage[256], err[300];
    BlOnlineSource src;
    g_get_calls = 0; g_get_fail = 0; g_prog_calls = 0;
    CHECK_EQ(bl_online_prepare(&P, files, jk, fake_get, NULL, 1, count_prog, NULL, stage, &src, err, sizeof err), 0);
    CHECK_EQ(g_get_calls, 1);
    CHECK_EQ(g_prog_calls, 1);
    CHECK_EQ(g_last_done, bl_blur12_zip_size + BL_ONLINE_DL_MARGIN);   /* progress reached the caller */
    CHECK_EQ(src, BL_ONLINE_SRC_MIXED);
    CHECK(has_text(stage, "Blur.exe", M_EXE));
    /* the download is kept in the zip folder under its name: the next Patch reuses it */
    char kept[800]; snprintf(kept, sizeof kept, "%s/blur_12.zip", jk);
    char h[65] = ""; bl_fs_sha256(kept, h, NULL);
    CHECK(!strcmp(h, blur_sha));
    g_get_calls = 0;
    bl_online_unstage(&P);
    CHECK_EQ(bl_online_prepare(&P, files, jk, fake_get, NULL, 1, NULL, NULL, stage, &src, err, sizeof err), 0);
    CHECK_EQ(g_get_calls, 0);
    CHECK_EQ(src, BL_ONLINE_SRC_PLACED);
    fresh_online(0);
    g_get_calls = 0;
    CHECK_EQ(bl_online_prepare(&P, files, jk, fake_get, NULL, 1, NULL, NULL, stage, &src, err, sizeof err), 0);
    CHECK_EQ(g_get_calls, 2);
    CHECK_EQ(src, BL_ONLINE_SRC_DOWNLOAD);
    /* a failed download (-1): named, with the next action; nothing left behind */
    fresh_online(0);
    g_get_fail = 1;
    CHECK_EQ(bl_online_prepare(&P, files, jk, fake_get, NULL, 1, NULL, NULL, stage, &src, err, sizeof err), BL_ONLINE_DOWNLOAD_ERR);
    char want[1000];
    snprintf(want, sizeof want, "Download of amax_client_files.zip failed. Nothing was changed. Press Patch again, or put the zips in %s.", jk);
    CHECK(!strcmp(err, want));
    if (strcmp(err, want)) fprintf(stderr, "  got: %s\n", err);
    CHECK_EQ(leftovers(), 0);
    CHECK(!staged());
    /* the wrong file, too much (stopped by the service: a server error), too
     * much (not stopped): a wrong file never keeps the final name, no .part */
    for (int mode = 2; mode <= 4; mode++) {
        fresh_online(0);
        g_get_fail = mode;
        CHECK_EQ(bl_online_prepare(&P, files, jk, fake_get, NULL, 1, NULL, NULL, stage, &src, err, sizeof err), BL_ONLINE_DOWNLOAD_ERR);
        if (mode == 3) CHECK(!strcmp(err, "The AMAX server did not answer. Nothing was changed. Try again later, or place the zip files by hand: press {square} Help."));
        else CHECK(!strcmp(err, "This zip is not the version JkFlow knows. Nothing was changed. Place the files from Help ({square})."));
        if (mode != 3 && strcmp(err, "This zip is not the version JkFlow knows. Nothing was changed. Place the files from Help ({square}).")) fprintf(stderr, "  got: %s\n", err);
        snprintf(kept, sizeof kept, "%s/amax_client_files.zip", jk);
        CHECK(!bl_fs_exists(kept));
        CHECK_EQ(leftovers(), 0);
        CHECK(!staged());
    }
    g_get_fail = 0;
}

/* Audit 8 (owner 2026-11-16): no connection, an interrupted download and a
 * certificate failure keep the .part (the next Patch resumes it); each
 * result has its own text with the next step. */
static void test_download_results_keep_part(void)
{
    char stage[256], err[400];
    BlOnlineSource src;
    const struct { int mode; const char *text; } cases[] = {
        { 10, "No connection. Nothing was changed. Check the PS4 network, or place the two zip files by hand: press {square} Help." },
        { 11, "Download interrupted (slow or lost connection). Nothing was changed. Press {cross} to continue where it stopped, or place the zip files by hand: press {square} Help." },
        { 13, NULL },                                   /* -6: the clock text (audit 9) */
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        fresh_online(0);
        g_get_fail = cases[i].mode;
        CHECK_EQ(bl_online_prepare(&P, files, jk, fake_get, NULL, 1, NULL, NULL, stage, &src, err, sizeof err), BL_ONLINE_DOWNLOAD_ERR);
        if (cases[i].text) CHECK(!strcmp(err, cases[i].text));
        if (cases[i].text && strcmp(err, cases[i].text)) fprintf(stderr, "  got: %s\n", err);
        CHECK(err[0] != 0);
        CHECK_EQ(leftovers(), 1);                       /* amax_client_files.zip.part kept */
        CHECK(!staged());
        /* the next Patch start keeps it too (the service resumes it) */
        bl_online_cleanup(&P, jk);
        CHECK_EQ(leftovers(), 1);
    }
    /* A resumed download: the progress starts at the kept part, not at 0. */
    fresh_online(0);
    g_get_fail = 14; g_first_done = -1;
    CHECK_EQ(bl_online_prepare(&P, files, jk, fake_get, NULL, 1, count_prog, NULL, stage, &src, err, sizeof err), 0);
    CHECK_EQ(g_first_done, g_resume_at);
    /* A finished (resumed) download with the wrong hash: the file is deleted,
     * no .part stays, the version text. */
    fresh_online(0);
    put(jk, "amax_client_files.zip.part", "PK an old half");
    g_get_fail = 2;
    CHECK_EQ(bl_online_prepare(&P, files, jk, fake_get, NULL, 1, NULL, NULL, stage, &src, err, sizeof err), BL_ONLINE_DOWNLOAD_ERR);
    char p[800]; snprintf(p, sizeof p, "%s/amax_client_files.zip", jk);
    CHECK(!bl_fs_exists(p));
    CHECK_EQ(leftovers(), 0);
    CHECK(!strcmp(err, "This zip is not the version JkFlow knows. Nothing was changed. Place the files from Help ({square})."));
    g_get_fail = 0;
}

/* A damaged placed zip is replaced by a download (spec order: placed, then download). */
static void test_bad_placed_then_download(void)
{
    fresh_online(0);
    place(amax_zip, "amax_client_files.zip");
    put(jk, "Blur_12.ZIP", "PK a damaged copy");
    char stage[256], err[300];
    BlOnlineSource src;
    g_get_calls = 0; g_get_fail = 0;
    CHECK_EQ(bl_online_prepare(&P, files, jk, fake_get, NULL, 1, NULL, NULL, stage, &src, err, sizeof err), 0);
    CHECK_EQ(g_get_calls, 1);
    char h[65] = "", p[800]; snprintf(p, sizeof p, "%s/Blur_12.ZIP", jk);
    bl_fs_sha256(p, h, NULL);
    CHECK(!strcmp(h, blur_sha));                         /* the same file, now right */
    /* the download fails too: the damaged zip is named */
    fresh_online(0);
    place(amax_zip, "amax_client_files.zip");
    put(jk, "blur_12.zip", "PK a damaged copy");
    g_get_fail = 1;
    CHECK_EQ(bl_online_prepare(&P, files, jk, fake_get, NULL, 1, NULL, NULL, stage, &src, err, sizeof err), BL_ONLINE_DOWNLOAD_ERR);
    g_get_fail = 0;
    CHECK(strstr(err, "Download of blur_12.zip failed") != NULL);
}

/* Owner finding 22 (2026-11-15): a whole zip with the right name but a hash
 * this version does not know is a newer AMAX release, not a damaged file:
 * downloading it again can never help. Nothing is staged; the player's zip
 * stays, a downloaded one goes. */
static void test_newer_zip(void)
{
    const char *want_fmt = "This %s is not the version JkFlow knows. Nothing was changed. Press {square} Help to see which files online play needs.";
    char stage[256], err[300], want[400];
    BlOnlineSource src;
    /* placed, same size, other content */
    fresh_online(0);
    place(amax_zip, "amax_client_files.zip");
    place(blur_zip, "blur_12.zip");
    bl_blur12_zip_sha = "0000000000000000000000000000000000000000000000000000000000000000";
    CHECK_EQ(bl_online_prepare(&P, files, jk, NULL, NULL, 1, NULL, NULL, stage, &src, err, sizeof err), -1);
    snprintf(want, sizeof want, want_fmt, "blur_12.zip");
    CHECK(!strcmp(err, want));
    if (strcmp(err, want)) fprintf(stderr, "  got: %s\n", err);
    CHECK(!staged());
    char p[800]; snprintf(p, sizeof p, "%s/blur_12.zip", jk);
    CHECK(bl_fs_exists(p));
    /* placed, another size (a newer AMAX set) */
    fresh_online(0);
    place(amax_zip, "amax_client_files.zip");
    place(blur_zip, "blur_12.zip");
    bl_amax_zip_size = amax_size + 100;
    CHECK_EQ(bl_online_prepare(&P, files, jk, NULL, NULL, 1, NULL, NULL, stage, &src, err, sizeof err), -1);
    snprintf(want, sizeof want, want_fmt, "amax_client_files.zip");
    CHECK(!strcmp(err, want));
    if (strcmp(err, want)) fprintf(stderr, "  got: %s\n", err);
    /* downloaded: the server now has a newer zip */
    fresh_online(0);
    place(amax_zip, "amax_client_files.zip");
    bl_blur12_zip_sha = "0000000000000000000000000000000000000000000000000000000000000000";
    g_get_calls = 0; g_get_fail = 0;
    CHECK_EQ(bl_online_prepare(&P, files, jk, fake_get, NULL, 1, NULL, NULL, stage, &src, err, sizeof err), BL_ONLINE_DOWNLOAD_ERR);
    CHECK_EQ(g_get_calls, 1);
    snprintf(want, sizeof want, "%s", "This zip is not the version JkFlow knows. Nothing was changed. Place the files from Help ({square}).");   /* audit 8/18 */
    CHECK(!strcmp(err, want));
    if (strcmp(err, want)) fprintf(stderr, "  got: %s\n", err);
    CHECK(!bl_fs_exists(p));
    CHECK_EQ(leftovers(), 0);
    CHECK(!staged());
}

/* Every Patch starts clean: the stage goes; our two .part names stay (the
 * service resumes them, audit 8), as do the zips and the player's files. */
static void test_cleanup(void)
{
    fresh_online(0);
    place(amax_zip, "amax_client_files.zip");
    put(jk, "blur_12.zip.part", "half a download");
    put(jk, "amax_client_files.zip.part", "half a download");
    put(jk, "notes.txt.part", "the player's own file");
    char stage[700]; snprintf(stage, sizeof stage, "%s/%s/x.zpart", P.state, BL_ONLINE_STAGE);
    put(P.state, BL_ONLINE_STAGE "/x.zpart", "left from a power cut");
    bl_online_cleanup(&P, jk);
    CHECK_EQ(leftovers(), 2);
    CHECK(!staged());
    char p[800]; snprintf(p, sizeof p, "%s/notes.txt.part", jk);
    CHECK(bl_fs_exists(p));
    snprintf(p, sizeof p, "%s/amax_client_files.zip", jk);
    CHECK(bl_fs_exists(p));
}

int main(void)
{
    RUN(test_inflate_raw);
    RUN(test_zip_stored_and_deflate);
    RUN(test_zip_refuses_traversal);
    RUN(test_zip_bomb_limit);
    RUN(test_urls_and_pins);
    RUN(test_no_zips_no_internet);
    RUN(test_both_placed);
    RUN(test_one_zip);
    RUN(test_bad_zip);
    RUN(test_member_mismatch);
    RUN(test_newer_zip);
    RUN(test_unpack_out_of_memory);
    RUN(test_download);
    RUN(test_download_results_keep_part);
    RUN(test_bad_placed_then_download);
    RUN(test_cleanup);
    char cmd[400]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); sh(cmd);
    return bl_test_report("test_amax");
}
