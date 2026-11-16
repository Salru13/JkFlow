/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests for wine64_main.c's blur100 section (owner findings 4+5, 10,
 * 13), cut verbatim from the prepared tree by run_title_tests.sh into
 * r4_cut.inc:
 *   restart_title     after Blur exits: LoadExec back into the title's own
 *                     launcher; leave_to_home only when that fails
 *   blur_session_end  a clean end only after the first frame (E101 stays)
 *   blur_find_roots   /data/<JkFlow any case>/System and /Logs, found before
 *                     anything is made; the old /data/BLUR moved in once,
 *                     or, holding Blur.exe, renamed to JkFlow/BLUR (a game)
 *   blur_install_dl_report  ntdll's module loader reports refused PRX loads
 *                     (path, kernel handle and result) into the session log */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bl_paths.h"
#include "bl_strings.h"
#include "pw_wine_dl.h"
#include "pw_wine_launch.h"

static int failures, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { failures++; fprintf(stderr, "  FAIL %s:%d %s\n", __FILE__, __LINE__, what); } } while (0)

/* ---- the title's neighbours, faked ---- */
static char logbuf[16384];
#define PS5LOG_LOG(...) do { size_t _n = strlen(logbuf); snprintf(logbuf + _n, sizeof logbuf - _n, __VA_ARGS__); \
    _n = strlen(logbuf); snprintf(logbuf + _n, sizeof logbuf - _n, "\n"); } while (0)
#define PW_SANDBOX_APP0 "/mnt/sandbox/BLUR00001_000/app0"

static jmp_buf left;
static char left_reason[64], loadexec_paths[4][128];
static int loadexec_calls, loadexec_rc = -1, quiesced, diag_closed, diag_resumed, clean_ends, argv_ok = 1;
static void leave_to_home(const char *reason) { snprintf(left_reason, sizeof left_reason, "%s", reason); longjmp(left, 1); }
static void display_quiesce(const char *reason) { (void)reason; quiesced++; }
static void pw_diagnostics_close(const char *reason) { (void)reason; diag_closed++; }
static void pw_diagnostics_resume(void) { diag_resumed++; }
static void bl_session_end_clean(void) { clean_ends++; }
int sceSystemServiceLoadExec(const char *path, char *const argv[])
{
    (void)argv;
    if (loadexec_calls < 4) snprintf(loadexec_paths[loadexec_calls], 128, "%s", path);
    loadexec_calls++;
    return loadexec_rc;
}
size_t pw_wine_launch_argv(const PwWineApp *app, uint32_t cycle, char *storage, size_t capacity, char **argv, size_t count)
{
    (void)app; (void)cycle; (void)storage; (void)capacity;
    if (!argv_ok || !count) return 0;
    argv[0] = NULL;
    return 1;
}
static int blur_first_frame;            /* wine64_main.c: set by loading_done */
static int32_t load_handle = 0x1234, load_result;
static char loaded_path[256];
int32_t sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags, const void *option, int *result)
{
    (void)argc; (void)argv; (void)flags; (void)option;
    snprintf(loaded_path, sizeof loaded_path, "%s", path);
    if (result) *result = load_result;
    return load_handle;
}
int sceKernelGetModuleInfo(int32_t handle, void *info) { (void)info; return handle == 0x1234 ? 0 : -1; }
static int stopped;
int sceKernelStopUnloadModule(int32_t handle, size_t argc, const void *argv, uint32_t flags, const void *option, int *result)
{
    (void)handle; (void)argc; (void)argv; (void)flags; (void)option; (void)result;
    stopped++;
    return 0;
}

#include "r4_cut.inc"

static void reset(void)
{
    logbuf[0] = 0; left_reason[0] = 0;
    loadexec_calls = quiesced = diag_closed = diag_resumed = clean_ends = 0;
    loadexec_rc = -1; argv_ok = 1;
}

