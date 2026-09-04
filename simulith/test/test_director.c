#include "simulith_director.h"
#include "unity.h"

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#ifndef DIRECTOR_FIXTURE_DIR
#error "DIRECTOR_FIXTURE_DIR must identify the test plugin directory"
#endif

struct component_state
{
    int ticks;
    int cleaned;
};

static struct component_state fake_state;
static uint64_t               fake_tick_time;
static int                    fake_context_valid;
static int                    fake_backdoor_calls;
static uint16_t               fake_backdoor_command;
static uint8_t                fake_backdoor_payload[16];
static size_t                 fake_backdoor_payload_length;

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
        "Orb[0].PosN = [2 3 4]\n";
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

static void fake_tick(component_state_t *state, uint64_t time_ns, const simulith_42_context_t *context)
{
    struct component_state *value = (struct component_state *)state;
    value->ticks++;
    fake_tick_time = time_ns;
    fake_context_valid = context->valid;
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
    fake_context_valid = -1;
    fake_backdoor_calls = 0;
    fake_backdoor_command = 0;
    fake_backdoor_payload_length = 0;
    memset(fake_backdoor_payload, 0, sizeof(fake_backdoor_payload));
    unsetenv("FORTYTWO_SOCKET_PATH");
    unsetenv("FORTYTWO_HOST");
    unsetenv("FORTYTWO_PORT");
    setenv("SIMULITH_42_RECONNECT_ATTEMPTS", "1", 1);
    setenv("SIMULITH_42_RECONNECT_DELAY_MS", "0", 1);
    setenv("SIMULITH_GSW_HOST", "127.0.0.1", 1);
}

void tearDown(void)
{
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

    char *help[] = {"director", "--help"};
    TEST_ASSERT_EQUAL_INT(-1, parse_args(2, help, &config));
}

static void test_component_loading_accepts_only_valid_plugins(void)
{
    director_config_t config;
    memset(&config, 0, sizeof(config));
    snprintf(config.components_dir, sizeof(config.components_dir), "%s",
             DIRECTOR_FIXTURE_DIR);

    TEST_ASSERT_EQUAL_INT(0, load_components(&config));
    TEST_ASSERT_EQUAL_INT(1, config.component_count);
    TEST_ASSERT_EQUAL_INT(3, config.lib_count);
    TEST_ASSERT_NOT_NULL(config.components[0].interface);
    TEST_ASSERT_EQUAL_STRING("director_fixture", config.components[0].interface->name);
    TEST_ASSERT_NOT_NULL(config.components[0].lib_handle);
    TEST_ASSERT_EQUAL_INT(1, config.components[0].active);

    cleanup_components(&config);
    TEST_ASSERT_EQUAL_INT(0, config.lib_count);
    TEST_ASSERT_EQUAL_INT(0, config.components[0].active);
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
        .name = "fake", .description = "test component", .init = fake_init,
        .tick = fake_tick, .cleanup = fake_cleanup, .backdoor = NULL};

    g_director_config.component_count = 1;
    g_director_config.components[0].active = 1;
    g_director_config.components[0].interface = &interface;
    g_director_config.enable_42 = 0;

    TEST_ASSERT_EQUAL_INT(0, initialize_components(&g_director_config));
    TEST_ASSERT_EQUAL_INT(1, g_director_config.threads_spawned);
    on_tick(1234);
    TEST_ASSERT_EQUAL_INT(1, fake_state.ticks);
    TEST_ASSERT_EQUAL_UINT64(1234, fake_tick_time);
    TEST_ASSERT_EQUAL_INT(0, fake_context_valid);
    cleanup_components(&g_director_config);
    TEST_ASSERT_EQUAL_INT(1, fake_state.cleaned);
    TEST_ASSERT_EQUAL_INT(0, g_director_config.threads_spawned);
}

static void test_initialization_failures_and_disabled_42(void)
{
    static const component_interface_t interface = {
        .name = "failure", .description = "failure", .init = failing_init,
        .tick = NULL, .cleanup = NULL, .backdoor = NULL};
    director_config_t config;
    memset(&config, 0, sizeof(config));
    config.component_count = 1;
    config.components[0].active = 1;
    config.components[0].interface = &interface;
    TEST_ASSERT_EQUAL_INT(-1, initialize_components(&config));
    TEST_ASSERT_EQUAL_INT(0, config.components[0].active);

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
        .name = "fake", .description = "test component", .init = fake_init,
        .tick = fake_tick, .cleanup = fake_cleanup, .backdoor = fake_backdoor};

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

    static const uint8_t truncated_payload[] = {
        'B', 'A', 'C', 'K', 'D', 'O', 'O', 'R', 4, 'f', 'a', 'k', 'e',
        0x12, 0x34, 0, 3, 0xaa};
    send_backdoor_datagram(truncated_payload, sizeof(truncated_payload));
    on_tick(4);
    TEST_ASSERT_EQUAL_INT(0, fake_backdoor_calls);

    static const uint8_t valid[] = {
        'B', 'A', 'C', 'K', 'D', 'O', 'O', 'R', 4, 'f', 'a', 'k', 'e',
        0x12, 0x34, 0, 3, 0xaa, 0xbb, 0xcc};
    send_backdoor_datagram(valid, sizeof(valid));
    on_tick(5);

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
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_args);
    RUN_TEST(test_component_loading_paths);
    RUN_TEST(test_component_loading_accepts_only_valid_plugins);
    RUN_TEST(test_component_lifecycle_and_tick);
    RUN_TEST(test_initialization_failures_and_disabled_42);
    RUN_TEST(test_backdoor_rejects_malformed_and_dispatches_valid_packet);
    RUN_TEST(test_telemetry_initialization);
    RUN_TEST(test_telemetry_serialization);
    RUN_TEST(test_live_42_tick_and_telemetry);
    RUN_TEST(test_tick_handles_42_state_failure);
    return UNITY_END();
}
