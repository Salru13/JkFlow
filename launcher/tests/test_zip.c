/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests for the streaming DEFLATE decoder and zip reader (review fix
 * 2026-11-15: the console's game process cannot allocate more than about
 * 12 MB in one piece; blur_12.zip is 27.5 MB with members of 13.7 and 22.9 MB).
 *  - differential: the system zlib (python3 zlib) compresses many random and
 *    real-like inputs with every level and strategy; the decoder must give
 *    the same bytes with the input handed over in random chunks (1 byte to
 *    256 KB);
 *  - every allocation is capped at 4 MB while a >16 MB zip with stored,
 *    deflated and data-descriptor members of more than 4 MB each extracts;
 *  - damaged and truncated zips leave no file; out of memory is its own error. */
#define _POSIX_C_SOURCE 200809L
#include "bl_fs.h"
#include "bl_inflate.h"
#include "bl_test.h"
#include "bl_zip.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[256];

static void sh(const char *cmd) { if (system(cmd) != 0) fprintf(stderr, "  command failed: %s\n", cmd); }

static unsigned char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { *len = 0; return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n > 0 ? (size_t)n : 1);
    *len = b && fread(b, 1, (size_t)n, f) == (size_t)n ? (size_t)n : 0;
    fclose(f);
    return b;
}

/* ---- the allocation cap ---- */
static size_t g_cap = (size_t)-1, g_largest;
static int g_refused;
static void *capped(size_t n)
{
    if (n > g_largest) g_largest = n;
    if (n > g_cap) { g_refused++; return NULL; }
    return malloc(n);
}
static void cap(size_t n) { g_cap = n; g_largest = 0; g_refused = 0; bl_zip_malloc = capped; }
static void uncap(void) { g_cap = (size_t)-1; bl_zip_malloc = malloc; }

/* ---- random chunks in, compare out ---- */
typedef struct Feed { const unsigned char *p; size_t len, pos; unsigned seed; } Feed;
static size_t feed_in(void *ctx, const unsigned char **buf)
{
    Feed *f = ctx;
    static const size_t SIZES[] = { 1, 2, 3, 7, 64, 511, 4096, 32768, 65537, 262144 };
    f->seed = f->seed * 1103515245u + 12345u;
    size_t n = SIZES[(f->seed >> 16) % (sizeof SIZES / sizeof SIZES[0])];
    if (n > f->len - f->pos) n = f->len - f->pos;
    *buf = f->p + f->pos;
    f->pos += n;
    return n;
}
typedef struct Sink { const unsigned char *want; size_t want_len, got; int bad; } Sink;
static int sink_out(void *ctx, const unsigned char *buf, size_t len)
{
    Sink *s = ctx;
    if (s->got + len > s->want_len || memcmp(s->want + s->got, buf, len)) s->bad = 1;
    s->got += len;
    return 0;
}

static void test_inflate_differential(void)
{
    char cmd[3000];
    snprintf(cmd, sizeof cmd,
             "python3 -I -c 'import sys,zlib,random\n"
             "r=random.Random(20261115); d=sys.argv[1]; n=0\n"
             "text=b\"\".join(b\"line %%d of a race on Brighton, lap %%d, %%s\\n\" %% (i,i%%7,b\"boost\"*(i%%5)) for i in range(40000))\n"
             "def kinds(size):\n"
             "  yield bytes(r.getrandbits(8) for _ in range(min(size,200000)))*(1+size//200000)\n"
             "  yield (text*(1+size//len(text)))[:size]\n"
             "  yield bytes(size)\n"
             "  yield bytes((i*i>>3)&0xff for i in range(size))\n"
             "for size in (0,1,2,100,1000,32767,32768,32769,65535,65536,70000,300000):\n"
             "  for data in kinds(size):\n"
             "    data=data[:size]\n"
             "    for lvl,st in ((0,0),(1,0),(6,0),(9,0),(6,zlib.Z_FILTERED),(6,zlib.Z_HUFFMAN_ONLY),(6,zlib.Z_RLE),(6,zlib.Z_FIXED)):\n"
             "      if size>70000 and (lvl,st) not in ((0,0),(6,0),(9,0),(6,zlib.Z_FIXED)): continue\n"
             "      c=zlib.compressobj(lvl,zlib.DEFLATED,-15,9,st); z=c.compress(data)+c.flush()\n"
             "      open(\"%%s/c%%d.z\"%%(d,n),\"wb\").write(z); open(\"%%s/c%%d.out\"%%(d,n),\"wb\").write(data); n+=1\n"
             "big=(text*60)[:3000000]; c=zlib.compressobj(9,zlib.DEFLATED,-15,9,0)\n"
             "open(\"%%s/c%%d.z\"%%(d,n),\"wb\").write(c.compress(big)+c.flush()); open(\"%%s/c%%d.out\"%%(d,n),\"wb\").write(big); n+=1\n"
             "open(\"%%s/count\"%%d,\"w\").write(str(n))' '%s'", root);
    sh(cmd);
    char p[600], count[16] = "";
    snprintf(p, sizeof p, "%s/count", root);
    bl_fs_read(p, count, sizeof count);
    int n = atoi(count), passed = 0;
    CHECK(n > 300);
    cap(BL_INFLATE_WINDOW);                              /* the window is all the decoder needs */
    for (int i = 0; i < n; i++) {
        size_t zl, ol;
        snprintf(p, sizeof p, "%s/c%d.z", root, i);
        unsigned char *z = slurp(p, &zl);
        snprintf(p, sizeof p, "%s/c%d.out", root, i);
        unsigned char *o = slurp(p, &ol);
        for (unsigned seed = 1; seed <= 3; seed++) {
            Feed f = { z, zl, 0, seed * 7919u + (unsigned)i };
            Sink s = { o, ol, 0, 0 };
            unsigned long long got = 0;
            int r = bl_inflate_back(feed_in, &f, sink_out, &s, ol, &got);
            int ok = r == 0 && !s.bad && s.got == ol && got == ol;
            if (!ok) fprintf(stderr, "  case %d seed %u: r=%d bad=%d got=%zu want=%zu\n", i, seed, r, s.bad, s.got, ol);
            passed += ok;
            CHECK(ok);
        }
        /* a stream cut short is an error, never a silent short file */
        if (zl > 4) {
            Feed f = { z, zl / 2, 0, 5 };
            Sink s = { o, ol, 0, 0 };
            CHECK(bl_inflate_back(feed_in, &f, sink_out, &s, ol, NULL) != 0);
        }
        /* more output than allowed is refused */
        if (ol > 10) {
            Feed f = { z, zl, 0, 9 };
            Sink s = { o, ol, 0, 0 };
            CHECK_EQ(bl_inflate_back(feed_in, &f, sink_out, &s, ol - 1, NULL), BL_INFLATE_TOO_BIG);
        }
        free(z); free(o);
    }
    CHECK(g_largest <= BL_INFLATE_WINDOW);
    uncap();
    fprintf(stderr, "  differential: %d streams x 3 chunkings decoded like zlib\n", passed / 3);
}

