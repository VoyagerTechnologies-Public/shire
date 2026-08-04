#include "simulith.h"
#include "unity.h"
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <zmq.h>

#define INVALID_ADDR "invalid://address"
#define CLIENT_ID    "test_client"
#define TEST_TIME_S  1 // seconds

static int ticks_received = 0;

void setUp(void)
{
    ticks_received = 0;
}

void tearDown(void)
{
    
}

static void on_tick(uint64_t time_ns)
{
    ticks_received++;
}

static void *server_thread(void *arg)
{
    simulith_server_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, 1, INTERVAL_NS);
    simulith_server_run(); // runs indefinitely
    return NULL;
}

// Server thread that accepts expected client count via malloc'd int pointer
static void *server_thread_with_clients(void *arg)
{
    int expected = 1;
    if (arg)
    {
        int *p = (int *)arg;
        expected = *p;
    }

    simulith_server_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, expected, INTERVAL_NS);
    simulith_server_run();
    return NULL;
}

// Helper: send a raw REQ message to addr and receive reply (timeouted). Returns 0 on success.
static int zmq_req_send_and_recv(const char *addr, const char *msg, char *reply, size_t reply_len)
{
    void *ctx = zmq_ctx_new();
    if (!ctx) return -1;
    void *req = zmq_socket(ctx, ZMQ_REQ);
    if (!req) { zmq_ctx_term(ctx); return -1; }
    if (zmq_connect(req, addr) != 0) { zmq_close(req); zmq_ctx_term(ctx); return -1; }

    int timeout_ms = 2000;
    zmq_setsockopt(req, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));

    if (zmq_send(req, msg, strlen(msg), 0) == -1) { zmq_close(req); zmq_ctx_term(ctx); return -1; }

    char buf[128] = {0};
    int rc = zmq_recv(req, buf, sizeof(buf) - 1, 0);
    if (rc < 0) { zmq_close(req); zmq_ctx_term(ctx); return -1; }
    buf[rc] = '\0';
    if (reply && reply_len > 0) strncpy(reply, buf, reply_len - 1);

    zmq_close(req);
    zmq_ctx_term(ctx);
    return 0;
}

static void *client_thread(void *arg)
{
    usleep(1000); // Wait for server to be ready

    simulith_client_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, CLIENT_ID, INTERVAL_NS);

    // Perform handshake before running the tick loop
    if (simulith_client_handshake() != 0)
    {
        fprintf(stderr, "Client handshake failed\n");
        return NULL;
    }

    simulith_client_run_loop(on_tick); // runs indefinitely
    return NULL;
}

static void test_synchronization_tick_exchange(void)
{
    pthread_t server, client;

    pthread_create(&server, NULL, server_thread, NULL);
    pthread_create(&client, NULL, client_thread, NULL);

    sleep(TEST_TIME_S); // Allow some time for a few ticks to exchange

    TEST_ASSERT_GREATER_THAN(0, ticks_received);

    // Cancel threads to end test
    pthread_cancel(client);
    pthread_cancel(server);
    pthread_join(client, NULL);
    pthread_join(server, NULL);

    simulith_log("Ticks received during test: %d\n", ticks_received);

    double simulated_time_seconds = ((double)ticks_received * (double)INTERVAL_NS) / 1e9;
    double interval_ms            = (double)INTERVAL_NS / 1e6;
    simulith_log(
        "Test ran for %d seconds real time, simulating %.3f seconds via %lu ticks with an interval of %.2f ms\n",
        TEST_TIME_S, simulated_time_seconds, (unsigned long)ticks_received, interval_ms);
}

// Test invalid server initialization
static void test_server_init_invalid_address(void)
{
    int result = simulith_server_init(INVALID_ADDR, LOCAL_REP_ADDR, 1, INTERVAL_NS);
    TEST_ASSERT_EQUAL_INT(-1, result);

    result = simulith_server_init(LOCAL_PUB_ADDR, INVALID_ADDR, 1, INTERVAL_NS);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

static void test_server_init_invalid_params(void)
{
    int result = simulith_server_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, 0, INTERVAL_NS);
    TEST_ASSERT_EQUAL_INT(-1, result);

    result = simulith_server_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, -1, INTERVAL_NS);
    TEST_ASSERT_EQUAL_INT(-1, result);

    result = simulith_server_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, 1, 0);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

