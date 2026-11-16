/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host test (BLUR 1.00 plan Task T1): the launcher's real profile (composed by
 * bl_profile_compose from the package's model-base.ini) parses with the
 * title's own profile parser once the title adds its prefix line, and the
 * built-in fallback does too. direct_profile_with_prefix and
 * direct_profile_text are cut from the patched wine64_main.c by
 * run_title_tests.sh (title_profile_cut.inc). Also counts the title's
 * extra[] use for this start against PW_WINE_START_MAX_ENV, and (audit
 * finding 7) runs the title's own blur_apply_decision (env_cut.inc, with its
 * capacity line) on the worst case of every launcher combination: logs
 * on/off x tested/untested exe x frame rate 30/60 x stats on/off, plus ten
 * more launcher lines; all start, nothing refused. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bl_config.h"
#include "bl_frontend.h"
#include "bl_profile.h"
#include "pw_game_profile.h"
#include "pw_wine_start.h"

#include "title_profile_cut.inc"

/* wine64_main.c's blur_apply_decision and its capacity line, cut verbatim. */
static char logbuf[8192];
#define PS5LOG_LOG(...) do { size_t _n = strlen(logbuf); snprintf(logbuf + _n, sizeof logbuf - _n, __VA_ARGS__); \
    _n = strlen(logbuf); snprintf(logbuf + _n, sizeof logbuf - _n, "\n"); } while (0)
static BlDecision blur_decision;
static int blur_logs_verbose;
#include "env_cap_cut.inc"
#include "env_cut.inc"

static int failures, checks;
#define CHECK(cond, what) do { checks++; if (cond) printf("PASS %s\n", what); \
    else { failures++; printf("FAIL %s (line %d)\n", what, __LINE__); } } while (0)

static int env_has(const BlDecision *d, const char *name, const char *value)
{
    for (unsigned i = 0; i < d->env_count; i++)
        if (!strcmp(d->env[i].name, name)) return !value || !strcmp(d->env[i].value, value);
    return 0;
}

/* The title's extra[] for this start (wine64_main.c main()): 7 fixed lines,
 * desktop, DLL overrides, graphics env, SDL hint, runtime env, 2 clock lines,
 * CPU env, [debug] env, then the launcher's env (WINEDEBUG goes to extra[0],
 * names already present are replaced), then NETCFG and the display name. */
static unsigned extra_count(const PwGameProfile *g, const BlDecision *d)
{
    PwGameEnv env[8];
    const char *names[64];
    unsigned n = 0;
    static const char *const fixed[] = { "WINEDEBUG", "WINEARCH", "HOME", "USER", "WINE_PS5_TRACE_STARTUP",
                                         "WINE_PS5_VIEW", "MESA_SHADER_CACHE_DIR", "WINE_PS5_DESKTOP",
                                         "WINEDLLOVERRIDES" };
    for (unsigned i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) names[n++] = fixed[i];
    size_t k = pw_game_graphics_env(g, env);
    for (size_t i = 0; i < k; i++) names[n++] = env[i].name;
    names[n++] = "SDL_JOYSTICK_RAWINPUT";
    k = pw_game_runtime_env(&g->runtime, env);
    for (size_t i = 0; i < k; i++) names[n++] = env[i].name;
    names[n++] = "PW_QPC_TSC_VALIDATED";
    names[n++] = "PW_QPC_TSC_HZ";
    k = pw_game_cpu_env(g, env);
    for (size_t i = 0; i < k; i++) names[n++] = env[i].name;
    k = pw_game_debug_env(g, env);
    for (size_t i = 0; i < k; i++) names[n++] = env[i].name;
    for (unsigned i = 0; i < d->env_count; i++) {
        unsigned j;
        if (!strcmp(d->env[i].name, "WINEDEBUG")) continue;
        for (j = 1; j < n; j++) if (!strcmp(names[j], d->env[i].name)) break;
        if (j == n) names[n++] = d->env[i].name;
    }
    names[n++] = "WINE_PS4_NETCFG";
    names[n++] = "BLUR_DISPLAY_NAME";
    return n;
}

/* The title's extra[] before the launcher's lines, worst case: every line
 * extra_count names up to the launcher, with WINEDEBUG in extra[0]. */
