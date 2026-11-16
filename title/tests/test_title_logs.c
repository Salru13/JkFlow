/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests for the title's two log writers in the BLUR 1.00 tree:
 *   pw_diagnostics.c  session-N.log straight into the Logs folder
 *                     (pw_diagnostics_open_dir), and no ps5log network
 *                     reconnect thread (finding 19 T6)
 *   pw_wow_log_ps4.c  wowblur.log only in $BLUR_LOG_DIR, nothing (and no
 *                     folder made) without it */
#define _POSIX_C_SOURCE 200809L
#define PW_DIAGNOSTICS_TESTING 1
#include "pw_diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void pw_wow_log(const char *format, ...);

static int failures, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { failures++; fprintf(stderr, "  FAIL %s:%d %s\n", __FILE__, __LINE__, what); } } while (0)

/* ps5log, faked: the product never connects. */
static int reconnects;
int ps5log_line(const char *level, const char *text) { (void)level; (void)text; return 0; }
int ps5log_raw(const void *bytes, size_t length) { (void)bytes; (void)length; return 0; }
int ps5log_enabled(void) { return 0; }
int ps5log_reconnect(void) { reconnects++; return 1; }
void ps5log_close(const char *reason) { (void)reason; }
int sceKernelDebugOutText(int channel, const char *text) { (void)channel; (void)text; return 0; }

static int file_has(const char *path, const char *text)
{
    char buf[8192] = "";
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return strstr(buf, text) != NULL;
}

static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

static void test_session_log_in_logs_folder(char *base)
{
    char logs[300], path[400];
    snprintf(logs, sizeof logs, "%s/JkFlow/Logs", base);
    snprintf(path, sizeof path, "%s/JkFlow", base);
    mkdir(path, 0777);
    pw_diagnostics_test_retry_interval(1000000ull);       /* 1 ms: a retry thread would run at once */
    CHECK(pw_diagnostics_open_dir(logs, "BLUR-1.00", "blur", 1) == 0, "open");
    pw_diagnostics_log("PW_WINE64 hello from=%s", "test");
    pw_diagnostics_test_settle();
    struct timespec t = { 0, 100000000L };
    nanosleep(&t, NULL);
    pw_diagnostics_close("test");
    snprintf(path, sizeof path, "%s/session-0.log", logs);
    CHECK(file_has(path, "PW_WINE64 hello from=test"), "session-0.log directly in Logs");
    CHECK(file_has(path, "build=BLUR-1.00"), "identity line");
    snprintf(path, sizeof path, "%s/logs", logs);
    CHECK(!exists(path), "no logs/ inside Logs");
    snprintf(path, sizeof path, "%s/next.txt", logs);
    CHECK(file_has(path, "1"), "next session index");
    CHECK(reconnects == 0, "no network reconnect thread (T6)");
}

static void test_wow_log(char *base)
{
    char dir[300], path[400];
    snprintf(dir, sizeof dir, "%s/JkFlow/Logs", base);
    unsetenv("BLUR_LOG_DIR");
    pw_wow_log("no dir %d\n", 1);                         /* returns: no fixed fallback folder */
    setenv("BLUR_LOG_DIR", dir, 1);
    pw_wow_log("emutls n=%d\n", 7);
    snprintf(path, sizeof path, "%s/wowblur.log", dir);
    CHECK(file_has(path, "emutls n=7"), "wowblur.log in BLUR_LOG_DIR");
    char missing[400];
    snprintf(missing, sizeof missing, "%s/nope", base);
    setenv("BLUR_LOG_DIR", missing, 1);
    pw_wow_log("x\n");
    CHECK(!exists(missing), "a missing log dir is not created");
    unsetenv("BLUR_LOG_DIR");
}

int main(void)
{
    char base[] = "/tmp/f1-logs-XXXXXX";
    if (!mkdtemp(base)) return 1;
    test_session_log_in_logs_folder(base);
    test_wow_log(base);
    char cmd[400];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", base);
    if (system(cmd) != 0) return 1;
    fprintf(stderr, "test_title_logs: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
