#include "simulith.h"
#include "unity.h"
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t server_pid = -1;
static int server_input = -1;
static int backdoor_socket = -1;
static void *context;
static void *subscriber;
static void *requester;

void setUp(void)
{
    unsetenv("SIMULITH_PUB_ADDR");
    unsetenv("SIMULITH_REP_ADDR");
    unsetenv("SIMULITH_PUB_BIND");
    unsetenv("SIMULITH_REP_BIND");
    unsetenv("SIMULITH_DEVICE_TRANSPORT");
    unsetenv("SIMULITH_DEVICE_HOST");
    unsetenv("SIMULITH_DEVICE_BIND");
}

void tearDown(void)
{
    if (server_input >= 0) close(server_input);
    if (backdoor_socket >= 0) close(backdoor_socket);
    if (server_pid > 0) { kill(server_pid, SIGTERM); waitpid(server_pid, NULL, 0); }
    if (subscriber) zmq_close(subscriber);
    if (requester) zmq_close(requester);
    if (context) zmq_ctx_term(context);
    server_input = -1;
    backdoor_socket = -1;
    server_pid = -1;
    subscriber = requester = context = NULL;
    unsetenv("SIMULITH_START_PAUSED");
    setUp();
}

static void test_sync_defaults_and_network_endpoints(void)
{
    TEST_ASSERT_EQUAL_STRING(LOCAL_PUB_ADDR, simulith_pub_address(0));
    TEST_ASSERT_EQUAL_STRING(LOCAL_REP_ADDR, simulith_rep_address(1));
    setenv("SIMULITH_PUB_ADDR", "tcp://server:50000", 1);
    setenv("SIMULITH_PUB_BIND", "tcp://0.0.0.0:50000", 1);
    setenv("SIMULITH_REP_ADDR", "tcp://server:50001", 1);
    setenv("SIMULITH_REP_BIND", "tcp://0.0.0.0:50001", 1);
    TEST_ASSERT_EQUAL_STRING("tcp://server:50000", simulith_pub_address(0));
    TEST_ASSERT_EQUAL_STRING("tcp://0.0.0.0:50000", simulith_pub_address(1));
    TEST_ASSERT_EQUAL_STRING("tcp://server:50001", simulith_rep_address(0));
    TEST_ASSERT_EQUAL_STRING("tcp://0.0.0.0:50001", simulith_rep_address(1));
    setenv("SIMULITH_PUB_ADDR", "", 1);
    TEST_ASSERT_EQUAL_STRING(LOCAL_PUB_ADDR, simulith_pub_address(0));
}

static void test_device_endpoints_preserve_local_and_explicit_addresses(void)
{
    char address[128];
    const char *logical = "ipc:///tmp/simulith_pub:51005";
    TEST_ASSERT_EQUAL_INT(0, simulith_transport_resolve_address(logical, 0, address, sizeof(address)));
    TEST_ASSERT_EQUAL_STRING(logical, address);
    setenv("SIMULITH_DEVICE_TRANSPORT", "", 1);
    TEST_ASSERT_EQUAL_INT(0, simulith_transport_resolve_address(logical, 0, address, sizeof(address)));
    TEST_ASSERT_EQUAL_STRING(logical, address);
    setenv("SIMULITH_DEVICE_TRANSPORT", "ipc", 1);
    TEST_ASSERT_EQUAL_INT(0, simulith_transport_resolve_address(logical, 0, address, sizeof(address)));
    TEST_ASSERT_EQUAL_STRING(logical, address);
    setenv("SIMULITH_DEVICE_TRANSPORT", "tcp", 1);
    TEST_ASSERT_EQUAL_INT(0, simulith_transport_resolve_address("tcp://peer:5000", 0, address, sizeof(address)));
    TEST_ASSERT_EQUAL_STRING("tcp://peer:5000", address);
}

