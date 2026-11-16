/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host tests: settings -> game profile/env/dxvk.conf (plan Task 6), server
 * address checks and BLUR_HOSTS (plan Task 7, Review Focus 5), log clearing. */
#define _DEFAULT_SOURCE
#include "bl_config.h"
#include "bl_logs.h"
#include "bl_net.h"
#include "bl_paths.h"
#include "bl_profile.h"
#include "bl_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[256], files[300];

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb"); fputs(text, f); fclose(f);
}

static const char *env_of(const BlDecision *d, const char *name)
{
    for (unsigned i = 0; i < d->env_count; i++) if (!strcmp(d->env[i].name, name)) return d->env[i].value;
    return NULL;
}

static void setup(void)
{
    snprintf(root, sizeof root, "/tmp/bl_test_profile_XXXXXX");
    if (!mkdtemp(root)) exit(1);
    snprintf(files, sizeof files, "%s/files", root);
    mkdir(files, 0755);
    char p[600];
    /* The shipped presets. */
    snprintf(p, sizeof p, "%s/model-base.ini", files);
    FILE *in = fopen(BL_TEST_DIR "/../assets/blur-files/model-base.ini", "rb");
    FILE *out = fopen(p, "wb");
    int ch;
    while (in && (ch = fgetc(in)) != EOF) fputc(ch, out);
    if (in) fclose(in);
    fclose(out);
}

static void test_base_profile(void)
{
    BlConfig c; bl_config_defaults(&c);
    BlDecision d; memset(&d, 0, sizeof d);
    CHECK_EQ(bl_profile_compose(&c, 0, files, &d), 0);
    const char *want[] = { "executable = " BL_WINE_GAME_DIR "\\Blur.exe", "working_directory = " BL_WINE_GAME_DIR, "dll_overrides = d3d9=n",
                           "desktop = 1920x1080", "mode = xinput", "cpu = native", "[application]", "[display]" };
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        CHECK(strstr(d.profile_text, want[i]) != NULL);
        if (!strstr(d.profile_text, want[i])) fprintf(stderr, "  missing: %s\n", want[i]);
    }
    CHECK(strcmp(d.dxvk_conf, "d3d9.maxFrameLatency = 1\nd3d9.maxFrameRate = 60\n") == 0);
    CHECK(strstr(d.profile_text, "prefix =") == NULL);         /* the title sets the prefix (T1) */
    /* Logging off by default. */
    CHECK(env_of(&d, "WINEDEBUG") && !strcmp(env_of(&d, "WINEDEBUG"), "-all"));
    /* Audit 15: logging off writes no renderer log at all (not even an empty Blur_d3d9.log). */
    CHECK(env_of(&d, "DXVK_LOG_LEVEL") && !strcmp(env_of(&d, "DXVK_LOG_LEVEL"), "none"));
    CHECK(env_of(&d, "BLUR_LOGS") && !strcmp(env_of(&d, "BLUR_LOGS"), "0"));
    CHECK(!env_of(&d, "DXVK_PS4_FRAME_LOG") && !env_of(&d, "DXVK_PS4_MAIN_SAMPLE") && !env_of(&d, "DXVK_PS4_AUTOTEST"));
    CHECK(env_of(&d, "DXVK_PS4_FPS_HUD") && !strcmp(env_of(&d, "DXVK_PS4_FPS_HUD"), "0"));
    CHECK(strstr(d.profile_text, "winedebug = -all") != NULL);
}

static void test_logging_and_stats(void)
{
    BlConfig c; bl_config_defaults(&c);
    BlDecision d;
    c.logging = 1; c.stats = BL_STATS_FPS;
    CHECK_EQ(bl_profile_compose(&c, 0, files, &d), 0);
    CHECK(!strcmp(env_of(&d, "WINEDEBUG"), "+tid,err+all,+loaddll"));
    CHECK(env_of(&d, "DXVK_PS4_FRAME_LOG") && !strcmp(env_of(&d, "DXVK_PS4_FRAME_LOG"), "1"));
    CHECK(!strcmp(env_of(&d, "BLUR_LOGS"), "1"));
    CHECK(!strcmp(env_of(&d, "DXVK_PS4_FPS_HUD"), "fps"));
    CHECK(!env_of(&d, "DXVK_PS4_DIAG") && !env_of(&d, "DXVK_PS4_MEASURE"));   /* measurement is for the bench */
    c.stats = BL_STATS_FULL;                       /* an older launcher.ini: Show FPS / ms is on or off */
    CHECK_EQ(bl_profile_compose(&c, 0, files, &d), 0);
    CHECK(!strcmp(env_of(&d, "DXVK_PS4_FPS_HUD"), "fps"));
}

