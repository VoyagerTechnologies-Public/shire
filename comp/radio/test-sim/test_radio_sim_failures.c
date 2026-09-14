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
static int request_calls;
static int request_target_call;
static uint8_t request_data[32];
static size_t request_length;
static int send_failure;
static int completion_calls;
static int completion_failure_call;
static int wait_result;

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
    return send_failure ? SIMULITH_TRANSPORT_ERROR : (int)length;
}

int simulith_transport_receive(transport_port_t *port, uint8_t *data, size_t length)
{
    (void)port;
    (void)data;
    (void)length;
    return 0;
}

int simulith_transport_receive_request(transport_port_t *port, uint8_t *data, size_t length,
                                       uint64_t *transaction_id)
{
    (void)port;
    request_calls++;
    if (transaction_id)
        *transaction_id = (uint64_t)request_calls;
    if (request_calls == request_target_call)
    {
        TEST_ASSERT_LESS_OR_EQUAL_size_t(length, request_length);
        memcpy(data, request_data, request_length);
        return (int)request_length;
    }
    return 0;
}

int simulith_transport_wait_for_request(transport_port_t *const ports[],
                                        size_t port_count, int interrupt_fd)
{
    (void)ports;
    (void)port_count;
    (void)interrupt_fd;
    return wait_result;
}

int simulith_transport_complete_request(transport_port_t *port, uint64_t transaction_id,
                                        int status)
{
    (void)port;
    (void)transaction_id;
    (void)status;
    completion_calls++;
    return completion_calls == completion_failure_call ?
        SIMULITH_TRANSPORT_ERROR : SIMULITH_TRANSPORT_SUCCESS;
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
    request_calls = 0;
    request_target_call = 0;
    request_length = 0;
    send_failure = 0;
    completion_calls = 0;
    completion_failure_call = 0;
    wait_result = SIMULITH_TRANSPORT_INTERRUPTED;
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
    request_calls = 0;
    request_target_call = 0;
    request_length = 0;
    send_failure = 0;
    completion_calls = 0;
    completion_failure_call = 0;
    wait_result = SIMULITH_TRANSPORT_INTERRUPTED;
}

static void initialize_service_state(radio_sim_state_t *state)
{
    memset(state, 0, sizeof(*state));
    TEST_ASSERT_EQUAL_INT(0, __real_pthread_mutex_init(&state->buffer_mutex, NULL));
    state->power_gpio.pin = RADIO_CFG_GPIO_POWER_PIN;
    state->interrupt_gpio.pin = RADIO_CFG_GPIO_INTERRUPT_PIN;
    state->power_gpio.value = 1;
    state->config.Mode = RADIO_MODE_DUPLEX;
    state->hk.Mode = RADIO_MODE_DUPLEX;
    state->spi_device.init = SIMULITH_TRANSPORT_INITIALIZED;
    state->power_gpio_device.init = SIMULITH_TRANSPORT_INITIALIZED;
    state->interrupt_gpio_device.init = SIMULITH_TRANSPORT_INITIALIZED;
}

static void script_spi_command(uint8_t command, uint16_t payload_length,
                               const uint8_t *payload)
{
    request_target_call = 3;
    request_length = (size_t)payload_length + 5U;
    request_data[0] = RADIO_DEVICE_HDR;
    request_data[1] = command;
    request_data[2] = (uint8_t)(payload_length >> 8);
    request_data[3] = (uint8_t)payload_length;
    if (payload_length > 0 && payload)
        memcpy(&request_data[4], payload, payload_length);
    request_data[request_length - 1U] = RADIO_DEVICE_TRAILER;
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
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, interface->create(&state));

    reset_faults();
    failure = FAIL_MUTEX;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, interface->create(&state));
    TEST_ASSERT_NULL(state);
}

static void test_component_wait_tick_and_service_failures(void)
{
    const component_interface_t *interface = get_component_interface();
    radio_sim_state_t state;
    initialize_service_state(&state);

    wait_result = COMPONENT_WORK;
    TEST_ASSERT_EQUAL_INT(COMPONENT_WORK,
                          interface->wait_for_service(
                              (component_state_t *)&state, 7));

    state.next_interrupt_update_ns = UINT64_MAX - 1U;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS,
                          interface->on_tick((component_state_t *)&state,
                                             UINT64_MAX, NULL));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, state.next_interrupt_update_ns);

    script_spi_command(RADIO_DEVICE_REQ_HK_CMD, 0, NULL);
    send_failure = 1;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          interface->service((component_state_t *)&state,
                                             0, NULL));

    pthread_mutex_destroy(&state.buffer_mutex);
    reset_faults();
    initialize_service_state(&state);
    uint8_t oversized_request[2] = {0x0F, 0xFD};
    script_spi_command(RADIO_DEVICE_RECEIVE_CMD, 2, oversized_request);
    TEST_ASSERT_EQUAL_INT(COMPONENT_WORK,
                          interface->service((component_state_t *)&state,
                                             0, NULL));

    pthread_mutex_destroy(&state.buffer_mutex);
}

static void test_component_completion_failures_propagate(void)
{
    const component_interface_t *interface = get_component_interface();
    radio_sim_state_t state;

    for (int target = 1; target <= 3; ++target)
    {
        reset_faults();
        initialize_service_state(&state);
        request_target_call = target;
        request_length = target == 3 ? 5U : 2U;
        if (target == 1)
        {
            request_data[0] = 0;
            request_data[1] = (uint8_t)state.power_gpio.pin;
        }
        else if (target == 2)
        {
            request_data[0] = 0;
            request_data[1] = (uint8_t)state.interrupt_gpio.pin;
        }
        else
        {
            script_spi_command(RADIO_DEVICE_NOOP_CMD, 0, NULL);
        }
        completion_failure_call = 1;
        TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                              interface->service((component_state_t *)&state,
                                                 0, NULL));
        pthread_mutex_destroy(&state.buffer_mutex);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_radio_init_dependency_failures);
    RUN_TEST(test_component_init_failure_paths);
    RUN_TEST(test_component_wait_tick_and_service_failures);
    RUN_TEST(test_component_completion_failures_propagate);
    return UNITY_END();
}
