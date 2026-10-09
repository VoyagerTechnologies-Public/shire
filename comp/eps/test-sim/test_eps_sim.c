/*
 * Unity test for the EPS component simulator (comp/eps/sim/eps_sim.c).
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <unistd.h>

#include "unity.h"
#include "simulith.h"
#include "simulith_component.h"
#include "simulith_transport.h"
#include "simulith_42_context.h"

#include "eps_sim.h"
#include "eps_device.h"
#include "device_cfg.h"

#ifndef EPS_SIM_SO_PATH
#error "EPS_SIM_SO_PATH must be defined (path to eps_sim.so)"
#endif

static void                        *g_handle = NULL;
static const component_interface_t *g_iface  = NULL;
static component_interface_t g_phase_iface;
static size_t (*g_console_state)(component_state_t *, uint64_t, double, uint8_t *, size_t);
static int (*g_prepare_tick)(component_state_t *, uint64_t,
                             const simulith_42_context_t *) = NULL;

typedef uint8_t (*eps_calc_crc_fn)(const uint8_t *, size_t);
typedef int  (*eps_init_fn)(eps_sim_state_t *);
typedef void (*eps_cleanup_fn)(eps_sim_state_t *);

static eps_calc_crc_fn g_eps_calc_crc    = NULL;
static eps_init_fn     g_eps_sim_init    = NULL;
static eps_cleanup_fn  g_eps_sim_cleanup = NULL;

/* Exercise the same PREPARE and EXECUTE order as the director. */
static int run_tick_phases(component_state_t *state, uint64_t tick_time_ns,
                           const simulith_42_context_t *context_42)
{
    int status = g_prepare_tick(state, tick_time_ns, context_42);
    if (state && g_phase_iface.service)
        g_phase_iface.service(state, 0, NULL);
    return status;
}

/* Monotonically-increasing test time also exercises deadline catch-up. */
static uint64_t g_next_tick_ns = 0;
static uint64_t next_tick(void)
{
    g_next_tick_ns += 2000000000ULL; /* +2s */
    return g_next_tick_ns;
}

static void test_wait_interrupt_and_deadline_saturation(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    int interrupt_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, interrupt_fd);
    uint64_t wake = 1;
    TEST_ASSERT_EQUAL_INT((int)sizeof(wake),
                          (int)write(interrupt_fd, &wake, sizeof(wake)));
    TEST_ASSERT_EQUAL_INT(COMPONENT_IDLE,
                          g_iface->wait_for_service(state, interrupt_fd));

    eps_sim_state_t *eps_state = (eps_sim_state_t *)state;
    eps_state->next_hk_update_ns = UINT64_MAX - 1U;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS,
                          g_prepare_tick(state, UINT64_MAX, NULL));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, eps_state->next_hk_update_ns);

    close(interrupt_fd);
    g_iface->destroy(state);
}

/* -------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/
static void eps_i2c_address(char *out, size_t cap)
{
    int port = SIMULITH_I2C_BASE_PORT + EPS_CFG_I2C_BUS_ID * 100 + EPS_CFG_I2C_DEVICE_ADDR;
    snprintf(out, cap, "ipc:///tmp/simulith_pub:%d", port);
}

static int open_client_port(transport_port_t *port, const char *name)
{
    memset(port, 0, sizeof(*port));
    snprintf(port->name, sizeof(port->name), "%s", name);
    eps_i2c_address(port->address, sizeof(port->address));
    port->is_server = 0;
    return simulith_transport_init(port);
}

static void encode_command(EPS_Command_t *cmd, uint8_t command, uint8_t payload)
{
    cmd->i2c_addr = EPS_CFG_I2C_DEVICE_ADDR;
    cmd->command  = command;
    cmd->payload  = payload;
    cmd->crc      = g_eps_calc_crc((const uint8_t *)cmd, sizeof(*cmd) - 1);
}

static size_t drain_all(transport_port_t *port, uint8_t *out, size_t cap)
{
    size_t total = 0;
    int    idle  = 0;
    for (int attempt = 0; attempt < 50 && idle < 5; ++attempt)
    {
        if (simulith_transport_available(port) > 0)
        {
            int got = simulith_transport_receive(port, out + total, cap - total);
            if (got <= 0)
            {
                break;
            }
            total += (size_t)got;
            idle = 0;
            if (total >= cap)
            {
                return total;
            }
        }
        else
        {
            idle++;
            usleep(1000);
        }
    }
    return total;
}

void setUp(void) {}
void tearDown(void) {}

/* -------------------------------------------------------------------------
 * Lifecycle / loader tests
 * -------------------------------------------------------------------------*/
static void test_dlopen_eps_sim_so(void)
{
    dlerror();
    void       *h   = dlopen(EPS_SIM_SO_PATH, RTLD_NOW);
    const char *err = dlerror();
    if (!h)
    {
        TEST_FAIL_MESSAGE(err ? err : "dlopen returned NULL");
    }
    TEST_ASSERT_EQUAL_INT(0, dlclose(h));
}

static void test_get_component_interface_symbol(void)
{
    TEST_ASSERT_NOT_NULL(g_iface);
    TEST_ASSERT_EQUAL_UINT32(SIMULITH_COMPONENT_API_VERSION,
                             g_iface->api_version);
    TEST_ASSERT_EQUAL_UINT32(sizeof(component_interface_t),
                             g_iface->struct_size);
    TEST_ASSERT_NOT_NULL(g_iface->name);
    TEST_ASSERT_NOT_NULL(g_iface->description);
    TEST_ASSERT_NOT_NULL(g_iface->create);
    TEST_ASSERT_NOT_NULL(g_iface->on_tick);
    TEST_ASSERT_NOT_NULL(g_iface->wait_for_service);
    TEST_ASSERT_NOT_NULL(g_iface->service);
    TEST_ASSERT_NOT_NULL(g_iface->actuate);
    TEST_ASSERT_NOT_NULL(g_iface->destroy);
    /* Simulator test controls use the shared director backdoor framing. */
    TEST_ASSERT_NOT_NULL(g_iface->backdoor);
    TEST_ASSERT_EQUAL_STRING("eps_sim", g_iface->name);
}