static void test_pro_preset(void)
{
    char p[600];
    BlConfig c; bl_config_defaults(&c);
    BlDecision base, pro;
    CHECK_EQ(bl_profile_compose(&c, 0, files, &base), 0);
    /* Auto on a Pro without model-pro.ini falls back to base. */
    CHECK_EQ(bl_profile_compose(&c, 1, files, &pro), 0);
    CHECK(!strcmp(base.profile_text, pro.profile_text) && !strcmp(base.dxvk_conf, pro.dxvk_conf));
    /* An edited Pro preset changes exactly what it lists. */
    snprintf(p, sizeof p, "%s/model-pro.ini", files);
    put(p, "[display]\ndesktop = 2560x1440\n[dxvk.conf]\nd3d9.maxFrameRate = 60\nd3d9.maxFrameLatency = 2\n[env]\nPRO_ONLY = 1\n");
    c.model = BL_MODEL_PRO;
    CHECK_EQ(bl_profile_compose(&c, 0, files, &pro), 0);
    CHECK(strstr(pro.profile_text, "desktop = 2560x1440") != NULL);
    CHECK(strstr(pro.profile_text, "desktop = 1920x1080") == NULL);
    CHECK(strstr(pro.profile_text, "executable = " BL_WINE_GAME_DIR "\\Blur.exe") != NULL);   /* inherited */
    CHECK(strstr(pro.dxvk_conf, "d3d9.maxFrameLatency = 2") != NULL);
    CHECK(env_of(&pro, "PRO_ONLY") != NULL);
    /* Auto on a Pro picks it; auto on a base console does not. */
    c.model = BL_MODEL_AUTO;
    CHECK_EQ(bl_profile_compose(&c, 1, files, &pro), 0);
    CHECK(strstr(pro.profile_text, "2560x1440") != NULL);
    CHECK_EQ(bl_profile_compose(&c, 0, files, &pro), 0);
    CHECK(strstr(pro.profile_text, "2560x1440") == NULL);
    unlink(p);
}

/* BLUR 1.00 plan Task L9: the profile env, all of it in extra[]. */
static void test_env_has_net_cache_3_and_paths(void)
{
    for (int logs = 0; logs < 2; logs++) {
        BlConfig c; bl_config_defaults(&c);
        c.logging = logs;
        BlDecision d;
        CHECK_EQ(bl_profile_compose(&c, 0, files, &d), 0);
        BlPaths cache; bl_paths_init(&cache, NULL);           /* the engine paths follow bl_paths */
        char conf[200]; snprintf(conf, sizeof conf, "%s/dxvk.conf", cache.cache);
        const char *want[][2] = {
            { "PW_NATIVE_ALERT_UMTX", "1" }, { "DXVK_PS4_NATIVE", "menu" }, { "PW_SERVER_PRIO", "699" }, { "DXVK_PS4_NET_CACHE", "3" },
            { "DXVK_CONFIG_FILE", conf },
            { "DXVK_STATE_CACHE_PATH", cache.cache }, { "DXVK_PS4_NATIVE_ONLY", "1" },
            { "BLUR_LOGS", logs ? "1" : "0" }, { "DXVK_PS4_FPS_HUD", "0" },
        };
        for (unsigned i = 0; i < sizeof want / sizeof want[0]; i++) {
            const char *v = env_of(&d, want[i][0]);
            CHECK(v && !strcmp(v, want[i][1]));
            if (!v || strcmp(v, want[i][1])) fprintf(stderr, "  %s = %s\n", want[i][0], v ? v : "(unset)");
        }
        CHECK(env_of(&d, "DXVK_LOG_PATH") == NULL);            /* dxn_arm.txt and the client log stay in game/ */
        /* BLUR_LOG_DIR only with logs on (finding 19, L5): without it the
         * engine writes no wowblur.log, radv.log or dxn_fault.txt. */
        BlPaths paths; bl_paths_init(&paths, NULL);
        const char *dir = env_of(&d, "BLUR_LOG_DIR");
        CHECK(logs ? dir && !strcmp(dir, paths.logs) : dir == NULL);
        CHECK(d.env_count <= 13);
        CHECK_EQ(d.env_count, logs ? 13u : 11u);
    }
    BlConfig c; bl_config_defaults(&c);
    c.stats = BL_STATS_FPS;
    BlDecision d;
    CHECK_EQ(bl_profile_compose(&c, 0, files, &d), 0);
    CHECK(!strcmp(env_of(&d, "DXVK_PS4_FPS_HUD"), "fps"));
}