/* Finding 10: back to the launcher, never _exit. */
static void test_restart_goes_back_to_launcher(void)
{
    reset();
    if (!setjmp(left)) restart_title(NULL, 2, "guest-exit");
    CHECK(loadexec_calls == 2, "LoadExec tried on both views of the eboot");
    CHECK(!strcmp(loadexec_paths[0], "/app0/eboot.bin"), "own eboot first");
    CHECK(!strcmp(loadexec_paths[1], PW_SANDBOX_APP0 "/eboot.bin"), "then the sandbox's view");
    CHECK(!strcmp(left_reason, "guest-exit"), "only when both fail: leave to home");
    CHECK(quiesced >= 1 && diag_closed == 2 && diag_resumed == 2, "display drained, log closed before each LoadExec");
    CHECK(strstr(logbuf, "restart to=launcher") != NULL, "logged");

    reset();
    loadexec_rc = 0;                         /* LoadExec does not return on success; fake a return of 0 */
    if (!setjmp(left)) restart_title(NULL, 2, "wine-exit");
    CHECK(loadexec_calls >= 1, "LoadExec tried");

    reset();
    argv_ok = 0;
    if (!setjmp(left)) restart_title(NULL, 2, "start-failed");
    CHECK(loadexec_calls == 0 && !strcmp(left_reason, "start-failed"), "no arguments: leave to home, never return");
}

/* A clean end only after Blur showed its first frame: a start that dies
 * before it still reaches the launcher's failed-start screen (E101). */
static void test_session_end(void)
{
    reset();
    blur_first_frame = 0;
    blur_session_end();
    CHECK(clean_ends == 0, "no first frame: not clean (E101 shown)");
    blur_first_frame = 1;
    blur_session_end();
    CHECK(clean_ends == 1, "after the first frame: a normal Quit");
}

static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }

/* Findings 4+5: roots in the player's JkFlow spelling, migration first. */
static void test_find_roots(void)
{
    char data[] = "/tmp/f1-roots-XXXXXX", sys[256], logs[256], want[300], cmd[400];
    CHECK(mkdtemp(data) != NULL, "temp");
    reset();
    CHECK(blur_find_roots(data, sys, sizeof sys, logs, sizeof logs) == 0, "fresh console");
    snprintf(want, sizeof want, "%s/JkFlow/System", data);
    CHECK(!strcmp(sys, want) && is_dir(want), "System made");
    snprintf(want, sizeof want, "%s/JkFlow/Logs", data);
    CHECK(!strcmp(logs, want) && is_dir(want), "Logs made");
    BlPaths p;
    bl_paths_init(&p, NULL);
    CHECK(!strcmp(p.root, sys), "launcher default root follows the title");
    snprintf(cmd, sizeof cmd, "rm -rf '%s/JkFlow' && mkdir -p '%s/jkflow/blur' '%s/BLUR/prefix' '%s/BLUR/logs'", data, data, data, data);
    CHECK(system(cmd) == 0, "setup");
    reset();
    CHECK(blur_find_roots(data, sys, sizeof sys, logs, sizeof logs) == 0, "player's spelling");
    snprintf(want, sizeof want, "%s/jkflow/System", data);
    CHECK(!strcmp(sys, want), "System in the player's jkflow");
    snprintf(want, sizeof want, "%s/JkFlow", data);
    CHECK(!is_dir(want), "no second JkFlow folder");
    snprintf(want, sizeof want, "%s/jkflow/System/prefix", data);
    CHECK(is_dir(want), "old prefix moved");
    snprintf(want, sizeof want, "%s/jkflow/Logs", data);
    CHECK(is_dir(want), "old logs became Logs");
    snprintf(want, sizeof want, "%s/BLUR", data);
    CHECK(!is_dir(want), "old root removed");
    CHECK(strstr(logbuf, "migrate: moved") != NULL && strstr(logbuf, "data root=") != NULL, "logged");
    bl_paths_set_default_root(NULL);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", data);
    CHECK(system(cmd) == 0, "cleanup");
}

