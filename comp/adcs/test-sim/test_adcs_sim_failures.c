/*
 * Fault-injection tests for the ADCS component simulator.
 *
 * adcs_sim.c is compiled directly into this binary (not dlopen'd) and linked
 * against the stub simulith_transport and simulith_42_send definitions below
 * instead of the real libsimulith.so, so every transport and actuator-command
 * failure path can be forced deterministically. malloc is wrapped for the
 * same reason, to exercise adcs_sim_component_create's allocation-failure
 * branch without relying on real memory pressure.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "simulith_component.h"
#include "simulith_transport.h"
#include "simulith_42_context.h"
#include "simulith_42_commands.h"

#include "adcs_sim.h"
#include "adcs_device.h"

const component_interface_t *get_component_interface(void);

static int malloc_should_fail;

static int transport_send_calls;
static int transport_send_failure_call;

static int transport_complete_calls;
static int transport_complete_failure_call;

static int transport_receive_calls;
static int injected_request_call;
static uint8_t injected_request[ADCS_DEVICE_CMD_SIZE];
static size_t injected_request_len;
static int receive_request_should_error;

static int mtb_send_calls;
static int mtb_send_failure_call;

static int wheel_send_calls;
static int wheel_send_failure_call;

void *__real_malloc(size_t size);

void *__wrap_malloc(size_t size)
{
    return malloc_should_fail ? NULL : __real_malloc(size);
}

int simulith_transport_init(transport_port_t *port)
{
    if (port) port->init = SIMULITH_TRANSPORT_INITIALIZED;
    return SIMULITH_TRANSPORT_SUCCESS;
}

int simulith_transport_close(transport_port_t *port)
{
    if (port) port->init = 0;
    return SIMULITH_TRANSPORT_SUCCESS;
}

int simulith_transport_send(transport_port_t *port, const uint8_t *data, size_t length)
{
    (void)port;
    (void)data;
    transport_send_calls++;
    if (transport_send_failure_call && transport_send_calls == transport_send_failure_call)
        return SIMULITH_TRANSPORT_ERROR;
    return (int)length;
}

int simulith_transport_receive_request(transport_port_t *port, uint8_t *data, size_t max_len,
                                       uint64_t *transaction_id)
{
    (void)port;
    (void)max_len;
    transport_receive_calls++;
    if (transaction_id) *transaction_id = (uint64_t)transport_receive_calls;
    if (receive_request_should_error)
        return SIMULITH_TRANSPORT_ERROR;
    if (injected_request_call && transport_receive_calls == injected_request_call)
    {
        memcpy(data, injected_request, injected_request_len);
        return (int)injected_request_len;
    }
    return 0;
}

int simulith_transport_wait_for_request(transport_port_t *const ports[], size_t port_count,
                                        int interrupt_fd)
{
    (void)ports;
    (void)port_count;
    (void)interrupt_fd;
    return SIMULITH_TRANSPORT_READY;
}

int simulith_transport_complete_request(transport_port_t *port, uint64_t transaction_id,
                                        int status)
{
    (void)port;
    (void)transaction_id;
    (void)status;
    transport_complete_calls++;
    if (transport_complete_failure_call && transport_complete_calls == transport_complete_failure_call)
        return SIMULITH_TRANSPORT_ERROR;
    return SIMULITH_TRANSPORT_SUCCESS;
}

int simulith_42_send_mtb_command(int spacecraft_id, const double dipole[3], int enable_mask)
{
    (void)spacecraft_id;
    (void)dipole;
    (void)enable_mask;
    mtb_send_calls++;
    if (mtb_send_failure_call && mtb_send_calls == mtb_send_failure_call)
        return -1;
    return 0;
}

int simulith_42_send_wheel_command(int spacecraft_id, const double torque[4], int enable_mask)
{
    (void)spacecraft_id;
    (void)torque;
    (void)enable_mask;
    wheel_send_calls++;
    if (wheel_send_failure_call && wheel_send_calls == wheel_send_failure_call)
        return -1;
    return 0;
}

static const component_interface_t *g_iface;

void setUp(void)
{
    malloc_should_fail = 0;
    transport_send_calls = 0;
    transport_send_failure_call = 0;
    transport_complete_calls = 0;
    transport_complete_failure_call = 0;
    transport_receive_calls = 0;
    injected_request_call = 0;
    injected_request_len = 0;
    receive_request_should_error = 0;
    mtb_send_calls = 0;
    mtb_send_failure_call = 0;
    wheel_send_calls = 0;
    wheel_send_failure_call = 0;
}

void tearDown(void) {}

static simulith_42_context_t zero_ctx(void)
{
    simulith_42_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.valid = 0;
    return ctx;
}

static void arm_controller(adcs_sim_state_t *as, int mode, uint64_t tick_ns)
{
    as->controller_active = 1;
    as->current_mode = mode;
    as->control_deadline_valid = 1;
    as->next_control_update_ns = tick_ns;
}

static void encode_command(uint8_t *buf, uint16_t cmd_id, uint16_t payload)
{
    buf[0] = ADCS_DEVICE_HDR_0;
    buf[1] = ADCS_DEVICE_HDR_1;
    buf[2] = (uint8_t)((cmd_id >> 8) & 0xFF);
    buf[3] = (uint8_t)(cmd_id & 0xFF);
    buf[4] = (uint8_t)((payload >> 8) & 0xFF);
    buf[5] = (uint8_t)(payload & 0xFF);
    buf[6] = ADCS_DEVICE_TRAILER_0;
    buf[7] = ADCS_DEVICE_TRAILER_1;
}

static void inject_command(uint16_t cmd_id, uint16_t payload)
{
    encode_command(injected_request, cmd_id, payload);
    injected_request_len = ADCS_DEVICE_CMD_SIZE;
    injected_request_call = 1;
}

/* -------------------------------------------------------------------------
 * Allocation failure
 * -------------------------------------------------------------------------*/
