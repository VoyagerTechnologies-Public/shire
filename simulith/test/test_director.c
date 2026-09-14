#include "simulith_director.h"
#include "unity.h"

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

#ifndef DIRECTOR_FIXTURE_DIR
#error "DIRECTOR_FIXTURE_DIR must identify the test plugin directory"
#endif

struct component_state
{
    int ticks;
    int actuations;
    int cleaned;
};

static struct component_state fake_state;
static uint64_t               fake_tick_time;
static uint64_t               fake_actuate_time;
static int                    fake_context_valid;
static int                    fake_backdoor_calls;
static uint16_t               fake_backdoor_command;
static uint8_t                fake_backdoor_payload[16];
static size_t                 fake_backdoor_payload_length;
static int                    fake_service_calls;
static int                    fake_wait_ready;
static int                    fail_wait;
static int                    fail_tick;
static int                    fail_actuate;
static int                    fail_service;
static int                    block_service;
static int                    service_entered;
static int                    release_service;
static int                    eventfd_failure;
static int                    thread_create_calls;
static int                    thread_create_failure_call;
static int                    sync_primitive_failure;
static pthread_mutex_t        service_test_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t         service_test_condition = PTHREAD_COND_INITIALIZER;

#define VALID_COMPONENT_API \
    .api_version = SIMULITH_COMPONENT_API_VERSION, \
    .struct_size = sizeof(component_interface_t)

int __real_eventfd(unsigned int initial_value, int flags);
int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                          void *(*start_routine)(void *), void *argument);
int __real_pthread_mutex_init(pthread_mutex_t *mutex,
                              const pthread_mutexattr_t *attributes);
int __real_pthread_condattr_init(pthread_condattr_t *attributes);
int __real_pthread_condattr_setclock(pthread_condattr_t *attributes,
                                     clockid_t clock_id);
int __real_pthread_cond_init(pthread_cond_t *condition,
                             const pthread_condattr_t *attributes);

int __wrap_eventfd(unsigned int initial_value, int flags)
{
    if (eventfd_failure)
    {
        errno = EMFILE;
        return -1;
    }
    return __real_eventfd(initial_value, flags);
}

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                          void *(*start_routine)(void *), void *argument)
{
    thread_create_calls++;
    if (thread_create_failure_call > 0 &&
        thread_create_calls == thread_create_failure_call)
        return EAGAIN;
    return __real_pthread_create(thread, attributes, start_routine, argument);
}

int __wrap_pthread_mutex_init(pthread_mutex_t *mutex,
                              const pthread_mutexattr_t *attributes)
{
    return sync_primitive_failure == 1 ? EAGAIN :
        __real_pthread_mutex_init(mutex, attributes);
}

int __wrap_pthread_condattr_init(pthread_condattr_t *attributes)
{
    return sync_primitive_failure == 2 ? EAGAIN :
        __real_pthread_condattr_init(attributes);
}

int __wrap_pthread_condattr_setclock(pthread_condattr_t *attributes,
                                     clockid_t clock_id)
{
    return sync_primitive_failure == 3 ? EINVAL :
        __real_pthread_condattr_setclock(attributes, clock_id);
}

int __wrap_pthread_cond_init(pthread_cond_t *condition,
                             const pthread_condattr_t *attributes)
{
    return sync_primitive_failure == 4 ? EAGAIN :
        __real_pthread_cond_init(condition, attributes);
}

typedef struct
{
    int listen_fd;
    int exchanges;
    int commands_seen;
} director_42_server_t;

static int recv_exact(int socket_fd, void *buffer, size_t length)
{
    size_t received = 0;
    while (received < length) {
        ssize_t count = recv(socket_fd, (uint8_t *)buffer + received,
                             length - received, 0);
        if (count <= 0)
            return -1;
        received += (size_t)count;
    }
    return 0;
}

static int open_loopback_listener(uint16_t *port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    int reuse = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(fd, 1) != 0) {
        close(fd);
        return -1;
    }
    socklen_t length = sizeof(address);
    if (getsockname(fd, (struct sockaddr *)&address, &length) != 0) {
        close(fd);
        return -1;
    }
    *port = ntohs(address.sin_port);
    return fd;
}

static void *director_42_server(void *argument)
{
    director_42_server_t *server = argument;
    int client = accept(server->listen_fd, NULL, NULL);
    if (client < 0)
        return NULL;
    static const char state[] =
        "TIME 2026-001-00:00:01.0\n"
        "SC[0].svb = [1 0 0]\n"
        "Orb[0].PosN = [2 3 4]\n"
        "[ENDMSG]\n";
    for (int i = 0; i < server->exchanges; i++) {
        if (send(client, state, sizeof(state) - 1, 0) < 0)
            break;
        char buffer[2048];
        if (recv_exact(client, buffer, 4) != 0)
            break;
        size_t used = 0;
        buffer[0] = '\0';
        while (!strstr(buffer, "[ENDMSG]\n") && used < sizeof(buffer) - 1) {
            ssize_t count = recv(client, buffer + used, sizeof(buffer) - 1 - used, 0);
            if (count <= 0) {
                used = 0;
                break;
            }
            used += (size_t)count;
            buffer[used] = '\0';
        }
        if (used == 0)
            break;
        if (strstr(buffer, "[ENDMSG]"))
            server->commands_seen++;
        if (send(client, "Ack", 4, 0) < 0)
            break;
    }
    close(client);
    return NULL;
}

static int fake_init(component_state_t **state)
{
    memset(&fake_state, 0, sizeof(fake_state));
    *state = (component_state_t *)&fake_state;
    return COMPONENT_SUCCESS;
}

static int failing_init(component_state_t **state)
{
    (void)state;
    return COMPONENT_ERROR;
}

static int partial_failing_init(component_state_t **state)
{
    *state = (component_state_t *)&fake_state;
    return COMPONENT_ERROR;
}

static int fake_wait_for_service(component_state_t *state, int interrupt_fd)
{
    (void)state;
    if (fail_wait)
        return COMPONENT_ERROR;
    if (__atomic_exchange_n(&fake_wait_ready, 0, __ATOMIC_ACQ_REL))
        return COMPONENT_WORK;
    struct pollfd item = {.fd = interrupt_fd, .events = POLLIN};
    int status;
    do
    {
        status = poll(&item, 1, -1);
    } while (status < 0 && errno == EINTR);
    if (status <= 0 || (item.revents & POLLIN) == 0)
        return COMPONENT_ERROR;
    uint64_t wake_count;
    return read(interrupt_fd, &wake_count, sizeof(wake_count)) ==
                   (ssize_t)sizeof(wake_count) ?
        COMPONENT_IDLE : COMPONENT_ERROR;
}