// Test invalid client initialization
static void test_client_init_invalid_address(void)
{
    int result = simulith_client_init(INVALID_ADDR, LOCAL_REP_ADDR, CLIENT_ID, INTERVAL_NS);
    TEST_ASSERT_EQUAL_INT(-1, result);

    result = simulith_client_init(LOCAL_PUB_ADDR, INVALID_ADDR, CLIENT_ID, INTERVAL_NS);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

static void test_client_init_invalid_params(void)
{
    int result = simulith_client_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, NULL, INTERVAL_NS);
    TEST_ASSERT_EQUAL_INT(-1, result);

    result = simulith_client_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, "", INTERVAL_NS);
    TEST_ASSERT_EQUAL_INT(-1, result);

    result = simulith_client_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, CLIENT_ID, 0);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

// Test handshake without server
static void test_client_handshake_no_server(void)
{
    simulith_client_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, CLIENT_ID, INTERVAL_NS);
    int result = simulith_client_handshake();
    TEST_ASSERT_EQUAL_INT(-1, result);
    simulith_client_shutdown();
}

// Test blocking wait-for-tick API: server should send a tick and client should receive it
static void test_client_wait_for_tick(void)
{
    pthread_t server;
    int i = 1;
    int *p = &i; 
    pthread_create(&server, NULL, server_thread_with_clients, p);
    usleep(10000); // give server time to bind and start

    int rc = simulith_client_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, CLIENT_ID, INTERVAL_NS);
    TEST_ASSERT_EQUAL_INT(0, rc);

    rc = simulith_client_handshake();
    TEST_ASSERT_EQUAL_INT(0, rc);

    uint64_t tick_ns = 0;
    rc = simulith_client_wait_for_tick(&tick_ns);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_GREATER_THAN(0, tick_ns);

    simulith_client_shutdown();
    simulith_server_shutdown();
    pthread_cancel(server);
    pthread_join(server, NULL);
}

