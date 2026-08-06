/*
 * Unity test for the ADCS component simulator (comp/adcs/sim/adcs_sim.c).
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "unity.h"
#include "simulith.h"
#include "simulith_component.h"
#include "simulith_transport.h"
#include "simulith_42_context.h"

#include "adcs_sim.h"
#include "adcs_device.h"
#include "simulith_42_commands.h"

#ifndef ADCS_SIM_SO_PATH
#error "ADCS_SIM_SO_PATH must be defined (path to adcs_sim.so)"
#endif

typedef const component_interface_t *(*get_component_interface_fn)(void);

static void             *g_handle              = NULL;
static const component_interface_t *g_iface    = NULL;
/* Holds the live state for any test that doesn't clean up inline, so
 * tearDown() can release the IPC socket even when Unity longjmps on failure. */
static component_state_t *g_state_under_test   = NULL;

void setUp(void) {}

void tearDown(void)
{
    if (g_state_under_test)
    {
        g_iface->cleanup(g_state_under_test);
        g_state_under_test = NULL;
    }
}

/* -------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/
static void adcs_sim_uart_address(char *out, size_t cap)
{
    snprintf(out, cap, "ipc:///tmp/simulith_pub:%d",
             SIMULITH_UART_BASE_PORT + ADCS_CFG_HANDLE);
}

static int open_client_port(transport_port_t *port, const char *name)
{
    memset(port, 0, sizeof(*port));
    snprintf(port->name, sizeof(port->name), "%s", name);
    adcs_sim_uart_address(port->address, sizeof(port->address));
    port->is_server = 0;
    return simulith_transport_init(port);
}

static void encode_command(uint8_t *buf, uint16_t cmd_id, uint16_t payload)
{
    buf[0] = ADCS_DEVICE_HDR_0;
    buf[1] = ADCS_DEVICE_HDR_1;
    buf[2] = (uint8_t)((cmd_id >> 8) & 0xFF);
    buf[3] = (uint8_t)(cmd_id  & 0xFF);
    buf[4] = (uint8_t)((payload >> 8) & 0xFF);
    buf[5] = (uint8_t)(payload & 0xFF);
    buf[6] = ADCS_DEVICE_TRAILER_0;
    buf[7] = ADCS_DEVICE_TRAILER_1;
}

/* Read up to cap bytes from port, polling for at most ~50 ms. */
static size_t drain_all(transport_port_t *port, uint8_t *out, size_t cap)
{
    size_t total = 0;
    int    idle  = 0;
    for (int attempt = 0; attempt < 50 && idle < 5; ++attempt)
    {
        if (simulith_transport_available(port) > 0)
        {
            int got = simulith_transport_receive(port, out + total, cap - total);
            if (got <= 0) break;
            total += (size_t)got;
            idle = 0;
            if (total >= cap) return total;
        }
        else
        {
            idle++;
            usleep(1000);
        }
    }
    return total;
}

/* Build a zero-initialized 42 context with valid=0.  Passing this instead of
 * NULL prevents a NULL-deref in adcs_controller_update, which computes
 * nadir_inertial from context_42->pos_n before any guard check. */
static simulith_42_context_t zero_ctx(void)
{
    simulith_42_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.valid = 0;
    return ctx;
}

/* Arm the controller in a given mode so it fires at tick_ns.
 * Sets last_control_time to exactly one required_dt before tick time so
 * dt == required_dt, which satisfies the "dt >= required_dt" gate. */
static void arm_controller(adcs_sim_state_t *as, int mode, uint64_t tick_ns)
{
    double tick_secs    = (double)tick_ns / 1e9;
    double required_dt  = 1.0 / ADCS_CONTROLLER_UPDATE_RATE_HZ;
    as->controller_active  = 1;
    as->current_mode       = mode;
    as->last_control_time  = tick_secs - required_dt; /* > 0 for tick_ns >= 200ms */
}

/* Drain all pending entries from the shared command queue.  Call this before
 * any convergence-intent test tick so stale commands from prior tests do not
 * contaminate the assertions. */
static void drain_command_queue(void)
{
    simulith_42_command_t cmd;
    while (dequeue_command(&cmd) == 0)
        ;
}

/* -------------------------------------------------------------------------
 * Lifecycle / Loader
 * -------------------------------------------------------------------------*/
static void test_dlopen_adcs_sim_so(void)
{
    dlerror();
    void       *h   = dlopen(ADCS_SIM_SO_PATH, RTLD_NOW);
    const char *err = dlerror();
    if (!h)
        TEST_FAIL_MESSAGE(err ? err : "dlopen returned NULL");
    TEST_ASSERT_EQUAL_INT(0, dlclose(h));
}

static void test_get_component_interface_symbol(void)
{
    TEST_ASSERT_NOT_NULL(g_iface);
    TEST_ASSERT_NOT_NULL(g_iface->name);
    TEST_ASSERT_NOT_NULL(g_iface->description);
    TEST_ASSERT_NOT_NULL(g_iface->init);
    TEST_ASSERT_NOT_NULL(g_iface->tick);
    TEST_ASSERT_NOT_NULL(g_iface->cleanup);
    TEST_ASSERT_EQUAL_STRING("adcs_sim", g_iface->name);
}

static void test_init_returns_success_and_state(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    TEST_ASSERT_NOT_NULL(state);
    g_iface->cleanup(state);
}

/* Pass a zero-initialized (non-NULL) context — avoids the NULL-deref in
 * adcs_controller_update's nadir_inertial initializer. */