static void test_remote_devices_bind_and_connect_to_distinct_hosts(void)
{
    char address[128];
    const char *logical = "ipc:///tmp/simulith_pub:53008";
    setenv("SIMULITH_DEVICE_TRANSPORT", "tcp", 1);
    TEST_ASSERT_EQUAL_INT(-1, simulith_transport_resolve_address(logical, 0, address, sizeof(address)));
    setenv("SIMULITH_DEVICE_HOST", "shire-director", 1);
    TEST_ASSERT_EQUAL_INT(0, simulith_transport_resolve_address(logical, 0, address, sizeof(address)));
    TEST_ASSERT_EQUAL_STRING("tcp://shire-director:53008", address);
    TEST_ASSERT_EQUAL_INT(0, simulith_transport_resolve_address(logical, 1, address, sizeof(address)));
    TEST_ASSERT_EQUAL_STRING("tcp://0.0.0.0:53008", address);
    setenv("SIMULITH_DEVICE_BIND", "", 1);
    TEST_ASSERT_EQUAL_INT(0, simulith_transport_resolve_address(logical, 1, address, sizeof(address)));
    TEST_ASSERT_EQUAL_STRING("tcp://0.0.0.0:53008", address);
    setenv("SIMULITH_DEVICE_BIND", "127.0.0.1", 1);
    TEST_ASSERT_EQUAL_INT(0, simulith_transport_resolve_address(logical, 1, address, sizeof(address)));
    TEST_ASSERT_EQUAL_STRING("tcp://127.0.0.1:53008", address);
}

static void test_invalid_network_configuration_fails_without_ipc_fallback(void)
{
    char address[128];
    setenv("SIMULITH_DEVICE_TRANSPORT", "invalid", 1);
    TEST_ASSERT_EQUAL_INT(-1, simulith_transport_resolve_address("ipc:///tmp/simulith_pub:51005", 0, address, sizeof(address)));
    setenv("SIMULITH_DEVICE_TRANSPORT", "tcp", 1);
    setenv("SIMULITH_DEVICE_HOST", "peer/path", 1);
    TEST_ASSERT_EQUAL_INT(-1, simulith_transport_resolve_address("ipc:///tmp/simulith_pub:51005", 0, address, sizeof(address)));
    setenv("SIMULITH_DEVICE_HOST", "peer", 1);
    TEST_ASSERT_EQUAL_INT(-1, simulith_transport_resolve_address("ipc:///tmp/simulith_pub:65536", 0, address, sizeof(address)));
    TEST_ASSERT_EQUAL_INT(-1, simulith_transport_resolve_address("ipc:///tmp/simulith_pub:51005junk", 0, address, sizeof(address)));
    TEST_ASSERT_EQUAL_INT(-1, simulith_transport_resolve_address("ipc:///tmp/simulith_pub:51005", 0, address, 4));
    TEST_ASSERT_EQUAL_INT(-1, simulith_transport_resolve_address(NULL, 0, address, sizeof(address)));
}

static void test_device_endpoint_rejects_bad_buffers_and_empty_client_host(void)
{
    char address[128];
    const char *logical = "ipc:///tmp/simulith_pub:51005";
    TEST_ASSERT_EQUAL_INT(-1, simulith_transport_resolve_address(logical, 0, NULL, sizeof(address)));
    TEST_ASSERT_EQUAL_INT(-1, simulith_transport_resolve_address(logical, 0, address, 0));
    setenv("SIMULITH_DEVICE_TRANSPORT", "tcp", 1);
    setenv("SIMULITH_DEVICE_HOST", "", 1);
    TEST_ASSERT_EQUAL_INT(-1, simulith_transport_resolve_address(logical, 0, address, sizeof(address)));
    transport_port_t port = {0};
    strcpy(port.address, logical);
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR, simulith_transport_init(&port));
    TEST_ASSERT_NULL(port.zmq_ctx);
    TEST_ASSERT_NULL(port.zmq_sock);
    TEST_ASSERT_EQUAL_STRING(logical, port.address);
}