static int has_banned(const char *s)
{
    char low[8192];
    size_t n = strlen(s);
    if (n >= sizeof low) n = sizeof low - 1;
    for (size_t i = 0; i < n; i++) low[i] = (char)((s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i]);
    low[n] = 0;
    return strstr(low, "prospero") || strstr(low, "/data/pc") || strstr(low, "\\data\\pc") || strstr(low, "pc\\blur");
}

static void test_no_prospero_or_pc_blur_in_env(void)
{
    for (int logs = 0; logs < 2; logs++) {
        BlConfig c; bl_config_defaults(&c);
        c.logging = logs; c.stats = BL_STATS_FPS;
        BlDecision d;
        CHECK_EQ(bl_profile_compose(&c, 0, files, &d), 0);
        CHECK(!has_banned(d.profile_text));
        CHECK(!has_banned(d.dxvk_conf));
        for (unsigned i = 0; i < d.env_count; i++) {
            CHECK(!has_banned(d.env[i].name) && !has_banned(d.env[i].value));
            if (has_banned(d.env[i].name) || has_banned(d.env[i].value)) fprintf(stderr, "  %s=%s\n", d.env[i].name, d.env[i].value);
        }
    }
}

static void test_env_overflow(void)
{
    char p[600], text[4096] = "[env]\n";
    snprintf(p, sizeof p, "%s/model-pro.ini", files);
    for (int i = 0; i < 30; i++) { char l[40]; snprintf(l, sizeof l, "V%d = %d\n", i, i); strcat(text, l); }
    put(p, text);
    BlConfig c; bl_config_defaults(&c); c.model = BL_MODEL_PRO;
    BlDecision d;
    CHECK(bl_profile_compose(&c, 0, files, &d) < 0);
    CHECK(d.env_count <= 24);
    unlink(p);
    CHECK(bl_decision_setenv(&d, "A", "1") >= -1);
    BlDecision e; memset(&e, 0, sizeof e);
    for (int i = 0; i < 24; i++) { char n[16]; snprintf(n, sizeof n, "N%d", i); CHECK_EQ(bl_decision_setenv(&e, n, "x"), 0); }
    CHECK(bl_decision_setenv(&e, "ONE_TOO_MANY", "x") < 0);
    CHECK_EQ(bl_decision_setenv(&e, "N3", "replaced"), 0);           /* existing names are replaced */
    CHECK(!strcmp(env_of(&e, "N3"), "replaced"));
    CHECK_EQ(e.env_count, 24);
}

static void test_host_validation(void)
{
    const char *good[] = { "blur.example.net", "10.0.0.5", "my-server", "a", "192.168.0.124", "x1.y2.z3" };
    const char *bad[] = { "", "a b", "x;y", "x=y", "256.1.1.1", "1.2.3", "-lead", "trail.", "..", "a..b",
                          "host/path", "ünïcode", "1.2.3.4.5", "http://x" };
    for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) CHECK(bl_host_valid(good[i]));
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(!bl_host_valid(bad[i]));
        if (bl_host_valid(bad[i])) fprintf(stderr, "  accepted: %s\n", bad[i]);
    }
    char longname[201]; memset(longname, 'a', 200); longname[200] = 0;
    CHECK(!bl_host_valid(longname));
    CHECK(bl_ipv4_valid("0.0.0.0") && bl_ipv4_valid("255.255.255.255") && !bl_ipv4_valid("01.2.3.4x"));
}

