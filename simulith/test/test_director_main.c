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
    SCENARIO_TELEMETRY_FAILURE,
    SCENARIO_SCENARIO_FAILURE,
    SCENARIO_CLIENT_INIT_FAILURE,
    SCENARIO_PHASE_CONFIG_FAILURE,
    SCENARIO_HANDSHAKE_FAILURE,
    SCENARIO_PHASE_RUN_FAILURE
} scenario_t;

director_config_t g_director_config;

static scenario_t scenario;
static int cleanup_calls;
static int client_shutdown_calls;
static int client_run_calls;
static int telemetry_calls;
static int configured_phase_calls;

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
    return scenario == SCENARIO_TELEMETRY_FAILURE ? -1 : 0;
}

int simulith_control_trace_open(const char *directory)
{
    (void)directory;
    return 0;
}

int director_configure_trace_duration(void)
{
    return 0;
}

void simulith_control_trace_abort(void)
{
}

int initialize_scenario(director_config_t *config)
{
    (void)config;
    return scenario == SCENARIO_SCENARIO_FAILURE ? -1 : 0;
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

int simulith_client_configure_phases(uint32_t phase_mask)
{
    TEST_ASSERT_EQUAL_UINT32(SIMULITH_PHASE_MASK_PREPARE |
                             SIMULITH_PHASE_MASK_EXECUTE |
                             SIMULITH_PHASE_MASK_COMMIT, phase_mask);
    configured_phase_calls++;
    return scenario == SCENARIO_PHASE_CONFIG_FAILURE ? -1 : 0;
}

void simulith_client_run_loop(simulith_tick_callback callback)
{
    TEST_ASSERT_EQUAL_PTR(on_tick, callback);
    client_run_calls++;
}

int simulith_client_run_phased_loop(simulith_phase_callback prepare,
                                    simulith_phase_callback execute,
                                    simulith_phase_callback commit)
{
    TEST_ASSERT_EQUAL_PTR(director_prepare_tick, prepare);
    TEST_ASSERT_EQUAL_PTR(director_execute_tick, execute);
    TEST_ASSERT_EQUAL_PTR(director_commit_tick, commit);
    client_run_calls++;
    return scenario == SCENARIO_PHASE_RUN_FAILURE ? -1 : 0;
}

void simulith_client_shutdown(void)
{
    client_shutdown_calls++;
}

void on_tick(uint64_t tick_time_ns)
{
    (void)tick_time_ns;
}

int director_prepare_tick(uint64_t sequence, uint64_t tick_time_ns)
{
    (void)sequence;
    (void)tick_time_ns;
    return 0;
}

int director_execute_tick(uint64_t sequence, uint64_t tick_time_ns)
{
    (void)sequence;
    (void)tick_time_ns;
    return 0;
}

int director_commit_tick(uint64_t sequence, uint64_t tick_time_ns)
{
    (void)sequence;
    (void)tick_time_ns;
    return 0;
}

void director_write_terminal_metrics(void)
{
}

void setUp(void)
{
    scenario = SCENARIO_SUCCESS;
    cleanup_calls = 0;
    client_shutdown_calls = 0;
    client_run_calls = 0;
    telemetry_calls = 0;
    configured_phase_calls = 0;
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

static void test_42_failure_refuses_open_loop_run(void)
{
    scenario = SCENARIO_42_FAILURE;
    TEST_ASSERT_EQUAL_INT(1, run_entry());
    TEST_ASSERT_EQUAL_INT(0, telemetry_calls);
    TEST_ASSERT_EQUAL_INT(0, client_run_calls);
    TEST_ASSERT_EQUAL_INT(0, client_shutdown_calls);
    TEST_ASSERT_EQUAL_INT(1, cleanup_calls);
}

static void test_client_startup_failures(void)
{
    scenario = SCENARIO_TELEMETRY_FAILURE;
    TEST_ASSERT_EQUAL_INT(1, run_entry());
    TEST_ASSERT_EQUAL_INT(1, telemetry_calls);
    TEST_ASSERT_EQUAL_INT(1, cleanup_calls);
    TEST_ASSERT_EQUAL_INT(0, client_shutdown_calls);

    setUp();
    scenario = SCENARIO_SCENARIO_FAILURE;
    TEST_ASSERT_EQUAL_INT(1, run_entry());
    TEST_ASSERT_EQUAL_INT(1, cleanup_calls);

    setUp();
    scenario = SCENARIO_CLIENT_INIT_FAILURE;
    TEST_ASSERT_EQUAL_INT(1, run_entry());
    TEST_ASSERT_EQUAL_INT(1, cleanup_calls);
    TEST_ASSERT_EQUAL_INT(0, client_shutdown_calls);

    setUp();
    scenario = SCENARIO_PHASE_CONFIG_FAILURE;
    TEST_ASSERT_EQUAL_INT(1, run_entry());
    TEST_ASSERT_EQUAL_INT(1, configured_phase_calls);
    TEST_ASSERT_EQUAL_INT(1, cleanup_calls);
    TEST_ASSERT_EQUAL_INT(1, client_shutdown_calls);

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

static void test_phase_failure_exits_nonzero(void)
{
    scenario = SCENARIO_PHASE_RUN_FAILURE;
    TEST_ASSERT_EQUAL_INT(1, run_entry());
    TEST_ASSERT_EQUAL_INT(1, client_run_calls);
    TEST_ASSERT_EQUAL_INT(1, client_shutdown_calls);
    TEST_ASSERT_EQUAL_INT(1, cleanup_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_outcomes);
    RUN_TEST(test_component_startup_failures);
    RUN_TEST(test_42_failure_refuses_open_loop_run);
    RUN_TEST(test_client_startup_failures);
    RUN_TEST(test_successful_lifecycle);
    RUN_TEST(test_phase_failure_exits_nonzero);
    return UNITY_END();
}
