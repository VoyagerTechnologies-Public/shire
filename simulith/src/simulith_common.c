#include "simulith.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef enum {
    LOG_MODE_STDOUT,
    LOG_MODE_FILE,
    LOG_MODE_BOTH,
    LOG_MODE_NONE
} simulith_log_mode_t;

/* When building tests we make these globals non-static so test code can
 * inspect/reset them. In production builds they remain translation-unit
 * static to preserve encapsulation. */
#ifdef SIMULITH_TESTING
simulith_log_mode_t simulith_log_mode = LOG_MODE_STDOUT;
int simulith_log_mode_initialized = 0;
FILE *simulith_log_file = NULL;
#else
static simulith_log_mode_t simulith_log_mode = LOG_MODE_STDOUT;
static int simulith_log_mode_initialized = 0;
static FILE *simulith_log_file = NULL;
#endif

static void simulith_log_init_mode(void) {
    const char *env = getenv("SIMULITH_LOG_MODE");
    if (!env) {
        simulith_log_mode = LOG_MODE_STDOUT;
    } else if (strcmp(env, "stdout") == 0) {
        simulith_log_mode = LOG_MODE_STDOUT;
    } else if (strcmp(env, "file") == 0) {
        simulith_log_mode = LOG_MODE_FILE;
    } else if (strcmp(env, "both") == 0) {
        simulith_log_mode = LOG_MODE_BOTH;
    } else if (strcmp(env, "none") == 0) {
        simulith_log_mode = LOG_MODE_NONE;
    } else {
        simulith_log_mode = LOG_MODE_STDOUT; // fallback
    }
    simulith_log_mode_initialized = 1;
}

void simulith_log(const char *fmt, ...)
{
    if (!simulith_log_mode_initialized) {
        simulith_log_init_mode();
    }
    if (simulith_log_mode == LOG_MODE_NONE) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    if (simulith_log_mode == LOG_MODE_STDOUT) {
        vfprintf(stdout, fmt, args);
        fflush(stdout);
        va_end(args);
    } else if (simulith_log_mode == LOG_MODE_FILE) {
        if (!simulith_log_file) {
            simulith_log_file = fopen("/tmp/simulith.log", "a");
        }
        if (simulith_log_file) {
            vfprintf(simulith_log_file, fmt, args);
            fflush(simulith_log_file);
        }
        va_end(args);
    } else if (simulith_log_mode == LOG_MODE_BOTH) {
        va_list args2;
        va_copy(args2, args);
        vfprintf(stdout, fmt, args);
        fflush(stdout);
        if (!simulith_log_file) {
            simulith_log_file = fopen("/tmp/simulith.log", "a");
        }
        if (simulith_log_file) {
            vfprintf(simulith_log_file, fmt, args2);
            fflush(simulith_log_file);
        }
        va_end(args2);
        va_end(args);
    } else {
        va_end(args);
    }
}

#ifdef SIMULITH_TESTING
/* Test-only helper: reset logging to uninitialized state and close any open
 * log file. Tests should call this between cases to avoid cross-test
 * interference. */
void simulith_log_reset_for_tests(void)
{
    if (simulith_log_file) {
        fclose(simulith_log_file);
        simulith_log_file = NULL;
    }
    simulith_log_mode_initialized = 0;
    simulith_log_mode = LOG_MODE_STDOUT;
}
#endif