static void test_hosts_env(void)
{
    BlConfig c; bl_config_defaults(&c);
    char out[1024];
    CHECK_EQ(bl_hosts_env(&c, NULL, NULL, NULL, out, sizeof out), 0);
    CHECK_EQ(out[0], 0);                                         /* official: nothing */
    c.online_mode = BL_ONLINE_OFF;
    CHECK_EQ(bl_hosts_env(&c, NULL, NULL, NULL, out, sizeof out), 0);
    CHECK(strstr(out, "blur-pc-live.auth.mmp3.demonware.net=0.0.0.0") != NULL);
    CHECK(strstr(out, "blur-pc-live.lsg.mmp3.demonware.net=0.0.0.0") != NULL);
    c.online_mode = BL_ONLINE_PRIVATE;
    snprintf(c.host, sizeof c.host, "blur.example.net");
    CHECK(bl_hosts_env(&c, NULL, NULL, NULL, out, sizeof out) < 0);          /* a name needs a resolved address */
    CHECK_EQ(bl_hosts_env(&c, "203.0.113.20", NULL, NULL, out, sizeof out), 0);
    CHECK(strstr(out, "blur-pc-live.auth.mmp3.demonware.net=203.0.113.20") != NULL);
    CHECK(strstr(out, "stun.eu.demonware.net=203.0.113.20") != NULL);
    snprintf(c.host, sizeof c.host, "10.0.0.5");
    snprintf(c.auth_host, sizeof c.auth_host, "10.0.0.9");
    CHECK_EQ(bl_hosts_env(&c, NULL, NULL, NULL, out, sizeof out), 0);
    CHECK(strstr(out, "blur-pc-live.auth.mmp3.demonware.net=10.0.0.9") != NULL);
    CHECK(strstr(out, "blur-pc-live.lsg.mmp3.demonware.net=10.0.0.5") != NULL);
    /* M7: names in auth_host / lobby_host are used (once resolved), not
     * silently replaced by the main server. */
    snprintf(c.auth_host, sizeof c.auth_host, "auth.example.net");
    snprintf(c.lobby_host, sizeof c.lobby_host, "lobby.example.net");
    CHECK(bl_hosts_env(&c, NULL, NULL, NULL, out, sizeof out) < 0);
    CHECK(bl_hosts_env(&c, NULL, "198.51.100.7", NULL, out, sizeof out) < 0);
    CHECK_EQ(bl_hosts_env(&c, NULL, "198.51.100.7", "198.51.100.8", out, sizeof out), 0);
    CHECK(strstr(out, "blur-pc-live.auth.mmp3.demonware.net=198.51.100.7") != NULL);
    CHECK(strstr(out, "blur-pc-live.lsg.mmp3.demonware.net=198.51.100.8") != NULL);
    CHECK(strstr(out, "stun.us.demonware.net=10.0.0.5") != NULL);
    c.lobby_host[0] = 0;
    /* Nothing hostile reaches the string. */
    snprintf(c.host, sizeof c.host, "1.2.3.4;evil=5.6.7.8");
    c.auth_host[0] = 0;
    CHECK(bl_hosts_env(&c, NULL, NULL, NULL, out, sizeof out) < 0);
    CHECK(bl_hosts_env(&c, "1.2.3.4;x=y", NULL, NULL, out, sizeof out) < 0);
    CHECK(bl_hosts_env(&c, NULL, NULL, NULL, out, 10) < 0);                  /* too small */
}