static void test_init_returns_success_and_state(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    TEST_ASSERT_NOT_NULL(state);

    eps_sim_state_t *es = (eps_sim_state_t *)state;
    for (int i = 0; i < EPS_NUM_SWITCHES; i++)
    {
        const uint8_t startup[] = EPS_SWITCH_STARTUP;
        TEST_ASSERT_EQUAL_UINT8(startup[i], es->hk.switches[i].state);
    }
    double expected = EPS_BATTERY_CAPACITY_WH * EPS_BATTERY_INITIAL_SOC;
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, expected, es->battery_energy_wh);

    g_iface->destroy(state);
}

static void test_lifecycle_callbacks_reject_null(void)
{
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, g_iface->create(NULL));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_prepare_tick(NULL, next_tick(), NULL));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->service(NULL, 0, NULL));
    g_iface->destroy(NULL);
}

static void test_cleanup_releases_i2c_socket(void)
{
    /* Guard against the bind-rebind regression: if cleanup leaks the bound
     * IPC socket, a second init in the same process will fail. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_iface->destroy(state);

    state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_iface->destroy(state);
}

/* -------------------------------------------------------------------------
 * Direct-call tests for non-static helpers (line coverage)
 * -------------------------------------------------------------------------*/
static void test_eps_sim_init_rejects_null_state(void)
{
    /* eps_sim_init is exported (non-static); calling with NULL exercises
     * the early-return guard at the top of the function. */
    TEST_ASSERT_NOT_NULL(g_eps_sim_init);
    TEST_ASSERT_EQUAL_INT(-1, g_eps_sim_init(NULL));
}

static void test_eps_sim_cleanup_with_null_is_safe(void)
{
    /* eps_sim_cleanup must tolerate a NULL pointer. */
    TEST_ASSERT_NOT_NULL(g_eps_sim_cleanup);
    g_eps_sim_cleanup(NULL);
}

/* -------------------------------------------------------------------------
 * Wire-protocol / I2C transport tests
 * -------------------------------------------------------------------------*/
static void test_wire_protocol_noop_increments_counter_silently(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    EPS_Command_t cmd;
    encode_command(&cmd, EPS_CMD_NOOP, 0);
    TEST_ASSERT_EQUAL_INT((int)sizeof(cmd),
                          simulith_transport_send(&client, (uint8_t *)&cmd, sizeof(cmd)));
    usleep(2000);

    g_iface->on_tick(state, next_tick(), NULL);

    uint8_t rx[64];
    TEST_ASSERT_EQUAL_size_t(0, drain_all(&client, rx, sizeof(rx)));

    eps_sim_state_t *es = (eps_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT32(1, es->device_counter);

    simulith_transport_close(&client);
    g_iface->destroy(state);
}

static void test_wire_protocol_get_hk_returns_framed_hk(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    EPS_Command_t cmd;
    encode_command(&cmd, EPS_CMD_GET_HK, 0);
    simulith_transport_send(&client, (uint8_t *)&cmd, sizeof(cmd));
    usleep(2000);
    g_iface->on_tick(state, next_tick(), NULL);

    EPS_Device_HK_tlm_t hk;
    size_t              n = drain_all(&client, (uint8_t *)&hk, sizeof(hk));
    TEST_ASSERT_EQUAL_size_t(sizeof(hk), n);
    /* Verify CRC: the simulator computes it just before sending. */
    uint8_t expected_crc = g_eps_calc_crc((const uint8_t *)&hk, sizeof(hk) - 1);
    TEST_ASSERT_EQUAL_HEX8(expected_crc, hk.crc);

    simulith_transport_close(&client);
    g_iface->destroy(state);
}

static void test_wire_protocol_switch_on_then_off(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    eps_sim_state_t *es = (eps_sim_state_t *)state;

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    EPS_Command_t cmd;
    encode_command(&cmd, EPS_CMD_SWITCH_ON, 3);
    simulith_transport_send(&client, (uint8_t *)&cmd, sizeof(cmd));
    usleep(2000);
    g_iface->on_tick(state, next_tick(), NULL);
    TEST_ASSERT_EQUAL_UINT8(EPS_SWITCH_ON, es->hk.switches[3].state);

    encode_command(&cmd, EPS_CMD_SWITCH_OFF, 3);
    simulith_transport_send(&client, (uint8_t *)&cmd, sizeof(cmd));
    usleep(2000);
    g_iface->on_tick(state, next_tick(), NULL);
    TEST_ASSERT_EQUAL_UINT8(EPS_SWITCH_OFF, es->hk.switches[3].state);

    simulith_transport_close(&client);
    g_iface->destroy(state);
}

static void test_wire_protocol_short_packet_is_rejected(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t partial[2] = {EPS_CFG_I2C_DEVICE_ADDR, EPS_CMD_NOOP};
    simulith_transport_send(&client, partial, sizeof(partial));
    usleep(2000);
    uint64_t tick_time_ns = next_tick();
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS,
                          g_prepare_tick(state, tick_time_ns, NULL));
    TEST_ASSERT_EQUAL_INT(COMPONENT_WORK,
                          g_phase_iface.service(state, tick_time_ns, NULL));

    eps_sim_state_t *es = (eps_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT32(0, es->device_counter);

    simulith_transport_close(&client);
    g_iface->destroy(state);
}

static void test_wire_protocol_wrong_i2c_addr_is_rejected(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    EPS_Command_t cmd;
    cmd.i2c_addr = (uint8_t)(EPS_CFG_I2C_DEVICE_ADDR ^ 0xFF); /* not us */
    cmd.command  = EPS_CMD_NOOP;
    cmd.payload  = 0;
    cmd.crc      = g_eps_calc_crc((const uint8_t *)&cmd, sizeof(cmd) - 1);
    simulith_transport_send(&client, (uint8_t *)&cmd, sizeof(cmd));
    usleep(2000);
    g_iface->on_tick(state, next_tick(), NULL);

    eps_sim_state_t *es = (eps_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT32(0, es->device_counter);

    simulith_transport_close(&client);
    g_iface->destroy(state);
}

static void test_wire_protocol_bad_crc_is_rejected(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    EPS_Command_t cmd;
    cmd.i2c_addr = EPS_CFG_I2C_DEVICE_ADDR;
    cmd.command  = EPS_CMD_NOOP;
    cmd.payload  = 0;
    /* Force a wrong CRC by inverting the correct one. */
    uint8_t correct = g_eps_calc_crc((const uint8_t *)&cmd, sizeof(cmd) - 1);
    cmd.crc         = (uint8_t)(correct ^ 0xFF);
    simulith_transport_send(&client, (uint8_t *)&cmd, sizeof(cmd));
    usleep(2000);
    g_iface->on_tick(state, next_tick(), NULL);

    eps_sim_state_t *es = (eps_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT32(0, es->device_counter);

    simulith_transport_close(&client);
    g_iface->destroy(state);
}

static void test_wire_protocol_unknown_cmd_default_arm(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    EPS_Command_t cmd;
    encode_command(&cmd, 0x99, 0); /* outside 0..3 */
    simulith_transport_send(&client, (uint8_t *)&cmd, sizeof(cmd));
    usleep(2000);
    g_iface->on_tick(state, next_tick(), NULL);

    /* Default arm just logs; counter still increments. */
    eps_sim_state_t *es = (eps_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT32(1, es->device_counter);

    simulith_transport_close(&client);
    g_iface->destroy(state);
}

static void test_wire_protocol_switch_invalid_index_is_ignored(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    eps_sim_state_t *es = (eps_sim_state_t *)state;

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    /* SWITCH_ON with payload >= EPS_NUM_SWITCHES — the if-guard rejects the
     * write, but the command still counts (counter increments). */
    EPS_Command_t cmd;
    encode_command(&cmd, EPS_CMD_SWITCH_ON, EPS_NUM_SWITCHES);
    simulith_transport_send(&client, (uint8_t *)&cmd, sizeof(cmd));
    usleep(2000);
    g_iface->on_tick(state, next_tick(), NULL);

    TEST_ASSERT_EQUAL_UINT32(1, es->device_counter);
    for (int i = 0; i < EPS_NUM_SWITCHES; i++)
    {
        const uint8_t startup[] = EPS_SWITCH_STARTUP;
        TEST_ASSERT_EQUAL_UINT8(startup[i], es->hk.switches[i].state);
    }

    /* Same for SWITCH_OFF with an invalid index. */
    encode_command(&cmd, EPS_CMD_SWITCH_OFF, 99);
    simulith_transport_send(&client, (uint8_t *)&cmd, sizeof(cmd));
    usleep(2000);
    g_iface->on_tick(state, next_tick(), NULL);
    TEST_ASSERT_EQUAL_UINT32(2, es->device_counter);

    simulith_transport_close(&client);
    g_iface->destroy(state);
}

static void test_wait_for_service_rejects_null(void)
{
    /* eps_component_wait_for_service guards against a NULL component_state_t*
     * before touching interrupt_fd or the transport ports array. */
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, g_iface->wait_for_service(NULL, -1));
}

static void test_service_errors_when_transport_uninitialized(void)
{
    /* Closing the I2C port makes simulith_transport_receive_request() return
     * SIMULITH_TRANSPORT_ERROR (-1); eps_component_service must propagate
     * that as COMPONENT_ERROR via its bytes_read < 0 branch. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    eps_sim_state_t *es = (eps_sim_state_t *)state;

    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          simulith_transport_close(&es->i2c_device));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, g_iface->service(state, 0, NULL));

    g_iface->destroy(state);
}

/* -------------------------------------------------------------------------
 * Tick / 42 context / battery model tests
 * -------------------------------------------------------------------------*/
static void test_tick_with_null_state_returns_safely(void)
{
    /* eps_component_tick guards against a NULL component_state_t* argument
     * with an early return. Calling with NULL must not crash. */
    g_iface->on_tick(NULL, next_tick(), NULL);
}

static void test_tick_with_null_42_no_solar_no_crash(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_iface->on_tick(state, next_tick(), NULL);
    g_iface->destroy(state);
}

static void test_tick_before_deadline_skips_hk_update(void)
{
    /* tick_time_ns < next_hk_update_ns must skip the whole HK-update block
     * (the false side of the deadline check at the top of on_tick). */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    eps_sim_state_t *es = (eps_sim_state_t *)state;

    uint64_t deadline_before = es->next_hk_update_ns;
    uint8_t  temp_before     = es->hk.battery_temperature;

    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS,
                          g_prepare_tick(state, deadline_before - 1U, NULL));

    TEST_ASSERT_EQUAL_UINT64(deadline_before, es->next_hk_update_ns);
    TEST_ASSERT_EQUAL_UINT8(temp_before, es->hk.battery_temperature);

    g_iface->destroy(state);
}

static void test_tick_with_42_eclipse_yields_no_solar(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    eps_sim_state_t *es = (eps_sim_state_t *)state;

    simulith_42_context_t ctx = {0};
    ctx.valid                 = 1;
    ctx.eclipse               = 1;
    ctx.sun_vector_body[0]    = 1.0; /* would be peak if not eclipsed */

    double before = es->battery_energy_wh;
    g_iface->on_tick(state, next_tick(), &ctx);

    TEST_ASSERT_TRUE(es->battery_energy_wh <= before);
    TEST_ASSERT_EQUAL_UINT8(0, es->hk.solar_voltage);

    g_iface->destroy(state);
}

static void test_tick_with_42_invalid_yields_no_solar(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));

    simulith_42_context_t ctx = {0};
    ctx.valid                 = 0; /* invalid */
    ctx.sun_vector_body[0]    = 1.0;
    g_iface->on_tick(state, next_tick(), &ctx);

    g_iface->destroy(state);
}

static void test_tick_with_42_negative_sun_x_yields_no_solar(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));

    simulith_42_context_t ctx = {0};
    ctx.valid                 = 1;
    ctx.sun_vector_body[0]    = -0.9; /* sun behind +X face */
    g_iface->on_tick(state, next_tick(), &ctx);

    g_iface->destroy(state);
}