static void test_inflate_out_of_memory(void)
{
    static const unsigned char z[] = { 0x03, 0x00 };   /* an empty fixed block */
    Feed f = { z, sizeof z, 0, 1 };
    Sink s = { (const unsigned char *)"", 0, 0, 0 };
    cap(1024);
    CHECK_EQ(bl_inflate_back(feed_in, &f, sink_out, &s, 10, NULL), BL_INFLATE_NOMEM);
    uncap();
    f.pos = 0;
    CHECK_EQ(bl_inflate_back(feed_in, &f, sink_out, &s, 10, NULL), 0);
}

/* Regular files under dir (any depth). */
static int files_in(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[1200]; struct stat st;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (lstat(p, &st) != 0) continue;
        n += S_ISDIR(st.st_mode) ? files_in(p) : 1;
    }
    closedir(d);
    return n;
}

/* A >16 MB zip written to a pipe-like (unseekable) file by python's zipfile,
 * so every member has a data descriptor: stored random 9 MB, deflated random
 * 8 MB, deflated text 6 MB, a small member and a folder. The sha256 of each
 * member goes to <zip>.sha. */
static void big_zip(const char *zip)
{
    char cmd[2500];
    snprintf(cmd, sizeof cmd,
             "python3 -I -c 'import sys,zipfile,random,hashlib\n"
             "class W:\n"
             "  def __init__(s,f): s.f=f; s.n=0\n"
             "  def write(s,b): s.f.write(b); s.n+=len(b); return len(b)\n"
             "  def tell(s): return s.n\n"
             "  def flush(s): s.f.flush()\n"
             "r=random.Random(7)\n"
             "m={\"stored.bin\":r.randbytes(9000000),\"cache/random.pak\":r.randbytes(8000000),\n"
             "   \"text.txt\":b\"\".join(b\"%%07d lap boost shunt barge\\n\"%%i for i in range(220000)),\"small.sig\":b\"sig\"}\n"
             "with open(sys.argv[1],\"wb\") as f:\n"
             "  z=zipfile.ZipFile(W(f),\"w\")\n"
             "  z.writestr(zipfile.ZipInfo(\"cache/\"),b\"\")\n"
             "  for k,v in m.items():\n"
             "    i=zipfile.ZipInfo(k); i.compress_type=zipfile.ZIP_STORED if k==\"stored.bin\" else zipfile.ZIP_DEFLATED\n"
             "    with z.open(i,\"w\",force_zip64=False) as o: o.write(v)\n"
             "  z.close()\n"
             "open(sys.argv[1]+\".sha\",\"w\").write(\"\".join(\"%%s %%s\\n\"%%(hashlib.sha256(v).hexdigest(),k) for k,v in m.items()))' '%s'", zip);
    sh(cmd);
}

static int same_sha(const char *dest, const char *shafile)
{
    char text[2000];
    if (bl_fs_read(shafile, text, sizeof text) < 0) return 0;
    int ok = 1, n = 0;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char sha[65], name[200], p[1200], got[65];
        if (sscanf(line, "%64s %199s", sha, name) != 2) continue;
        snprintf(p, sizeof p, "%s/%s", dest, name);
        if (bl_fs_sha256(p, got, NULL) != 0 || strcmp(got, sha)) { fprintf(stderr, "  member %s differs\n", name); ok = 0; }
        n++;
    }
    return ok && n == 4;
}

