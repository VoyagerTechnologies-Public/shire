#include "simulith_transport.h"
#include "unity.h"

#include <stdarg.h>
#include <stdint.h>
#include <string.h>

typedef enum
{
    FAIL_NONE,
    FAIL_CONTEXT,
    FAIL_SOCKET,
    FAIL_SEND,
    POLL_WITHOUT_INPUT,
    FAIL_MESSAGE_RECEIVE
} failure_t;

static failure_t failure;

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
    return 0;
}

void *__wrap_zmq_socket(void *context, int type)
{
    (void)context;
    (void)type;
    return failure == FAIL_SOCKET ? NULL : (void *)(uintptr_t)2;
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
    (void)length;
    (void)flags;
    return failure == FAIL_SEND ? -1 : 0;
}

int __wrap_zmq_poll(zmq_pollitem_t *items, int count, long timeout)
{
    (void)count;
    (void)timeout;
    if (failure == FAIL_MESSAGE_RECEIVE) {
        items[0].revents = ZMQ_POLLIN;
        return 1;
    }
    if (failure == POLL_WITHOUT_INPUT) {
        items[0].revents = 0;
        return 1;
    }
    return 0;
}

int __wrap_zmq_msg_recv(zmq_msg_t *message, void *socket, int flags)
{
    (void)message;
    (void)socket;
    (void)flags;
    return -1;
}

void setUp(void)
{
    failure = FAIL_NONE;
}

void tearDown(void)
{
}

static void test_transport_creation_failures(void)
{
    transport_port_t port = {0};
    failure = FAIL_CONTEXT;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR, simulith_transport_init(&port));
    failure = FAIL_SOCKET;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR, simulith_transport_init(&port));
}

static void test_transport_runtime_failures(void)
{
    transport_port_t port = {
        .init = SIMULITH_TRANSPORT_INITIALIZED,
        .zmq_sock = (void *)(uintptr_t)2,
    };
    uint8_t value = 1;
    failure = FAIL_SEND;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_send(&port, &value, sizeof(value)));
    failure = FAIL_MESSAGE_RECEIVE;
    TEST_ASSERT_EQUAL_INT(0, simulith_transport_available(&port));
    failure = POLL_WITHOUT_INPUT;
    TEST_ASSERT_EQUAL_INT(0, simulith_transport_available(&port));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_transport_creation_failures);
    RUN_TEST(test_transport_runtime_failures);
    return UNITY_END();
}
