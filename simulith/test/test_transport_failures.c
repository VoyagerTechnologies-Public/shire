#include "simulith_transport.h"
#include "unity.h"

#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

typedef enum
{
    FAIL_NONE,
    FAIL_CONTEXT,
    FAIL_SOCKET,
    FAIL_SEND,
    POLL_WITHOUT_INPUT,
    FAIL_MESSAGE_RECEIVE,
    FAIL_POLL,
    POLL_ERROR_READY,
    POLL_EMPTY_READY,
    REPLY_BYTE
} failure_t;

static failure_t failure;
static int poll_calls;
static int message_receive_calls;

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
    return failure == FAIL_SEND ? -1 : (int)length;
}

int __wrap_zmq_poll(zmq_pollitem_t *items, int count, long timeout)
{
    (void)count;
    (void)timeout;
    poll_calls++;
    if (failure == FAIL_POLL) {
        errno = EIO;
        return -1;
    }
    if (failure == FAIL_MESSAGE_RECEIVE || failure == REPLY_BYTE) {
        if (message_receive_calls > 0)
            return 0;
        items[0].revents = ZMQ_POLLIN;
        return 1;
    }
    if (failure == POLL_ERROR_READY) {
        items[0].revents = ZMQ_POLLERR;
        return 1;
    }
    if (failure == POLL_EMPTY_READY)
        return 1;
    if (failure == POLL_WITHOUT_INPUT) {
        items[0].revents = 0;
        return 1;
    }
    return 0;
}

int __wrap_zmq_msg_recv(zmq_msg_t *message, void *socket, int flags)
{
    (void)socket;
    (void)flags;
    message_receive_calls++;
    if (failure == REPLY_BYTE)
    {
        zmq_msg_close(message);
        TEST_ASSERT_EQUAL_INT(0, zmq_msg_init_size(message, 1));
        *(uint8_t *)zmq_msg_data(message) = 0xA5;
        return 1;
    }
    errno = EIO;
    return -1;
}

void setUp(void)
{
    failure = FAIL_NONE;
    poll_calls = 0;
    message_receive_calls = 0;
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
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_available(&port));
    failure = POLL_WITHOUT_INPUT;
    TEST_ASSERT_EQUAL_INT(0, simulith_transport_available(&port));
}

static void test_transport_public_argument_guards(void)
{
    transport_port_t empty = {0};
    uint8_t value = 1;

    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_send(NULL, &value, sizeof(value)));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_receive(&empty, &value, sizeof(value)));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_available(&empty));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_flush(&empty));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_close(&empty));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_complete_request(&empty, 1, 0));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_request(NULL, &value, sizeof(value), 1));

    transport_port_t initialized = {
        .init = SIMULITH_TRANSPORT_INITIALIZED,
        .zmq_sock = (void *)(uintptr_t)2,
    };
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          simulith_transport_complete_request(&initialized, 0, 0));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_request(&initialized, NULL, 0, 1));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_request(&initialized, &value, sizeof(value), -1));
    uint64_t transaction_id = 0;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_receive_request(
                              NULL, &value, sizeof(value), &transaction_id));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_receive_request(
                              &initialized, NULL, sizeof(value), &transaction_id));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_receive_request(
                              &initialized, &value, sizeof(value), NULL));
}

static void test_transport_request_runtime_failures(void)
{
    uint8_t value = 1;
    transport_port_t port = {
        .init = SIMULITH_TRANSPORT_INITIALIZED,
        .zmq_sock = (void *)(uintptr_t)2,
    };
    strcpy(port.name, "failure-port");

    failure = FAIL_SEND;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_request(&port, &value,
                                                     sizeof(value), 1));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_complete_request(&port, 1, 0));

    failure = FAIL_MESSAGE_RECEIVE;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_request(&port, &value,
                                                     sizeof(value), 1));

    failure = REPLY_BYTE;
    port.rx_buf_len = sizeof(port.rx_buf);
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_request(&port, &value,
                                                     sizeof(value), 1));

    failure = POLL_WITHOUT_INPUT;
    port.next_transaction_id = UINT64_MAX;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_request(&port, &value,
                                                     sizeof(value), 0));
}

static void test_transport_wait_runtime_failures(void)
{
    transport_port_t initialized = {
        .init = SIMULITH_TRANSPORT_INITIALIZED,
        .zmq_sock = (void *)(uintptr_t)2,
    };
    transport_port_t uninitialized = {0};
    transport_port_t *valid_ports[] = {&initialized};
    transport_port_t *invalid_ports[] = {&uninitialized};
    int interrupt_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, interrupt_fd);

    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_wait_for_request(
                              invalid_ports, 1, interrupt_fd));
    failure = FAIL_POLL;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_wait_for_request(
                              valid_ports, 1, interrupt_fd));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_receive_exact(
                              &initialized, (uint8_t[1]){0}, 1, 1));
    failure = POLL_ERROR_READY;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_wait_for_request(
                              valid_ports, 1, interrupt_fd));
    failure = POLL_EMPTY_READY;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,
                          simulith_transport_wait_for_request(
                              valid_ports, 1, interrupt_fd));

    close(interrupt_fd);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_transport_creation_failures);
    RUN_TEST(test_transport_runtime_failures);
    RUN_TEST(test_transport_public_argument_guards);
    RUN_TEST(test_transport_request_runtime_failures);
    RUN_TEST(test_transport_wait_runtime_failures);
    return UNITY_END();
}