static uint32_t title_lines(const PwGameProfile *g, PwWineStartEnv *extra)
{
    static const BlDecision none;
    unsigned n = extra_count(g, &none) - 2;          /* not NETCFG and the display name */
    PwGameEnv env[8];
    const char *names[64];
    unsigned k = 0;
    static const char *const fixed[] = { "WINEDEBUG", "WINEARCH", "HOME", "USER", "WINE_PS5_TRACE_STARTUP",
                                         "WINE_PS5_VIEW", "MESA_SHADER_CACHE_DIR", "WINE_PS5_DESKTOP",
                                         "WINEDLLOVERRIDES" };
    for (unsigned i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) names[k++] = fixed[i];
    size_t m = pw_game_graphics_env(g, env);
    static char keep[64][48];
    unsigned kept = 0;
    for (size_t i = 0; i < m; i++) { snprintf(keep[kept], 48, "%s", env[i].name); names[k++] = keep[kept++]; }
    names[k++] = "SDL_JOYSTICK_RAWINPUT";
    m = pw_game_runtime_env(&g->runtime, env);
    for (size_t i = 0; i < m; i++) { snprintf(keep[kept], 48, "%s", env[i].name); names[k++] = keep[kept++]; }
    names[k++] = "PW_QPC_TSC_VALIDATED";
    names[k++] = "PW_QPC_TSC_HZ";
    m = pw_game_cpu_env(g, env);
    for (size_t i = 0; i < m; i++) { snprintf(keep[kept], 48, "%s", env[i].name); names[k++] = keep[kept++]; }
    m = pw_game_debug_env(g, env);
    for (size_t i = 0; i < m; i++) { snprintf(keep[kept], 48, "%s", env[i].name); names[k++] = keep[kept++]; }
    CHECK(k == n, "title lines counted the same way");
    for (unsigned i = 0; i < k; i++) extra[i] = (PwWineStartEnv){ names[i], "t" };
    return k;
}

/* Audit finding 7: every launcher combination, worst case, through the
 * title's own fail-closed merge. Returns the largest extra[] count seen
 * (launcher merged, before NETCFG and the display name). */
