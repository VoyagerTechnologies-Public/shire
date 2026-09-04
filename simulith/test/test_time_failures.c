#include "simulith_time.h"
#include "unity.h"

#include <stdint.h>
#include <stdlib.h>
#include <zmq.h>

typedef enum
{
    FAIL_NONE,
    FAIL_MALLOC,
    FAIL_CONTEXT,
    FAIL_SOCKET,
    FAIL_CONNECT,
    FAIL_RECEIVE
} failure_t;

static failure_t failure;

void *__real_malloc(size_t size);

void *__wrap_malloc(size_t size)
{
    return failure == FAIL_MALLOC ? NULL : __real_malloc(size);
}

void *__wrap_zmq_ctx_new(void)
{
    return failure == FAIL_CONTEXT ? NULL : (void *)(uintptr_t)1;
}

int __wrap_zmq_ctx_destroy(void *context)
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

int __wrap_zmq_connect(void *socket, const char *endpoint)
{
    (void)socket;
    (void)endpoint;
    return failure == FAIL_CONNECT ? -1 : 0;
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

int __wrap_zmq_recv(void *socket, void *buffer, size_t length, int flags)
{
    (void)socket;
    (void)buffer;
    (void)length;
    (void)flags;
    return failure == FAIL_RECEIVE ? -1 : (int)sizeof(uint64_t);
}

void setUp(void)
{
    failure = FAIL_NONE;
}

void tearDown(void)
{
}

static void test_time_initialization_failures(void)
{
    failure = FAIL_MALLOC;
    TEST_ASSERT_NULL(simulith_time_init());
    failure = FAIL_CONTEXT;
    TEST_ASSERT_NULL(simulith_time_init());
    failure = FAIL_SOCKET;
    TEST_ASSERT_NULL(simulith_time_init());
    failure = FAIL_CONNECT;
    TEST_ASSERT_NULL(simulith_time_init());
}

static void test_time_receive_failure_and_cleanup(void)
{
    void *provider = simulith_time_init();
    TEST_ASSERT_NOT_NULL(provider);
    failure = FAIL_RECEIVE;
    TEST_ASSERT_EQUAL_INT(-1, simulith_time_wait_for_next_tick(provider));
    simulith_time_cleanup(provider);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_time_initialization_failures);
    RUN_TEST(test_time_receive_failure_and_cleanup);
    return UNITY_END();
}