static void test_tick_with_42_positive_sun_x_charges_battery(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    eps_sim_state_t *es = (eps_sim_state_t *)state;

    /* Drop battery below capacity so positive solar has somewhere to go. */
    es->battery_energy_wh = 1.0;

    simulith_42_context_t ctx = {0};
    ctx.valid                 = 1;
    ctx.sun_vector_body[0]    = 1.0; /* peak generation */
    g_prepare_tick(state, 0, &ctx);
    g_iface->on_tick(state, next_tick(), &ctx);

    TEST_ASSERT_TRUE(es->battery_energy_wh > 1.0);

    g_iface->destroy(state);
}

static void test_tick_drains_battery_and_clamps_low(void)
{
    /* Stage near-empty battery + all switches ON. One tick of consumption
     * pushes the energy < 0; the low-clamp must pin it to 0. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    eps_sim_state_t *es = (eps_sim_state_t *)state;

    es->battery_energy_wh = 0.0001;
    for (int i = 0; i < EPS_NUM_SWITCHES; i++)
    {
        es->power.effective[i] = EPS_SWITCH_ON;
    }
    g_iface->on_tick(state, next_tick(), NULL);

    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, es->battery_energy_wh);

    g_iface->destroy(state);
}

static void test_tick_charges_battery_and_clamps_high(void)
{
    /* Stage near-full battery + max solar + no consumption. The high-clamp
     * must pin energy to capacity. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    eps_sim_state_t *es = (eps_sim_state_t *)state;

    es->battery_energy_wh = EPS_BATTERY_CAPACITY_WH - 0.0001;

    simulith_42_context_t ctx = {0};
    ctx.valid                 = 1;
    ctx.sun_vector_body[0]    = 1.0;
    g_prepare_tick(state, 0, &ctx);
    g_iface->on_tick(state, next_tick(), &ctx);

    TEST_ASSERT_DOUBLE_WITHIN(1e-9, EPS_BATTERY_CAPACITY_WH, es->battery_energy_wh);

    g_iface->destroy(state);
}

static void test_tick_switch_voltage_reflects_index_range(void)
{
    /* Turn ON one switch in each of the four index pairs (0|1, 2|3, 4|5,
     * 6|7) so the per-index voltage assignment in the tick switch loop
     * covers all four ranges. Switches still OFF cover the else arm. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    eps_sim_state_t *es = (eps_sim_state_t *)state;

    memset(es->power.effective, 0, sizeof(es->power.effective));
    es->power.effective[0] = EPS_SWITCH_ON; /* 3.3V band */
    es->power.effective[2] = EPS_SWITCH_ON; /* 5.0V band */
    es->power.effective[4] = EPS_SWITCH_ON; /* 12.0V band */
    es->power.effective[6] = EPS_SWITCH_ON; /* 24.0V band */

    g_iface->on_tick(state, next_tick(), NULL);

    /* Convert expected voltages to telemetry counts (32V / 255 per count,
     * cast truncates toward zero). */
    uint8_t v3v3 = (uint8_t)(3.3f / (32.0f / 255.0f));
    uint8_t v5v  = (uint8_t)(5.0f / (32.0f / 255.0f));
    uint8_t v12v = (uint8_t)(12.0f / (32.0f / 255.0f));
    uint8_t v24v = (uint8_t)(24.0f / (32.0f / 255.0f));

    TEST_ASSERT_EQUAL_UINT8(v3v3, es->hk.switches[0].voltage);
    TEST_ASSERT_EQUAL_UINT8(0, es->hk.switches[1].voltage);
    TEST_ASSERT_EQUAL_UINT8(v5v, es->hk.switches[2].voltage);
    TEST_ASSERT_EQUAL_UINT8(0, es->hk.switches[3].voltage);
    TEST_ASSERT_EQUAL_UINT8(v12v, es->hk.switches[4].voltage);
    TEST_ASSERT_EQUAL_UINT8(0, es->hk.switches[5].voltage);
    TEST_ASSERT_EQUAL_UINT8(v24v, es->hk.switches[6].voltage);
    TEST_ASSERT_EQUAL_UINT8(0, es->hk.switches[7].voltage);

    g_iface->destroy(state);
}