static void test_create_fails_when_malloc_fails(void)
{
    malloc_should_fail = 1;
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, g_iface->create(&state));
    TEST_ASSERT_NULL(state);
}

/* -------------------------------------------------------------------------
 * Controller actuator-command failures
 * -------------------------------------------------------------------------*/
static void test_mode0_disabled_reports_error_when_wheel_send_fails(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 0, 300000000ULL);
    wheel_send_failure_call = 1;

    simulith_42_context_t ctx = {0}; ctx.valid = 1;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->actuate(state, 300000000ULL, &ctx));
    g_iface->destroy(state);
}

static void test_mode0_disabled_reports_error_when_mtb_send_fails(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 0, 300000000ULL);
    mtb_send_failure_call = 1;

    simulith_42_context_t ctx = {0}; ctx.valid = 1;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->actuate(state, 300000000ULL, &ctx));
    g_iface->destroy(state);
}

static void test_mode1_bdot_reports_error_when_mtb_send_fails(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 1, 300000000ULL);
    mtb_send_failure_call = 1;

    simulith_42_context_t ctx = {0}; ctx.valid = 1; ctx.mag_field_body[2] = 1.0;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->actuate(state, 300000000ULL, &ctx));
    g_iface->destroy(state);
}

static void test_mode1_bdot_reports_error_when_wheel_send_fails(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 1, 300000000ULL);
    wheel_send_failure_call = 1;

    simulith_42_context_t ctx = {0}; ctx.valid = 1; ctx.mag_field_body[2] = 1.0;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->actuate(state, 300000000ULL, &ctx));
    g_iface->destroy(state);
}

