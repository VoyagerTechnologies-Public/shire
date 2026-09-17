#include "simulith.h"
#include "unity.h"
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <zmq.h>

#include "test_sleep.h"

#define INVALID_ADDR "invalid://address"
#define CLIENT_ID    "test_client"
#define TEST_TIME_S  1 // seconds

#ifndef SIMULITH_SERVER_PATH
#error "SIMULITH_SERVER_PATH must identify the standalone server executable"
#endif

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
    int linger = 0;
    zmq_setsockopt(req, ZMQ_LINGER, &linger, sizeof(linger));
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
    test_sleep_us(1000); // Wait for server to be ready

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

    simulith_client_request_stop();
    pthread_join(client, NULL);
    simulith_server_request_stop();
    pthread_join(server, NULL);
    simulith_client_shutdown();
    simulith_server_shutdown();

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

    result = simulith_server_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, 33, INTERVAL_NS);
    TEST_ASSERT_EQUAL_INT(-1, result);

    result = simulith_server_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, 1, 0);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

static void test_server_periodic_broadcast_reporting(void)
{
    static const char pub[] = "ipc:///tmp/simulith-report-pub.sock";
    static const char rep[] = "ipc:///tmp/simulith-report-rep.sock";
    static const char log_path[] = "/tmp/simulith.log";
    char line[256];
    int report_count = 0;

    unlink(log_path);
    setenv("SIMULITH_LOG_MODE", "file", 1);
    simulith_log_reset_for_tests();
    TEST_ASSERT_EQUAL_INT(0, simulith_server_init(pub, rep, 1, INTERVAL_NS));
    simulith_server_broadcast_for_test(10000000000ULL);
    test_sleep_us(1000);
    simulith_server_broadcast_for_test(20000000000ULL);
    simulith_server_shutdown();
    simulith_log_reset_for_tests();
    unsetenv("SIMULITH_LOG_MODE");

    FILE *log = fopen(log_path, "r");
    TEST_ASSERT_NOT_NULL(log);
    while (fgets(line, sizeof(line), log) != NULL)
        if (strstr(line, "Simulation time:") != NULL)
            report_count++;
    fclose(log);
    TEST_ASSERT_EQUAL_INT(2, report_count);
    unlink(log_path);
}

static void test_server_cli_command_parser(void)
{
    int paused = 0;
    double speed = 1.0;

    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("p", &paused, &speed));
    TEST_ASSERT_EQUAL_INT(1, paused);
    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("p", &paused, &speed));
    TEST_ASSERT_EQUAL_INT(0, paused);

    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("+", &paused, &speed));
    TEST_ASSERT_TRUE(speed == 2.0);
    speed = 1024.0;
    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("+", &paused, &speed));
    TEST_ASSERT_TRUE(speed == 1024.0);

    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("-", &paused, &speed));
    TEST_ASSERT_TRUE(speed == 512.0);
    speed = 0.015625;
    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("-", &paused, &speed));
    TEST_ASSERT_TRUE(speed == 0.015625);

    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("speed 25\n", &paused, &speed));
    TEST_ASSERT_TRUE(speed == 25.0);
    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("speed max\n", &paused, &speed));
    TEST_ASSERT_TRUE(speed == 0.0);
    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("+\n", &paused, &speed));
    TEST_ASSERT_TRUE(speed == 0.0);
    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("-\n", &paused, &speed));
    TEST_ASSERT_TRUE(speed == 1024.0);

    speed = 25.0;
    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("speed 0\n", &paused, &speed));
    TEST_ASSERT_TRUE(speed == 25.0);
    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("speed 0.001\n", &paused, &speed));
    TEST_ASSERT_TRUE(speed == 25.0);
    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("speed nan\n", &paused, &speed));
    TEST_ASSERT_TRUE(speed == 25.0);
    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("speed 2048\n", &paused, &speed));
    TEST_ASSERT_TRUE(speed == 25.0);
    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("speed 25 trailing\n", &paused, &speed));
    TEST_ASSERT_TRUE(speed == 25.0);

    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("pause", &paused, &speed));
    TEST_ASSERT_EQUAL_INT(0, paused);
    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("quit-now", &paused, &speed));
    TEST_ASSERT_EQUAL_INT(0, simulith_server_process_cli_command_for_test("unknown", &paused, &speed));
    TEST_ASSERT_EQUAL_INT(1, simulith_server_process_cli_command_for_test("quit", &paused, &speed));
}