static void test_tick_switch_voltage_reflects_odd_index_range(void)
{
    /* Companion to test_tick_switch_voltage_reflects_index_range: turn ON
     * the second member of each index pair (1, 3, 5, 7) so the "|| i == odd"
     * side of each band comparison is exercised true as well. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    eps_sim_state_t *es = (eps_sim_state_t *)state;

    memset(es->power.effective, 0, sizeof(es->power.effective));
    es->power.effective[1] = EPS_SWITCH_ON; /* 3.3V band */
    es->power.effective[3] = EPS_SWITCH_ON; /* 5.0V band */
    es->power.effective[5] = EPS_SWITCH_ON; /* 12.0V band */
    es->power.effective[7] = EPS_SWITCH_ON; /* 24.0V band */

    g_iface->on_tick(state, next_tick(), NULL);

    uint8_t v3v3 = (uint8_t)(3.3f / (32.0f / 255.0f));
    uint8_t v5v  = (uint8_t)(5.0f / (32.0f / 255.0f));
    uint8_t v12v = (uint8_t)(12.0f / (32.0f / 255.0f));
    uint8_t v24v = (uint8_t)(24.0f / (32.0f / 255.0f));

    TEST_ASSERT_EQUAL_UINT8(v3v3, es->hk.switches[1].voltage);
    TEST_ASSERT_EQUAL_UINT8(v5v, es->hk.switches[3].voltage);
    TEST_ASSERT_EQUAL_UINT8(v12v, es->hk.switches[5].voltage);
    TEST_ASSERT_EQUAL_UINT8(v24v, es->hk.switches[7].voltage);

    g_iface->destroy(state);
}

/* -------------------------------------------------------------------------
 * Init failure path
 * -------------------------------------------------------------------------*/