/* Review finding 1: a move that leaves old data in /data/BLUR stops the
 * title before any fresh System data is made; the retry after the player
 * fixed it moves the old data in, never beside fresh data. */
static void test_find_roots_blocked_then_retry(void)
{
    char data[] = "/tmp/f2-block-XXXXXX", sys[256], logs[256], want[600], cmd[400];
    CHECK(mkdtemp(data) != NULL, "temp");
    snprintf(cmd, sizeof cmd, "mkdir -p '%s/JkFlow' '%s/BLUR/prefix' '%s/BLUR/state' '%s/BLUR/logs' && "
             "echo reg > '%s/BLUR/prefix/system.reg' && echo ini > '%s/BLUR/state/launcher.ini' && "
             "echo file > '%s/JkFlow/System'", data, data, data, data, data, data, data);
    CHECK(system(cmd) == 0, "setup: System cannot be made (a file is in the way)");
    reset();
    blur_migrate_blocked = 0;
    bl_paths_set_default_root(NULL);
    CHECK(blur_find_roots(data, sys, sizeof sys, logs, sizeof logs) == 1, "blocked, not ok and not a plain failure");
    CHECK(blur_migrate_blocked == 1, "the title knows it must stop");
    snprintf(want, sizeof want, "Your old JkFlow data in %s/BLUR could not be moved to %s/JkFlow/System. "
             "Nothing was deleted. Free some space and start JkFlow again.", data, data);
    CHECK(!strcmp(blur_migrate_text, want), "the one sentence, with the two folders");
    BlPaths p;
    bl_paths_init(&p, NULL);
    CHECK(strcmp(p.root, sys) != 0, "the launcher's root is not switched to the new System");
    snprintf(want, sizeof want, "%s/BLUR/prefix/system.reg", data);
    CHECK(access(want, F_OK) == 0, "old prefix untouched");
    snprintf(want, sizeof want, "%s/BLUR/state/launcher.ini", data);
    CHECK(access(want, F_OK) == 0, "old state untouched");
    CHECK(strstr(logbuf, "blocked") != NULL, "logged");
    /* The player frees the way; the next start moves everything in. */
    snprintf(cmd, sizeof cmd, "rm -f '%s/JkFlow/System'", data);
    CHECK(system(cmd) == 0, "fix");
    reset();
    blur_migrate_blocked = 0;
    CHECK(blur_find_roots(data, sys, sizeof sys, logs, sizeof logs) == 0, "retry ok");
    CHECK(blur_migrate_blocked == 0, "not blocked");
    snprintf(want, sizeof want, "%s/JkFlow/System/prefix/system.reg", data);
    CHECK(access(want, F_OK) == 0, "old prefix now in System");
    snprintf(want, sizeof want, "%s/JkFlow/System/prefix.old-BLUR", data);
    CHECK(access(want, F_OK) != 0, "never parked beside fresh data");
    snprintf(want, sizeof want, "%s/BLUR", data);
    CHECK(!is_dir(want), "old root gone");
    bl_paths_set_default_root(NULL);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", data);
    CHECK(system(cmd) == 0, "cleanup");
}

/* Owner decision 2026-11-16: an old <data>/BLUR holding Blur.exe directly
 * (any letter case) is a game folder: it becomes <JkFlow>/BLUR in one rename
 * when no game is there (missing, any letter case, or empty), and stays put
 * when one is. Never merged into System. */
