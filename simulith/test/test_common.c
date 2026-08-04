#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "simulith.h"

// Test-only helper: reset internals by accessing module globals directly.
// When building tests we define SIMULITH_TESTING which makes these symbols
// non-static in the implementation so they are visible here.
extern int simulith_log_mode_initialized;
extern int simulith_log_mode; /* enum type compatible with int */
extern FILE *simulith_log_file;

void simulith_log_reset_for_tests(void)
{
    if (simulith_log_file) {
        fclose(simulith_log_file);
        simulith_log_file = NULL;
    }
    simulith_log_mode_initialized = 0;
    simulith_log_mode = 0; // LOG_MODE_STDOUT
}

void setUp(void) { }
void tearDown(void) { }

// Helper: capture stdout output of a function by redirecting to a temporary file
static char* capture_stdout_of(void (*fn)(const char*))
{
    char tmpl[] = "/tmp/simulith_test_stdout_XXXXXX";
    int fd = mkstemp(tmpl);
    TEST_ASSERT_TRUE(fd >= 0);

    // Save stdout
    int saved_stdout = dup(STDOUT_FILENO);
    TEST_ASSERT_TRUE(saved_stdout >= 0);

    // Redirect stdout to temp file
    fflush(stdout);
    dup2(fd, STDOUT_FILENO);

    // Call function
    fn("hello-from-test\n");

    // Restore stdout
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    // Read file contents
    lseek(fd, 0, SEEK_SET);
    off_t sz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    TEST_ASSERT_NOT_NULL(buf);
    ssize_t r = read(fd, buf, sz);
    (void)r;
    buf[sz] = '\0';
    close(fd);

    return buf; // caller should free
}

// Adapter to call simulith_log with one string
static void call_log(const char* s)
{
    (void)s; // simulith_log uses printf-style, but we'll pass a constant
    simulith_log("%s", "hello-from-test\n");
}

static void test_log_default_stdout(void)
{
    // Ensure env unset
    unsetenv("SIMULITH_LOG_MODE");

    char *out = capture_stdout_of(call_log);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(strstr(out, "hello-from-test") != NULL);
    free(out);
}

static void test_log_none(void)
{
    setenv("SIMULITH_LOG_MODE", "none", 1);
    simulith_log_reset_for_tests();
    char *out = capture_stdout_of(call_log);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(strstr(out, "hello-from-test") == NULL);
    free(out);
    unsetenv("SIMULITH_LOG_MODE");
}

static void test_log_file_and_both(void)
{
    const char *logpath = "/tmp/simulith.log";
    // ensure file removed
    unlink(logpath);

    // file only
    setenv("SIMULITH_LOG_MODE", "file", 1);
    simulith_log_reset_for_tests();
    simulith_log("%s", "file-only\n");
    // Give the logging subsystem a moment to open/write the file
    fflush(NULL);
    usleep(1000);

    FILE *f = fopen(logpath, "r");
    TEST_ASSERT_NOT_NULL(f);
    char buf[128];
    bool found = false;
    while (fgets(buf, sizeof(buf), f)) {
        if (strstr(buf, "file-only")) { found = true; break; }
    }
    fclose(f);
    TEST_ASSERT_TRUE(found);

    // both
    unlink(logpath);
    setenv("SIMULITH_LOG_MODE", "both", 1);
    simulith_log_reset_for_tests();
    char *out = capture_stdout_of(call_log);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(strstr(out, "hello-from-test") != NULL);
    free(out);

    f = fopen(logpath, "r");
    TEST_ASSERT_NOT_NULL(f);
    found = false;
    while (fgets(buf, sizeof(buf), f)) {
        if (strstr(buf, "hello-from-test")) { found = true; break; }
    }
    fclose(f);
    TEST_ASSERT_TRUE(found);

    unsetenv("SIMULITH_LOG_MODE");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_log_default_stdout);
    RUN_TEST(test_log_none);
    RUN_TEST(test_log_file_and_both);
    return UNITY_END();
}