static void test_device_endpoint_port_boundaries_and_host_characters(void)
{
    char address[128];
    setenv("SIMULITH_DEVICE_TRANSPORT", "tcp", 1);
    setenv("SIMULITH_DEVICE_HOST", "node_1.example-host", 1);
    TEST_ASSERT_EQUAL_INT(0, simulith_transport_resolve_address("ipc:///tmp/simulith_pub:1", 0, address, sizeof(address)));
    TEST_ASSERT_EQUAL_STRING("tcp://node_1.example-host:1", address);
    TEST_ASSERT_EQUAL_INT(0, simulith_transport_resolve_address("ipc:///tmp/simulith_pub:65535", 0, address, sizeof(address)));
    TEST_ASSERT_EQUAL_STRING("tcp://node_1.example-host:65535", address);
    const char *invalid[] = {"ipc:///tmp/simulith_pub:", "ipc:///tmp/simulith_pub:0",
        "ipc:///tmp/simulith_pub:-1", "ipc:///tmp/simulith_pub:65536",
        "ipc:///tmp/simulith_pub:not-a-port", "ipc:///tmp/simulith_pub:51005junk"};
    for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index)
        TEST_ASSERT_EQUAL_INT(-1, simulith_transport_resolve_address(invalid[index], 0, address, sizeof(address)));
    setenv("SIMULITH_DEVICE_HOST", "bad host", 1);
    TEST_ASSERT_EQUAL_INT(-1, simulith_transport_resolve_address("ipc:///tmp/simulith_pub:1", 0, address, sizeof(address)));
}

static void send_ground_step(int malformed)
{
    /* BACKDOOR / shire_server / STEP / payload length / optional bad payload. */
    const uint8_t frame[] = {'B','A','C','K','D','O','O','R',12,
        's','h','i','r','e','_','s','e','r','v','e','r',0,4,0,(uint8_t)malformed,0};
    struct sockaddr_in destination = {.sin_family = AF_INET, .sin_port = htons(50061)};
    destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    size_t length = sizeof(frame) - (malformed ? 0U : 1U);
    TEST_ASSERT_EQUAL_INT((int)length, sendto(backdoor_socket, frame, length, 0,
                                            (struct sockaddr *)&destination, sizeof(destination)));
}