static void test_find_roots_game_folder(void)
{
    char data[] = "/tmp/f4-game-XXXXXX", sys[256], logs[256], want[600], line[600], cmd[800];
    CHECK(mkdtemp(data) != NULL, "temp");

    /* 1. No JkFlow yet: the whole folder moves, contents and all. */
    snprintf(cmd, sizeof cmd, "mkdir -p '%s/BLUR/data' && echo exe > '%s/BLUR/blur.EXE' && "
             "echo pak > '%s/BLUR/data/gamedata.pak'", data, data, data);
    CHECK(system(cmd) == 0, "setup: a game in /data/BLUR");
    reset();
    blur_migrate_blocked = 0;
    CHECK(blur_find_roots(data, sys, sizeof sys, logs, sizeof logs) == 0, "game folder: ok");
    CHECK(blur_migrate_blocked == 0, "game folder: not blocked");
    snprintf(want, sizeof want, "%s/JkFlow/BLUR/blur.EXE", data);
    CHECK(access(want, F_OK) == 0, "game is JkFlow/BLUR");
    snprintf(want, sizeof want, "%s/JkFlow/BLUR/data/gamedata.pak", data);
    CHECK(access(want, F_OK) == 0, "with its folders");
    snprintf(want, sizeof want, "%s/BLUR", data);
    CHECK(!is_dir(want), "old folder gone (one rename)");
    snprintf(want, sizeof want, "%s/JkFlow/System/blur.EXE", data);
    CHECK(access(want, F_OK) != 0, "nothing merged into System");
    snprintf(want, sizeof want, "%s/JkFlow/System", data);
    CHECK(!strcmp(sys, want) && is_dir(want), "System still made");
    snprintf(line, sizeof line, "PW_WINE64 migrate: %s/BLUR is a game folder -> JkFlow/BLUR\n", data);
    CHECK(strstr(logbuf, line) != NULL, "move logged");

    /* 2. An empty jkflow/blur (any letter case) is taken, not a second folder. */
    snprintf(cmd, sizeof cmd, "rm -rf '%s'/* && mkdir -p '%s/jkflow/blur' '%s/BLUR' && echo exe > '%s/BLUR/Blur.exe'",
             data, data, data, data);
    CHECK(system(cmd) == 0, "setup: empty jkflow/blur");
    reset();
    CHECK(blur_find_roots(data, sys, sizeof sys, logs, sizeof logs) == 0, "empty target: ok");
    snprintf(want, sizeof want, "%s/jkflow/blur/Blur.exe", data);
    CHECK(access(want, F_OK) == 0, "game moved into the empty blur folder");
    snprintf(want, sizeof want, "%s/jkflow/BLUR", data);
    CHECK(!is_dir(want), "no second BLUR folder");
    snprintf(want, sizeof want, "%s/BLUR", data);
    CHECK(!is_dir(want), "old folder gone");
    CHECK(strstr(logbuf, "is a game folder -> JkFlow/BLUR") != NULL, "move logged");

    /* 3. A game already in JkFlow/Blur: nothing moves, nothing is merged. */
    snprintf(cmd, sizeof cmd, "rm -rf '%s'/* && mkdir -p '%s/JkFlow/Blur' '%s/BLUR/prefix' && "
             "echo old > '%s/JkFlow/Blur/Blur.exe' && echo new > '%s/BLUR/BLUR.EXE'", data, data, data, data, data);
    CHECK(system(cmd) == 0, "setup: game already present");
    reset();
    blur_migrate_blocked = 0;
    CHECK(blur_find_roots(data, sys, sizeof sys, logs, sizeof logs) == 0, "game present: ok");
    CHECK(blur_migrate_blocked == 0, "game present: not blocked");
    snprintf(want, sizeof want, "%s/BLUR/BLUR.EXE", data);
    CHECK(access(want, F_OK) == 0, "old game folder kept");
    snprintf(want, sizeof want, "%s/BLUR/prefix", data);
    CHECK(is_dir(want), "its folders kept");
    snprintf(want, sizeof want, "%s/JkFlow/System/prefix", data);
    CHECK(!is_dir(want), "nothing merged into System");
    snprintf(want, sizeof want, "%s/JkFlow/Blur/Blur.exe", data);
    CHECK(access(want, F_OK) == 0, "present game untouched");
    snprintf(line, sizeof line, "PW_WINE64 migrate: %s/BLUR kept (game already present)\n", data);
    CHECK(strstr(logbuf, line) != NULL, "kept logged");
    CHECK(strstr(logbuf, "is a game folder") == NULL, "no move logged");

    /* 4. Blur.exe only deeper down: not a game folder, the System migration. */
    snprintf(cmd, sizeof cmd, "rm -rf '%s'/* && mkdir -p '%s/BLUR/games' && echo exe > '%s/BLUR/games/Blur.exe'",
             data, data, data);
    CHECK(system(cmd) == 0, "setup: Blur.exe not directly in BLUR");
    reset();
    CHECK(blur_find_roots(data, sys, sizeof sys, logs, sizeof logs) == 0, "data folder: ok");
    snprintf(want, sizeof want, "%s/JkFlow/System/games/Blur.exe", data);
    CHECK(access(want, F_OK) == 0, "moved into System as before");
    snprintf(want, sizeof want, "%s/JkFlow/BLUR", data);
    CHECK(!is_dir(want), "not taken for a game");
    CHECK(strstr(logbuf, "game folder") == NULL && strstr(logbuf, "migrate: moved") != NULL, "System migration logged");

    bl_paths_set_default_root(NULL);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", data);
    CHECK(system(cmd) == 0, "cleanup");
}