/* Builds a backdoor frame using the same MAGIC/target/cmd_id/payload layout
 * process_backdoor_command() parses (mirrors simulith_director.c's backdoor
 * protocol). Returns the frame length written to buf. */
static size_t build_backdoor_frame(uint8_t *buf, uint16_t cmd_id,
                                    const uint8_t *payload, uint16_t payload_len,
                                    const char *target)
{
    size_t off = 0;
    memcpy(&buf[off], "BACKDOOR", 8);
    off += 8;
    size_t target_len = strlen(target);
    buf[off++] = (uint8_t)target_len;
    memcpy(&buf[off], target, target_len);
    off += target_len;
    buf[off++] = (uint8_t)(cmd_id >> 8);
    buf[off++] = (uint8_t)(cmd_id & 0xFFu);
    buf[off++] = (uint8_t)(payload_len >> 8);
    buf[off++] = (uint8_t)(payload_len & 0xFFu);
    if (payload_len > 0)
    {
        memcpy(&buf[off], payload, payload_len);
        off += payload_len;
    }
    return off;
}

static void encode_be_double(uint8_t *out, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 8; ++i)
        out[i] = (uint8_t)(bits >> (8 * (7 - i)));
}

static void test_server_backdoor_command_parser(void)
{
    int paused = 0;
    double speed = 1.0;
    uint8_t frame[256];
    uint8_t payload[8];
    size_t len;

    len = build_backdoor_frame(frame, 0x0001, NULL, 0, "shire_server");
    simulith_server_process_backdoor_command_for_test(frame, len, &paused, &speed);
    TEST_ASSERT_EQUAL_INT(1, paused);

    len = build_backdoor_frame(frame, 0x0002, NULL, 0, "shire_server");
    simulith_server_process_backdoor_command_for_test(frame, len, &paused, &speed);
    TEST_ASSERT_EQUAL_INT(0, paused);

    encode_be_double(payload, 4.0);
    len = build_backdoor_frame(frame, 0x0003, payload, 8, "shire_server");
    simulith_server_process_backdoor_command_for_test(frame, len, &paused, &speed);
    TEST_ASSERT_TRUE(speed == 4.0);

    /* An all-zero payload is the "max" sentinel, same as the CLI's "speed max". */
    memset(payload, 0, sizeof(payload));
    len = build_backdoor_frame(frame, 0x0003, payload, 8, "shire_server");
    simulith_server_process_backdoor_command_for_test(frame, len, &paused, &speed);
    TEST_ASSERT_TRUE(speed == 0.0);

    /* Out-of-range speed is rejected by the reused CLI validation; speed unchanged. */
    speed = 4.0;
    encode_be_double(payload, 2048.0);
    len = build_backdoor_frame(frame, 0x0003, payload, 8, "shire_server");
    simulith_server_process_backdoor_command_for_test(frame, len, &paused, &speed);
    TEST_ASSERT_TRUE(speed == 4.0);

    /* Malformed or unrecognized frames are silently ignored. */
    len = build_backdoor_frame(frame, 0x0001, NULL, 0, "shire_server");
    frame[0] = 'X';
    simulith_server_process_backdoor_command_for_test(frame, len, &paused, &speed);
    TEST_ASSERT_EQUAL_INT(0, paused);

    len = build_backdoor_frame(frame, 0x0001, NULL, 0, "someone_else");
    simulith_server_process_backdoor_command_for_test(frame, len, &paused, &speed);
    TEST_ASSERT_EQUAL_INT(0, paused);

    len = build_backdoor_frame(frame, 0x0001, NULL, 0, "shire_server");
    simulith_server_process_backdoor_command_for_test(frame, len - 1, &paused, &speed);
    TEST_ASSERT_EQUAL_INT(0, paused);

    len = build_backdoor_frame(frame, 0x00FF, NULL, 0, "shire_server");
    simulith_server_process_backdoor_command_for_test(frame, len, &paused, &speed);
    TEST_ASSERT_EQUAL_INT(0, paused);
    TEST_ASSERT_TRUE(speed == 4.0);
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
    int result = simulith_client_init(NULL, LOCAL_REP_ADDR, CLIENT_ID, INTERVAL_NS);
    TEST_ASSERT_EQUAL_INT(-1, result);

    result = simulith_client_init(LOCAL_PUB_ADDR, NULL, CLIENT_ID, INTERVAL_NS);
    TEST_ASSERT_EQUAL_INT(-1, result);

    result = simulith_client_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, NULL, INTERVAL_NS);
    TEST_ASSERT_EQUAL_INT(-1, result);

    result = simulith_client_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, "", INTERVAL_NS);
    TEST_ASSERT_EQUAL_INT(-1, result);

    result = simulith_client_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, CLIENT_ID, 0);
    TEST_ASSERT_EQUAL_INT(-1, result);

    uint64_t tick = 0;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_wait_for_tick(NULL));
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_wait_for_tick(&tick));
    simulith_client_shutdown();
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
    test_sleep_us(10000); // give server time to bind and start

    int rc = simulith_client_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, "shire-fsw", INTERVAL_NS);
    TEST_ASSERT_EQUAL_INT(0, rc);

    rc = simulith_client_handshake();
    TEST_ASSERT_EQUAL_INT(0, rc);

    uint64_t tick_ns = 0;
    rc = simulith_client_wait_for_tick(&tick_ns);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT64(0, tick_ns);

    simulith_client_shutdown();
    simulith_server_request_stop();
    pthread_join(server, NULL);
    simulith_server_shutdown();
}

