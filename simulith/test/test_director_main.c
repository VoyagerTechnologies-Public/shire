#include "simulith_director.h"
#include "unity.h"

#include <string.h>

typedef enum
{
    SCENARIO_SUCCESS,
    SCENARIO_PARSE_HELP,
    SCENARIO_PARSE_ERROR,
    SCENARIO_LOAD_FAILURE,
    SCENARIO_COMPONENT_FAILURE,
    SCENARIO_42_FAILURE,
    SCENARIO_CLIENT_INIT_FAILURE,
    SCENARIO_HANDSHAKE_FAILURE
} scenario_t;

director_config_t g_director_config;

static scenario_t scenario;
static int cleanup_calls;
static int client_shutdown_calls;
static int client_run_calls;
static int telemetry_calls;

int simulith_director_main(int argc, char *argv[]);

unsigned int __wrap_sleep(unsigned int seconds)
{
    (void)seconds;
    return 0;
}

int parse_args(int argc, char *argv[], director_config_t *config)
{
    (void)argc;
    (void)argv;
    memset(config, 0, sizeof(*config));
    if (scenario == SCENARIO_PARSE_HELP)
        return -1;
    if (scenario == SCENARIO_PARSE_ERROR)
        return 1;
    return 0;
}

int load_components(director_config_t *config)
{
    (void)config;
    return scenario == SCENARIO_LOAD_FAILURE ? -1 : 0;
}

int initialize_components(director_config_t *config)
{
    (void)config;
    return scenario == SCENARIO_COMPONENT_FAILURE ? -1 : 0;
}

void cleanup_components(director_config_t *config)
{
    (void)config;
    cleanup_calls++;
}

int initialize_42(director_config_t *config)
{
    if (scenario == SCENARIO_42_FAILURE) {
        config->enable_42 = 1;
        config->fortytwo_initialized = 1;
        return -1;
    }
    return 0;
}

int initialize_telemetry(void)
{
    telemetry_calls++;
    return 0;
}

int simulith_client_init(const char *pub_addr, const char *rep_addr,
                         const char *id, uint64_t rate_ns)
{
    (void)pub_addr;
    (void)rep_addr;
    (void)id;
    (void)rate_ns;
    return scenario == SCENARIO_CLIENT_INIT_FAILURE ? -1 : 0;
}

int simulith_client_handshake(void)
{
    return scenario == SCENARIO_HANDSHAKE_FAILURE ? -1 : 0;
}

void simulith_client_run_loop(simulith_tick_callback callback)
{
    TEST_ASSERT_EQUAL_PTR(on_tick, callback);
    client_run_calls++;
}

void simulith_client_shutdown(void)
{
    client_shutdown_calls++;
}

void on_tick(uint64_t tick_time_ns)
{
    (void)tick_time_ns;
}

void setUp(void)
{
    scenario = SCENARIO_SUCCESS;
    cleanup_calls = 0;
    client_shutdown_calls = 0;
    client_run_calls = 0;
    telemetry_calls = 0;
    memset(&g_director_config, 0, sizeof(g_director_config));
}

void tearDown(void)
{
}

static int run_entry(void)
{
    char *arguments[] = {"simulith_director", NULL};
    return simulith_director_main(1, arguments);
}

static void test_parse_outcomes(void)
{
    scenario = SCENARIO_PARSE_HELP;
    TEST_ASSERT_EQUAL_INT(0, run_entry());
    scenario = SCENARIO_PARSE_ERROR;
    TEST_ASSERT_EQUAL_INT(1, run_entry());
    TEST_ASSERT_EQUAL_INT(0, cleanup_calls);
}

static void test_component_startup_failures(void)
{
    scenario = SCENARIO_LOAD_FAILURE;
    TEST_ASSERT_EQUAL_INT(1, run_entry());
    TEST_ASSERT_EQUAL_INT(0, cleanup_calls);

    scenario = SCENARIO_COMPONENT_FAILURE;
    TEST_ASSERT_EQUAL_INT(1, run_entry());
    TEST_ASSERT_EQUAL_INT(1, cleanup_calls);
}

static void test_42_failure_is_nonfatal(void)
{
    scenario = SCENARIO_42_FAILURE;
    TEST_ASSERT_EQUAL_INT(0, run_entry());
    TEST_ASSERT_EQUAL_INT(1, telemetry_calls);
    TEST_ASSERT_EQUAL_INT(1, client_run_calls);
    TEST_ASSERT_EQUAL_INT(1, client_shutdown_calls);
    TEST_ASSERT_EQUAL_INT(1, cleanup_calls);
    TEST_ASSERT_EQUAL_INT(0, g_director_config.enable_42);
    TEST_ASSERT_EQUAL_INT(0, g_director_config.fortytwo_initialized);
}

static void test_client_startup_failures(void)
{
    scenario = SCENARIO_CLIENT_INIT_FAILURE;
    TEST_ASSERT_EQUAL_INT(1, run_entry());
    TEST_ASSERT_EQUAL_INT(1, cleanup_calls);
    TEST_ASSERT_EQUAL_INT(0, client_shutdown_calls);

    setUp();
    scenario = SCENARIO_HANDSHAKE_FAILURE;
    TEST_ASSERT_EQUAL_INT(1, run_entry());
    TEST_ASSERT_EQUAL_INT(1, cleanup_calls);
    TEST_ASSERT_EQUAL_INT(1, client_shutdown_calls);
}

static void test_successful_lifecycle(void)
{
    TEST_ASSERT_EQUAL_INT(0, run_entry());
    TEST_ASSERT_EQUAL_INT(1, telemetry_calls);
    TEST_ASSERT_EQUAL_INT(1, client_run_calls);
    TEST_ASSERT_EQUAL_INT(1, client_shutdown_calls);
    TEST_ASSERT_EQUAL_INT(1, cleanup_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_outcomes);
    RUN_TEST(test_component_startup_failures);
    RUN_TEST(test_42_failure_is_nonfatal);
    RUN_TEST(test_client_startup_failures);
    RUN_TEST(test_successful_lifecycle);
    return UNITY_END();
}
