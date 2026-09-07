#include "simulith.h"
#include "unity.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <zmq.h>

typedef enum
{
    FAIL_NONE,
    FAIL_CONTEXT,
    FAIL_PUBLISHER,
    FAIL_RESPONDER,
    SCRIPT_HANDSHAKES
} failure_t;

static failure_t failure;
static int socket_calls;
static int receive_calls;
static int close_calls;
static int term_calls;
static int send_calls;

void simulith_log(const char *format, ...)
{
    (void)format;
}

void *__wrap_zmq_ctx_new(void)
{
    return failure == FAIL_CONTEXT ? NULL : (void *)(uintptr_t)1;
}

int __wrap_zmq_ctx_term(void *context)
{
    (void)context;
    term_calls++;
    return 0;
}

void *__wrap_zmq_socket(void *context, int type)
{
    (void)context;
    (void)type;
    socket_calls++;
    if (failure == FAIL_PUBLISHER && socket_calls == 1)
        return NULL;
    if (failure == FAIL_RESPONDER && socket_calls == 2)
        return NULL;
    return (void *)(uintptr_t)(socket_calls + 1);
}

int __wrap_zmq_bind(void *socket, const char *endpoint)
{
    (void)socket;
    (void)endpoint;
    return 0;
}

int __wrap_zmq_setsockopt(void *socket, int option, const void *value, size_t length)
{
    (void)socket;
    (void)option;
    (void)value;
    (void)length;
    return 0;
}

int __wrap_zmq_close(void *socket)
{
    (void)socket;
    close_calls++;
    return 0;
}

int __wrap_zmq_send(void *socket, const void *buffer, size_t length, int flags)
{
    (void)socket;
    (void)buffer;
    (void)flags;
    send_calls++;
    return (int)length;
}

int __wrap_zmq_recv(void *socket, void *buffer, size_t length, int flags)
{
    (void)socket;
    (void)flags;
    static const char *const messages[] = {
        NULL,
        NULL,
        "BAD READY",
        "READY ",
        "READY first",
        "READY second",
    };

    if (failure != SCRIPT_HANDSHAKES)
        return -1;

    int call = receive_calls++;
    if (call == 0) {
        errno = EAGAIN;
        return -1;
    }
    if (call == 1) {
        errno = EIO;
        return -1;
    }

    const char *message = messages[call];
    size_t message_length = strlen(message);
    TEST_ASSERT_LESS_OR_EQUAL_size_t(length, message_length);
    memcpy(buffer, message, message_length);
    if (call == 5)
        simulith_server_request_stop();
    return (int)message_length;
}

void setUp(void)
{
    failure = FAIL_NONE;
    socket_calls = 0;
    receive_calls = 0;
    close_calls = 0;
    term_calls = 0;
    send_calls = 0;
}

void tearDown(void)
{
    simulith_server_shutdown();
}

static void test_server_context_creation_failure(void)
{
    failure = FAIL_CONTEXT;
    TEST_ASSERT_EQUAL_INT(-1, simulith_server_init("pub", "rep", 1, 1));
    TEST_ASSERT_EQUAL_INT(0, close_calls);
    TEST_ASSERT_EQUAL_INT(0, term_calls);
}

static void test_server_publisher_creation_failure_cleans_context(void)
{
    failure = FAIL_PUBLISHER;
    TEST_ASSERT_EQUAL_INT(-1, simulith_server_init("pub", "rep", 1, 1));
    TEST_ASSERT_EQUAL_INT(0, close_calls);
    TEST_ASSERT_EQUAL_INT(1, term_calls);
}

static void test_server_responder_creation_failure_cleans_publisher(void)
{
    failure = FAIL_RESPONDER;
    TEST_ASSERT_EQUAL_INT(-1, simulith_server_init("pub", "rep", 1, 1));
    TEST_ASSERT_EQUAL_INT(1, close_calls);
    TEST_ASSERT_EQUAL_INT(1, term_calls);
}

static void test_server_rejects_malformed_handshakes_before_two_clients_join(void)
{
    failure = SCRIPT_HANDSHAKES;
    TEST_ASSERT_EQUAL_INT(0, simulith_server_init("pub", "rep", 2, 1));
    simulith_server_run();
    TEST_ASSERT_EQUAL_INT(6, receive_calls);
    TEST_ASSERT_EQUAL_INT(4, send_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_server_context_creation_failure);
    RUN_TEST(test_server_publisher_creation_failure_cleans_context);
    RUN_TEST(test_server_responder_creation_failure_cleans_publisher);
    RUN_TEST(test_server_rejects_malformed_handshakes_before_two_clients_join);
    return UNITY_END();
}