static void test_init_fails_when_address_path_is_a_directory(void)
{
    /* Pre-create a directory at the simulator's IPC bind path so ZMQ's
     * underlying bind() returns EADDRINUSE. Covers both the i2c-bind
     * failure path in eps_component_init and the matching cleanup. */
    char path[256];
    int  port = SIMULITH_I2C_BASE_PORT + EPS_CFG_I2C_BUS_ID * 100 + EPS_CFG_I2C_DEVICE_ADDR;
    snprintf(path, sizeof(path), "/tmp/simulith_pub:%d", port);
    (void)unlink(path);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mkdir(path, 0755), "could not stage path squat");

    component_state_t *state = NULL;
    int                rc    = g_iface->create(&state);

    (void)rmdir(path);

    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, rc);
}

/* -------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------*/
static void test_backdoor_payload_lengths_and_invalid_selectors(void)
{
    static const size_t lengths[] = {0,4,5,2,5,2,2,3,0,0};
    eps_sim_state_t s;
    TEST_ASSERT_EQUAL_INT(0, g_eps_sim_init(&s));
    uint8_t payload[9] = {0};
    for (uint16_t id = 1; id <= 9; id++) {
        for (size_t len = 0; len <= sizeof(payload); len++) {
            if (len == lengths[id]) continue;
            double energy = s.battery_energy_wh;
            simulith_power_supply_t physical = s.power;
            uint64_t rejected = s.backdoor_rejected;
            g_iface->backdoor((component_state_t *)&s, id, payload, len);
            TEST_ASSERT_EQUAL_UINT64(rejected + 1, s.backdoor_rejected);
            TEST_ASSERT_DOUBLE_WITHIN(0, energy, s.battery_energy_wh);
            TEST_ASSERT_EQUAL_MEMORY(&physical, &s.power, sizeof(physical));
        }
    }
    static const uint8_t invalid[][5] = {
        {0,15,66,65,0}, /* SOC 1000001 */
        {2,0,0,0,0}, /* solar selector */
        {8,1,0,0,0}, /* switch index */
        {0,2,0,0,0}, /* switch state */
        {3,0,7,161,32}, /* component */
        {0,3,0,0,0}, /* fault */
        {4,0,1,0,0}, /* request selector */
    };
    static const uint16_t ids[] = {1,2,3,3,4,5,7};
    for (size_t i = 0; i < sizeof(ids)/sizeof(ids[0]); i++) {
        simulith_power_supply_t physical = s.power;
        uint64_t rejected = s.backdoor_rejected;
        g_iface->backdoor((component_state_t *)&s, ids[i], invalid[i], lengths[ids[i]]);
        TEST_ASSERT_EQUAL_UINT64(rejected + 1, s.backdoor_rejected);
        TEST_ASSERT_EQUAL_MEMORY(&physical, &s.power, sizeof(physical));
    }
}

static void test_backdoor_reset_deterministic_noise_and_ordered_cycles(void)
{
    eps_sim_state_t s;
    TEST_ASSERT_EQUAL_INT(0, g_eps_sim_init(&s));
    s.power.configured = 1;
    s.power.effective[0] = s.power.startup[0] = 1;
    s.power.transition_count = 0;
    uint8_t off[] = {0,0}, on[] = {0,1};
    g_iface->backdoor((component_state_t *)&s, EPS_BD_SET_SWITCH, off, 2);
    g_iface->backdoor((component_state_t *)&s, EPS_BD_SET_SWITCH, on, 2);
    g_iface->backdoor((component_state_t *)&s, EPS_BD_SET_SWITCH, on, 2);
    TEST_ASSERT_EQUAL_UINT16(2, s.power.transition_count);
    TEST_ASSERT_EQUAL_UINT8(0, s.power.transitions[0].on);
    TEST_ASSERT_EQUAL_UINT8(1, s.power.transitions[1].on);
    s.power.transition_count = 0;
    g_iface->backdoor((component_state_t *)&s, EPS_BD_RESET, NULL, 0);
    TEST_ASSERT_EQUAL_UINT8(1, s.power.transitions[0].reset);
    TEST_ASSERT_EQUAL_UINT8(0, s.power.transitions[1].reset);
    TEST_ASSERT_EQUAL_UINT8(0, s.power.transitions[1].on);
    TEST_ASSERT_EQUAL_UINT8(0, s.power.transitions[2].reset);
    TEST_ASSERT_EQUAL_UINT8(1, s.power.transitions[2].on);
    g_prepare_tick((component_state_t *)&s, 1000000000ULL, NULL);
    EPS_Device_HK_tlm_t hk = s.hk;
    g_iface->backdoor((component_state_t *)&s, EPS_BD_RESET, NULL, 0);
    g_prepare_tick((component_state_t *)&s, 2000000000ULL, NULL);
    TEST_ASSERT_EQUAL_UINT8(hk.battery_temperature, s.hk.battery_temperature);
    TEST_ASSERT_EQUAL_UINT8(hk.solar_temperature, s.hk.solar_temperature);
}