static void test_tick_does_not_crash_with_zero_42_context(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    simulith_42_context_t ctx = zero_ctx();
    g_iface->tick(state, 0ULL, &ctx);
    g_iface->tick(state, 100000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_cleanup_releases_ipc_socket(void)
{
    /* If cleanup leaks the IPC socket a second init in the same process fails
     * to rebind.  This guards against that regression. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_iface->cleanup(state);

    state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_iface->cleanup(state);
}

/* -------------------------------------------------------------------------
 * Tick data paths (42 context vs. fallback)
 * -------------------------------------------------------------------------*/
static void test_tick_with_42_context_scales_channels(void)
{
    /* When a valid 42 context is supplied adcs_sim maps SVB into Chan{1,2,3}:
     *   chan = (uint16_t)(svb * 10000.0 + 32768.0) */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    simulith_42_context_t ctx = {0};
    ctx.valid              = 1;
    ctx.sun_vector_body[0] = 0.5;   /* ->  5000 + 32768 = 37768 */
    ctx.sun_vector_body[1] = -0.25; /* -> -2500 + 32768 = 30268 */
    ctx.sun_vector_body[2] = 0.0;   /* ->     0 + 32768 = 32768 */

    g_iface->tick(state, 200000000ULL, &ctx); /* 200 ms >= 1/10 s gate */

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(37768, as->data.Chan1);
    TEST_ASSERT_EQUAL_UINT16(30268, as->data.Chan2);
    TEST_ASSERT_EQUAL_UINT16(32768, as->data.Chan3);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_tick_without_valid_42_context_uses_counter_channels(void)
{
    /* Fallback path (context_42->valid == 0): channels derived from
     * DeviceCounter as Chan1=ctr*1, Chan2=ctr*2, Chan3=ctr*3. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as  = (adcs_sim_state_t *)state;
    as->hk.DeviceCounter  = 5;

    simulith_42_context_t ctx = zero_ctx(); /* valid = 0 */
    g_iface->tick(state, 200000000ULL, &ctx);

    TEST_ASSERT_EQUAL_UINT16(5,  as->data.Chan1);
    TEST_ASSERT_EQUAL_UINT16(10, as->data.Chan2);
    TEST_ASSERT_EQUAL_UINT16(15, as->data.Chan3);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_tick_populates_hk_from_42_context(void)
{
    /* When context_42->valid, the HK struct is populated from 42 fields. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    simulith_42_context_t ctx = {0};
    ctx.valid          = 1;
    ctx.dyn_time       = 1234.5;
    ctx.eclipse        = 1;
    ctx.pos_n[0]       = 100.0; ctx.pos_n[1] = 200.0; ctx.pos_n[2] = 300.0;
    ctx.vel_n[0]       = 1.0;   ctx.vel_n[1] = 2.0;   ctx.vel_n[2] = 3.0;
    ctx.qn[0]          = 1.0f;
    ctx.wn[0]          = 0.01;  ctx.wn[1] = 0.02;     ctx.wn[2] = 0.03;
    ctx.sun_vector_body[0] = 1.0;

    g_iface->tick(state, 200000000ULL, &ctx);

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT32(1234, as->hk.GpsSeconds);
    TEST_ASSERT_EQUAL_UINT8(1, as->hk.Eclipse);
    TEST_ASSERT_EQUAL_UINT8(1, as->hk.AttitudeSource);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, as->hk.GpsPosition[0]);
    TEST_ASSERT_EQUAL_FLOAT(1.0f,   as->hk.Velocity[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, as->hk.Quaternion[0]);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

/* -------------------------------------------------------------------------
 * Wire protocol
 * -------------------------------------------------------------------------*/
static void test_wire_protocol_noop_echoes_command(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd[ADCS_DEVICE_CMD_SIZE];
    encode_command(cmd, ADCS_DEVICE_NOOP_CMD, 0);
    simulith_transport_send(&client, cmd, sizeof(cmd));
    usleep(2000);

    simulith_42_context_t ctx = zero_ctx();
    g_iface->tick(state, 100000000ULL, &ctx);

    uint8_t rx[ADCS_DEVICE_CMD_SIZE];
    size_t  n = drain_all(&client, rx, sizeof(rx));
    TEST_ASSERT_EQUAL_size_t(sizeof(cmd), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(cmd, rx, sizeof(cmd));

    simulith_transport_close(&client);
    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_wire_protocol_req_hk_returns_framed_hk(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd[ADCS_DEVICE_CMD_SIZE];
    encode_command(cmd, ADCS_DEVICE_REQ_HK_CMD, 0);
    simulith_transport_send(&client, cmd, sizeof(cmd));
    usleep(2000);

    simulith_42_context_t ctx = zero_ctx();
    g_iface->tick(state, 100000000ULL, &ctx);

    /* Expect: 8-byte echo + ADCS_DEVICE_HK_SIZE-byte HK frame */
    uint8_t rx[ADCS_DEVICE_CMD_SIZE + ADCS_DEVICE_HK_SIZE];
    size_t  n = drain_all(&client, rx, sizeof(rx));
    TEST_ASSERT_EQUAL_size_t(sizeof(rx), n);

    /* Echo matches command */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(cmd, rx, sizeof(cmd));

    /* HK frame header and trailer */
    const uint8_t *hk = rx + sizeof(cmd);
    TEST_ASSERT_EQUAL_HEX8(ADCS_DEVICE_HDR_0, hk[0]);
    TEST_ASSERT_EQUAL_HEX8(ADCS_DEVICE_HDR_1, hk[1]);
    TEST_ASSERT_EQUAL_HEX8(ADCS_DEVICE_TRAILER_0, hk[ADCS_DEVICE_HK_SIZE - 2]);
    TEST_ASSERT_EQUAL_HEX8(ADCS_DEVICE_TRAILER_1, hk[ADCS_DEVICE_HK_SIZE - 1]);

    simulith_transport_close(&client);
    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_wire_protocol_get_css_returns_data_frame(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    as->data.Chan1 = 0x1234;
    as->data.Chan2 = 0x5678;
    as->data.Chan3 = 0x9ABC;

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd[ADCS_DEVICE_CMD_SIZE];
    encode_command(cmd, ADCS_DEVICE_GET_CSS_CMD, 0);
    simulith_transport_send(&client, cmd, sizeof(cmd));
    usleep(2000);

    /* Tick at t=0: update gate does not fire, channels stay as pre-seeded. */
    simulith_42_context_t ctx = zero_ctx();
    g_iface->tick(state, 0ULL, &ctx);

    uint8_t rx[ADCS_DEVICE_CMD_SIZE + ADCS_DEVICE_DATA_SIZE];
    size_t  n = drain_all(&client, rx, sizeof(rx));
    TEST_ASSERT_EQUAL_size_t(sizeof(rx), n);

    const uint8_t *d = rx + sizeof(cmd);
    TEST_ASSERT_EQUAL_HEX8(ADCS_DEVICE_HDR_0, d[0]);
    TEST_ASSERT_EQUAL_HEX8(ADCS_DEVICE_HDR_1, d[1]);
    uint16_t c1 = (uint16_t)((d[2] << 8) | d[3]);
    uint16_t c2 = (uint16_t)((d[4] << 8) | d[5]);
    uint16_t c3 = (uint16_t)((d[6] << 8) | d[7]);
    TEST_ASSERT_EQUAL_HEX8(ADCS_DEVICE_TRAILER_0, d[8]);
    TEST_ASSERT_EQUAL_HEX8(ADCS_DEVICE_TRAILER_1, d[9]);
    TEST_ASSERT_EQUAL_UINT16(0x1234, c1);
    TEST_ASSERT_EQUAL_UINT16(0x5678, c2);
    TEST_ASSERT_EQUAL_UINT16(0x9ABC, c3);

    simulith_transport_close(&client);
    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_wire_protocol_get_imu_returns_data_frame(void)
{
    /* Exercises a different GET_* code in the same switch fall-through block. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd[ADCS_DEVICE_CMD_SIZE];
    encode_command(cmd, ADCS_DEVICE_GET_IMU_CMD, 0);
    simulith_transport_send(&client, cmd, sizeof(cmd));
    usleep(2000);

    simulith_42_context_t ctx = zero_ctx();
    g_iface->tick(state, 0ULL, &ctx);

    uint8_t rx[ADCS_DEVICE_CMD_SIZE + ADCS_DEVICE_DATA_SIZE];
    size_t  n = drain_all(&client, rx, sizeof(rx));
    TEST_ASSERT_EQUAL_size_t(sizeof(rx), n);

    const uint8_t *d = rx + sizeof(cmd);
    TEST_ASSERT_EQUAL_HEX8(ADCS_DEVICE_HDR_0,     d[0]);
    TEST_ASSERT_EQUAL_HEX8(ADCS_DEVICE_HDR_1,     d[1]);
    TEST_ASSERT_EQUAL_HEX8(ADCS_DEVICE_TRAILER_0, d[8]);
    TEST_ASSERT_EQUAL_HEX8(ADCS_DEVICE_TRAILER_1, d[9]);

    simulith_transport_close(&client);
    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_wire_protocol_set_mode_activates_controller(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd[ADCS_DEVICE_CMD_SIZE];
    encode_command(cmd, ADCS_DEVICE_SET_MODE_CMD, 1); /* mode 1 = bdot */
    simulith_transport_send(&client, cmd, sizeof(cmd));
    usleep(2000);

    simulith_42_context_t ctx = zero_ctx();
    g_iface->tick(state, 100000000ULL, &ctx);

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    TEST_ASSERT_EQUAL_INT(1, as->current_mode);
    TEST_ASSERT_EQUAL_INT(1, as->controller_active);

    simulith_transport_close(&client);
    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_wire_protocol_set_mode_zero_deactivates_controller(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    as->controller_active = 1; /* pre-activate */

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd[ADCS_DEVICE_CMD_SIZE];
    encode_command(cmd, ADCS_DEVICE_SET_MODE_CMD, 0); /* mode 0 = disabled */
    simulith_transport_send(&client, cmd, sizeof(cmd));
    usleep(2000);

    simulith_42_context_t ctx = zero_ctx();
    g_iface->tick(state, 100000000ULL, &ctx);

    TEST_ASSERT_EQUAL_INT(0, as->current_mode);
    TEST_ASSERT_EQUAL_INT(0, as->controller_active);

    simulith_transport_close(&client);
    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_wire_protocol_set_target_positive_x(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd[ADCS_DEVICE_CMD_SIZE];
    encode_command(cmd, ADCS_DEVICE_SET_TARGET_CMD, 1);
    simulith_transport_send(&client, cmd, sizeof(cmd));
    usleep(2000);

    simulith_42_context_t ctx = zero_ctx();
    g_iface->tick(state, 100000000ULL, &ctx);

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(1, as->hk.Target);

    simulith_transport_close(&client);
    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_wire_protocol_set_target_negative_x(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd[ADCS_DEVICE_CMD_SIZE];
    encode_command(cmd, ADCS_DEVICE_SET_TARGET_CMD, 2);
    simulith_transport_send(&client, cmd, sizeof(cmd));
    usleep(2000);

    simulith_42_context_t ctx = zero_ctx();
    g_iface->tick(state, 100000000ULL, &ctx);

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(2, as->hk.Target);

    simulith_transport_close(&client);
    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_wire_protocol_set_target_3_keeps_current(void)
{
    /* Payload 3 leaves g_inertial_target unchanged (CLI / manual override). */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd[ADCS_DEVICE_CMD_SIZE];
    encode_command(cmd, ADCS_DEVICE_SET_TARGET_CMD, 3);
    simulith_transport_send(&client, cmd, sizeof(cmd));
    usleep(2000);

    simulith_42_context_t ctx = zero_ctx();
    g_iface->tick(state, 100000000ULL, &ctx);

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(3, as->hk.Target);

    simulith_transport_close(&client);
    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_wire_protocol_unknown_cmd_echoed_only(void)
{
    /* A well-framed packet with an unknown cmd_id is echoed and counted
     * but no telemetry frame is appended (default switch arm). */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd[ADCS_DEVICE_CMD_SIZE];
    encode_command(cmd, 0x00FF, 0); /* outside any defined cmd_id */
    simulith_transport_send(&client, cmd, sizeof(cmd));
    usleep(2000);

    simulith_42_context_t ctx = zero_ctx();
    g_iface->tick(state, 100000000ULL, &ctx);

    uint8_t rx[ADCS_DEVICE_CMD_SIZE * 2];
    size_t  n = drain_all(&client, rx, sizeof(rx));
    TEST_ASSERT_EQUAL_size_t(sizeof(cmd), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(cmd, rx, sizeof(cmd));

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(1, as->hk.DeviceCounter);

    simulith_transport_close(&client);
    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_wire_protocol_short_packet_rejected(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t partial[4] = {ADCS_DEVICE_HDR_0, ADCS_DEVICE_HDR_1, 0x00, ADCS_DEVICE_NOOP_CMD};
    simulith_transport_send(&client, partial, sizeof(partial));
    usleep(2000);

    simulith_42_context_t ctx = zero_ctx();
    g_iface->tick(state, 100000000ULL, &ctx);

    uint8_t rx[16];
    TEST_ASSERT_EQUAL_size_t(0, drain_all(&client, rx, sizeof(rx)));

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, as->hk.DeviceCounter);

    simulith_transport_close(&client);
    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_wire_protocol_bad_header_rejected(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd[ADCS_DEVICE_CMD_SIZE] = {
        0xDE, 0xAD,
        0x00, ADCS_DEVICE_NOOP_CMD,
        0x00, 0x00,
        ADCS_DEVICE_TRAILER_0, ADCS_DEVICE_TRAILER_1
    };
    simulith_transport_send(&client, cmd, sizeof(cmd));
    usleep(2000);

    simulith_42_context_t ctx = zero_ctx();
    g_iface->tick(state, 100000000ULL, &ctx);

    uint8_t rx[16];
    TEST_ASSERT_EQUAL_size_t(0, drain_all(&client, rx, sizeof(rx)));

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, as->hk.DeviceCounter);

    simulith_transport_close(&client);
    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_wire_protocol_bad_trailer_rejected(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd[ADCS_DEVICE_CMD_SIZE] = {
        ADCS_DEVICE_HDR_0, ADCS_DEVICE_HDR_1,
        0x00, ADCS_DEVICE_NOOP_CMD,
        0x00, 0x00,
        0xBA, 0xD0
    };
    simulith_transport_send(&client, cmd, sizeof(cmd));
    usleep(2000);

    simulith_42_context_t ctx = zero_ctx();
    g_iface->tick(state, 100000000ULL, &ctx);

    uint8_t rx[16];
    TEST_ASSERT_EQUAL_size_t(0, drain_all(&client, rx, sizeof(rx)));

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, as->hk.DeviceCounter);

    simulith_transport_close(&client);
    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

/* -------------------------------------------------------------------------
 * Controller paths
 *
 * All controller tests drive adcs_sim_state_t directly (cast from the opaque
 * component_state_t*) rather than going through the wire protocol, so they
 * exercise the controller logic without requiring ZMQ round-trips.
 * -------------------------------------------------------------------------*/
static void test_controller_inactive_does_not_run(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as  = (adcs_sim_state_t *)state;
    as->last_control_time = 0.1;
    /* controller_active = 0 (default): update must return early */

    simulith_42_context_t ctx = {0}; ctx.valid = 1;
    g_iface->tick(state, 300000000ULL, &ctx);
    /* last_control_time unchanged: controller never ran */
    TEST_ASSERT_TRUE(fabs(as->last_control_time - 0.1) < 0.001);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_first_tick_initializes_timer(void)
{
    /* When last_control_time == 0.0 the controller initializes the timer and
     * returns without running.  After the tick, last_control_time == current. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    as->controller_active = 1;
    as->current_mode      = 1;
    /* last_control_time = 0.0 (default after init) */

    simulith_42_context_t ctx = {0}; ctx.valid = 1; ctx.mag_field_body[2] = 1.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    TEST_ASSERT_TRUE(fabs(as->last_control_time - 0.3) < 0.001);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_rate_too_low_does_not_run(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as  = (adcs_sim_state_t *)state;
    as->controller_active = 1;
    as->current_mode      = 1;
    as->last_control_time = 0.25; /* 250ms */

    simulith_42_context_t ctx = {0}; ctx.valid = 1;
    /* 300ms tick: dt = 0.05 s < required_dt (0.2 s), controller must skip */
    g_iface->tick(state, 300000000ULL, &ctx);
    TEST_ASSERT_TRUE(fabs(as->last_control_time - 0.25) < 0.001);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_time_backwards_resyncs(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as  = (adcs_sim_state_t *)state;
    as->controller_active = 1;
    as->current_mode      = 0;
    as->last_control_time = 5.0; /* future → dt < 0 at 300ms tick */

    simulith_42_context_t ctx = {0}; ctx.valid = 1;
    g_iface->tick(state, 300000000ULL, &ctx);
    /* Timer resynced to current_time */
    TEST_ASSERT_TRUE(fabs(as->last_control_time - 0.3) < 0.001);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_mode0_disabled(void)
{
    /* Mode 0 sends zero wheel and MTB commands; just verify no crash. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 0, 300000000ULL);

    simulith_42_context_t ctx = {0}; ctx.valid = 1;
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_mode1_bdot_high_rate(void)
{
    /* rate > ADCS_HIGH_RATE_THRESHOLD → high detumble gain branch. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 1, 300000000ULL);

    simulith_42_context_t ctx = {0};
    ctx.valid            = 1;
    ctx.wn[0]            = 1.0; /* > 0.5 threshold */
    ctx.mag_field_body[2] = 1.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_mode1_bdot_low_rate(void)
{
    /* rate < ADCS_HIGH_RATE_THRESHOLD → linear-interpolated gain branch. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 1, 300000000ULL);

    simulith_42_context_t ctx = {0};
    ctx.valid            = 1;
    ctx.wn[0]            = 0.1; /* < 0.5 threshold */
    ctx.mag_field_body[2] = 1.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_mode1_bdot_invalid_context(void)
{
    /* context_42->valid == 0 → bdot_controller returns early. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 1, 300000000ULL);

    simulith_42_context_t ctx = {0}; ctx.valid = 0;
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_mode2_sun_normal_case(void)
{
    /* Sun at 45° from +X → normal cross-product attitude error. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 2, 300000000ULL);

    simulith_42_context_t ctx = {0};
    ctx.valid              = 1;
    ctx.sun_vector_body[0] = 0.707;
    ctx.sun_vector_body[1] = 0.707;
    ctx.sun_vector_body[2] = 0.0;
    ctx.wn[0] = 0.0; ctx.wn[1] = 0.0; ctx.wn[2] = 0.0;
    ctx.mag_field_body[2] = 1.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_mode2_sun_nearly_aligned(void)
{
    /* sun_dot_target >= 1-EPS → attitude error zeroed (aligned case).
     * With zero rates and no saturation, MTBs are disabled (else branch). */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 2, 300000000ULL);

    simulith_42_context_t ctx = {0};
    ctx.valid              = 1;
    ctx.sun_vector_body[0] = 1.0;
    ctx.sun_vector_body[1] = 0.0;
    ctx.sun_vector_body[2] = 0.0;
    ctx.wn[0] = 0.0; ctx.wn[1] = 0.0; ctx.wn[2] = 0.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_mode2_sun_anti_aligned(void)
{
    /* sun_dot_target <= -(1-EPS) → anti-aligned special handling. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 2, 300000000ULL);

    simulith_42_context_t ctx = {0};
    ctx.valid              = 1;
    ctx.sun_vector_body[0] = -1.0;
    ctx.sun_vector_body[1] = 0.0;
    ctx.sun_vector_body[2] = 0.0;
    ctx.wn[0] = 0.0; ctx.wn[1] = 0.0; ctx.wn[2] = 0.0;
    ctx.mag_field_body[2] = 1.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_mode2_eclipse(void)
{
    /* eclipse == 1 → bdot rate-damping + zero wheel torques. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 2, 300000000ULL);

    simulith_42_context_t ctx = {0};
    ctx.valid         = 1;
    ctx.eclipse       = 1;
    ctx.wn[0]         = 0.3;
    ctx.mag_field_body[2] = 1.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_mode2_invalid_sun_vector(void)
{
    /* Zero sun vector → magnitude < 1e-6 → hybrid returns early. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 2, 300000000ULL);

    simulith_42_context_t ctx = {0};
    ctx.valid = 1;
    /* sun_vector_body all zero */
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_mode2_invalid_context(void)
{
    /* context_42->valid == 0 → hybrid sun-pointing returns early. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 2, 300000000ULL);

    simulith_42_context_t ctx = {0}; ctx.valid = 0;
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_mode2_high_rate_mtb_assist(void)
{
    /* rate_magnitude > 0.1 → MTB assist branch; rate > 0.2 → extra gain. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 2, 300000000ULL);

    simulith_42_context_t ctx = {0};
    ctx.valid              = 1;
    ctx.sun_vector_body[0] = 0.707;
    ctx.sun_vector_body[1] = 0.707;
    ctx.sun_vector_body[2] = 0.0;
    ctx.wn[0]  = 0.5; /* > 0.2 → both MTB-assist conditions fire */
    ctx.mag_field_body[2] = 1.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_mode3_nadir_dot1_branch(void)
{
    /* Identity quaternion → v1 == v2, dot1 == dot2, first (>=) branch taken
     * in rotate_inertial_to_body_safe. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 3, 300000000ULL);

    simulith_42_context_t ctx = {0};
    ctx.valid  = 1;
    ctx.qn[0]  = 1.0f; /* identity quaternion */
    /* pos_n = (-1,0,0) → nadir_inertial = (1,0,0): aligned with +X → zero error */
    ctx.pos_n[0] = -1.0; ctx.pos_n[1] = 0.0; ctx.pos_n[2] = 0.0;
    ctx.wn[0] = 0.0; ctx.wn[1] = 0.0; ctx.wn[2] = 0.0;
    ctx.mag_field_body[2] = 1.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_mode3_nadir_dot2_branch(void)
{
    /* 90° rotation around Y: q=(cos45°, 0, sin45°, 0).
     * With nadir_inertial=(0,0,1): v1[0]=-1, v2[0]=+1 → dot1 < dot2 →
     * else branch (v2 chosen) in rotate_inertial_to_body_safe. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 3, 300000000ULL);

    simulith_42_context_t ctx = {0};
    ctx.valid  = 1;
    ctx.qn[0]  = 0.7071068f; ctx.qn[1] = 0.0f;
    ctx.qn[2]  = 0.7071068f; ctx.qn[3] = 0.0f;
    /* pos_n = (0,0,-1) → nadir_inertial = (0,0,1) */
    ctx.pos_n[0] = 0.0; ctx.pos_n[1] = 0.0; ctx.pos_n[2] = -1.0;
    ctx.wn[0] = 0.0; ctx.wn[1] = 0.0; ctx.wn[2] = 0.0;
    ctx.mag_field_body[2] = 1.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_mode3_nadir_high_rate_mtb_assist(void)
{
    /* rate_magnitude > 0.1 → MTB assist branch in adcs_point_vector_controller. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 3, 300000000ULL);

    simulith_42_context_t ctx = {0};
    ctx.valid  = 1;
    ctx.qn[0]  = 1.0f;
    ctx.pos_n[0] = 0.0; ctx.pos_n[1] = 0.0; ctx.pos_n[2] = -1.0;
    ctx.wn[0]  = 0.5; /* > 0.1 → MTB assist */
    ctx.wn[1]  = 0.0; ctx.wn[2] = 0.0;
    ctx.mag_field_body[2] = 1.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_mode3_point_vector_zero_magnitude(void)
{
    /* pos_n = (0,0,0) → nadir_inertial = (0,0,0), vmag < 1e-6 →
     * adcs_point_vector_controller returns early with a warning. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 3, 300000000ULL);

    simulith_42_context_t ctx = {0};
    ctx.valid = 1;
    ctx.qn[0] = 1.0f;
    /* pos_n all zeros → nadir = (0,0,0) */
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_mode4_target_track(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 4, 300000000ULL);

    simulith_42_context_t ctx = {0};
    ctx.valid    = 1;
    ctx.qn[0]    = 1.0f;
    ctx.pos_n[2] = -1.0;
    ctx.wn[0]    = 0.0; ctx.wn[1] = 0.0; ctx.wn[2] = 0.0;
    ctx.mag_field_body[2] = 1.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_mode5_inertial_pointing(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 5, 300000000ULL);

    simulith_42_context_t ctx = {0};
    ctx.valid    = 1;
    ctx.qn[0]    = 1.0f;
    ctx.pos_n[2] = -1.0;
    ctx.wn[0]    = 0.0; ctx.wn[1] = 0.0; ctx.wn[2] = 0.0;
    ctx.mag_field_body[2] = 1.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_controller_default_unknown_mode(void)
{
    /* An unrecognised mode hits the default switch arm with a printf. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 99, 300000000ULL);

    simulith_42_context_t ctx = {0};
    ctx.valid    = 1;
    ctx.pos_n[2] = -1.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

/* -------------------------------------------------------------------------
 * Init failure path & REGISTER_COMPONENT alias
 * -------------------------------------------------------------------------*/
static void test_init_fails_when_address_path_is_a_directory(void)
{
    char path[256];
    snprintf(path, sizeof(path), "/tmp/simulith_pub:%d",
             SIMULITH_UART_BASE_PORT + ADCS_CFG_HANDLE);
    (void)unlink(path);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mkdir(path, 0755),
                                  "could not stage path squat");

    component_state_t *state = NULL;
    int                rc    = g_iface->init(&state);
    (void)rmdir(path);
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, rc);
}

static void test_get_adcs_sim_component_interface_alias(void)
{
    /* REGISTER_COMPONENT(adcs_sim) emits get_adcs_sim_component_interface().
     * Call it and confirm it returns the same struct as get_component_interface. */
    dlerror();
    get_component_interface_fn fn =
        (get_component_interface_fn)dlsym(g_handle,
                                          "get_adcs_sim_component_interface");
    const char *err = dlerror();
    TEST_ASSERT_NULL_MESSAGE(err, err ? err : "");
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_EQUAL_PTR(g_iface, fn());
}

/* -------------------------------------------------------------------------
 * Convergence-intent
 *
 * Each test drives the controller with a known non-zero attitude error and
 * verifies that the actuator command direction is physically correct —
 * i.e. the torque/dipole would reduce the error if applied.
 *
 * The test binary and adcs_sim.so both link against the same libsimulith.so
 * instance, so dequeue_command() here reads exactly what the controller
 * enqueued during the tick.
 * -------------------------------------------------------------------------*/
static void test_convergence_mode0_commands_zero_actuators(void)
{
    /* Mode 0 (disabled) must send zero wheel and zero MTB commands.
     * enable_mask must be 0x00 for both so the director does not apply any
     * output to the 42 simulation. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 0, 300000000ULL);
    drain_command_queue();

    simulith_42_context_t ctx = {0}; ctx.valid = 1;
    g_iface->tick(state, 300000000ULL, &ctx);

    simulith_42_command_t cmd_wheel = {0}, cmd_mtb = {0}, tmp;
    while (dequeue_command(&tmp) == 0) {
        if (tmp.type == SIMULITH_42_CMD_WHEEL_TORQUE) cmd_wheel = tmp;
        else if (tmp.type == SIMULITH_42_CMD_MTB_TORQUE) cmd_mtb = tmp;
    }

    TEST_ASSERT_EQUAL_INT(SIMULITH_42_CMD_WHEEL_TORQUE, cmd_wheel.type);
    TEST_ASSERT_EQUAL_INT(SIMULITH_42_CMD_MTB_TORQUE,   cmd_mtb.type);
    TEST_ASSERT_EQUAL_INT(0x00, cmd_wheel.cmd.wheel.enable_mask);
    TEST_ASSERT_EQUAL_INT(0x00, cmd_mtb.cmd.mtb.enable_mask);
    TEST_ASSERT_TRUE(fabs(cmd_wheel.cmd.wheel.torque[0]) < 1e-9);
    TEST_ASSERT_TRUE(fabs(cmd_wheel.cmd.wheel.torque[1]) < 1e-9);
    TEST_ASSERT_TRUE(fabs(cmd_wheel.cmd.wheel.torque[2]) < 1e-9);
    TEST_ASSERT_TRUE(fabs(cmd_mtb.cmd.mtb.dipole[0]) < 1e-9);
    TEST_ASSERT_TRUE(fabs(cmd_mtb.cmd.mtb.dipole[1]) < 1e-9);
    TEST_ASSERT_TRUE(fabs(cmd_mtb.cmd.mtb.dipole[2]) < 1e-9);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_convergence_mode1_bdot_dipole_opposes_rotation(void)
{
    /* w=[1,0,0] rad/s, B=[0,0,1] T:
     *   w x B = [0, -1, 0]  →  dipole = -gain * [0,-1,0] = [0, +gain, 0]
     * dipole[1] > 0: the torque opposes the +X spin. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 1, 300000000ULL);
    drain_command_queue();

    simulith_42_context_t ctx = {0};
    ctx.valid             = 1;
    ctx.wn[0]             = 1.0;  /* > ADCS_HIGH_RATE_THRESHOLD → high gain */
    ctx.mag_field_body[2] = 1.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    simulith_42_command_t cmd_mtb = {0}, tmp;
    while (dequeue_command(&tmp) == 0)
        if (tmp.type == SIMULITH_42_CMD_MTB_TORQUE) cmd_mtb = tmp;

    TEST_ASSERT_EQUAL_INT(SIMULITH_42_CMD_MTB_TORQUE, cmd_mtb.type);
    TEST_ASSERT_TRUE(cmd_mtb.cmd.mtb.dipole[1] > 0.0);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_convergence_mode2_wheel_torque_reduces_pointing_error(void)
{
    /* sun=[0,1,0], target=+X body:
     *   attitude_error = sun x target = [0,1,0] x [1,0,0] = [0,0,-1]
     *   PD law → wheel_torques[2] < 0 (negative Z torque rotates +Y toward +X). */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 2, 300000000ULL);
    drain_command_queue();

    simulith_42_context_t ctx = {0};
    ctx.valid              = 1;
    ctx.eclipse            = 0;
    ctx.sun_vector_body[1] = 1.0; /* sun along +Y body axis */
    ctx.wn[0] = 0.0; ctx.wn[1] = 0.0; ctx.wn[2] = 0.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    simulith_42_command_t cmd_wheel = {0}, tmp;
    while (dequeue_command(&tmp) == 0)
        if (tmp.type == SIMULITH_42_CMD_WHEEL_TORQUE) cmd_wheel = tmp;

    TEST_ASSERT_EQUAL_INT(SIMULITH_42_CMD_WHEEL_TORQUE, cmd_wheel.type);
    TEST_ASSERT_TRUE(cmd_wheel.cmd.wheel.torque[2] < 0.0);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_convergence_mode2_eclipse_bdot_rate_damping(void)
{
    /* eclipse=1: hybrid sun-pointing falls back to B-dot rate damping.
     * w=[1,0,0], B=[0,0,1] → dipole[1] > 0 (same math as mode 1). */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 2, 300000000ULL);
    drain_command_queue();

    simulith_42_context_t ctx = {0};
    ctx.valid             = 1;
    ctx.eclipse           = 1;
    ctx.wn[0]             = 1.0;
    ctx.mag_field_body[2] = 1.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    simulith_42_command_t cmd_mtb = {0}, tmp;
    while (dequeue_command(&tmp) == 0)
        if (tmp.type == SIMULITH_42_CMD_MTB_TORQUE) cmd_mtb = tmp;

    TEST_ASSERT_EQUAL_INT(SIMULITH_42_CMD_MTB_TORQUE, cmd_mtb.type);
    TEST_ASSERT_TRUE(cmd_mtb.cmd.mtb.dipole[1] > 0.0);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_convergence_mode3_nadir_wheel_torque_reduces_error(void)
{
    /* q=identity, pos_n=[0,0,-1]:
     *   nadir_inertial = [0,0,1]; rotate_inertial_to_body_safe(identity) → [0,0,1]
     *   attitude_error = [0,0,1] x [1,0,0] = [0,1,0]
     *   PD law → wheel_torques[1] > 0 (positive Y torque rotates +Z toward +X). */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 3, 300000000ULL);
    drain_command_queue();

    simulith_42_context_t ctx = {0};
    ctx.valid    = 1;
    ctx.qn[0]    = 1.0;   /* identity quaternion (scalar=1, vector=0) */
    ctx.pos_n[2] = -1.0;  /* nadir_inertial = [0,0,1] */
    ctx.wn[0] = 0.0; ctx.wn[1] = 0.0; ctx.wn[2] = 0.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    simulith_42_command_t cmd_wheel = {0}, tmp;
    while (dequeue_command(&tmp) == 0)
        if (tmp.type == SIMULITH_42_CMD_WHEEL_TORQUE) cmd_wheel = tmp;

    TEST_ASSERT_EQUAL_INT(SIMULITH_42_CMD_WHEEL_TORQUE, cmd_wheel.type);
    TEST_ASSERT_TRUE(cmd_wheel.cmd.wheel.torque[1] > 0.0);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_convergence_mode4_track_wheel_torque_reduces_error(void)
{
    /* SET_TARGET 1 → g_inertial_target=[1,0,0].
     * q=(cos45°,0,sin45°,0) — 90° around Y:
     *   tgt_body = rotate_inertial_to_body_safe([1,0,0]) = [0,0,1]
     *   attitude_error = [0,0,1] x [1,0,0] = [0,1,0]
     *   PD law → wheel_torques[1] > 0. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd_buf[ADCS_DEVICE_CMD_SIZE];
    encode_command(cmd_buf, ADCS_DEVICE_SET_TARGET_CMD, 1); /* positive X */
    simulith_transport_send(&client, cmd_buf, sizeof(cmd_buf));
    usleep(2000);

    /* Tick at 100ms to process SET_TARGET; controller not yet armed. */
    simulith_42_context_t ctx0 = zero_ctx();
    g_iface->tick(state, 100000000ULL, &ctx0);

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 4, 300000000ULL);
    drain_command_queue();

    simulith_42_context_t ctx = {0};
    ctx.valid    = 1;
    ctx.qn[0]    = 0.7071068; ctx.qn[1] = 0.0;
    ctx.qn[2]    = 0.7071068; ctx.qn[3] = 0.0; /* 90° around Y axis */
    ctx.wn[0] = 0.0; ctx.wn[1] = 0.0; ctx.wn[2] = 0.0;
    ctx.pos_n[2] = -1.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    simulith_42_command_t cmd_wheel = {0}, tmp;
    while (dequeue_command(&tmp) == 0)
        if (tmp.type == SIMULITH_42_CMD_WHEEL_TORQUE) cmd_wheel = tmp;

    simulith_transport_close(&client);
    TEST_ASSERT_EQUAL_INT(SIMULITH_42_CMD_WHEEL_TORQUE, cmd_wheel.type);
    TEST_ASSERT_TRUE(cmd_wheel.cmd.wheel.torque[1] > 0.0);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_convergence_mode5_inertial_wheel_torque_reduces_error(void)
{
    /* Same geometry as mode 4 but mode=5 (fixed inertial pointing).
     * SET_TARGET 1, q=(cos45°,0,sin45°,0) → wheel_torques[1] > 0. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd_buf[ADCS_DEVICE_CMD_SIZE];
    encode_command(cmd_buf, ADCS_DEVICE_SET_TARGET_CMD, 1);
    simulith_transport_send(&client, cmd_buf, sizeof(cmd_buf));
    usleep(2000);

    simulith_42_context_t ctx0 = zero_ctx();
    g_iface->tick(state, 100000000ULL, &ctx0);

    adcs_sim_state_t *as = (adcs_sim_state_t *)state;
    arm_controller(as, 5, 300000000ULL);
    drain_command_queue();

    simulith_42_context_t ctx = {0};
    ctx.valid    = 1;
    ctx.qn[0]    = 0.7071068; ctx.qn[1] = 0.0;
    ctx.qn[2]    = 0.7071068; ctx.qn[3] = 0.0;
    ctx.wn[0] = 0.0; ctx.wn[1] = 0.0; ctx.wn[2] = 0.0;
    ctx.pos_n[2] = -1.0;
    g_iface->tick(state, 300000000ULL, &ctx);

    simulith_42_command_t cmd_wheel = {0}, tmp;
    while (dequeue_command(&tmp) == 0)
        if (tmp.type == SIMULITH_42_CMD_WHEEL_TORQUE) cmd_wheel = tmp;

    simulith_transport_close(&client);
    TEST_ASSERT_EQUAL_INT(SIMULITH_42_CMD_WHEEL_TORQUE, cmd_wheel.type);
    TEST_ASSERT_TRUE(cmd_wheel.cmd.wheel.torque[1] > 0.0);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

/* -------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------*/
int main(void)
{
    g_handle = dlopen(ADCS_SIM_SO_PATH, RTLD_NOW);
    if (!g_handle)
    {
        fprintf(stderr, "Failed to dlopen %s: %s\n", ADCS_SIM_SO_PATH, dlerror());
        return 1;
    }

    dlerror();
    get_component_interface_fn get_iface =
        (get_component_interface_fn)dlsym(g_handle, "get_component_interface");
    const char *err = dlerror();
    if (err || !get_iface)
    {
        fprintf(stderr, "Failed to dlsym(get_component_interface): %s\n",
                err ? err : "symbol not found");
        dlclose(g_handle);
        return 1;
    }

    g_iface = get_iface();
    if (!g_iface)
    {
        fprintf(stderr, "get_component_interface() returned NULL\n");
        dlclose(g_handle);
        return 1;
    }

    UNITY_BEGIN();

    /* Lifecycle / Loader */
    RUN_TEST(test_dlopen_adcs_sim_so);
    RUN_TEST(test_get_component_interface_symbol);
    RUN_TEST(test_init_returns_success_and_state);
    RUN_TEST(test_tick_does_not_crash_with_zero_42_context);
    RUN_TEST(test_cleanup_releases_ipc_socket);

    /* Tick data paths */
    RUN_TEST(test_tick_with_42_context_scales_channels);
    RUN_TEST(test_tick_without_valid_42_context_uses_counter_channels);
    RUN_TEST(test_tick_populates_hk_from_42_context);

    /* Wire protocol */
    RUN_TEST(test_wire_protocol_noop_echoes_command);
    RUN_TEST(test_wire_protocol_req_hk_returns_framed_hk);
    RUN_TEST(test_wire_protocol_get_css_returns_data_frame);
    RUN_TEST(test_wire_protocol_get_imu_returns_data_frame);
    RUN_TEST(test_wire_protocol_set_mode_activates_controller);
    RUN_TEST(test_wire_protocol_set_mode_zero_deactivates_controller);
    RUN_TEST(test_wire_protocol_set_target_positive_x);
    RUN_TEST(test_wire_protocol_set_target_negative_x);
    RUN_TEST(test_wire_protocol_set_target_3_keeps_current);
    RUN_TEST(test_wire_protocol_unknown_cmd_echoed_only);
    RUN_TEST(test_wire_protocol_short_packet_rejected);
    RUN_TEST(test_wire_protocol_bad_header_rejected);
    RUN_TEST(test_wire_protocol_bad_trailer_rejected);

    /* Controller */
    RUN_TEST(test_controller_inactive_does_not_run);
    RUN_TEST(test_controller_first_tick_initializes_timer);
    RUN_TEST(test_controller_rate_too_low_does_not_run);
    RUN_TEST(test_controller_time_backwards_resyncs);
    RUN_TEST(test_controller_mode0_disabled);
    RUN_TEST(test_controller_mode1_bdot_high_rate);
    RUN_TEST(test_controller_mode1_bdot_low_rate);
    RUN_TEST(test_controller_mode1_bdot_invalid_context);
    RUN_TEST(test_controller_mode2_sun_normal_case);
    RUN_TEST(test_controller_mode2_sun_nearly_aligned);
    RUN_TEST(test_controller_mode2_sun_anti_aligned);
    RUN_TEST(test_controller_mode2_eclipse);
    RUN_TEST(test_controller_mode2_invalid_sun_vector);
    RUN_TEST(test_controller_mode2_invalid_context);
    RUN_TEST(test_controller_mode2_high_rate_mtb_assist);
    RUN_TEST(test_controller_mode3_nadir_dot1_branch);
    RUN_TEST(test_controller_mode3_nadir_dot2_branch);
    RUN_TEST(test_controller_mode3_nadir_high_rate_mtb_assist);
    RUN_TEST(test_controller_mode3_point_vector_zero_magnitude);
    RUN_TEST(test_controller_mode4_target_track);
    RUN_TEST(test_controller_mode5_inertial_pointing);
    RUN_TEST(test_controller_default_unknown_mode);

    /* Init failure & alias */
    RUN_TEST(test_init_fails_when_address_path_is_a_directory);
    RUN_TEST(test_get_adcs_sim_component_interface_alias);

    /* Convergence-intent */
    RUN_TEST(test_convergence_mode0_commands_zero_actuators);
    RUN_TEST(test_convergence_mode1_bdot_dipole_opposes_rotation);
    RUN_TEST(test_convergence_mode2_wheel_torque_reduces_pointing_error);
    RUN_TEST(test_convergence_mode2_eclipse_bdot_rate_damping);
    RUN_TEST(test_convergence_mode3_nadir_wheel_torque_reduces_error);
    RUN_TEST(test_convergence_mode4_track_wheel_torque_reduces_error);
    RUN_TEST(test_convergence_mode5_inertial_wheel_torque_reduces_error);

    int result = UNITY_END();

    dlclose(g_handle);
    return result;
}