static void test_logs(void)
{
    char game[400], data[400], logs[450], p[600];
    snprintf(game, sizeof game, "%s/game", root); mkdir(game, 0755);
    snprintf(data, sizeof data, "%s/data", root); mkdir(data, 0755);
    snprintf(logs, sizeof logs, "%s/logs", data); mkdir(logs, 0755);
    /* BLUR 1.00 (L10): every engine log is in <root>/logs (BLUR_LOG_DIR). */
    const char *ours[] = { "game/Blur_d3d9.log", "data/logs/radv.log", "data/logs/dxn_server.txt", "data/logs/session-3.log", "data/logs/wowblur.log" };
    const char *decoys[] = { "game/Blur.exe", "game/save.dat", "data/radv.log", "data/events.log", "game/dxn_server.txt", "game/d3d9.dll" };
    for (size_t i = 0; i < 5; i++) { snprintf(p, sizeof p, "%s/%s", root, ours[i]); put(p, "log line\n"); }
    for (size_t i = 0; i < 6; i++) { snprintf(p, sizeof p, "%s/%s", root, decoys[i]); put(p, "keep me\n"); }
    BlLogFile list[16];
    int n = bl_logs_list(game, data, list, 16);
    CHECK_EQ(n, 5);
    long long total = 0;
    for (int i = 0; i < n; i++) total += list[i].size;
    CHECK_EQ(total, 5 * 9);
    CHECK_EQ(bl_logs_clear(game, data, NULL), 5);
    for (size_t i = 0; i < 5; i++) { snprintf(p, sizeof p, "%s/%s", root, ours[i]); CHECK(access(p, F_OK) != 0); }
    for (size_t i = 0; i < 6; i++) { snprintf(p, sizeof p, "%s/%s", root, decoys[i]); CHECK(access(p, F_OK) == 0); }
    CHECK_EQ(bl_logs_list(game, data, list, 16), 0);
}

/* The game folder the launcher found (/data/JkFlow/Blur, any case) replaces the
 * preset's executable and working_directory; nothing else changes. */
static void test_set_game(void)
{
    BlDecision d;
    memset(&d, 0, sizeof d);
    snprintf(d.profile_text, sizeof d.profile_text, "[app]\nexecutable = Z:\\data\\BLUR\\game\\Blur.exe\nworking_directory = Z:\\data\\BLUR\\game\ndll_overrides = d3d9=n\n");
    CHECK_EQ(bl_profile_set_game(&d, "/data/jkflow/BLUR"), 0);
    CHECK(!strcmp(d.profile_text, "[app]\nexecutable = Z:\\data\\jkflow\\BLUR\\Blur.exe\nworking_directory = Z:\\data\\jkflow\\BLUR\ndll_overrides = d3d9=n\n"));
    CHECK_EQ(bl_profile_set_game(&d, NULL), -1);
    /* Indented keys are found too; a profile without an executable line is refused (review 2026-11-15). */
    BlDecision e; memset(&e, 0, sizeof e);
    snprintf(e.profile_text, sizeof e.profile_text, "[app]\n  executable = Z:\\x\\Blur.exe\r\n\tworking_directory = Z:\\x\r\n");
    CHECK_EQ(bl_profile_set_game(&e, "/data/JkFlow/Blur"), 0);
    CHECK(!strcmp(e.profile_text, "[app]\nexecutable = Z:\\data\\JkFlow\\Blur\\Blur.exe\nworking_directory = Z:\\data\\JkFlow\\Blur\n"));
    snprintf(e.profile_text, sizeof e.profile_text, "[app]\ndll_overrides = d3d9=n\n");
    CHECK_EQ(bl_profile_set_game(&e, "/data/JkFlow/Blur"), -1);
    CHECK(!strcmp(e.profile_text, "[app]\ndll_overrides = d3d9=n\n"));
    char longdir[5000]; memset(longdir, 'a', sizeof longdir - 1); longdir[sizeof longdir - 1] = 0; longdir[0] = '/';
    char before[sizeof d.profile_text]; memcpy(before, d.profile_text, sizeof before);
    CHECK_EQ(bl_profile_set_game(&d, longdir), -1);
    CHECK(!strcmp(before, d.profile_text));                  /* unchanged when it does not fit */
}

int main(void)
{
    RUN(test_set_game);
    setup();
    RUN(test_base_profile);
    RUN(test_logging_and_stats);
    RUN(test_pro_preset);
    RUN(test_env_overflow);
    RUN(test_env_has_net_cache_3_and_paths);
    RUN(test_no_prospero_or_pc_blur_in_env);
    RUN(test_host_validation);
    RUN(test_hosts_env);
    RUN(test_logs);
    return bl_test_report("test_profile");
}