static PwWineDlOps installed;
static char installed_dir[256];
static int configured;
static void fake_configure(const PwWineDlOps *ops, const char *dir)
{
    installed = *ops;
    snprintf(installed_dir, sizeof installed_dir, "%s", dir ? dir : "");
    configured++;
}

/* Finding 13: a refused module load names path, handle and result. */
static void test_dl_report(void)
{
    reset();
    CHECK(blur_install_dl_report(NULL, NULL) == 0, "no configure export: nothing");
    CHECK(blur_install_dl_report(fake_configure, NULL) == 1 && configured == 1, "installed");
    CHECK(!strcmp(installed_dir, "/app0/sce_module"), "ntdll's own default module dir");
    CHECK(blur_install_dl_report(fake_configure, "/x/sce_module") == 1 && !strcmp(installed_dir, "/x/sce_module"), "WINE_PRX_DIR kept");
    CHECK(installed.load_start && installed.module_info && installed.stop_unload && installed.report, "all four ops");
    int result = 0;
    load_handle = 0x1234; load_result = 0;
    CHECK(installed.load_start("/a/ws2_32.prx", 0, NULL, 0, NULL, &result) == 0x1234, "a load passes through");
    CHECK(strstr(logbuf, "refused") == NULL, "a good load is not logged");
    load_handle = (int32_t)0x80020002; load_result = 7;
    CHECK(installed.load_start("/a/ws2_32.prx", 0, NULL, 0, NULL, &result) == (int32_t)0x80020002 && result == 7, "kernel answer returned");
    CHECK(strstr(logbuf, "module load refused path=/a/ws2_32.prx handle=0x80020002 result=0x00000007") != NULL, "kernel answer logged");
    CHECK(installed.module_info(0x1234, NULL) == 0, "module info forwarded");
    CHECK(installed.stop_unload(1, 0, NULL, 0, NULL, &result) == 0 && stopped == 1, "unload forwarded");
    installed.report("pw_wine_dl: cannot open libvulkan.so, tried /x/libvulkan.prx: not found");
    CHECK(strstr(logbuf, "PW_WINE64 pw_wine_dl: cannot open libvulkan.so") != NULL, "loader report in the session log");
}

int main(void)
{
    test_restart_goes_back_to_launcher();
    test_session_end();
    test_find_roots();
    test_find_roots_blocked_then_retry();
    test_find_roots_game_folder();
    test_dl_report();
    fprintf(stderr, "test_title_r4: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