static int fake_tick(component_state_t *state, uint64_t time_ns,
                     const simulith_42_context_t *context)
{
    struct component_state *value = (struct component_state *)state;
    value->ticks++;
    fake_tick_time = time_ns;
    fake_context_valid = context->valid;
    return fail_tick ? COMPONENT_ERROR : COMPONENT_SUCCESS;
}

static int fake_service(component_state_t *state, uint64_t time_ns,
                         const simulith_42_context_t *context)
{
    (void)state;
    (void)time_ns;
    (void)context;
    __atomic_add_fetch(&fake_service_calls, 1, __ATOMIC_RELAXED);
    pthread_mutex_lock(&service_test_mutex);
    if (block_service)
    {
        service_entered = 1;
        pthread_cond_broadcast(&service_test_condition);
        while (!release_service)
            pthread_cond_wait(&service_test_condition, &service_test_mutex);
    }
    pthread_mutex_unlock(&service_test_mutex);
    return fail_service ? COMPONENT_ERROR : COMPONENT_IDLE;
}

typedef struct
{
    uint64_t sequence;
    uint64_t time_ns;
    int result;
} commit_call_t;

static void *call_commit(void *argument)
{
    commit_call_t *call = argument;
    call->result = director_commit_tick(call->sequence, call->time_ns);
    return NULL;
}

static int fake_actuate(component_state_t *state, uint64_t time_ns,
                        const simulith_42_context_t *context)
{
    struct component_state *value = (struct component_state *)state;
    value->actuations++;
    fake_actuate_time = time_ns;
    fake_context_valid = context->valid;
    return fail_actuate ? COMPONENT_ERROR : COMPONENT_SUCCESS;
}

static void fake_cleanup(component_state_t *state)
{
    ((struct component_state *)state)->cleaned++;
}

static void fake_backdoor(component_state_t *state, uint16_t command_id,
                          const uint8_t *payload, uint16_t payload_length)
{
    (void)state;
    fake_backdoor_calls++;
    fake_backdoor_command = command_id;
    fake_backdoor_payload_length = payload_length;
    if (payload_length <= sizeof(fake_backdoor_payload))
        memcpy(fake_backdoor_payload, payload, payload_length);
}

static void send_backdoor_datagram(const uint8_t *packet, size_t length)
{
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, socket_fd);

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(BACKDOOR_PORT);
    TEST_ASSERT_EQUAL_INT((int)length,
                          (int)sendto(socket_fd, packet, length, 0,
                                      (struct sockaddr *)&address, sizeof(address)));
    close(socket_fd);
}

void setUp(void)
{
    memset(&g_director_config, 0, sizeof(g_director_config));
    fake_tick_time = 0;
    fake_actuate_time = 0;
    fake_context_valid = -1;
    fake_backdoor_calls = 0;
    fake_backdoor_command = 0;
    fake_backdoor_payload_length = 0;
    fake_service_calls = 0;
    fake_wait_ready = 1;
    fail_wait = 0;
    fail_tick = 0;
    fail_actuate = 0;
    fail_service = 0;
    block_service = 0;
    service_entered = 0;
    release_service = 0;
    eventfd_failure = 0;
    thread_create_calls = 0;
    thread_create_failure_call = 0;
    sync_primitive_failure = 0;
    memset(fake_backdoor_payload, 0, sizeof(fake_backdoor_payload));
    unsetenv("FORTYTWO_SOCKET_PATH");
    unsetenv("FORTYTWO_HOST");
    unsetenv("FORTYTWO_PORT");
    setenv("FORTYTWO_IPC_MODE", "text", 1);
    setenv("SIMULITH_42_RECONNECT_ATTEMPTS", "1", 1);
    setenv("SIMULITH_42_RECONNECT_DELAY_MS", "0", 1);
    setenv("SIMULITH_GSW_HOST", "127.0.0.1", 1);
}

void tearDown(void)
{
    unsetenv("FORTYTWO_IPC_MODE");
}

