/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host test for wine64_main.c blur_apply_decision (review finding 3), cut
 * verbatim by run_title_tests.sh into env_cut.inc: every launcher env line
 * reaches extra[] or the start fails closed (-1, the line named in the log);
 * a dropped line is never only logged. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bl_frontend.h"
#include "pw_wine_start.h"

static int failures, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { failures++; fprintf(stderr, "  FAIL %s:%d %s\n", __FILE__, __LINE__, what); } } while (0)

static char logbuf[8192];
#define PS5LOG_LOG(...) do { size_t _n = strlen(logbuf); snprintf(logbuf + _n, sizeof logbuf - _n, __VA_ARGS__); \
    _n = strlen(logbuf); snprintf(logbuf + _n, sizeof logbuf - _n, "\n"); } while (0)
static BlDecision blur_decision;
static int blur_logs_verbose;

#include "env_cap_cut.inc"
#include "env_cut.inc"

static void decision(unsigned n, const char *const *names)
{
    memset(&blur_decision, 0, sizeof blur_decision);
    blur_decision.action = BL_START_GAME;
    for (unsigned i = 0; i < n; i++) {
        snprintf(blur_decision.env[i].name, sizeof blur_decision.env[i].name, "%s", names[i]);
        snprintf(blur_decision.env[i].value, sizeof blur_decision.env[i].value, "v%u", i);
    }
    blur_decision.env_count = n;
}

int main(void)
{
    PwWineStartEnv extra[8];
    uint32_t count;

    /* Everything fits: 0, appended and replaced. */
    static const char *const fit[] = { "WINEDEBUG", "DXVK_HUD", "BLUR_LOGS", "MESA_SHADER_CACHE_DIR" };
    memset(extra, 0, sizeof extra);
    extra[0] = (PwWineStartEnv){ "WINEDEBUG", "x" };
    extra[1] = (PwWineStartEnv){ "MESA_SHADER_CACHE_DIR", "old" };
    count = 2;
    decision(4, fit);
    logbuf[0] = 0;
    CHECK(blur_apply_decision(extra, &count, 4) == 0, "all lines applied");
    CHECK(count == 4, "two appended, one replaced, WINEDEBUG through extra[0]");
    CHECK(!strcmp(extra[0].value, "v0") && !strcmp(extra[1].value, "v3"), "replaced in place");
    CHECK(strstr(logbuf, "dropped") == NULL, "nothing dropped");

    /* One line too many: fail closed, the line named. */
    static const char *const over[] = { "DXVK_HUD", "BLUR_LOGS", "DXVK_LOG_LEVEL" };
    memset(extra, 0, sizeof extra);
    extra[0] = (PwWineStartEnv){ "WINEDEBUG", "x" };
    count = 2;
    decision(3, over);
    logbuf[0] = 0;
    CHECK(blur_apply_decision(extra, &count, 3) == -1, "a line that does not fit stops the start");
    CHECK(strstr(logbuf, "DXVK_LOG_LEVEL") != NULL, "the dropped line is logged by name");
    CHECK(count <= 3, "never past the capacity");

    /* Audit finding 7: the title's capacity is 48 lines before NETCFG and the
     * display name; 48 start, a 49th refuses cleanly (fail closed, logged by
     * name, never past the capacity). */
    CHECK(BLUR_ENV_CAPACITY == 48, "capacity 48");
    CHECK(BLUR_ENV_CAPACITY + BLUR_ENV_AFTER <= PW_WINE_START_MAX_ENV, "pw_wine_start sets every line");
    CHECK(PW_WINE_START_MAX_ENV + BLUR_PRX_ENV_OWN <= BLUR_PRX_ENV_MAX, "ntdll's module environment holds them all");
    {
        static PwWineStartEnv big[BLUR_ENV_CAPACITY + BLUR_ENV_AFTER];
        static char names[BLUR_ENV_CAPACITY + 2][24];
        const char *launcher[24];
        unsigned title = BLUR_ENV_CAPACITY - 20;     /* 28 title lines + 20 launcher lines = 48 */
        for (unsigned round = 0; round < 2; round++) {
            memset(big, 0, sizeof big);
            big[0] = (PwWineStartEnv){ "WINEDEBUG", "x" };
            for (unsigned i = 1; i < title; i++) {
                snprintf(names[i], sizeof names[i], "TITLE_%u", i);
                big[i] = (PwWineStartEnv){ names[i], "t" };
            }
            unsigned n = 20 + round;                  /* round 1: the 49th line */
            for (unsigned i = 0; i < n; i++) {
                snprintf(names[title + i], sizeof names[title + i], "LAUNCHER_%u", i);
                launcher[i] = names[title + i];
            }
            decision(n, launcher);
            count = title;
            logbuf[0] = 0;
            int rc = blur_apply_decision(big, &count, BLUR_ENV_CAPACITY);
            if (!round) CHECK(rc == 0 && count == 48 && strstr(logbuf, "(no room)") == NULL, "48 lines start");
            else CHECK(rc == -1 && count == 48 && strstr(logbuf, "refused=LAUNCHER_20") != NULL &&
                       big[48].name == NULL, "the 49th line refuses cleanly");
        }
    }
    /* The launcher's hand-off holds 24 lines: a count past its slots, or a
     * line without its terminator, refuses before anything is read past them. */
    {
        PwWineStartEnv small[8] = { { "WINEDEBUG", "x" } };
        decision(1, fit);
        blur_decision.env_count = sizeof(blur_decision.env) / sizeof(blur_decision.env[0]) + 1;
        count = 1;
        logbuf[0] = 0;
        CHECK(blur_apply_decision(small, &count, 8) == -1 && count == 1 && strstr(logbuf, "hand-off") != NULL,
              "an env count past the hand-off refuses");
        decision(2, fit);
        memset(blur_decision.env[1].value, 'A', sizeof(blur_decision.env[1].value));
        count = 1;
        logbuf[0] = 0;
        CHECK(blur_apply_decision(small, &count, 8) == -1 && count == 1 && strstr(logbuf, "hand-off") != NULL,
              "an unterminated value refuses");
    }

    fprintf(stderr, "test_title_env: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