static void test_mode3_point_vector_saturated_mtb_send_fails(void)
{
    /* Large attitude error saturates the wheels, taking the "wheels_saturated
     * || rate > 0.1" branch that sends a non-zero MTB dipole. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 3, 300000000ULL);
    mtb_send_failure_call = 1;

    simulith_42_context_t ctx = {0};
    ctx.valid = 1;
    ctx.qn[0] = 1.0;
    ctx.pos_n[2] = -1.0;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->actuate(state, 300000000ULL, &ctx));
    g_iface->destroy(state);
}

static void test_mode3_point_vector_unsaturated_mtb_send_fails(void)
{
    /* Zero rates and an aligned target keep wheels unsaturated, taking the
     * else branch that sends a zero MTB dipole. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 3, 300000000ULL);
    mtb_send_failure_call = 1;

    simulith_42_context_t ctx = {0};
    ctx.valid = 1;
    ctx.qn[0] = 1.0;
    ctx.pos_n[0] = -1.0; /* nadir_inertial = (1,0,0): aligned with +X, zero error */
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->actuate(state, 300000000ULL, &ctx));
    g_iface->destroy(state);
}

static void test_mode3_point_vector_reports_error_when_wheel_send_fails(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 3, 300000000ULL);
    wheel_send_failure_call = 1;

    simulith_42_context_t ctx = {0};
    ctx.valid = 1;
    ctx.qn[0] = 1.0;
    ctx.pos_n[2] = -1.0;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->actuate(state, 300000000ULL, &ctx));
    g_iface->destroy(state);
}

static void test_mode2_eclipse_reports_error_when_bdot_mtb_send_fails(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 2, 300000000ULL);
    mtb_send_failure_call = 1;

    simulith_42_context_t ctx = {0};
    ctx.valid = 1;
    ctx.eclipse = 1;
    ctx.mag_field_body[2] = 1.0;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->actuate(state, 300000000ULL, &ctx));
    g_iface->destroy(state);
}

static void test_mode2_eclipse_reports_error_when_wheel_send_fails(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 2, 300000000ULL);
    wheel_send_failure_call = 1;

    simulith_42_context_t ctx = {0};
    ctx.valid = 1;
    ctx.eclipse = 1;
    ctx.mag_field_body[2] = 1.0;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->actuate(state, 300000000ULL, &ctx));
    g_iface->destroy(state);
}

static void test_mode2_hybrid_saturated_reports_error_when_mtb_send_fails(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 2, 300000000ULL);
    mtb_send_failure_call = 1;

    simulith_42_context_t ctx = {0};
    ctx.valid = 1;
    ctx.sun_vector_body[0] = 0.707;
    ctx.sun_vector_body[1] = 0.707;
    ctx.mag_field_body[2] = 1.0;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->actuate(state, 300000000ULL, &ctx));
    g_iface->destroy(state);
}

static void test_mode2_hybrid_unsaturated_reports_error_when_mtb_send_fails(void)
{
    /* Sun aligned with the target axis and zero rates keep wheels unsaturated
     * and rates low, taking the "disable MTBs" else branch. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 2, 300000000ULL);
    mtb_send_failure_call = 1;

    simulith_42_context_t ctx = {0};
    ctx.valid = 1;
    ctx.sun_vector_body[0] = 1.0;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->actuate(state, 300000000ULL, &ctx));
    g_iface->destroy(state);
}

static void test_mode2_hybrid_reports_error_when_wheel_send_fails(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 2, 300000000ULL);
    wheel_send_failure_call = 1;

    simulith_42_context_t ctx = {0};
    ctx.valid = 1;
    ctx.sun_vector_body[0] = 0.707;
    ctx.sun_vector_body[1] = 0.707;
    ctx.mag_field_body[2] = 1.0;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->actuate(state, 300000000ULL, &ctx));
    g_iface->destroy(state);
}

static void test_actuator_reset_pending_reports_error_when_wheel_send_fails(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    as->actuator_reset_pending = 1U;
    wheel_send_failure_call = 1;

    simulith_42_context_t ctx = zero_ctx();
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->actuate(state, 300000000ULL, &ctx));
    TEST_ASSERT_EQUAL_UINT8(1, as->actuator_reset_pending);
    g_iface->destroy(state);
}

static void test_actuator_reset_pending_reports_error_when_mtb_send_fails(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    as->actuator_reset_pending = 1U;
    mtb_send_failure_call = 1;

    simulith_42_context_t ctx = zero_ctx();
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->actuate(state, 300000000ULL, &ctx));
    TEST_ASSERT_EQUAL_UINT8(1, as->actuator_reset_pending);
    g_iface->destroy(state);
}

/* -------------------------------------------------------------------------
 * Wire protocol transport failures
 * -------------------------------------------------------------------------*/
static void test_service_reports_error_when_echo_send_fails(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    inject_command(ADCS_DEVICE_NOOP_CMD, 0);
    transport_send_failure_call = 1; /* the echo is the first send */

    simulith_42_context_t ctx = zero_ctx();
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->service(state, 0ULL, &ctx));
    g_iface->destroy(state);
}