static void test_parse_args(void)
{
    director_config_t config;
    char *defaults[] = {"director"};
    TEST_ASSERT_EQUAL_INT(0, parse_args(1, defaults, &config));
    TEST_ASSERT_EQUAL_STRING("./components", config.components_dir);
    TEST_ASSERT_EQUAL_INT(100, config.time_step_ms);
    TEST_ASSERT_EQUAL_INT(1, config.enable_42);

    char *options[] = {"director", "--verbose", "--enable-42", "--42-config", "/tmp/42"};
    TEST_ASSERT_EQUAL_INT(0, parse_args(5, options, &config));
    TEST_ASSERT_EQUAL_INT(1, config.verbose);
    TEST_ASSERT_EQUAL_STRING("/tmp/42", config.fortytwo_config);

    char *scenario[] = {"director", "--scenario", "/tmp/scenario.json"};
    TEST_ASSERT_EQUAL_INT(0, parse_args(3, scenario, &config));
    TEST_ASSERT_EQUAL_STRING("/tmp/scenario.json", config.scenario_file);

    char *help[] = {"director", "--help"};
    TEST_ASSERT_EQUAL_INT(-1, parse_args(2, help, &config));

    /* A value-taking option at argv's boundary must not read past argv. */
    char *missing_config[] = {"director", "--42-config"};
    TEST_ASSERT_EQUAL_INT(1, parse_args(2, missing_config, &config));
    TEST_ASSERT_EQUAL_STRING("./InOut", config.fortytwo_config);

    char *missing_scenario[] = {"director", "--scenario"};
    TEST_ASSERT_EQUAL_INT(1, parse_args(2, missing_scenario, &config));

    char *unknown[] = {"director", "--unknown"};
    TEST_ASSERT_EQUAL_INT(1, parse_args(2, unknown, &config));

    char long_path[300];
    memset(long_path, 'x', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';
    char *oversized[] = {"director", "--42-config", long_path};
    TEST_ASSERT_EQUAL_INT(1, parse_args(3, oversized, &config));

    char *oversized_scenario[] = {"director", "--scenario", long_path};
    TEST_ASSERT_EQUAL_INT(1, parse_args(3, oversized_scenario, &config));

    TEST_ASSERT_EQUAL_INT(1, parse_args(1, defaults, NULL));
}

static void test_scenario_validation_and_one_shot_injection(void)
{
    int receiver = socket(AF_INET, SOCK_DGRAM, 0);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, receiver);
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    TEST_ASSERT_EQUAL_INT(0, bind(receiver, (struct sockaddr *)&address,
                                  sizeof(address)));
    socklen_t address_length = sizeof(address);
    TEST_ASSERT_EQUAL_INT(0, getsockname(receiver, (struct sockaddr *)&address,
                                         &address_length));
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 10000};
    TEST_ASSERT_EQUAL_INT(0, setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO,
                                        &timeout, sizeof(timeout)));

    char path[] = "/tmp/shire-scenario-test-XXXXXX";
    int file = mkstemp(path);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, file);
    char json[512];
    int json_length = snprintf(
        json, sizeof(json),
        "{\"schema_version\":1,\"commands\":["
        "{\"sequence\":7,\"host\":\"127.0.0.1\",\"port\":%u,"
        "\"packet_hex\":\"18d4c000000102f0\"}]}",
        (unsigned)ntohs(address.sin_port));
    TEST_ASSERT_GREATER_THAN_INT(0, json_length);
    TEST_ASSERT_EQUAL_INT(json_length, (int)write(file, json, (size_t)json_length));
    close(file);

    director_config_t config;
    memset(&config, 0, sizeof(config));
    snprintf(config.scenario_file, sizeof(config.scenario_file), "%s", path);
    TEST_ASSERT_EQUAL_INT(0, initialize_scenario(&config));
    TEST_ASSERT_EQUAL_size_t(1, config.scenario_command_count);
    TEST_ASSERT_EQUAL_UINT64(7, config.scenario_commands[0].sequence);
    TEST_ASSERT_EQUAL_INT(0, director_inject_scenario_commands(&config, 6));

    uint8_t packet[16];
    TEST_ASSERT_EQUAL_INT(0, director_inject_scenario_commands(&config, 7));
    TEST_ASSERT_EQUAL_INT(8, (int)recv(receiver, packet, sizeof(packet), 0));
    static const uint8_t expected[] = {0x18, 0xd4, 0xc0, 0x00,
                                      0x00, 0x01, 0x02, 0xf0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, packet, sizeof(expected));
    TEST_ASSERT_EQUAL_UINT64(1, config.scenario_injected);

    TEST_ASSERT_EQUAL_INT(0, director_inject_scenario_commands(&config, 7));
    TEST_ASSERT_EQUAL_INT(-1, (int)recv(receiver, packet, sizeof(packet), 0));
    TEST_ASSERT_EQUAL_UINT64(1, config.scenario_injected);

    cleanup_components(&config);
    close(receiver);
    unlink(path);

    memset(&config, 0, sizeof(config));
    snprintf(config.scenario_file, sizeof(config.scenario_file), "%s", path);
    TEST_ASSERT_EQUAL_INT(-1, initialize_scenario(&config));
}

static void test_scenario_parser_rejects_malformed_payloads(void)
{
    static const char *bad_payloads[] = {
        "{\"schema_version\":0,\"commands\":[{\"sequence\":1,\"host\":\"127.0.0.1\",\"port\":9999,\"packet_hex\":\"aa\"}]}",
        "{\"schema_version\":1,\"commands\":[{\"sequence\":2,\"host\":\"127.0.0.1\",\"port\":0,\"packet_hex\":\"aa\"}]}",
        "{\"schema_version\":1,\"commands\":[{\"sequence\":2,\"host\":\"127.0.0.1\",\"port\":9999,\"packet_hex\":\"a\"}]}",
        "{\"schema_version\":1,\"commands\":[{\"sequence\":2,\"host\":\"127.0.0.1\",\"port\":9999,\"packet_hex\":\"g0\"}]}",
        "{\"schema_version\":1,\"commands\":[{\"sequence\":2,\"host\":\"not-a-real-host.invalid\",\"port\":9999,\"packet_hex\":\"0123456789abcdefABCDEF\"}]}",
        "{\"schema_version\":1,\"commands\":[{\"sequence\":3,\"host\":\"127.0.0.1\",\"port\":9999,\"packet_hex\":\"aa\"},{\"sequence\":2,\"host\":\"127.0.0.1\",\"port\":9999,\"packet_hex\":\"aa\"}]}",
        "{\"schema_version\":1,\"commands\":[{\"sequence\":1,\"host\":\"127.0.0.1\",\"port\":9999,\"packet_hex\":\"aa\"}\"trailing\":true]}",
        "{\"schema_version\":1,\"bad\":[{\"sequence\":1,\"host\":\"127.0.0.1\",\"port\":9999,\"packet_hex\":\"aa\"}]}"
    };

    for (size_t index = 0; index < sizeof(bad_payloads) / sizeof(bad_payloads[0]); ++index)
    {
        char path[] = "/tmp/shire-scenario-bad-XXXXXX";
        int file = mkstemp(path);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, file);
        size_t length = strlen(bad_payloads[index]);
        TEST_ASSERT_EQUAL_INT((int)length, (int)write(file, bad_payloads[index], length));
        close(file);

        director_config_t config;
        memset(&config, 0, sizeof(config));
        snprintf(config.scenario_file, sizeof(config.scenario_file), "%s", path);
        TEST_ASSERT_EQUAL_INT(-1, initialize_scenario(&config));
        unlink(path);
    }

    char empty_path[] = "/tmp/shire-scenario-empty-XXXXXX";
    int empty_file = mkstemp(empty_path);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, empty_file);
    close(empty_file);
    director_config_t config;
    memset(&config, 0, sizeof(config));
    snprintf(config.scenario_file, sizeof(config.scenario_file), "%s", empty_path);
    TEST_ASSERT_EQUAL_INT(-1, initialize_scenario(&config));
    unlink(empty_path);

    char unterminated_path[] = "/tmp/shire-scenario-unterminated-XXXXXX";
    int unterminated_file = mkstemp(unterminated_path);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, unterminated_file);
    static const char unterminated[] =
        "{\"schema_version\":1,\"commands\":[{\"sequence\":1,"
        "\"host\":\"127.0.0.1\",\"port\":9999,\"packet_hex\":\"aa\"";
    TEST_ASSERT_EQUAL_INT((int)sizeof(unterminated) - 1,
                          (int)write(unterminated_file, unterminated,
                                     sizeof(unterminated) - 1));
    close(unterminated_file);
    memset(&config, 0, sizeof(config));
    snprintf(config.scenario_file, sizeof(config.scenario_file), "%s",
             unterminated_path);
    TEST_ASSERT_EQUAL_INT(-1, initialize_scenario(&config));
    unlink(unterminated_path);

    /* A pipe is readable through /proc but cannot be seeked like a scenario file. */
    int pipe_fds[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(pipe_fds));
    char pipe_path[64];
    snprintf(pipe_path, sizeof(pipe_path), "/proc/self/fd/%d", pipe_fds[0]);
    memset(&config, 0, sizeof(config));
    snprintf(config.scenario_file, sizeof(config.scenario_file), "%s", pipe_path);
    TEST_ASSERT_EQUAL_INT(-1, initialize_scenario(&config));
    close(pipe_fds[0]);
    close(pipe_fds[1]);
}

