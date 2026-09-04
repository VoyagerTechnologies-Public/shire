#include "radio_sim.h"
#include "simulith_component.h"
#include "simulith_transport.h"
#include "unity.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>

typedef enum
{
    FAIL_NONE,
    FAIL_MALLOC,
    FAIL_MUTEX,
    FAIL_TRANSPORT,
    FAIL_SOCKET,
    FAIL_BIND,
    FAIL_THREAD
} failure_t;

static failure_t failure;
static int transport_calls;
static int transport_failure_call;
static int socket_calls;
static int socket_failure_call;

const component_interface_t *get_component_interface(void);

void *__real_malloc(size_t size);
int __real_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attributes);

void *__wrap_malloc(size_t size)
{
    return failure == FAIL_MALLOC ? NULL : __real_malloc(size);
}

int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attributes)
{
    if (failure == FAIL_MUTEX)
        return 1;
    return __real_pthread_mutex_init(mutex, attributes);
}

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                          void *(*start_routine)(void *), void *argument)
{
    (void)thread;
    (void)attributes;
    (void)start_routine;
    (void)argument;
    (void)failure;
    return 1;
}

int __wrap_socket(int domain, int type, int protocol)
{
    (void)domain;
    (void)type;
    (void)protocol;
    socket_calls++;
    return socket_calls == socket_failure_call ? -1 : 100 + socket_calls;
}

int __wrap_bind(int socket_fd, const struct sockaddr *address, socklen_t length)
{
    (void)socket_fd;
    (void)address;
    (void)length;
    return failure == FAIL_BIND ? -1 : 0;
}

int simulith_transport_init(transport_port_t *port)
{
    transport_calls++;
    if (transport_calls == transport_failure_call)
        return SIMULITH_TRANSPORT_ERROR;
    port->init = SIMULITH_TRANSPORT_INITIALIZED;
    return SIMULITH_TRANSPORT_SUCCESS;
}

int simulith_transport_close(transport_port_t *port)
{
    if (port)
        port->init = 0;
    return SIMULITH_TRANSPORT_SUCCESS;
}

int simulith_transport_send(transport_port_t *port, const uint8_t *data, size_t length)
{
    (void)port;
    (void)data;
    return (int)length;
}

int simulith_transport_receive(transport_port_t *port, uint8_t *data, size_t length)
{
    (void)port;
    (void)data;
    (void)length;
    return 0;
}

int simulith_transport_available(transport_port_t *port)
{
    (void)port;
    return 0;
}

int simulith_transport_flush(transport_port_t *port)
{
    (void)port;
    return SIMULITH_TRANSPORT_SUCCESS;
}

void simulith_time_cleanup(void *handle)
{
    (void)handle;
}

void setUp(void)
{
    failure = FAIL_NONE;
    transport_calls = 0;
    transport_failure_call = 0;
    socket_calls = 0;
    socket_failure_call = 0;
    setenv("RADIO_GROUND_HOST", "127.0.0.1", 1);
}

void tearDown(void)
{
    unsetenv("RADIO_GROUND_HOST");
}

static void reset_faults(void)
{
    failure = FAIL_NONE;
    transport_calls = 0;
    transport_failure_call = 0;
    socket_calls = 0;
    socket_failure_call = 0;
}

static void test_radio_init_dependency_failures(void)
{
    radio_sim_state_t state;

    failure = FAIL_MUTEX;
    TEST_ASSERT_EQUAL_INT(RADIO_SIM_ERROR, radio_sim_init(&state));

    reset_faults();
    failure = FAIL_TRANSPORT;
    transport_failure_call = 1;
    TEST_ASSERT_EQUAL_INT(RADIO_SIM_ERROR, radio_sim_init(&state));

    reset_faults();
    failure = FAIL_TRANSPORT;
    transport_failure_call = 2;
    TEST_ASSERT_EQUAL_INT(RADIO_SIM_ERROR, radio_sim_init(&state));

    reset_faults();
    failure = FAIL_TRANSPORT;
    transport_failure_call = 3;
    TEST_ASSERT_EQUAL_INT(RADIO_SIM_ERROR, radio_sim_init(&state));

    reset_faults();
    failure = FAIL_SOCKET;
    socket_failure_call = 1;
    TEST_ASSERT_EQUAL_INT(RADIO_SIM_ERROR, radio_sim_init(&state));

    reset_faults();
    failure = FAIL_SOCKET;
    socket_failure_call = 2;
    TEST_ASSERT_EQUAL_INT(RADIO_SIM_ERROR, radio_sim_init(&state));

    reset_faults();
    failure = FAIL_BIND;
    TEST_ASSERT_EQUAL_INT(RADIO_SIM_ERROR, radio_sim_init(&state));

    reset_faults();
    failure = FAIL_THREAD;
    TEST_ASSERT_EQUAL_INT(RADIO_SIM_ERROR, radio_sim_init(&state));
}

static void test_component_init_failure_paths(void)
{
    const component_interface_t *interface = get_component_interface();
    component_state_t *state = NULL;

    failure = FAIL_MALLOC;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, interface->init(&state));

    reset_faults();
    failure = FAIL_MUTEX;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, interface->init(&state));
    TEST_ASSERT_NULL(state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_radio_init_dependency_failures);
    RUN_TEST(test_component_init_failure_paths);
    return UNITY_END();
}