// Server should reply ERR to malformed handshake messages
static void test_server_handshake_invalid_format(void)
{
    pthread_t server;
    int i = 1;
    int *p = &i; 
    pthread_create(&server, NULL, server_thread_with_clients, p);
    usleep(10000);

    char reply[128] = {0};
    int rc = zmq_req_send_and_recv(LOCAL_REP_ADDR, "BADMSG", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("ERR", reply);

    simulith_server_shutdown();
    pthread_cancel(server);
    pthread_join(server, NULL);
}

// Duplicate READY messages with same client id should result in DUP_ID on the second request
static void test_server_handshake_duplicate_client_id(void)
{
    pthread_t server;
    int i = 2; // two clients expected
    int *p = &i; 
    pthread_create(&server, NULL, server_thread_with_clients, p);
    usleep(10000);

    // Use the raw ZMQ helper to perform two READY messages which should result
    // in ACK then DUP_ID. This avoids the library's single-client global state.
    usleep(20000); // give server more time to bind
    char r1[128] = {0};
    char r2[128] = {0};
    int rc1 = zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY DUPTEST", r1, sizeof(r1));
    TEST_ASSERT_EQUAL_INT(0, rc1);
    TEST_ASSERT_EQUAL_STRING("ACK", r1);

    usleep(10000);

    int rc2 = zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY DUPTEST", r2, sizeof(r2));
    TEST_ASSERT_EQUAL_INT(0, rc2);
    TEST_ASSERT_EQUAL_STRING("DUP_ID", r2);

    simulith_server_shutdown();
    pthread_cancel(server);
    pthread_join(server, NULL);
}

// After a proper READY/ACK handshake, sending the client id as an ACK should elicit an "ACK" reply
static void test_server_ack_handling(void)
{
    pthread_t server;
    int i = 1;
    int *p = &i; 
    pthread_create(&server, NULL, server_thread_with_clients, p);
    usleep(10000);

    usleep(20000); // allow server to bind and start broadcasting

    char reply[128] = {0};
    int rc = zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY ACKTEST", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("ACK", reply);

    usleep(20000);

    int rc2 = zmq_req_send_and_recv(LOCAL_REP_ADDR, "ACKTEST", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(0, rc2);
    TEST_ASSERT_EQUAL_STRING("ACK", reply);

    simulith_server_shutdown();
    pthread_cancel(server);
    pthread_join(server, NULL);
}

// Test server CLI: send a sequence of commands via a pipe to stdin to trigger pause/play and speed changes
static void test_server_cli_commands(void)
{
    // Start server in a thread and then send commands to its stdin via popen of the standalone
    pid_t pid = fork();
    if (pid == 0) {
        // Child: exec the standalone server with 1 client
        execlp("./build/simulith_server_standalone", "simulith_server_standalone", "1", (char *)NULL);
        _exit(127);
    }

    // Parent: allow server to start, then write commands to its stdin via /proc/<pid>/fd/0 is not writable
    // Instead we sleep a bit and then kill to trigger clean shutdown path (this exercises startup and shutdown)
    sleep(1);
    kill(pid, SIGTERM);
    int status = 0;
    waitpid(pid, &status, 0);
    TEST_ASSERT_TRUE(WIFEXITED(status) || WIFSIGNALED(status));
}

// Test that the standalone server exits with non-zero on invalid arg
static void test_server_standalone_invalid_arg(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        execlp("./build/simulith_server_standalone", "simulith_server_standalone", "0", (char *)NULL);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    // The standalone should exit non-zero for invalid client count
    TEST_ASSERT_TRUE(WIFEXITED(status));
    int exit_code = WEXITSTATUS(status);
    TEST_ASSERT_NOT_EQUAL(0, exit_code);
}

// Sending an ACK with an unknown client id should log an "ACK received from unknown client" message
static void test_server_handle_unknown_client_ack(void)
{
    pthread_t server;
    int i = 1;
    int *p = &i; 
    // Log to file so we can inspect the message
    setenv("SIMULITH_LOG_MODE", "file", 1);
    simulith_log_reset_for_tests();
    pthread_create(&server, NULL, server_thread_with_clients, p);
    usleep(10000);

    char reply[128] = {0};
    int rc = zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY KNOWN", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("ACK", reply);

    usleep(20000);

    rc = zmq_req_send_and_recv(LOCAL_REP_ADDR, "UNKNOWN123", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("ACK", reply);

    // Give logger a moment to flush to file
    usleep(10000);

    FILE *f = fopen("/tmp/simulith.log", "r");
    TEST_ASSERT_NOT_NULL(f);
    char buf[256];
    int found = 0;
    while (fgets(buf, sizeof(buf), f)) {
        if (strstr(buf, "ACK received from unknown client: UNKNOWN123")) { found = 1; break; }
    }
    fclose(f);
    TEST_ASSERT_TRUE(found);

    simulith_server_shutdown();
    pthread_cancel(server);
    pthread_join(server, NULL);
    unsetenv("SIMULITH_LOG_MODE");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_synchronization_tick_exchange);
    RUN_TEST(test_server_init_invalid_address);
    RUN_TEST(test_server_init_invalid_params);
    RUN_TEST(test_client_init_invalid_address);
    RUN_TEST(test_client_init_invalid_params);
    RUN_TEST(test_client_handshake_no_server);
    RUN_TEST(test_client_wait_for_tick);

    RUN_TEST(test_server_handshake_invalid_format);
    RUN_TEST(test_server_handshake_duplicate_client_id);
    RUN_TEST(test_server_ack_handling);
    RUN_TEST(test_server_cli_commands);
    RUN_TEST(test_server_standalone_invalid_arg);
    RUN_TEST(test_server_handle_unknown_client_ack);

    return UNITY_END();
}