static void test_scenario_injection_reports_send_failure(void)
{
    director_config_t config;
    memset(&config, 0, sizeof(config));
    strcpy(config.scenario_file, "configured");
    config.scenario_socket = -1;
    config.scenario_command_count = 1;
    config.scenario_commands[0].sequence = 44;
    config.scenario_commands[0].packet_length = 1;

    TEST_ASSERT_EQUAL_INT(-1, director_inject_scenario_commands(&config, 44));
    TEST_ASSERT_EQUAL_UINT64(1, config.scenario_errors);
    TEST_ASSERT_EQUAL_INT(1, config.scenario_commands[0].injected);
}

static void test_component_loading_accepts_only_valid_plugins(void)
{
    director_config_t config;
    memset(&config, 0, sizeof(config));
    snprintf(config.components_dir, sizeof(config.components_dir), "%s",
             DIRECTOR_FIXTURE_DIR);

    TEST_ASSERT_EQUAL_INT(0, load_components(&config));
    TEST_ASSERT_EQUAL_INT(1, config.component_count);
    TEST_ASSERT_EQUAL_INT(1, config.lib_count);
    TEST_ASSERT_NOT_NULL(config.components[0].interface);
    TEST_ASSERT_EQUAL_STRING("director_fixture", config.components[0].interface->name);
    TEST_ASSERT_NOT_NULL(config.components[0].lib_handle);
    TEST_ASSERT_EQUAL_INT(1, config.components[0].active);

    cleanup_components(&config);
    TEST_ASSERT_EQUAL_INT(0, config.lib_count);
    TEST_ASSERT_EQUAL_INT(0, config.components[0].active);
}

static void test_component_worker_start_failures_are_cleaned_up(void)
{
    static const component_interface_t interface = {
        VALID_COMPONENT_API,
        .name = "worker-failure", .description = "worker failure",
        .create = fake_init, .wait_for_service = fake_wait_for_service,
        .service = fake_service, .destroy = fake_cleanup};

    g_director_config.component_count = 1;
    g_director_config.components[0].active = 1;
    g_director_config.components[0].interface = &interface;
    eventfd_failure = 1;
    TEST_ASSERT_EQUAL_INT(-1, initialize_components(&g_director_config));
    TEST_ASSERT_EQUAL_INT(0, g_director_config.threads_spawned);
    cleanup_components(&g_director_config);

    memset(&g_director_config, 0, sizeof(g_director_config));
    g_director_config.component_count = 2;
    for (int index = 0; index < 2; ++index)
    {
        g_director_config.components[index].active = 1;
        g_director_config.components[index].interface = &interface;
    }
    eventfd_failure = 0;
    thread_create_calls = 0;
    thread_create_failure_call = 2;
    TEST_ASSERT_EQUAL_INT(-1, initialize_components(&g_director_config));
    TEST_ASSERT_EQUAL_INT(0, g_director_config.threads_spawned);
    TEST_ASSERT_EQUAL_INT(-1, g_director_config.component_interrupt_fds[0]);
    TEST_ASSERT_EQUAL_INT(-1, g_director_config.component_interrupt_fds[1]);
    cleanup_components(&g_director_config);
}

static void test_component_sync_primitive_failures_are_reported(void)
{
    for (int injected_failure = 1; injected_failure <= 4; ++injected_failure)
    {
        memset(&g_director_config, 0, sizeof(g_director_config));
        sync_primitive_failure = injected_failure;
        int status = initialize_components(&g_director_config);
        sync_primitive_failure = 0;
        TEST_ASSERT_EQUAL_INT(-1, status);
        TEST_ASSERT_EQUAL_INT(0, g_director_config.worker_sync_initialized);
        cleanup_components(&g_director_config);
    }
}

static void test_component_loading_paths(void)
{
    director_config_t config;
    memset(&config, 0, sizeof(config));
    strcpy(config.components_dir, "/tmp/shire-components-do-not-exist");
    TEST_ASSERT_EQUAL_INT(0, load_components(&config));
    TEST_ASSERT_EQUAL_INT(0, config.component_count);

    char directory[] = "/tmp/shire-director-test-XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(directory));
    char invalid_library[512];
    snprintf(invalid_library, sizeof(invalid_library), "%s/invalid.so", directory);
    FILE *file = fopen(invalid_library, "w");
    TEST_ASSERT_NOT_NULL(file);
    fputs("not a shared library", file);
    fclose(file);
    strcpy(config.components_dir, directory);
    TEST_ASSERT_EQUAL_INT(0, load_components(&config));
    TEST_ASSERT_EQUAL_INT(0, config.component_count);
    unlink(invalid_library);
    rmdir(directory);
}

static void test_component_lifecycle_and_tick(void)
{
    static const component_interface_t interface = {
        VALID_COMPONENT_API,
        .name = "fake", .description = "test component", .create = fake_init,
        .on_tick = fake_tick, .service = NULL, .actuate = fake_actuate,
        .destroy = fake_cleanup, .backdoor = NULL};

    g_director_config.component_count = 1;
    g_director_config.components[0].active = 1;
    g_director_config.components[0].interface = &interface;
    g_director_config.enable_42 = 0;

    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));
    TEST_ASSERT_EQUAL_INT(0, g_director_config.threads_spawned);
    on_tick(1234);
    TEST_ASSERT_EQUAL_INT(1, fake_state.ticks);
    TEST_ASSERT_EQUAL_INT(1, fake_state.actuations);
    TEST_ASSERT_EQUAL_UINT64(1234, fake_tick_time);
    TEST_ASSERT_EQUAL_UINT64(1234, fake_actuate_time);
    TEST_ASSERT_EQUAL_INT(0, fake_context_valid);
    cleanup_components(&g_director_config);
    TEST_ASSERT_EQUAL_INT(1, fake_state.cleaned);
    TEST_ASSERT_EQUAL_INT(0, g_director_config.threads_spawned);
}

