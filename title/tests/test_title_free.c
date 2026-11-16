/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host test for wine64_main.c blur_host_free_bytes (audit finding 16), cut
 * verbatim by run_title_tests.sh into free_cut.inc, against a fake kernel
 * fstatfs: free = f_bavail * f_bsize of a FreeBSD struct statfs, and
 * unknown (<0) whenever the answer cannot be trusted. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { failures++; fprintf(stderr, "  FAIL %s:%d %s\n", __FILE__, __LINE__, what); } } while (0)

/* The FreeBSD 9 struct statfs as the console's kernel writes it. */
struct fbsd_statfs {
    uint32_t f_version, f_type;
    uint64_t f_flags, f_bsize, f_iosize, f_blocks, f_bfree;
    int64_t f_bavail;
    uint64_t f_files;
    int64_t f_ffree;
    uint64_t f_syncwrites, f_asyncwrites, f_syncreads, f_asyncreads, f_spare[10];
    uint32_t f_namemax, f_owner;
    uint64_t f_fsid;
    char f_charspare[80], f_fstypename[16], f_mntfromname[88], f_mntonname[88];
};
static struct fbsd_statfs answer;
static int fstatfs_rc, fstatfs_calls, last_fd = -1;
int _fstatfs(int fd, void *buf)
{
    fstatfs_calls++;
    last_fd = fd;
    if (fstatfs_rc) return fstatfs_rc;
    memcpy(buf, &answer, sizeof(answer));
    return 0;
}

#include "free_cut.inc"

static void fill(uint32_t version, uint64_t bsize, int64_t bavail)
{
    memset(&answer, 0, sizeof answer);
    answer.f_version = version;
    answer.f_bsize = bsize;
    answer.f_bavail = bavail;
    answer.f_bfree = (uint64_t)bavail + 100;     /* the superuser's reserve is not counted */
    answer.f_blocks = (uint64_t)bavail * 4;
    fstatfs_rc = 0;
}

int main(void)
{
    CHECK(sizeof(struct fbsd_statfs) == 472, "the FreeBSD 9 layout is 472 bytes");
    fill(0x20030518u, 32768, 1000000);
    CHECK(blur_host_free_bytes(NULL, "/tmp") == 32768LL * 1000000, "free = f_bavail * f_bsize");
    CHECK(fstatfs_calls == 1 && last_fd >= 0, "asked the kernel about the path's volume");
    fill(0x20030518u, 4096, 0);
    CHECK(blur_host_free_bytes(NULL, "/tmp") == 0, "a full disk is 0, not unknown");
    fill(0x20030518u, 4096, 1);
    fstatfs_rc = -1;
    CHECK(blur_host_free_bytes(NULL, "/tmp") < 0, "fstatfs failed: unknown");
    fill(0x12345678u, 4096, 1000);
    CHECK(blur_host_free_bytes(NULL, "/tmp") < 0, "another layout: unknown");
    fill(0x20030518u, 0, 1000);
    CHECK(blur_host_free_bytes(NULL, "/tmp") < 0, "no block size: unknown");
    fill(0x20030518u, 4096, -5);
    CHECK(blur_host_free_bytes(NULL, "/tmp") < 0, "negative: unknown");
    fill(0x20030518u, 1u << 20, INT64_MAX / 2);
    CHECK(blur_host_free_bytes(NULL, "/tmp") < 0, "overflow: unknown");
    fill(0x20030518u, 4096, 1000);
    int before = fstatfs_calls;
    CHECK(blur_host_free_bytes(NULL, "/nonexistent-f16/x") < 0 && fstatfs_calls == before, "path cannot be opened: unknown");
    CHECK(blur_host_free_bytes(NULL, NULL) < 0, "no path: unknown");
    int probe = open("/tmp", O_RDONLY);
    CHECK(probe >= 0 && probe == last_fd, "the descriptor is closed after the call");
    if (probe >= 0) close(probe);
    fprintf(stderr, "test_title_free: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
