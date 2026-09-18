/*
 * Fault-injection tests for the demo component simulator.
 *
 * demo_sim.c is compiled directly into this executable (not dlopen'd) so the
 * transport functions it calls can be replaced with scripted mocks. This is
 * the only way to force simulith_transport_send/_complete_request/_receive_
 * request into the error returns that a real ZMQ_PAIR transport essentially
 * never produces in a fast unit test, and to force malloc() failure in
 * create(). Mirrors comp/radio/test-sim/test_radio_sim_failures.c.
 */
#include "demo_sim.h"
#include "simulith_component.h"
#include "simulith_transport.h"
#include "unity.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

const component_interface_t *get_component_interface(void);

static int malloc_should_fail;
static int send_calls;
static int send_failure_call;
static int complete_calls;
static int complete_failure_call;
static int request_calls;
static int request_target_call;
static int request_negative_call;
static uint8_t request_data[DEMO_DEVICE_CMD_SIZE];
static size_t request_length;

void *__real_malloc(size_t size);

void *__wrap_malloc(size_t size)
{
    return malloc_should_fail ? NULL : __real_malloc(size);
}

int simulith_transport_init(transport_port_t *port)
{
    if (port)
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
    send_calls++;
    return send_calls == send_failure_call ? SIMULITH_TRANSPORT_ERROR : (int)length;
}

int simulith_transport_receive(transport_port_t *port, uint8_t *data, size_t max_len)
{
    (void)port;
    (void)data;
    (void)max_len;
    return 0;
}

int simulith_transport_receive_request(transport_port_t *port, uint8_t *data, size_t max_len,
                                       uint64_t *transaction_id)
{
    (void)port;
    (void)max_len;
    request_calls++;
    if (transaction_id)
        *transaction_id = (uint64_t)request_calls;
    if (request_calls == request_negative_call)
        return SIMULITH_TRANSPORT_ERROR;
    if (request_calls == request_target_call)
    {
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
    return SIMULITH_TRANSPORT_INTERRUPTED;
}

int simulith_transport_complete_request(transport_port_t *port, uint64_t transaction_id,
                                        int status)
{
    (void)port;
    (void)transaction_id;
    (void)status;
    complete_calls++;
    return complete_calls == complete_failure_call ?
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

void setUp(void)
{
    malloc_should_fail = 0;
    send_calls = 0;
    send_failure_call = 0;
    complete_calls = 0;
    complete_failure_call = 0;
    request_calls = 0;
    request_target_call = 0;
    request_negative_call = 0;
    request_length = 0;
}

void tearDown(void) {}

static void script_command(uint16_t cmd_id, uint16_t payload)
{
    request_target_call = 1;
    request_length = DEMO_DEVICE_CMD_SIZE;
    request_data[0] = DEMO_DEVICE_HDR_0;
    request_data[1] = DEMO_DEVICE_HDR_1;
    request_data[2] = (uint8_t)(cmd_id >> 8);
    request_data[3] = (uint8_t)cmd_id;
    request_data[4] = (uint8_t)(payload >> 8);
    request_data[5] = (uint8_t)payload;
    request_data[6] = DEMO_DEVICE_TRAILER_0;
    request_data[7] = DEMO_DEVICE_TRAILER_1;
}

static void test_create_fails_when_malloc_fails(void)
{
    const component_interface_t *interface = get_component_interface();
    component_state_t *state = NULL;

    malloc_should_fail = 1;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, interface->create(&state));
    TEST_ASSERT_NULL(state);
}

static void test_service_echo_send_failure_returns_component_error(void)
{
    /* Covers the echo-send failure guard in handle_command (the transaction
     * is well-framed, but the first simulith_transport_send() call, which
     * echoes the command back, fails). */
    const component_interface_t *interface = get_component_interface();
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, interface->create(&state));

    script_command(DEMO_DEVICE_NOOP_CMD, 0);
    send_failure_call = 1;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, interface->service(state, 0, NULL));

    interface->destroy(state);
}

static void test_service_hk_send_failure_returns_component_error(void)
{
    /* Echo (send call 1) succeeds; send_housekeeping's frame (send call 2)
     * fails, covering both its own success-ternary error arm and the
     * REQ_HK_CMD case's failure guard in handle_command. */
    const component_interface_t *interface = get_component_interface();
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, interface->create(&state));

    script_command(DEMO_DEVICE_REQ_HK_CMD, 0);
    send_failure_call = 2;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, interface->service(state, 0, NULL));

    interface->destroy(state);
}

static void test_service_data_send_failure_returns_component_error(void)
{
    /* Same shape as the HK case, but for send_demo_data's REQ_DATA_CMD
     * frame. */
    const component_interface_t *interface = get_component_interface();
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, interface->create(&state));

    script_command(DEMO_DEVICE_REQ_DATA_CMD, 0);
    send_failure_call = 2;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, interface->service(state, 0, NULL));

    interface->destroy(state);
}

static void test_service_completion_failure_returns_component_error(void)
{
    /* A fully successful command still surfaces COMPONENT_ERROR when the
     * final completion acknowledgement fails to send. */
    const component_interface_t *interface = get_component_interface();
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, interface->create(&state));

    script_command(DEMO_DEVICE_NOOP_CMD, 0);
    complete_failure_call = 1;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, interface->service(state, 0, NULL));

    interface->destroy(state);
}

static void test_service_negative_receive_returns_component_error(void)
{
    /* Covers the real-transport-failure path in service(): receive_request
     * itself returns a negative status rather than 0 (idle) or a byte
     * count. */
    const component_interface_t *interface = get_component_interface();
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, interface->create(&state));

    request_negative_call = 1;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, interface->service(state, 0, NULL));

    interface->destroy(state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_create_fails_when_malloc_fails);
    RUN_TEST(test_service_echo_send_failure_returns_component_error);
    RUN_TEST(test_service_hk_send_failure_returns_component_error);
    RUN_TEST(test_service_data_send_failure_returns_component_error);
    RUN_TEST(test_service_completion_failure_returns_component_error);
    RUN_TEST(test_service_negative_receive_returns_component_error);
    return UNITY_END();
}