static void test_component_phase_boundaries_and_failures(void)
{
    static const component_interface_t interface = {
        VALID_COMPONENT_API,
        .name = "phased", .description = "phase test", .create = fake_init,
        .on_tick = fake_tick, .wait_for_service = fake_wait_for_service,
        .service = fake_service,
        .actuate = fake_actuate, .destroy = fake_cleanup, .backdoor = NULL};

    g_director_config.component_count = 1;
    g_director_config.components[0].active = 1;
    g_director_config.components[0].interface = &interface;
    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));

    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS,
                          director_prepare_tick(7, 1234));
    TEST_ASSERT_EQUAL_INT(0, fake_service_calls);
    TEST_ASSERT_EQUAL_INT(0, fake_state.actuations);
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          director_execute_tick(8, 1234));
    TEST_ASSERT_EQUAL_INT(0, fake_service_calls);

    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS,
                          director_execute_tick(7, 1234));
    for (int attempt = 0; attempt < 100 && fake_service_calls == 0; ++attempt)
    {
        struct timespec delay = {.tv_nsec = 1000000L};
        nanosleep(&delay, NULL);
    }
    TEST_ASSERT_GREATER_THAN_INT(0, fake_service_calls);
    TEST_ASSERT_EQUAL_INT(0, fake_state.actuations);

    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS,
                          director_commit_tick(7, 1234));
    TEST_ASSERT_EQUAL_INT(1, fake_state.actuations);
    cleanup_components(&g_director_config);

    memset(&g_director_config, 0, sizeof(g_director_config));
    g_director_config.component_count = 1;
    g_director_config.components[0].active = 1;
    g_director_config.components[0].interface = &interface;
    fail_tick = 1;
    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          director_prepare_tick(8, 1244));
    TEST_ASSERT_EQUAL_UINT64(1, g_director_config.component_phase_errors);
    cleanup_components(&g_director_config);

    memset(&g_director_config, 0, sizeof(g_director_config));
    g_director_config.component_count = 1;
    g_director_config.components[0].active = 1;
    g_director_config.components[0].interface = &interface;
    fail_tick = 0;
    fail_service = 1;
    fake_wait_ready = 1;
    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS,
                          director_prepare_tick(9, 1254));
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS,
                          director_execute_tick(9, 1254));
    pthread_mutex_lock(&g_director_config.tick_mutex);
    while (g_director_config.components[0].phase_status == COMPONENT_SUCCESS)
        pthread_cond_wait(&g_director_config.tick_cond,
                          &g_director_config.tick_mutex);
    pthread_mutex_unlock(&g_director_config.tick_mutex);
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          director_commit_tick(9, 1254));
    TEST_ASSERT_EQUAL_UINT64(1, g_director_config.component_service_errors);
    TEST_ASSERT_EQUAL_UINT64(1, g_director_config.component_phase_errors);
    TEST_ASSERT_EQUAL_INT(0, fake_state.actuations);
    cleanup_components(&g_director_config);

    memset(&g_director_config, 0, sizeof(g_director_config));
    g_director_config.component_count = 1;
    g_director_config.components[0].active = 1;
    g_director_config.components[0].interface = &interface;
    fail_service = 0;
    fail_wait = 1;
    fake_wait_ready = 0;
    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS,
                          director_prepare_tick(10, 1264));
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS,
                          director_execute_tick(10, 1264));
    pthread_mutex_lock(&g_director_config.tick_mutex);
    while (g_director_config.components[0].phase_status == COMPONENT_SUCCESS)
        pthread_cond_wait(&g_director_config.tick_cond,
                          &g_director_config.tick_mutex);
    pthread_mutex_unlock(&g_director_config.tick_mutex);
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          director_commit_tick(10, 1264));
    TEST_ASSERT_EQUAL_UINT64(1, g_director_config.component_service_errors);
    cleanup_components(&g_director_config);

    memset(&g_director_config, 0, sizeof(g_director_config));
    g_director_config.component_count = 1;
    g_director_config.components[0].active = 1;
    g_director_config.components[0].interface = &interface;
    fail_service = 0;
    fail_actuate = 1;
    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS,
                          director_prepare_tick(10, 1264));
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS,
                          director_execute_tick(10, 1264));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          director_commit_tick(10, 1264));
    TEST_ASSERT_EQUAL_UINT64(1, g_director_config.component_phase_errors);
    TEST_ASSERT_EQUAL_INT(1, fake_state.actuations);
    cleanup_components(&g_director_config);
}

static void test_commit_waits_for_active_service_callback(void)
{
    static const component_interface_t interface = {
        VALID_COMPONENT_API,
        .name = "blocking-service", .description = "commit quiescence test",
        .create = fake_init, .on_tick = fake_tick,
        .wait_for_service = fake_wait_for_service, .service = fake_service,
        .actuate = fake_actuate, .destroy = fake_cleanup, .backdoor = NULL};

    g_director_config.component_count = 1;
    g_director_config.components[0].active = 1;
    g_director_config.components[0].interface = &interface;
    block_service = 1;
    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, director_prepare_tick(11, 1274));
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, director_execute_tick(11, 1274));

    pthread_mutex_lock(&service_test_mutex);
    while (!service_entered)
        pthread_cond_wait(&service_test_condition, &service_test_mutex);
    pthread_mutex_unlock(&service_test_mutex);

    commit_call_t call = {.sequence = 11, .time_ns = 1274, .result = -1};
    pthread_t commit_thread;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&commit_thread, NULL, call_commit, &call));
    struct timespec delay = {.tv_nsec = 5000000L};
    nanosleep(&delay, NULL);
    TEST_ASSERT_EQUAL_INT(0, fake_state.actuations);

    pthread_mutex_lock(&service_test_mutex);
    release_service = 1;
    pthread_cond_broadcast(&service_test_condition);
    pthread_mutex_unlock(&service_test_mutex);
    TEST_ASSERT_EQUAL_INT(0, pthread_join(commit_thread, NULL));
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, call.result);
    TEST_ASSERT_EQUAL_INT(1, fake_state.actuations);
    cleanup_components(&g_director_config);
}