// Server should reply ERR to malformed handshake messages
static void test_server_handshake_invalid_format(void)
{
    pthread_t server;
    int i = 1;
    int *p = &i; 
    pthread_create(&server, NULL, server_thread_with_clients, p);
    test_sleep_us(10000);

    char reply[128] = {0};
    char oversized[128];
    memset(oversized, 'x', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(
        LOCAL_REP_ADDR, oversized, reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR", reply);
    int rc = zmq_req_send_and_recv(LOCAL_REP_ADDR, "BADMSG", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("ERR", reply);

    simulith_server_request_stop();
    pthread_join(server, NULL);
    simulith_server_shutdown();
}

// Duplicate READY messages with same client id should result in DUP_ID on the second request
static void test_server_handshake_duplicate_client_id(void)
{
    pthread_t server;
    int i = 2; // two clients expected
    int *p = &i; 
    pthread_create(&server, NULL, server_thread_with_clients, p);
    test_sleep_us(10000);

    // Use the raw ZMQ helper to perform two READY messages which should result
    // in ACK then DUP_ID. This avoids the library's single-client global state.
    test_sleep_us(20000); // give server more time to bind
    char r1[128] = {0};
    char r2[128] = {0};
    int rc1 = zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY DUPTEST", r1, sizeof(r1));
    TEST_ASSERT_EQUAL_INT(0, rc1);
    TEST_ASSERT_EQUAL_STRING("ACK", r1);

    test_sleep_us(10000);

    int rc2 = zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY DUPTEST", r2, sizeof(r2));
    TEST_ASSERT_EQUAL_INT(0, rc2);
    TEST_ASSERT_EQUAL_STRING("DUP_ID", r2);

    simulith_server_request_stop();
    pthread_join(server, NULL);
    simulith_server_shutdown();
}

// A sequence-numbered completion for the registered participant is accepted.
static void test_server_ack_handling(void)
{
    pthread_t server;
    int i = 1;
    int *p = &i; 
    pthread_create(&server, NULL, server_thread_with_clients, p);
    test_sleep_us(10000);

    test_sleep_us(20000); // allow server to bind and start broadcasting

    char reply[128] = {0};
    int rc = zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY ACKTEST", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("ACK", reply);

    test_sleep_us(20000);

    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "COMPLETE malformed", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR_PROTOCOL", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "COMPLETE 0 3 ACKTEST extra", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR_PROTOCOL", reply);
    int rc2 = zmq_req_send_and_recv(LOCAL_REP_ADDR, "COMPLETE 0 3 ACKTEST", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(0, rc2);
    TEST_ASSERT_EQUAL_STRING("ACK", reply);

    simulith_server_request_stop();
    pthread_join(server, NULL);
    simulith_server_shutdown();
}

// Test server CLI: send a sequence of commands via a pipe to stdin to trigger pause/play and speed changes
static void test_server_cli_commands(void)
{
    int input_pipe[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(input_pipe));
    pid_t pid = fork();
    if (pid == 0) {
        close(input_pipe[1]);
        dup2(input_pipe[0], STDIN_FILENO);
        close(input_pipe[0]);
        execl(SIMULITH_SERVER_PATH, "simulith_server_standalone", "1", (char *)NULL);
        _exit(127);
    }

    close(input_pipe[0]);
    test_sleep_us(50000);
    char reply[16] = {0};
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY CLI", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ACK", reply);
    TEST_ASSERT_EQUAL_INT(2, (int)write(input_pipe[1], "p\n", 2));
    test_sleep_us(150000);
    TEST_ASSERT_EQUAL_INT(5, (int)write(input_pipe[1], "quit\n", 5));
    close(input_pipe[1]);
    int status = 0;
    waitpid(pid, &status, 0);
    TEST_ASSERT_TRUE(WIFEXITED(status));
    TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));
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

static void test_server_standalone_invalid_timing_options(void)
{
    static const char *duration_values[] = {
        "abc", "1x", "-1", "0", "nan", "inf",
        "18446744074.0"
    };
    static const char *warmup_values[] = {
        "abc", "1x", "-1", "nan", "inf",
        "18446744074.0"
    };

    for (size_t index = 0;
         index < sizeof(duration_values) / sizeof(duration_values[0]); ++index)
    {
        setenv("SIMULITH_DURATION", duration_values[index], 1);
        pid_t pid = fork();
        if (pid == 0)
        {
            execl(SIMULITH_SERVER_PATH, "simulith_server_standalone",
                  (char *)NULL);
            _exit(127);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        TEST_ASSERT_TRUE(WIFEXITED(status));
        TEST_ASSERT_EQUAL_INT(1, WEXITSTATUS(status));
    }
    unsetenv("SIMULITH_DURATION");

    for (size_t index = 0;
         index < sizeof(warmup_values) / sizeof(warmup_values[0]); ++index)
    {
        setenv("SIMULITH_WARMUP", warmup_values[index], 1);
        pid_t pid = fork();
        if (pid == 0)
        {
            execl(SIMULITH_SERVER_PATH, "simulith_server_standalone",
                  (char *)NULL);
            _exit(127);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        TEST_ASSERT_TRUE(WIFEXITED(status));
        TEST_ASSERT_EQUAL_INT(1, WEXITSTATUS(status));
    }
    unsetenv("SIMULITH_WARMUP");
}

// Unknown participants are rejected and attributed in the watchdog/protocol log.
static void test_server_handle_unknown_client_ack(void)
{
    pthread_t server;
    int i = 1;
    int *p = &i; 
    // Log to file so we can inspect the message
    setenv("SIMULITH_LOG_MODE", "file", 1);
    simulith_log_reset_for_tests();
    pthread_create(&server, NULL, server_thread_with_clients, p);
    test_sleep_us(10000);

    char reply[128] = {0};
    int rc = zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY KNOWN", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("ACK", reply);

    test_sleep_us(20000);

    rc = zmq_req_send_and_recv(LOCAL_REP_ADDR, "COMPLETE 0 3 UNKNOWN123", reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("ERR_UNKNOWN", reply);

    // Give logger a moment to flush to file
    test_sleep_us(10000);

    FILE *f = fopen("/tmp/simulith.log", "r");
    TEST_ASSERT_NOT_NULL(f);
    char buf[256];
    int found = 0;
    while (fgets(buf, sizeof(buf), f)) {
        if (strstr(buf, "Completion received from unknown participant: UNKNOWN123")) { found = 1; break; }
    }
    fclose(f);
    TEST_ASSERT_TRUE(found);

    simulith_server_request_stop();
    pthread_join(server, NULL);
    simulith_server_shutdown();
    unsetenv("SIMULITH_LOG_MODE");
}

static void test_server_rejects_bad_completion_sequences(void)
{
    pthread_t server;
    int expected = 2;
    pthread_create(&server, NULL, server_thread_with_clients, &expected);
    test_sleep_us(20000);

    char reply[128] = {0};
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY FIRST", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ACK", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY SECOND", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ACK", reply);
    test_sleep_us(20000);

    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "COMPLETE 1 3 FIRST", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR_FUTURE", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "COMPLETE 0 2 FIRST", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR_PHASE", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "COMPLETE 0 3 FIRST", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ACK", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "COMPLETE 0 3 FIRST", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR_DUPLICATE", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "COMPLETE 0 3 SECOND", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ACK", reply);
    test_sleep_us(20000);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "COMPLETE 0 3 FIRST", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR_STALE", reply);

    simulith_server_request_stop();
    pthread_join(server, NULL);
    simulith_server_shutdown();
}

static void test_server_enforces_prepare_execute_commit_order(void)
{
    pthread_t server;
    int expected = 2;
    pthread_create(&server, NULL, server_thread_with_clients, &expected);
    test_sleep_us(20000);

    char reply[128] = {0};
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(
        LOCAL_REP_ADDR, "READY DIRECTOR 5", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ACK 0", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(
        LOCAL_REP_ADDR, "READY FSW 2", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ACK 1", reply);
    test_sleep_us(20000);

    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(
        LOCAL_REP_ADDR, "COMPLETE 0 2 FSW", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR_PHASE", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(
        LOCAL_REP_ADDR, "COMPLETE 0 3 DIRECTOR", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR_PHASE", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(
        LOCAL_REP_ADDR, "COMPLETE 0 1 DIRECTOR", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ACK", reply);

    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(
        LOCAL_REP_ADDR, "COMPLETE 0 3 DIRECTOR", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR_PHASE", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(
        LOCAL_REP_ADDR, "COMPLETE 0 2 FSW", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ACK", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(
        LOCAL_REP_ADDR, "COMPLETE 0 3 DIRECTOR", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ACK", reply);

    simulith_server_request_stop();
    pthread_join(server, NULL);
    simulith_server_shutdown();
}

static void test_server_configuration_and_metrics_paths(void)
{
    static const char metrics_path[] = "/tmp/simulith-metrics-test.json";
    unlink(metrics_path);
    TEST_ASSERT_EQUAL_INT(-1, simulith_server_configure(-1.0, 0, NULL));
    TEST_ASSERT_EQUAL_INT(-1, simulith_server_configure(NAN, 0, NULL));
    TEST_ASSERT_EQUAL_INT(0, simulith_server_configure(0.0, 0, NULL));
    TEST_ASSERT_EQUAL_INT(0, simulith_server_configure(2.5, 5000000ULL,
                                                        metrics_path));
    TEST_ASSERT_EQUAL_INT(-1, simulith_server_configure_warmup(5000001ULL));
    TEST_ASSERT_EQUAL_INT(0, simulith_server_configure_warmup(2500000ULL));

    const char *pub = "ipc:///tmp/simulith-config-pub.sock";
    const char *rep = "ipc:///tmp/simulith-config-rep.sock";
    setenv("SIMULITH_WATCHDOG_SECONDS", "0.001", 1);
    TEST_ASSERT_EQUAL_INT(0, simulith_server_init(pub, rep, 1, INTERVAL_NS));
    simulith_server_request_stop();
    simulith_server_shutdown();

    setenv("SIMULITH_WATCHDOG_SECONDS", "not-a-number", 1);
    TEST_ASSERT_EQUAL_INT(0, simulith_server_init(pub, rep, 1, INTERVAL_NS));
    simulith_server_request_stop();
    simulith_server_shutdown();
    unsetenv("SIMULITH_WATCHDOG_SECONDS");

    FILE *metrics = fopen(metrics_path, "r");
    TEST_ASSERT_NOT_NULL(metrics);
    char line[256];
    int found_requested = 0;
    int found_completions = 0;
    while (fgets(line, sizeof(line), metrics) != NULL)
    {
        if (strstr(line, "\"requested_speed\":") != NULL)
            found_requested = 1;
        if (strstr(line, "\"completions\":") != NULL)
            found_completions = 1;
    }
    fclose(metrics);
    TEST_ASSERT_TRUE(found_requested);
    TEST_ASSERT_TRUE(found_completions);
    unlink(metrics_path);
    unlink("/tmp/simulith-config-pub.sock");
    unlink("/tmp/simulith-config-rep.sock");
}

static void test_server_rejects_invalid_handshake_transport_and_duplicate_ids(void)
{
    pthread_t server;
    int expected = 2;
    pthread_create(&server, NULL, server_thread_with_clients, &expected);
    test_sleep_us(20000);

    char reply[128] = {0};
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "BAD HANDSHAKE", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY bad! 3", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY clientA 0 zap", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY clientA 8", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY client! 5", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY clientA 5 zap", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR TRANSPORT", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY clientA 5 shared", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ACK 0", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY clientA 5 shared", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("DUP_ID", reply);

    simulith_server_request_stop();
    pthread_join(server, NULL);
    simulith_server_shutdown();
}

static void test_server_rejects_unavailable_and_mixed_shared_transport(void)
{
    pthread_t server;
    int expected = 2;
    setenv("SIMULITH_SYNC_TRANSPORT", "zmq", 1);
    pthread_create(&server, NULL, server_thread_with_clients, &expected);
    test_sleep_us(20000);

    char reply[128] = {0};
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY sharedbad 5 shared", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR TRANSPORT", reply);
    unsetenv("SIMULITH_SYNC_TRANSPORT");

    simulith_server_request_stop();
    pthread_join(server, NULL);
    simulith_server_shutdown();

    pthread_create(&server, NULL, server_thread_with_clients, &expected);
    test_sleep_us(20000);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY sharedok 5 shared", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ACK 0", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(LOCAL_REP_ADDR, "READY mixed 5 zmq", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR TRANSPORT", reply);
    simulith_server_request_stop();
    pthread_join(server, NULL);
    simulith_server_shutdown();
}

static void test_server_accepts_default_and_legacy_handshakes(void)
{
    pthread_t server;
    int expected = 1;
    char reply[128] = {0};

    pthread_create(&server, NULL, server_thread_with_clients, &expected);
    test_sleep_us(20000);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(
        LOCAL_REP_ADDR, "READY shire-fsw", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ACK", reply);
    simulith_server_request_stop();
    pthread_join(server, NULL);
    simulith_server_shutdown();

    pthread_create(&server, NULL, server_thread_with_clients, &expected);
    test_sleep_us(20000);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(
        LOCAL_REP_ADDR, "READY legacy 2 trailing", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ERR TRANSPORT", reply);
    TEST_ASSERT_EQUAL_INT(0, zmq_req_send_and_recv(
        LOCAL_REP_ADDR, "READY legacy 2", reply, sizeof(reply)));
    TEST_ASSERT_EQUAL_STRING("ACK 0", reply);
    simulith_server_request_stop();
    pthread_join(server, NULL);
    simulith_server_shutdown();
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_synchronization_tick_exchange);
    RUN_TEST(test_server_init_invalid_address);
    RUN_TEST(test_server_init_invalid_params);
    RUN_TEST(test_server_periodic_broadcast_reporting);
    RUN_TEST(test_server_cli_command_parser);
    RUN_TEST(test_server_backdoor_command_parser);
    RUN_TEST(test_client_init_invalid_address);
    RUN_TEST(test_client_init_invalid_params);
    RUN_TEST(test_client_handshake_no_server);
    RUN_TEST(test_client_wait_for_tick);

    RUN_TEST(test_server_handshake_invalid_format);
    RUN_TEST(test_server_handshake_duplicate_client_id);
    RUN_TEST(test_server_ack_handling);
    RUN_TEST(test_server_cli_commands);
    RUN_TEST(test_server_standalone_invalid_arg);
    RUN_TEST(test_server_standalone_invalid_timing_options);
    RUN_TEST(test_server_handle_unknown_client_ack);
    RUN_TEST(test_server_rejects_bad_completion_sequences);
    RUN_TEST(test_server_enforces_prepare_execute_commit_order);
    RUN_TEST(test_server_configuration_and_metrics_paths);
    RUN_TEST(test_server_rejects_invalid_handshake_transport_and_duplicate_ids);
    RUN_TEST(test_server_rejects_unavailable_and_mixed_shared_transport);
    RUN_TEST(test_server_accepts_default_and_legacy_handshakes);

    return UNITY_END();
}