static void test_backdoor_state_fault_reset_and_validation(void)
{
    eps_sim_state_t s;
    TEST_ASSERT_EQUAL_INT(0, g_eps_sim_init(&s));
    uint8_t soc[] = {0, 7, 161, 32}; /* 500000 ppm */
    g_iface->backdoor((component_state_t *)&s, EPS_BD_SET_SOC, soc, 4);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, EPS_BATTERY_CAPACITY_WH / 2, s.battery_energy_wh);
    double saved = s.battery_energy_wh;
    soc[0] = 255;
    g_iface->backdoor((component_state_t *)&s, EPS_BD_SET_SOC, soc, 4);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, saved, s.battery_energy_wh);
    TEST_ASSERT_EQUAL_UINT64(1, s.backdoor_rejected);
    g_iface->backdoor((component_state_t *)&s, EPS_BD_SET_SOC, NULL, 4);
    g_iface->backdoor((component_state_t *)&s, 0xffff, NULL, 0);
    for (uint8_t i = 0; i < EPS_NUM_SWITCHES; i++) {
        uint8_t sw[] = {i, 1};
        uint8_t fault[] = {i, 1};
        g_iface->backdoor((component_state_t *)&s, EPS_BD_SET_SWITCH_FAULT, fault, 2);
        g_iface->backdoor((component_state_t *)&s, EPS_BD_SET_SWITCH, sw, 2);
        TEST_ASSERT_EQUAL_UINT8(1, s.power.requested[i]);
        TEST_ASSERT_EQUAL_UINT8(0, s.power.effective[i]);
        fault[1] = 2;
        g_iface->backdoor((component_state_t *)&s, EPS_BD_SET_SWITCH_FAULT, fault, 2);
        sw[1] = 0;
        g_iface->backdoor((component_state_t *)&s, EPS_BD_SET_SWITCH, sw, 2);
        TEST_ASSERT_EQUAL_UINT8(0, s.power.requested[i]);
        TEST_ASSERT_EQUAL_UINT8(1, s.power.effective[i]);
    }
    uint8_t solar[] = {1, 0, 0, 0, 0};
    g_iface->backdoor((component_state_t *)&s, EPS_BD_SET_SOLAR, solar, 5);
    TEST_ASSERT_EQUAL_UINT8(1, s.solar_override);
    uint8_t scale[] = {0, 0, 7, 161, 32};
    g_iface->backdoor((component_state_t *)&s, EPS_BD_SET_LOAD_SCALE, scale, 5);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.5, s.power.loads[0].scale);
    g_iface->backdoor((component_state_t *)&s, EPS_BD_CLEAR_FAULTS, NULL, 0);
    TEST_ASSERT_EQUAL_UINT8(1, s.solar_override);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.5, s.power.loads[0].scale);
    for (uint8_t i = 0; i < EPS_NUM_SWITCHES; i++) TEST_ASSERT_EQUAL_UINT8(0, s.power.effective[i]);
    uint64_t generation = s.backdoor_generation;
    s.model_time_ns = 5000000000ULL;
    g_iface->backdoor((component_state_t *)&s, EPS_BD_RESET, NULL, 0);
    TEST_ASSERT_EQUAL_UINT64(generation + 1, s.backdoor_generation);
    TEST_ASSERT_EQUAL_UINT64(5000000000ULL, s.model_time_ns);
    TEST_ASSERT_EQUAL_UINT64(6000000000ULL, s.next_hk_update_ns);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, EPS_BATTERY_CAPACITY_WH * EPS_BATTERY_INITIAL_SOC, s.battery_energy_wh);
    TEST_ASSERT_EQUAL_UINT8(0, s.solar_override);
    for (uint8_t i = 0; i < EPS_NUM_SWITCHES; i++) TEST_ASSERT_EQUAL_UINT8(s.power.startup[i], s.power.effective[i]);
}

static void test_backdoor_crc_and_request_failure_counts(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    eps_sim_state_t *s = (eps_sim_state_t *)state;
    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(0, open_client_port(&client, "fault_client"));
    uint8_t count[] = {0, 1};
    uint8_t fail[] = {1, 0, 1};
    g_iface->backdoor(state, EPS_BD_CORRUPT_HK_CRC, count, 2);
    g_iface->backdoor(state, EPS_BD_FAIL_REQUESTS, fail, 3);
    EPS_Command_t cmd;
    encode_command(&cmd, EPS_CMD_GET_HK, 0);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_INT((int)sizeof(cmd), simulith_transport_send(&client, (uint8_t *)&cmd, sizeof(cmd)));
        usleep(2000);
        TEST_ASSERT_EQUAL_INT(COMPONENT_WORK, g_iface->service(state, 0, NULL));
        EPS_Device_HK_tlm_t hk;
        size_t n = drain_all(&client, (uint8_t *)&hk, sizeof(hk));
        if (!i) { TEST_ASSERT_EQUAL_size_t(0, n); TEST_ASSERT_EQUAL_UINT16(1, s->crc_remaining); }
        else {
            TEST_ASSERT_EQUAL_size_t(sizeof(hk), n);
            uint8_t crc = g_eps_calc_crc((uint8_t *)&hk, sizeof(hk) - 1);
            if (i == 1) TEST_ASSERT_NOT_EQUAL(crc, hk.crc);
            else TEST_ASSERT_EQUAL_UINT8(crc, hk.crc);
        }
    }
    TEST_ASSERT_EQUAL_UINT16(0, s->fail_remaining);
    TEST_ASSERT_EQUAL_UINT16(0, s->crc_remaining);
    count[1] = 5;
    g_iface->backdoor(state, EPS_BD_CORRUPT_HK_CRC, count, 2);
    g_iface->backdoor(state, EPS_BD_CLEAR_FAULTS, NULL, 0);
    TEST_ASSERT_EQUAL_UINT16(0, s->crc_remaining);
    simulith_transport_close(&client);
    g_iface->destroy(state);
}

static void test_subsecond_energy_and_telemetry_balance(void)
{
    eps_sim_state_t s;
    TEST_ASSERT_EQUAL_INT(0, g_eps_sim_init(&s));
    s.power.configured = 1;
    s.power.snapshots[0].watts = 0.8;
    s.power.snapshots[1].watts = 2.5;
    s.power.snapshots[2].watts = 4.5;
    g_iface->actuate((component_state_t *)&s, 0, NULL);
    double load = s.load_power_w;
    simulith_42_context_t ctx = {0}; ctx.valid = 1; ctx.sun_vector_body[0] = 1;
    g_prepare_tick((component_state_t *)&s, 0, &ctx);
    double initial = s.battery_energy_wh;
    g_prepare_tick((component_state_t *)&s, 250000000ULL, &ctx);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, initial + (EPS_MAX_SOLAR_POWER_W - load) / 14400.0, s.battery_energy_wh);
    TEST_ASSERT_EQUAL_UINT64(1000000000ULL, s.next_hk_update_ns);
    uint8_t packet[EPS_CONSOLE_DOUBLES * 8];
    TEST_ASSERT_EQUAL_size_t(sizeof(packet), g_console_state((component_state_t *)&s,
        250000000ULL, 123.0, packet, sizeof(packet)));
    double values[EPS_CONSOLE_DOUBLES]; memcpy(values, packet, sizeof(packet));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, s.battery_energy_wh, values[6]);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0, values[7]);
    TEST_ASSERT_EQUAL_size_t(0, g_console_state((component_state_t *)&s, 0, 0, packet, 1));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, g_prepare_tick((component_state_t *)&s, 1, &ctx));
}

