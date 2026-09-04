#include "simulith.h"
#include "unity.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

typedef enum
{
    RECV_ACK,
    RECV_EAGAIN,
    RECV_ERROR,
    RECV_DUPLICATE,
    RECV_UNEXPECTED,
    RECV_TICK_SEND_FAILURE,
    RECV_TICK_ACK_FAILURE,
    RECV_RUN_NULL_CALLBACK
} receive_mode_t;

static int socket_calls;
static int socket_failure_call;
static int connect_calls;
static int connect_failure_call;
static int send_failure;
static int receive_calls;
static receive_mode_t receive_mode;

void simulith_log(const char *format, ...)
{
    (void)format;
}

void *__wrap_zmq_ctx_new(void)
{
    return socket_failure_call == -1 ? NULL : (void *)(uintptr_t)1;
}

int __wrap_zmq_ctx_term(void *context)
{
    (void)context;
    return 0;
}

void *__wrap_zmq_socket(void *context, int type)
{
    (void)context;
    (void)type;
    socket_calls++;
    return socket_calls == socket_failure_call ? NULL : (void *)(uintptr_t)(socket_calls + 1);
}

int __wrap_zmq_connect(void *socket, const char *endpoint)
{
    (void)socket;
    (void)endpoint;
    connect_calls++;
    return connect_calls == connect_failure_call ? -1 : 0;
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
    return 0;
}

int __wrap_zmq_send(void *socket, const void *buffer, size_t length, int flags)
{
    (void)socket;
    (void)buffer;
    (void)flags;
    return send_failure ? -1 : (int)length;
}

static int copy_reply(void *buffer, size_t length, const char *reply)
{
    size_t reply_length = strlen(reply);
    if (reply_length > length)
        reply_length = length;
    memcpy(buffer, reply, reply_length);
    return (int)reply_length;
}

int __wrap_zmq_recv(void *socket, void *buffer, size_t length, int flags)
{
    (void)socket;
    (void)flags;
    receive_calls++;
    switch (receive_mode) {
        case RECV_EAGAIN:
            errno = EAGAIN;
            return -1;
        case RECV_ERROR:
            errno = EIO;
            return -1;
        case RECV_DUPLICATE:
            return copy_reply(buffer, length, "DUP_ID");
        case RECV_UNEXPECTED:
            return copy_reply(buffer, length, "WHAT");
        case RECV_TICK_SEND_FAILURE:
            if (receive_calls == 1)
                return 0;
            *(uint64_t *)buffer = 42;
            return (int)sizeof(uint64_t);
        case RECV_TICK_ACK_FAILURE:
            if (receive_calls == 1) {
                *(uint64_t *)buffer = 43;
                return (int)sizeof(uint64_t);
            }
            return -1;
        case RECV_RUN_NULL_CALLBACK:
            if (receive_calls == 1) {
                *(uint64_t *)buffer = 44;
                return (int)sizeof(uint64_t);
            }
            simulith_client_request_stop();
            return copy_reply(buffer, length, "ACK");
        case RECV_ACK:
        default:
            return copy_reply(buffer, length, "ACK");
    }
}

void setUp(void)
{
    simulith_client_shutdown();
    socket_calls = 0;
    socket_failure_call = 0;
    connect_calls = 0;
    connect_failure_call = 0;
    send_failure = 0;
    receive_calls = 0;
    receive_mode = RECV_ACK;
}

void tearDown(void)
{
    simulith_client_shutdown();
}

static void test_client_initialization_resource_failures(void)
{
    socket_failure_call = -1;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_init("pub", "rep", "id", 1));

    socket_failure_call = 1;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_init("pub", "rep", "id", 1));

    socket_calls = 0;
    socket_failure_call = 2;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_init("pub", "rep", "id", 1));

    socket_calls = 0;
    socket_failure_call = 0;
    connect_calls = 0;
    connect_failure_call = 1;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_init("pub", "rep", "id", 1));

    socket_calls = 0;
    connect_calls = 0;
    connect_failure_call = 2;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_init("pub", "rep", "id", 1));
}

static void test_client_handshake_failures(void)
{
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "rep", "id", 1));
    send_failure = 1;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_handshake());
    send_failure = 0;

    receive_mode = RECV_EAGAIN;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_handshake());
    receive_mode = RECV_ERROR;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_handshake());
    receive_mode = RECV_DUPLICATE;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_handshake());
    receive_mode = RECV_UNEXPECTED;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_handshake());
}

static void test_client_tick_failures_and_null_callback(void)
{
    TEST_ASSERT_EQUAL_INT(0, simulith_client_init("pub", "rep", "id", 1));
    uint64_t tick = 0;

    receive_mode = RECV_TICK_SEND_FAILURE;
    send_failure = 1;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_wait_for_tick(&tick));
    TEST_ASSERT_EQUAL_UINT64(42, tick);

    receive_calls = 0;
    receive_mode = RECV_TICK_ACK_FAILURE;
    send_failure = 0;
    TEST_ASSERT_EQUAL_INT(-1, simulith_client_wait_for_tick(&tick));

    receive_calls = 0;
    receive_mode = RECV_RUN_NULL_CALLBACK;
    simulith_client_run_loop(NULL);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_client_initialization_resource_failures);
    RUN_TEST(test_client_handshake_failures);
    RUN_TEST(test_client_tick_failures_and_null_callback);
    return UNITY_END();
}