static unsigned combinations(const char *files)
{
    static BlDecision d;
    static char text[sizeof(d.profile_text) + 32];
    static PwWineStartEnv extra[BLUR_ENV_CAPACITY + BLUR_ENV_AFTER];
    PwGameProfile g;
    BlConfig c;
    unsigned worst = 0, worst_launcher = 0;
    char what[160];

    for (int logging = 0; logging <= 1; logging++)
    for (int untested = 0; untested <= 1; untested++)
    for (int fps60 = 0; fps60 <= 1; fps60++)
    for (int stats = 0; stats <= 1; stats++) {
        bl_config_defaults(&c);
        c.logging = logging;
        c.stats = stats ? BL_STATS_FPS : BL_STATS_OFF;
        memset(&d, 0, sizeof(d));
        snprintf(what, sizeof what, "logs=%d exe=%s fps=%d stats=%d", logging, untested ? "untested" : "tested",
                 fps60 ? 60 : 30, stats);
        CHECK(bl_profile_compose(&c, -1, files, &d) == 0, what);
        /* bl_frontend.c compose(): the race lock line, force on an untested exe. */
        const char *lock = untested ? "force" : fps60 ? "60" : NULL;
        if (lock) CHECK(bl_decision_setenv(&d, "DXVK_PS4_RACE_LOCK", lock) == 0, what);
        CHECK(direct_profile_with_prefix(d.profile_text, text, sizeof(text)) == 0 &&
              pw_game_profile_parse((const uint8_t *)text, strlen(text), &g) == PW_OK, what);
        memset(extra, 0, sizeof extra);
        uint32_t count = title_lines(&g, extra);
        blur_decision = d;
        logbuf[0] = 0;
        CHECK(blur_apply_decision(extra, &count, BLUR_ENV_CAPACITY) == 0, what);
        CHECK(strstr(logbuf, "(no room)") == NULL, what);
        CHECK(count + 2 <= BLUR_ENV_CAPACITY + BLUR_ENV_AFTER, what);   /* NETCFG and the display name after it */
        unsigned base = count;
        if (count > worst) worst = count;
        if (d.env_count > worst_launcher) worst_launcher = d.env_count;

        /* Ten more launcher lines on top of this combination, as far as the
         * launcher's hand-off (BlDecision.env) holds them. */
        unsigned slots = sizeof(d.env) / sizeof(d.env[0]), more = 0;
        for (unsigned i = 0; i < 10 && d.env_count < slots; i++, more++) {
            char name[32];
            snprintf(name, sizeof name, "BLUR_HEADROOM_%u", i);
            CHECK(bl_decision_setenv(&d, name, "1") == 0, what);
        }
        memset(extra, 0, sizeof extra);
        count = title_lines(&g, extra);
        blur_decision = d;
        logbuf[0] = 0;
        CHECK(blur_apply_decision(extra, &count, BLUR_ENV_CAPACITY) == 0, "ten more launcher lines still start");
        CHECK(strstr(logbuf, "(no room)") == NULL, "ten more launcher lines: nothing refused");
        printf("info %s launcher=%u extra=%u (+%u more: %u) of %u\n", what, d.env_count - more, base,
               more, (unsigned)count, (unsigned)BLUR_ENV_CAPACITY);
    }
    printf("info worst case: launcher env=%u, title extra[]=%u of %u (headroom %u); +NETCFG +display name = %u of %u\n",
           worst_launcher, worst, (unsigned)BLUR_ENV_CAPACITY, (unsigned)BLUR_ENV_CAPACITY - worst, worst + 2,
           (unsigned)PW_WINE_START_MAX_ENV);
    CHECK(BLUR_ENV_CAPACITY - worst >= 10, "at least ten lines of headroom over the worst case");
    return worst;
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: %s BLUR_FILES_DIR\n", argv[0]); return 2; }
    static BlDecision d;
    static char text[sizeof(d.profile_text) + 32];
    PwGameProfile g;
    BlConfig c;

    for (int logging = 0; logging <= 1; logging++) {
        bl_config_defaults(&c);
        c.logging = logging;
        memset(&d, 0, sizeof(d));
        CHECK(bl_profile_compose(&c, -1, argv[1], &d) == 0, "launcher composes the profile");
        CHECK(pw_game_profile_parse((const uint8_t *)d.profile_text, strlen(d.profile_text), &g) != PW_OK,
              "launcher profile alone lacks the prefix line (title must add it)");
        CHECK(direct_profile_with_prefix(d.profile_text, text, sizeof(text)) == 0, "prefix line added");
        CHECK(pw_game_profile_parse((const uint8_t *)text, strlen(text), &g) == PW_OK, "title parses the launcher profile");
        CHECK(!strcmp(g.app.prefix, "default"), "prefix is default (<root>/prefix)");
        CHECK(!strcmp(g.app.executable, "Z:\\data\\JkFlow\\Blur\\Blur.exe"), "executable in /data/JkFlow/Blur");
        CHECK(g.runtime.fast_clock, "fast_clock on");
        CHECK(pw_game_cpu_native(&g), "native CPU");
        CHECK(!strcmp(g.winedebug, logging ? "+tid,err+all,+loaddll" : "-all"), "winedebug from the launcher");
        CHECK(env_has(&d, "DXVK_PS4_NATIVE", "menu") && env_has(&d, "DXVK_PS4_NATIVE_ONLY", "1") &&
              env_has(&d, "DXVK_PS4_NET_CACHE", "3") && env_has(&d, "PW_SERVER_PRIO", "699") &&
              env_has(&d, "PW_NATIVE_ALERT_UMTX", "1"), "BLUR engine env present");
        CHECK(!env_has(&d, "DXVK_PS4_DIAG", NULL), "no DXVK_PS4_DIAG");
        /* BLUR_LOG_DIR may be left out with logging off (finding 19 L5); when set it is the Logs root. */
        CHECK((!env_has(&d, "BLUR_LOG_DIR", NULL) || env_has(&d, "BLUR_LOG_DIR", "/data/JkFlow/Logs")) &&
              env_has(&d, "DXVK_CONFIG_FILE", "/data/JkFlow/System/cache/dxvk.conf") &&
              env_has(&d, "DXVK_STATE_CACHE_PATH", "/data/JkFlow/System/cache"), "engine paths under /data/JkFlow");
        CHECK(!logging || env_has(&d, "BLUR_LOG_DIR", "/data/JkFlow/Logs"), "logging on: engine logs in /data/JkFlow/Logs");
        unsigned n = extra_count(&g, &d);
        printf("info logging=%d launcher env=%u extra=%u of %u\n", logging, d.env_count, n, (unsigned)PW_WINE_START_MAX_ENV);
        CHECK(n <= PW_WINE_START_MAX_ENV, "extra[] fits");
        /* blur_apply_decision fails closed past sizeof(extra) - 4 (review finding 3); the
         * two lines after it (NETCFG, display name) are not counted there. Both settings start. */
        CHECK(n - 2 <= PW_WINE_START_MAX_ENV - 4, "the launcher's env stays under the fail-closed limit");
        CHECK(!strcmp(d.dxvk_conf, "d3d9.maxFrameLatency = 1\nd3d9.maxFrameRate = 60\n"), "dxvk.conf text equals the 0.8.33 dxvk.conf");
    }
    (void)combinations(argv[1]);
    /* A profile that already names a prefix is left alone. */
    CHECK(direct_profile_with_prefix("[application]\nprefix = x\n", text, sizeof(text)) == 0 &&
          !strcmp(text, "[application]\nprefix = x\n"), "existing prefix line kept");
    CHECK(direct_profile_with_prefix(d.profile_text, text, 8) == -1, "too small a buffer refused");
    /* The built-in fallback parses too. */
    CHECK(pw_game_profile_parse((const uint8_t *)direct_profile_text, sizeof(direct_profile_text) - 1, &g) == PW_OK,
          "built-in profile parses");
    CHECK(g.debug_env_count == 4, "built-in profile carries the 4 engine env lines");
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