static void test_component_optional_interface_paths(void)
{
    static const component_interface_t optional_phases = {
        VALID_COMPONENT_API,
        .name = "no-tick", .description = "no tick", .create = fake_init,
        .on_tick = NULL, .service = NULL, .actuate = NULL,
        .destroy = fake_cleanup, .backdoor = NULL};

    g_director_config.component_count = 1;
    g_director_config.components[0].active = 1;
    g_director_config.components[0].interface = &optional_phases;

    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));
    TEST_ASSERT_EQUAL_INT(0, g_director_config.threads_spawned);
    on_tick(4321);
    TEST_ASSERT_EQUAL_INT(0, fake_state.ticks);
    cleanup_components(&g_director_config);
    TEST_ASSERT_EQUAL_INT(0, g_director_config.threads_spawned);

    g_director_config.component_count = 1;
    g_director_config.components[0].active = 1;
    g_director_config.components[0].interface = &optional_phases;
    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));
    g_director_config.components[0].state = NULL;
    on_tick(4322);
    cleanup_components(&g_director_config);

    static const component_interface_t missing_create = {
        VALID_COMPONENT_API,
        .name = "missing-create", .description = "invalid",
        .create = NULL, .destroy = fake_cleanup};
    memset(&g_director_config, 0, sizeof(g_director_config));
    g_director_config.component_count = 1;
    g_director_config.components[0].active = 1;
    g_director_config.components[0].interface = &missing_create;
    TEST_ASSERT_EQUAL_INT(-1, initialize_components(&g_director_config));
    TEST_ASSERT_EQUAL_INT(0, g_director_config.components[0].active);

    static const component_interface_t missing_destroy = {
        VALID_COMPONENT_API,
        .name = "missing-destroy", .description = "invalid",
        .create = fake_init, .destroy = NULL};
    memset(&g_director_config, 0, sizeof(g_director_config));
    g_director_config.component_count = 1;
    g_director_config.components[0].active = 1;
    g_director_config.components[0].interface = &missing_destroy;
    TEST_ASSERT_EQUAL_INT(-1, initialize_components(&g_director_config));
    TEST_ASSERT_EQUAL_INT(0, g_director_config.components[0].active);

    static const component_interface_t missing_wait = {
        VALID_COMPONENT_API,
        .name = "missing-wait", .description = "invalid",
        .create = fake_init, .service = fake_service, .destroy = fake_cleanup};
    memset(&g_director_config, 0, sizeof(g_director_config));
    g_director_config.component_count = 1;
    g_director_config.components[0].active = 1;
    g_director_config.components[0].interface = &missing_wait;
    TEST_ASSERT_EQUAL_INT(-1, initialize_components(&g_director_config));
    TEST_ASSERT_EQUAL_INT(0, g_director_config.components[0].active);
}

static void test_initialization_failures_and_disabled_42(void)
{
    static const component_interface_t interface = {
        VALID_COMPONENT_API,
        .name = "failure", .description = "failure", .create = failing_init,
        .on_tick = NULL, .service = NULL, .actuate = NULL,
        .destroy = fake_cleanup, .backdoor = NULL};
    director_config_t config;
    memset(&config, 0, sizeof(config));
    config.component_count = 1;
    config.components[0].active = 1;
    config.components[0].interface = &interface;
    TEST_ASSERT_EQUAL_INT(-1, initialize_components(&config));
    TEST_ASSERT_EQUAL_INT(0, config.components[0].active);

    static const component_interface_t partial_interface = {
        VALID_COMPONENT_API,
        .name = "partial-failure", .description = "partial failure",
        .create = partial_failing_init, .destroy = fake_cleanup};
    memset(&config, 0, sizeof(config));
    config.component_count = 1;
    config.components[0].active = 1;
    config.components[0].interface = &partial_interface;
    TEST_ASSERT_EQUAL_INT(-1, initialize_components(&config));
    TEST_ASSERT_EQUAL_INT(1, fake_state.cleaned);
    TEST_ASSERT_NULL(config.components[0].state);

    memset(&config, 0, sizeof(config));
    TEST_ASSERT_EQUAL_INT(0, initialize_42(&config));
    config.enable_42 = 1;
    setenv("FORTYTWO_SOCKET_PATH", "/tmp/shire-no-42.sock", 1);
    TEST_ASSERT_EQUAL_INT(-1, initialize_42(&config));
    TEST_ASSERT_EQUAL_INT(0, config.enable_42);

    config.enable_42 = 1;
    unsetenv("FORTYTWO_SOCKET_PATH");
    setenv("FORTYTWO_HOST", "127.0.0.1", 1);
    setenv("FORTYTWO_PORT", "1", 1);
    TEST_ASSERT_EQUAL_INT(-1, initialize_42(&config));
    TEST_ASSERT_EQUAL_INT(0, config.enable_42);
}

static void test_telemetry_serialization(void)
{
    simulith_42_context_t context;
    memset(&context, 0, sizeof(context));
    context.dyn_time = 1.25;
    context.pos_n[0] = 2.5;
    context.sun_vector_body[2] = 3.75;
    context.mag_field_body[1] = 4.5;
    context.hvb[2] = 5.25;
    context.wn[0] = 6.5;
    context.qn[3] = 7.25;
    context.mass = 8.5;
    context.cm[2] = 9.25;
    context.inertia[2][2] = 10.5;
    context.eclipse = 1;
    context.atmo_density = 11.25;

    uint8_t packet[SIMULITH_42_TELEMETRY_SIZE];
    TEST_ASSERT_EQUAL_size_t(0, simulith_serialize_42_telemetry(NULL, packet, sizeof(packet)));
    TEST_ASSERT_EQUAL_size_t(0, simulith_serialize_42_telemetry(&context, NULL, sizeof(packet)));
    TEST_ASSERT_EQUAL_size_t(0, simulith_serialize_42_telemetry(&context, packet, sizeof(packet) - 1));
    TEST_ASSERT_EQUAL_size_t(sizeof(packet),
                             simulith_serialize_42_telemetry(&context, packet, sizeof(packet)));

    double value;
    memcpy(&value, packet, sizeof(value));
    TEST_ASSERT_TRUE(value == context.dyn_time);
    memcpy(&value, packet + sizeof(double), sizeof(value));
    TEST_ASSERT_TRUE(value == context.pos_n[0]);
    memcpy(&value, packet + sizeof(packet) - sizeof(double), sizeof(value));
    TEST_ASSERT_TRUE(value == context.atmo_density);
}