static void test_zip_streams_big_members(void)
{
    char zip[600], dest[600], sha[700];
    snprintf(zip, sizeof zip, "%s/big.zip", root);
    snprintf(sha, sizeof sha, "%s.sha", zip);
    snprintf(dest, sizeof dest, "%s/out", root);
    big_zip(zip);
    struct stat st;
    CHECK(stat(zip, &st) == 0 && st.st_size > 16 * 1024 * 1024);
    cap(4u * 1024 * 1024);
    CHECK_EQ(bl_zip_extract(zip, dest, 64LL * 1024 * 1024), 0);
    CHECK(g_largest <= 4u * 1024 * 1024);
    CHECK_EQ(g_refused, 0);
    uncap();
    CHECK(same_sha(dest, sha));
    char p[700]; snprintf(p, sizeof p, "%s/cache", dest);
    CHECK(stat(p, &st) == 0 && S_ISDIR(st.st_mode));
    CHECK_EQ(files_in(dest), 4);                        /* no .zpart left */
}

static void test_zip_damaged_leaves_nothing(void)
{
    char zip[600], bad[700], dest[600], cmd[2000];
    snprintf(zip, sizeof zip, "%s/big.zip", root);
    snprintf(dest, sizeof dest, "%s/damaged", root);
    /* a flipped byte in the middle of the deflated text */
    snprintf(bad, sizeof bad, "%s/flip.zip", root);
    snprintf(cmd, sizeof cmd, "python3 -I -c 'import sys; b=bytearray(open(sys.argv[1],\"rb\").read()); i=b.find(b\"text.txt\")+60000; b[i]^=0x55; open(sys.argv[2],\"wb\").write(b)' '%s' '%s'", zip, bad);
    sh(cmd);
    int r = bl_zip_extract(bad, dest, 64LL * 1024 * 1024);
    CHECK(r == BL_ZIP_DAMAGED);
    CHECK_EQ(files_in(dest), 0);
    /* cut short: the central directory is gone */
    snprintf(bad, sizeof bad, "%s/cut.zip", root);
    snprintf(cmd, sizeof cmd, "head -c 9000000 '%s' > '%s'", zip, bad);
    sh(cmd);
    CHECK_EQ(bl_zip_extract(bad, dest, 64LL * 1024 * 1024), BL_ZIP_DAMAGED);
    CHECK_EQ(files_in(dest), 0);
    /* cut inside the last member's data, central directory copied back on */
    snprintf(bad, sizeof bad, "%s/hole.zip", root);
    snprintf(cmd, sizeof cmd, "python3 -I -c 'import sys; b=open(sys.argv[1],\"rb\").read(); i=b.find(b\"PK\\x01\\x02\"); open(sys.argv[2],\"wb\").write(b[:i-300000]+b[i:])' '%s' '%s'", zip, bad);
    sh(cmd);
    CHECK(bl_zip_extract(bad, dest, 64LL * 1024 * 1024) < 0);
    CHECK_EQ(files_in(dest), 0);
}

static void test_zip_out_of_memory(void)
{
    char zip[600], dest[600];
    snprintf(zip, sizeof zip, "%s/big.zip", root);
    snprintf(dest, sizeof dest, "%s/oom", root);
    cap(16 * 1024);
    CHECK_EQ(bl_zip_extract(zip, dest, 64LL * 1024 * 1024), BL_ZIP_NOMEM);
    uncap();
    CHECK_EQ(files_in(dest), 0);
}

/* The checker sees each member before it gets its name; a refusal leaves nothing. */
static int g_checked;
static int refuse_text(void *ctx, const char *name, const char *path)
{
    (void)ctx;
    g_checked++;
    CHECK(strstr(path, ".zpart") != NULL);
    return strcmp(name, "text.txt") ? 0 : -1;
}

static void test_zip_checker(void)
{
    char zip[600], dest[600];
    snprintf(zip, sizeof zip, "%s/big.zip", root);
    snprintf(dest, sizeof dest, "%s/checked", root);
    g_checked = 0;
    CHECK_EQ(bl_zip_extract_checked(zip, dest, 64LL * 1024 * 1024, refuse_text, NULL), BL_ZIP_CHECK);
    CHECK(g_checked >= 1);
    CHECK_EQ(files_in(dest), 0);
}

int main(void)
{
    snprintf(root, sizeof root, "/tmp/bl-zip-XXXXXX");
    if (!mkdtemp(root)) return 1;
    RUN(test_inflate_differential);
    RUN(test_inflate_out_of_memory);
    RUN(test_zip_streams_big_members);
    RUN(test_zip_damaged_leaves_nothing);
    RUN(test_zip_out_of_memory);
    RUN(test_zip_checker);
    char cmd[400]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", root); sh(cmd);
    return bl_test_report("test_zip");
}