static void exercise_tcp_phases_pause_step_and_completion_barrier(const char *start_paused)
{
    int input[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(input));
    setenv("SIMULITH_PUB_BIND", "tcp://127.0.0.1:51321", 1);
    setenv("SIMULITH_REP_BIND", "tcp://127.0.0.1:51322", 1);
    setenv("SIMULITH_START_PAUSED", start_paused, 1);
    /* CLI pause must also hold the initial tick when the environment says run. */
    if (strcmp(start_paused, "1") != 0)
        TEST_ASSERT_EQUAL_INT(6, write(input[1], "pause\n", 6));
    server_pid = fork();
    TEST_ASSERT_TRUE(server_pid >= 0);
    if (server_pid == 0)
    {
        close(input[1]);
        dup2(input[0], STDIN_FILENO);
        close(input[0]);
        execl(SIMULITH_SERVER_PATH, SIMULITH_SERVER_PATH, "1", "--duration", "0.04", "--speed", "max", (char *)NULL);
        _exit(127);
    }
    close(input[0]);
    server_input = input[1];
    context = zmq_ctx_new();
    subscriber = zmq_socket(context, ZMQ_SUB);
    requester = zmq_socket(context, ZMQ_REQ);
    int linger = 0, timeout = 20000; /* only reached if the server process is badly starved */
    zmq_setsockopt(subscriber, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_setsockopt(requester, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_setsockopt(subscriber, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    zmq_setsockopt(requester, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0);
    TEST_ASSERT_EQUAL_INT(0, zmq_connect(subscriber, "tcp://127.0.0.1:51321"));
    TEST_ASSERT_EQUAL_INT(0, zmq_connect(requester, "tcp://127.0.0.1:51322"));
    const char ready[] = "READY probe 7 zmq";
    TEST_ASSERT_TRUE(zmq_send(requester, ready, sizeof(ready) - 1, 0) > 0);
    char reply[32] = {0};
    int ack_length = zmq_recv(requester, reply, sizeof(reply), 0);
    char ack_message[96];
    snprintf(ack_message, sizeof(ack_message), "no READY acknowledgement: %s", zmq_strerror(zmq_errno()));
    TEST_ASSERT_EQUAL_INT_MESSAGE(5, ack_length, ack_message);
    TEST_ASSERT_EQUAL_MEMORY("ACK 0", reply, 5);
    zmq_pollitem_t item = {.socket = subscriber, .events = ZMQ_POLLIN};
    TEST_ASSERT_EQUAL_INT(0, zmq_poll(&item, 1, 100));
    backdoor_socket = socket(AF_INET, SOCK_DGRAM, 0);
    TEST_ASSERT_TRUE(backdoor_socket >= 0);
    send_ground_step(1);
    TEST_ASSERT_EQUAL_INT(0, zmq_poll(&item, 1, 100));
    TEST_ASSERT_EQUAL_INT(5, write(server_input, "step\n", 5));
    for (uint64_t sequence = 0; sequence < 4; ++sequence)
    {
        for (unsigned phase = 1; phase <= 3; ++phase)
        {
            simulith_tick_message_t tick;
            TEST_ASSERT_EQUAL_INT(sizeof(tick), zmq_recv(subscriber, &tick, sizeof(tick), 0));
            TEST_ASSERT_EQUAL_UINT64(sequence, tick.sequence);
            TEST_ASSERT_EQUAL_UINT64(sequence * INTERVAL_NS, tick.time_ns);
            TEST_ASSERT_EQUAL_UINT(phase, tick.phase);
            /* Neither the next phase nor the next tick can bypass unfinished work. */
            TEST_ASSERT_EQUAL_INT(0, zmq_poll(&item, 1, 30));
            char completion[80];
            int length = snprintf(completion, sizeof(completion), "COMPLETE %" PRIu64 " %u probe", sequence, phase);
            TEST_ASSERT_EQUAL_INT(length, zmq_send(requester, completion, (size_t)length, 0));
            TEST_ASSERT_EQUAL_INT(3, zmq_recv(requester, reply, sizeof(reply), 0));
        }
        if (sequence == 0)
        {
            TEST_ASSERT_EQUAL_INT(0, zmq_poll(&item, 1, 100));
            send_ground_step(0);
        }
        else if (sequence == 1)
        {
            TEST_ASSERT_EQUAL_INT(0, zmq_poll(&item, 1, 100));
            TEST_ASSERT_EQUAL_INT(7, write(server_input, "resume\n", 7));
        }
    }
    simulith_tick_message_t stop;
    TEST_ASSERT_EQUAL_INT(sizeof(stop), zmq_recv(subscriber, &stop, sizeof(stop), 0));
    TEST_ASSERT_EQUAL_UINT(SIMULITH_PHASE_STOP, stop.phase);
    int status;
    TEST_ASSERT_EQUAL_INT(server_pid, waitpid(server_pid, &status, 0));
    server_pid = -1;
    TEST_ASSERT_TRUE(WIFEXITED(status));
    TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));
}

static void test_tcp_phases_pause_step_and_completion_barrier(void)
{
    exercise_tcp_phases_pause_step_and_completion_barrier("1");
}

static void test_explicit_running_environment_can_be_paused_before_first_tick(void)
{
    exercise_tcp_phases_pause_step_and_completion_barrier("0");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sync_defaults_and_network_endpoints);
    RUN_TEST(test_device_endpoints_preserve_local_and_explicit_addresses);
    RUN_TEST(test_remote_devices_bind_and_connect_to_distinct_hosts);
    RUN_TEST(test_invalid_network_configuration_fails_without_ipc_fallback);
    RUN_TEST(test_device_endpoint_rejects_bad_buffers_and_empty_client_host);
    RUN_TEST(test_device_endpoint_port_boundaries_and_host_characters);
    RUN_TEST(test_tcp_phases_pause_step_and_completion_barrier);
    RUN_TEST(test_explicit_running_environment_can_be_paused_before_first_tick);
    return UNITY_END();
}