static void test_service_reports_error_when_hk_send_fails(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    inject_command(ADCS_DEVICE_REQ_HK_CMD, 0);
    transport_send_failure_call = 2; /* echo succeeds, HK frame send fails */

    simulith_42_context_t ctx = zero_ctx();
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->service(state, 0ULL, &ctx));
    g_iface->destroy(state);
}

static void test_service_reports_error_when_data_send_fails(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    inject_command(ADCS_DEVICE_GET_CSS_CMD, 0);
    transport_send_failure_call = 2; /* echo succeeds, data frame send fails */

    simulith_42_context_t ctx = zero_ctx();
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->service(state, 0ULL, &ctx));
    g_iface->destroy(state);
}

static void test_service_reports_error_when_complete_request_fails(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    inject_command(ADCS_DEVICE_NOOP_CMD, 0);
    transport_complete_failure_call = 1;

    simulith_42_context_t ctx = zero_ctx();
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->service(state, 0ULL, &ctx));
    g_iface->destroy(state);
}

static void test_service_reports_error_when_receive_request_fails(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    receive_request_should_error = 1;

    simulith_42_context_t ctx = zero_ctx();
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->service(state, 0ULL, &ctx));
    g_iface->destroy(state);
}

int main(void)
{
    g_iface = get_component_interface();
    if (!g_iface)
    {
        fprintf(stderr, "get_component_interface() returned NULL\n");
        return 1;
    }

    UNITY_BEGIN();

    RUN_TEST(test_create_fails_when_malloc_fails);

    RUN_TEST(test_mode0_disabled_reports_error_when_wheel_send_fails);
    RUN_TEST(test_mode0_disabled_reports_error_when_mtb_send_fails);
    RUN_TEST(test_mode1_bdot_reports_error_when_mtb_send_fails);
    RUN_TEST(test_mode1_bdot_reports_error_when_wheel_send_fails);
    RUN_TEST(test_mode3_point_vector_saturated_mtb_send_fails);
    RUN_TEST(test_mode3_point_vector_unsaturated_mtb_send_fails);
    RUN_TEST(test_mode3_point_vector_reports_error_when_wheel_send_fails);
    RUN_TEST(test_mode2_eclipse_reports_error_when_bdot_mtb_send_fails);
    RUN_TEST(test_mode2_eclipse_reports_error_when_wheel_send_fails);
    RUN_TEST(test_mode2_hybrid_saturated_reports_error_when_mtb_send_fails);
    RUN_TEST(test_mode2_hybrid_unsaturated_reports_error_when_mtb_send_fails);
    RUN_TEST(test_mode2_hybrid_reports_error_when_wheel_send_fails);
    RUN_TEST(test_actuator_reset_pending_reports_error_when_wheel_send_fails);
    RUN_TEST(test_actuator_reset_pending_reports_error_when_mtb_send_fails);

    RUN_TEST(test_service_reports_error_when_echo_send_fails);
    RUN_TEST(test_service_reports_error_when_hk_send_fails);
    RUN_TEST(test_service_reports_error_when_data_send_fails);
    RUN_TEST(test_service_reports_error_when_complete_request_fails);
    RUN_TEST(test_service_reports_error_when_receive_request_fails);

    return UNITY_END();
}