static void test_live_42_tick_and_telemetry(void)
{
    uint16_t port = 0;
    director_42_server_t server = {
        .listen_fd = open_loopback_listener(&port),
        .exchanges = UDP_PUBLISH_INTERVAL_TICKS,
    };
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, server.listen_fd);
    pthread_t server_thread;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&server_thread, NULL, director_42_server, &server));

    char port_string[16];
    snprintf(port_string, sizeof(port_string), "%u", port);
    setenv("FORTYTWO_HOST", "127.0.0.1", 1);
    setenv("FORTYTWO_PORT", port_string, 1);
    g_director_config.enable_42 = 1;
    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));
    TEST_ASSERT_EQUAL_INT(0, initialize_telemetry());
    TEST_ASSERT_EQUAL_INT(0, initialize_42(&g_director_config));
    TEST_ASSERT_EQUAL_INT(1, g_director_config.fortytwo_initialized);

    double torque[4] = {0.25, 0.0, 0.0, 0.0};
    TEST_ASSERT_EQUAL_INT(0, simulith_42_send_wheel_command(0, torque, 1));
    for (int i = 0; i < UDP_PUBLISH_INTERVAL_TICKS; i++)
        on_tick((uint64_t)i * 1000000U);

    cleanup_components(&g_director_config);
    TEST_ASSERT_EQUAL_INT(0, pthread_join(server_thread, NULL));
    TEST_ASSERT_EQUAL_INT(UDP_PUBLISH_INTERVAL_TICKS, server.commands_seen);
    close(server.listen_fd);
}

static void test_tick_handles_42_state_failure(void)
{
    g_director_config.enable_42 = 1;
    g_director_config.fortytwo_initialized = 1;
    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));
    on_tick(123);
    TEST_ASSERT_EQUAL_INT(0, g_director_config.shared_context_42.valid);
    g_director_config.enable_42 = 0;
    g_director_config.fortytwo_initialized = 0;
    cleanup_components(&g_director_config);
}

static void test_backdoor_rejects_malformed_and_dispatches_valid_packet(void)
{
    static const component_interface_t interface = {
        VALID_COMPONENT_API,
        .name = "fake", .description = "test component", .create = fake_init,
        .on_tick = fake_tick, .destroy = fake_cleanup, .backdoor = fake_backdoor};

    g_director_config.component_count = 1;
    g_director_config.components[0].active = 1;
    g_director_config.components[0].interface = &interface;
    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));

    /* The first tick creates the non-blocking listener. */
    on_tick(1);

    static const uint8_t too_short[] = {'B', 'A', 'C', 'K'};
    send_backdoor_datagram(too_short, sizeof(too_short));
    on_tick(2);

    static const uint8_t wrong_magic[] = {
        'N', 'O', 'T', 'D', 'O', 'O', 'R', '!', 4, 'f', 'a', 'k', 'e', 0, 1, 0, 0};
    send_backdoor_datagram(wrong_magic, sizeof(wrong_magic));
    on_tick(3);

    static const uint8_t zero_target[] = {
        'B', 'A', 'C', 'K', 'D', 'O', 'O', 'R', 0, 0, 1, 0, 0};
    send_backdoor_datagram(zero_target, sizeof(zero_target));
    on_tick(4);

    uint8_t oversized_target[8 + 1 + 65 + 2 + 2] = {
        'B', 'A', 'C', 'K', 'D', 'O', 'O', 'R', 65};
    send_backdoor_datagram(oversized_target, sizeof(oversized_target));
    on_tick(5);

    static const uint8_t truncated_target[] = {
        'B', 'A', 'C', 'K', 'D', 'O', 'O', 'R', 4, 'f'};
    send_backdoor_datagram(truncated_target, sizeof(truncated_target));
    on_tick(6);

    static const uint8_t truncated_payload[] = {
        'B', 'A', 'C', 'K', 'D', 'O', 'O', 'R', 4, 'f', 'a', 'k', 'e',
        0x12, 0x34, 0, 3, 0xaa};
    send_backdoor_datagram(truncated_payload, sizeof(truncated_payload));
    on_tick(7);
    TEST_ASSERT_EQUAL_INT(0, fake_backdoor_calls);

    static const uint8_t valid[] = {
        'B', 'A', 'C', 'K', 'D', 'O', 'O', 'R', 4, 'f', 'a', 'k', 'e',
        0x12, 0x34, 0, 3, 0xaa, 0xbb, 0xcc};
    /* Walk every optional component field before reaching the valid target. */
    g_director_config.components[0].active = 0;
    send_backdoor_datagram(valid, sizeof(valid));
    on_tick(8);
    g_director_config.components[0].active = 1;
    g_director_config.components[0].interface = NULL;
    send_backdoor_datagram(valid, sizeof(valid));
    on_tick(9);
    static const component_interface_t unnamed = {
        .name = NULL, .description = "unnamed", .create = NULL,
        .on_tick = NULL, .destroy = NULL, .backdoor = NULL};
    g_director_config.components[0].interface = &unnamed;
    send_backdoor_datagram(valid, sizeof(valid));
    on_tick(10);
    static const component_interface_t other = {
        .name = "other", .description = "other", .create = NULL,
        .on_tick = NULL, .destroy = NULL, .backdoor = NULL};
    g_director_config.components[0].interface = &other;
    send_backdoor_datagram(valid, sizeof(valid));
    on_tick(11);
    static const component_interface_t no_backdoor = {
        .name = "fake", .description = "no backdoor", .create = NULL,
        .on_tick = NULL, .destroy = NULL, .backdoor = NULL};
    g_director_config.components[0].interface = &no_backdoor;
    send_backdoor_datagram(valid, sizeof(valid));
    on_tick(12);
    g_director_config.components[0].interface = &interface;
    send_backdoor_datagram(valid, sizeof(valid));
    on_tick(13);

    TEST_ASSERT_EQUAL_INT(1, fake_backdoor_calls);
    TEST_ASSERT_EQUAL_HEX16(0x1234, fake_backdoor_command);
    TEST_ASSERT_EQUAL_size_t(3, fake_backdoor_payload_length);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&valid[17], fake_backdoor_payload, 3);
    cleanup_components(&g_director_config);
}

static void test_telemetry_initialization(void)
{
    TEST_ASSERT_EQUAL_INT(0, initialize_telemetry());
    cleanup_components(&g_director_config);

    unsetenv("SIMULITH_GSW_HOST");
    TEST_ASSERT_EQUAL_INT(0, initialize_telemetry());
    cleanup_components(&g_director_config);

    setenv("SIMULITH_GSW_HOST", "256.256.256.256", 1);
    TEST_ASSERT_EQUAL_INT(0, initialize_telemetry());
    cleanup_components(&g_director_config);
}