static void test_console_layout_all_fields_and_buffer_bounds(void)
{
    eps_sim_state_t s;
    TEST_ASSERT_EQUAL_INT(0, g_eps_sim_init(&s));
    s.battery_energy_wh=12; s.initial_energy_wh=10;
    s.solar_energy_wh=3; s.load_energy_wh=2; s.adjustment_wh=1;
    s.solar_power_w=4; s.solar_override=1; s.solar_override_w=9;
    s.backdoor_accepted=17; s.backdoor_rejected=18;
    s.backdoor_last_id=EPS_BD_SET_SOLAR; s.backdoor_last_status=1;
    s.backdoor_generation=19; s.backdoor_applied_ns=UINT64_C(2000000000);
    s.crc_remaining=20; s.fail_remaining=21; s.fail_selector=3;
    s.requests_successful=22; s.requests_rejected=23;
    s.power.configured=1;
    for (unsigned int j=0; j<8; j++) {
        s.power.requested[j]=j%2;
        s.power.effective[j]=!(j%2);
        s.power.fault[j]=j%3;
        s.power.voltage[j]=j+2;
    }
    for (unsigned int j=0; j<3; j++) {
        strcpy(s.power.loads[j].component,"fixture");
        s.power.loads[j].switch_id=j ? 2*j+2 : 0;
        s.power.loads[j].scale=j+1;
        s.power.snapshots[j]=(simulith_power_snapshot_t){
            .supplied=1, .ready=j%2, .mode=j+1, .cycles=30+j,
            .successful_requests=40+j, .rejected_requests=50+j,
            .last_response_ns=(j+1)*UINT64_C(1000000000), .watts=2*j+3,
            .rf_received=60+j, .rf_sent=70+j};
    }
    double expected[110]={1, 3, 12, 12/EPS_BATTERY_CAPACITY_WH, 4,
        EPS_BASE_LOAD_W+15, 12, 0, 17, 18, EPS_BD_SET_SOLAR, 1,
        19, 2, 1, 20, 21, 3, 10, 3, 2, 1};
    const double rail_watts[8]={3,0,0,0,5,0,7,0};
    for (unsigned int j=0; j<8; j++) {
        size_t offset=22+6*j;
        expected[offset]=j%2;
        expected[offset+1]=!(j%2);
        expected[offset+2]=j%3;
        expected[offset+3]=j%2 ? 0 : j+2;
        expected[offset+4]=rail_watts[j]/(j+2);
        expected[offset+5]=rail_watts[j];
    }
    for (unsigned int j=0; j<3; j++) {
        const double fields[12]={1,j%2,j+1,30+j,40+j,50+j,j+1,
                                2*j+3,j+1,60+j,70+j,j ? 2*j+2 : 0};
        memcpy(expected+70+12*j,fields,sizeof(fields));
    }
    expected[106]=UINT64_C(2000000000)/INTERVAL_NS;
    expected[107]=9; expected[108]=22; expected[109]=23;
    uint8_t packet[110*8+16];
    memset(packet,0xa5,sizeof(packet));
    TEST_ASSERT_EQUAL_size_t(110*8,g_console_state((component_state_t *)&s,
        UINT64_C(3000000000),1,packet,sizeof(packet)));
    const uint8_t little_endian_one[8]={0,0,0,0,0,0,0xf0,0x3f};
    TEST_ASSERT_EQUAL_MEMORY(little_endian_one,packet,8);
    for (unsigned int j=0; j<110; j++) {
        uint64_t bits=0;
        for (unsigned int k=0; k<8; k++) bits|=(uint64_t)packet[j*8+k]<<(8*k);
        double value; memcpy(&value,&bits,sizeof(value));
        TEST_ASSERT_DOUBLE_WITHIN(1e-12,expected[j],value);
    }
    for (size_t j=110*8; j<sizeof(packet); j++) TEST_ASSERT_EQUAL_HEX8(0xa5,packet[j]);
    memset(packet,0xa5,sizeof(packet));
    TEST_ASSERT_EQUAL_size_t(0,g_console_state((component_state_t *)&s,
        0,0,packet,110*8-1));
    for (size_t j=0; j<sizeof(packet); j++) TEST_ASSERT_EQUAL_HEX8(0xa5,packet[j]);
}

static int capture_actuate(eps_sim_state_t *s, uint64_t ns, char *output, size_t capacity)
{
    simulith_42_context_t context = {.valid = 1, .dyn_time = 1.0};
    FILE *capture = tmpfile();
    TEST_ASSERT_NOT_NULL(capture);
    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    TEST_ASSERT_TRUE(saved >= 0);
    TEST_ASSERT_TRUE(dup2(fileno(capture), STDOUT_FILENO) >= 0);
    int status = g_iface->actuate((component_state_t *)s, ns, &context);
    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved);
    rewind(capture);
    size_t count = fread(output, 1, capacity - 1, capture);
    output[count] = '\0';
    fclose(capture);
    return status;
}