static void test_tick_command_failure_and_boundary_paths(void)
{
    simulith_42_command_t command = {
        .type = SIMULITH_42_CMD_WHEEL_TORQUE,
        .valid = 1,
        .cmd.wheel.enable_mask = 1,
    };

    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));

    /* Enabled but not initialized covers the second half of both guards. */
    g_director_config.enable_42 = 1;
    g_director_config.fortytwo_initialized = 0;
    on_tick(1);

    /* A disconnected client makes batch transmission fail deterministically. */
    g_director_config.fortytwo_initialized = 1;
    g_director_config.verbose = 0;
    TEST_ASSERT_EQUAL_INT(0, enqueue_command(&command));
    on_tick(2);
    g_director_config.verbose = 1;
    TEST_ASSERT_EQUAL_INT(0, enqueue_command(&command));
    on_tick(3);

    /* Two commands remain queued because failed PREPARE never commits partial
     * output. Fill the remaining capacity, then verify overflow accounting. */
    for (int i = 0; i < SIMULITH_42_CMD_QUEUE_SIZE - 2; ++i)
        TEST_ASSERT_EQUAL_INT(0, enqueue_command(&command));
    TEST_ASSERT_EQUAL_INT(-1, enqueue_command(&command));
    on_tick(4);

    g_director_config.fortytwo_initialized = 0;
    cleanup_components(&g_director_config);
}

static void test_commit_reports_42_and_scenario_failures(void)
{
    simulith_42_command_t command = {
        .type = SIMULITH_42_CMD_WHEEL_TORQUE,
        .valid = 1,
        .cmd.wheel.enable_mask = 1,
    };
    simulith_42_command_t discarded;
    while (dequeue_command(&discarded) == 0)
        ;

    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));
    g_director_config.enable_42 = 1;
    g_director_config.fortytwo_initialized = 1;
    g_director_config.verbose = 1;
    g_director_config.shared_tick_sequence = 51;
    g_director_config.shared_tick_time_ns = 5100;
    TEST_ASSERT_EQUAL_INT(0, enqueue_command(&command));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, director_commit_tick(51, 5100));
    g_director_config.fortytwo_initialized = 0;
    cleanup_components(&g_director_config);

    memset(&g_director_config, 0, sizeof(g_director_config));
    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));
    g_director_config.enable_42 = 1;
    g_director_config.fortytwo_initialized = 0;
    g_director_config.shared_tick_sequence = 52;
    g_director_config.shared_tick_time_ns = 5200;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, director_commit_tick(52, 5200));
    cleanup_components(&g_director_config);

    memset(&g_director_config, 0, sizeof(g_director_config));
    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));
    g_director_config.enable_42 = 1;
    g_director_config.fortytwo_initialized = 1;
    g_director_config.shared_tick_sequence = 54;
    g_director_config.shared_tick_time_ns = 5400;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, director_commit_tick(54, 5400));
    g_director_config.fortytwo_initialized = 0;
    cleanup_components(&g_director_config);

    memset(&g_director_config, 0, sizeof(g_director_config));
    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));
    strcpy(g_director_config.scenario_file, "configured");
    g_director_config.scenario_socket = -1;
    g_director_config.scenario_command_count = 1;
    g_director_config.scenario_commands[0].sequence = 53;
    g_director_config.scenario_commands[0].packet_length = 1;
    g_director_config.shared_tick_sequence = 53;
    g_director_config.shared_tick_time_ns = 5300;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, director_commit_tick(53, 5300));
    cleanup_components(&g_director_config);
}

static void test_director_identity_validations_and_terminal_metrics(void)
{
    g_director_config.enable_42 = 0;
    g_director_config.shared_tick_sequence = 7;
    g_director_config.shared_tick_time_ns = 1234;
    TEST_ASSERT_EQUAL_INT(-1, director_execute_tick(8, 1235));
    TEST_ASSERT_EQUAL_INT(-1, director_commit_tick(8, 1235));

    g_director_config.shared_context_42.valid = 1;
    g_director_config.shared_context_42.sim_time = 9.5;
    g_director_config.shared_context_42.dyn_time = 9.5;
    g_director_config.shared_context_42.qn[0] = 1.0;
    g_director_config.shared_context_42.wn[2] = 2.0;
    g_director_config.shared_context_42.pos_n[1] = 3.0;
    g_director_config.shared_context_42.vel_n[2] = 4.0;
    g_director_config.component_service_errors = 1;
    g_director_config.component_phase_errors = 2;
    g_director_config.scenario_digest = 0xfeedface;
    g_director_config.scenario_command_count = 2;
    g_director_config.scenario_injected = 1;
    g_director_config.scenario_errors = 0;
    director_write_terminal_metrics();

    memset(&g_director_config, 0, sizeof(g_director_config));
    g_director_config.component_count = 1;
    g_director_config.components[0].active = 1;
    g_director_config.components[0].interface = &((const component_interface_t){
        VALID_COMPONENT_API,
        .name = "metrics", .description = "terminal metrics",
        .create = fake_init, .on_tick = fake_tick,
        .actuate = fake_actuate, .destroy = fake_cleanup,
        .backdoor = NULL,
    });
    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));
    TEST_ASSERT_EQUAL_INT(0, director_prepare_tick(9, 9000));
    TEST_ASSERT_EQUAL_INT(0, director_execute_tick(9, 9000));
    TEST_ASSERT_EQUAL_INT(0, director_commit_tick(9, 9000));
    cleanup_components(&g_director_config);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_args);
    RUN_TEST(test_scenario_validation_and_one_shot_injection);
    RUN_TEST(test_scenario_parser_rejects_malformed_payloads);
    RUN_TEST(test_scenario_injection_reports_send_failure);
    RUN_TEST(test_component_loading_paths);
    RUN_TEST(test_component_loading_accepts_only_valid_plugins);
    RUN_TEST(test_component_worker_start_failures_are_cleaned_up);
    RUN_TEST(test_component_sync_primitive_failures_are_reported);
    RUN_TEST(test_component_lifecycle_and_tick);
    RUN_TEST(test_component_phase_boundaries_and_failures);
    RUN_TEST(test_commit_waits_for_active_service_callback);
    RUN_TEST(test_component_optional_interface_paths);
    RUN_TEST(test_initialization_failures_and_disabled_42);
    RUN_TEST(test_backdoor_rejects_malformed_and_dispatches_valid_packet);
    RUN_TEST(test_telemetry_initialization);
    RUN_TEST(test_telemetry_serialization);
    RUN_TEST(test_live_42_tick_and_telemetry);
    RUN_TEST(test_tick_handles_42_state_failure);
    RUN_TEST(test_tick_command_failure_and_boundary_paths);
    RUN_TEST(test_commit_reports_42_and_scenario_failures);
    RUN_TEST(test_director_identity_validations_and_terminal_metrics);
    return UNITY_END();
}