static void test_console_state_requires_debug(void)
{
    eps_sim_state_t s;
    char output[8192];
    uint8_t packet[EPS_CONSOLE_DOUBLES * 8U];
    const char *previous = getenv("SIMULITH_EPS_DEBUG");
    char *saved = previous ? strdup(previous) : NULL;
    setenv("SIMULITH_EPS_DEBUG", "0", 1);
    TEST_ASSERT_EQUAL_INT(0, g_eps_sim_init(&s));
    TEST_ASSERT_EQUAL_INT(0, capture_actuate(&s, 990000000, output, sizeof(output)));
    TEST_ASSERT_EQUAL_STRING("", output);
    TEST_ASSERT_EQUAL_size_t(sizeof(packet), g_console_state(
        (component_state_t *)&s, 990000000, 1, packet, sizeof(packet)));
    setenv("SIMULITH_EPS_DEBUG", "1", 1);
    TEST_ASSERT_EQUAL_INT(0, g_eps_sim_init(&s));
    TEST_ASSERT_EQUAL_INT(0, capture_actuate(&s, 980000000, output, sizeof(output)));
    TEST_ASSERT_EQUAL_STRING("", output);
    TEST_ASSERT_EQUAL_INT(0, capture_actuate(&s, 990000000, output, sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output, "EPS_SIM_STATE {\"time_ns\":990000000,\"values\":["));
    TEST_ASSERT_EQUAL_size_t(sizeof(packet),g_console_state(
        (component_state_t *)&s,990000000,1,packet,sizeof(packet)));
    char *cursor=strchr(output,'[')+1;
    for (unsigned int j=0; j<EPS_CONSOLE_DOUBLES; j++) {
        char *end=NULL;
        double value=strtod(cursor,&end), expected;
        uint64_t bits=0;
        for (unsigned int k=0; k<8; k++) bits|=(uint64_t)packet[j*8+k]<<(8*k);
        memcpy(&expected,&bits,sizeof(expected));
        TEST_ASSERT_NOT_EQUAL(cursor,end);
        TEST_ASSERT_DOUBLE_WITHIN(1e-12,expected,value);
        TEST_ASSERT_EQUAL_CHAR(j+1<EPS_CONSOLE_DOUBLES ? ',' : ']',*end);
        cursor=end+1;
    }
    TEST_ASSERT_EQUAL_STRING("}\n",cursor);
    g_iface->backdoor((component_state_t *)&s, EPS_BD_RESET, NULL, 0);
    TEST_ASSERT_TRUE(s.debug_enabled);
    setenv("SIMULITH_EPS_DEBUG", "invalid", 1);
    TEST_ASSERT_EQUAL_INT(-1, g_eps_sim_init(&s));
    unsetenv("SIMULITH_EPS_DEBUG");
    TEST_ASSERT_EQUAL_INT(0, g_eps_sim_init(&s));
#ifdef EPS_CFG_DEBUG
    TEST_ASSERT_TRUE(s.debug_enabled);
#else
    TEST_ASSERT_FALSE(s.debug_enabled);
#endif
    if (saved) { setenv("SIMULITH_EPS_DEBUG", saved, 1); free(saved); }
}

int main(void)
{
    g_handle = dlopen(EPS_SIM_SO_PATH, RTLD_NOW);
    if (!g_handle)
    {
        fprintf(stderr, "Failed to dlopen %s: %s\n", EPS_SIM_SO_PATH, dlerror());
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

    const component_interface_t *loaded_iface = get_iface();
    if (!loaded_iface)
    {
        fprintf(stderr, "get_component_interface() returned NULL\n");
        dlclose(g_handle);
        return 1;
    }
    g_phase_iface = *loaded_iface;
    g_prepare_tick = loaded_iface->on_tick;
    g_phase_iface.on_tick = run_tick_phases;
    g_iface = &g_phase_iface;
    g_console_state = (size_t (*)(component_state_t *, uint64_t, double, uint8_t *, size_t))
        dlsym(g_handle, "eps_sim_console_state");
    if (!g_console_state) { dlclose(g_handle); return 1; }

    g_eps_calc_crc    = (eps_calc_crc_fn)dlsym(g_handle, "EPS_Calculate_CRC8");
    g_eps_sim_init    = (eps_init_fn)dlsym(g_handle, "eps_sim_init");
    g_eps_sim_cleanup = (eps_cleanup_fn)dlsym(g_handle, "eps_sim_cleanup");

    UNITY_BEGIN();

    /* Lifecycle / loader */
    RUN_TEST(test_backdoor_payload_lengths_and_invalid_selectors);
    RUN_TEST(test_backdoor_reset_deterministic_noise_and_ordered_cycles);
    RUN_TEST(test_backdoor_state_fault_reset_and_validation);
    RUN_TEST(test_backdoor_crc_and_request_failure_counts);
    RUN_TEST(test_subsecond_energy_and_telemetry_balance);
    RUN_TEST(test_console_layout_all_fields_and_buffer_bounds);
    RUN_TEST(test_console_state_requires_debug);
    RUN_TEST(test_dlopen_eps_sim_so);
    RUN_TEST(test_get_component_interface_symbol);
    RUN_TEST(test_init_returns_success_and_state);
    RUN_TEST(test_lifecycle_callbacks_reject_null);
    RUN_TEST(test_wait_interrupt_and_deadline_saturation);
    RUN_TEST(test_cleanup_releases_i2c_socket);
    RUN_TEST(test_wait_for_service_rejects_null);
    RUN_TEST(test_service_errors_when_transport_uninitialized);

    /* Direct helpers */
    RUN_TEST(test_eps_sim_init_rejects_null_state);
    RUN_TEST(test_eps_sim_cleanup_with_null_is_safe);

    /* Wire protocol */
    RUN_TEST(test_wire_protocol_noop_increments_counter_silently);
    RUN_TEST(test_wire_protocol_get_hk_returns_framed_hk);
    RUN_TEST(test_wire_protocol_switch_on_then_off);
    RUN_TEST(test_wire_protocol_short_packet_is_rejected);
    RUN_TEST(test_wire_protocol_wrong_i2c_addr_is_rejected);
    RUN_TEST(test_wire_protocol_bad_crc_is_rejected);
    RUN_TEST(test_wire_protocol_unknown_cmd_default_arm);
    RUN_TEST(test_wire_protocol_switch_invalid_index_is_ignored);

    /* Tick paths */
    RUN_TEST(test_tick_with_null_state_returns_safely);
    RUN_TEST(test_tick_with_null_42_no_solar_no_crash);
    RUN_TEST(test_tick_before_deadline_skips_hk_update);
    RUN_TEST(test_tick_with_42_eclipse_yields_no_solar);
    RUN_TEST(test_tick_with_42_invalid_yields_no_solar);
    RUN_TEST(test_tick_with_42_negative_sun_x_yields_no_solar);
    RUN_TEST(test_tick_with_42_positive_sun_x_charges_battery);
    RUN_TEST(test_tick_drains_battery_and_clamps_low);
    RUN_TEST(test_tick_charges_battery_and_clamps_high);
    RUN_TEST(test_tick_switch_voltage_reflects_index_range);
    RUN_TEST(test_tick_switch_voltage_reflects_odd_index_range);

    /* Init failure path */
    RUN_TEST(test_init_fails_when_address_path_is_a_directory);

    int result = UNITY_END();

    dlclose(g_handle);
    return result;
}
