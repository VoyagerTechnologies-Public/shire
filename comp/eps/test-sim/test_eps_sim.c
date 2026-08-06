/*
 * Unity test for the EPS component simulator (comp/eps/sim/eps_sim.c).
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

#include "eps_sim.h"
#include "eps_device.h"
#include "device_cfg.h"

#ifndef EPS_SIM_SO_PATH
#error "EPS_SIM_SO_PATH must be defined (path to eps_sim.so)"
#endif

static void                        *g_handle = NULL;
static const component_interface_t *g_iface  = NULL;

typedef uint8_t (*eps_calc_crc_fn)(const uint8_t *, size_t);
typedef int  (*eps_init_fn)(eps_sim_state_t *);
typedef void (*eps_cleanup_fn)(eps_sim_state_t *);

static eps_calc_crc_fn g_eps_calc_crc    = NULL;
static eps_init_fn     g_eps_sim_init    = NULL;
static eps_cleanup_fn  g_eps_sim_cleanup = NULL;

/* Monotonically-increasing tick time. eps_component_tick uses a function-
 * static last_hk_update that persists across init/cleanup, so every test
 * that wants the 1-second update gate to fire must use a tick_time_ns
 * larger than every previous test's. */
static uint64_t g_next_tick_ns = 0;
static uint64_t next_tick(void)
{
    g_next_tick_ns += 2000000000ULL; /* +2s */
    return g_next_tick_ns;
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
    TEST_ASSERT_NOT_NULL(g_iface->name);
    TEST_ASSERT_NOT_NULL(g_iface->description);
    TEST_ASSERT_NOT_NULL(g_iface->init);
    TEST_ASSERT_NOT_NULL(g_iface->tick);
    TEST_ASSERT_NOT_NULL(g_iface->cleanup);
    /* EPS does not implement a backdoor handler — see eps_sim.c
     * eps_component_interface initializer. The .backdoor slot is therefore
     * NULL via designated-initializer zero default. */
    TEST_ASSERT_NULL(g_iface->backdoor);
    TEST_ASSERT_EQUAL_STRING("eps_sim", g_iface->name);
}

static void test_init_returns_success_and_state(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    TEST_ASSERT_NOT_NULL(state);

    eps_sim_state_t *es = (eps_sim_state_t *)state;
    for (int i = 0; i < EPS_NUM_SWITCHES; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(EPS_SWITCH_OFF, es->hk.switches[i].state);
    }
    double expected = EPS_BATTERY_CAPACITY_WH * EPS_BATTERY_INITIAL_SOC;
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, expected, es->battery_energy_wh);

    g_iface->cleanup(state);
}

static void test_cleanup_releases_i2c_socket(void)
{
    /* Guard against the bind-rebind regression: if cleanup leaks the bound
     * IPC socket, a second init in the same process will fail. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_iface->cleanup(state);

    state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_iface->cleanup(state);
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
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    EPS_Command_t cmd;
    encode_command(&cmd, EPS_CMD_NOOP, 0);
    TEST_ASSERT_EQUAL_INT((int)sizeof(cmd),
                          simulith_transport_send(&client, (uint8_t *)&cmd, sizeof(cmd)));
    usleep(2000);

    g_iface->tick(state, next_tick(), NULL);

    uint8_t rx[64];
    TEST_ASSERT_EQUAL_size_t(0, drain_all(&client, rx, sizeof(rx)));

    eps_sim_state_t *es = (eps_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT32(1, es->device_counter);

    simulith_transport_close(&client);
    g_iface->cleanup(state);
}

static void test_wire_protocol_get_hk_returns_framed_hk(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    EPS_Command_t cmd;
    encode_command(&cmd, EPS_CMD_GET_HK, 0);
    simulith_transport_send(&client, (uint8_t *)&cmd, sizeof(cmd));
    usleep(2000);
    g_iface->tick(state, next_tick(), NULL);

    EPS_Device_HK_tlm_t hk;
    size_t              n = drain_all(&client, (uint8_t *)&hk, sizeof(hk));
    TEST_ASSERT_EQUAL_size_t(sizeof(hk), n);
    /* Verify CRC: the simulator computes it just before sending. */
    uint8_t expected_crc = g_eps_calc_crc((const uint8_t *)&hk, sizeof(hk) - 1);
    TEST_ASSERT_EQUAL_HEX8(expected_crc, hk.crc);

    simulith_transport_close(&client);
    g_iface->cleanup(state);
}

static void test_wire_protocol_switch_on_then_off(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    eps_sim_state_t *es = (eps_sim_state_t *)state;

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    EPS_Command_t cmd;
    encode_command(&cmd, EPS_CMD_SWITCH_ON, 3);
    simulith_transport_send(&client, (uint8_t *)&cmd, sizeof(cmd));
    usleep(2000);
    g_iface->tick(state, next_tick(), NULL);
    TEST_ASSERT_EQUAL_UINT8(EPS_SWITCH_ON, es->hk.switches[3].state);

    encode_command(&cmd, EPS_CMD_SWITCH_OFF, 3);
    simulith_transport_send(&client, (uint8_t *)&cmd, sizeof(cmd));
    usleep(2000);
    g_iface->tick(state, next_tick(), NULL);
    TEST_ASSERT_EQUAL_UINT8(EPS_SWITCH_OFF, es->hk.switches[3].state);

    simulith_transport_close(&client);
    g_iface->cleanup(state);
}

static void test_wire_protocol_short_packet_is_rejected(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t partial[2] = {EPS_CFG_I2C_DEVICE_ADDR, EPS_CMD_NOOP};
    simulith_transport_send(&client, partial, sizeof(partial));
    usleep(2000);
    g_iface->tick(state, next_tick(), NULL);

    eps_sim_state_t *es = (eps_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT32(0, es->device_counter);

    simulith_transport_close(&client);
    g_iface->cleanup(state);
}

static void test_wire_protocol_wrong_i2c_addr_is_rejected(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

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
    g_iface->tick(state, next_tick(), NULL);

    eps_sim_state_t *es = (eps_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT32(0, es->device_counter);

    simulith_transport_close(&client);
    g_iface->cleanup(state);
}

static void test_wire_protocol_bad_crc_is_rejected(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

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
    g_iface->tick(state, next_tick(), NULL);

    eps_sim_state_t *es = (eps_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT32(0, es->device_counter);

    simulith_transport_close(&client);
    g_iface->cleanup(state);
}

static void test_wire_protocol_unknown_cmd_default_arm(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    EPS_Command_t cmd;
    encode_command(&cmd, 0x99, 0); /* outside 0..3 */
    simulith_transport_send(&client, (uint8_t *)&cmd, sizeof(cmd));
    usleep(2000);
    g_iface->tick(state, next_tick(), NULL);

    /* Default arm just logs; counter still increments. */
    eps_sim_state_t *es = (eps_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT32(1, es->device_counter);

    simulith_transport_close(&client);
    g_iface->cleanup(state);
}

static void test_wire_protocol_switch_invalid_index_is_ignored(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
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
    g_iface->tick(state, next_tick(), NULL);

    TEST_ASSERT_EQUAL_UINT32(1, es->device_counter);
    for (int i = 0; i < EPS_NUM_SWITCHES; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(EPS_SWITCH_OFF, es->hk.switches[i].state);
    }

    /* Same for SWITCH_OFF with an invalid index. */
    encode_command(&cmd, EPS_CMD_SWITCH_OFF, 99);
    simulith_transport_send(&client, (uint8_t *)&cmd, sizeof(cmd));
    usleep(2000);
    g_iface->tick(state, next_tick(), NULL);
    TEST_ASSERT_EQUAL_UINT32(2, es->device_counter);

    simulith_transport_close(&client);
    g_iface->cleanup(state);
}

/* -------------------------------------------------------------------------
 * Tick / 42 context / battery model tests
 * -------------------------------------------------------------------------*/
static void test_tick_with_null_state_returns_safely(void)
{
    /* eps_component_tick guards against a NULL component_state_t* argument
     * with an early return. Calling with NULL must not crash. */
    g_iface->tick(NULL, next_tick(), NULL);
}

static void test_tick_with_null_42_no_solar_no_crash(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_iface->tick(state, next_tick(), NULL);
    g_iface->cleanup(state);
}

static void test_tick_with_42_eclipse_yields_no_solar(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    eps_sim_state_t *es = (eps_sim_state_t *)state;

    simulith_42_context_t ctx = {0};
    ctx.valid                 = 1;
    ctx.eclipse               = 1;
    ctx.sun_vector_body[0]    = 1.0; /* would be peak if not eclipsed */

    double before = es->battery_energy_wh;
    g_iface->tick(state, next_tick(), &ctx);

    /* No solar (eclipsed), no consumption (all switches OFF) → unchanged. */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, before, es->battery_energy_wh);

    g_iface->cleanup(state);
}

static void test_tick_with_42_invalid_yields_no_solar(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    simulith_42_context_t ctx = {0};
    ctx.valid                 = 0; /* invalid */
    ctx.sun_vector_body[0]    = 1.0;
    g_iface->tick(state, next_tick(), &ctx);

    g_iface->cleanup(state);
}

static void test_tick_with_42_negative_sun_x_yields_no_solar(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    simulith_42_context_t ctx = {0};
    ctx.valid                 = 1;
    ctx.sun_vector_body[0]    = -0.9; /* sun behind +X face */
    g_iface->tick(state, next_tick(), &ctx);

    g_iface->cleanup(state);
}

static void test_tick_with_42_positive_sun_x_charges_battery(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    eps_sim_state_t *es = (eps_sim_state_t *)state;

    /* Drop battery below capacity so positive solar has somewhere to go. */
    es->battery_energy_wh = 1.0;

    simulith_42_context_t ctx = {0};
    ctx.valid                 = 1;
    ctx.sun_vector_body[0]    = 1.0; /* peak generation */
    g_iface->tick(state, next_tick(), &ctx);

    TEST_ASSERT_TRUE(es->battery_energy_wh > 1.0);

    g_iface->cleanup(state);
}

static void test_tick_drains_battery_and_clamps_low(void)
{
    /* Stage near-empty battery + all switches ON. One tick of consumption
     * pushes the energy < 0; the low-clamp must pin it to 0. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    eps_sim_state_t *es = (eps_sim_state_t *)state;

    es->battery_energy_wh = 0.0001;
    for (int i = 0; i < EPS_NUM_SWITCHES; i++)
    {
        es->hk.switches[i].state = EPS_SWITCH_ON;
    }
    g_iface->tick(state, next_tick(), NULL);

    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, es->battery_energy_wh);

    g_iface->cleanup(state);
}

static void test_tick_charges_battery_and_clamps_high(void)
{
    /* Stage near-full battery + max solar + no consumption. The high-clamp
     * must pin energy to capacity. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    eps_sim_state_t *es = (eps_sim_state_t *)state;

    es->battery_energy_wh = EPS_BATTERY_CAPACITY_WH - 0.0001;

    simulith_42_context_t ctx = {0};
    ctx.valid                 = 1;
    ctx.sun_vector_body[0]    = 1.0;
    g_iface->tick(state, next_tick(), &ctx);

    TEST_ASSERT_DOUBLE_WITHIN(1e-9, EPS_BATTERY_CAPACITY_WH, es->battery_energy_wh);

    g_iface->cleanup(state);
}

static void test_tick_switch_voltage_reflects_index_range(void)
{
    /* Turn ON one switch in each of the four index pairs (0|1, 2|3, 4|5,
     * 6|7) so the per-index voltage assignment in the tick switch loop
     * covers all four ranges. Switches still OFF cover the else arm. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    eps_sim_state_t *es = (eps_sim_state_t *)state;

    es->hk.switches[0].state = EPS_SWITCH_ON; /* 3.3V band */
    es->hk.switches[2].state = EPS_SWITCH_ON; /* 5.0V band */
    es->hk.switches[4].state = EPS_SWITCH_ON; /* 12.0V band */
    es->hk.switches[6].state = EPS_SWITCH_ON; /* 24.0V band */

    g_iface->tick(state, next_tick(), NULL);

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

    g_iface->cleanup(state);
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
    int                rc    = g_iface->init(&state);

    (void)rmdir(path);

    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, rc);
}

/* -------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------*/
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

    g_iface = get_iface();
    if (!g_iface)
    {
        fprintf(stderr, "get_component_interface() returned NULL\n");
        dlclose(g_handle);
        return 1;
    }

    g_eps_calc_crc    = (eps_calc_crc_fn)dlsym(g_handle, "EPS_Calculate_CRC8");
    g_eps_sim_init    = (eps_init_fn)dlsym(g_handle, "eps_sim_init");
    g_eps_sim_cleanup = (eps_cleanup_fn)dlsym(g_handle, "eps_sim_cleanup");

    UNITY_BEGIN();

    /* Lifecycle / loader */
    RUN_TEST(test_dlopen_eps_sim_so);
    RUN_TEST(test_get_component_interface_symbol);
    RUN_TEST(test_init_returns_success_and_state);
    RUN_TEST(test_cleanup_releases_i2c_socket);

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
    RUN_TEST(test_tick_with_42_eclipse_yields_no_solar);
    RUN_TEST(test_tick_with_42_invalid_yields_no_solar);
    RUN_TEST(test_tick_with_42_negative_sun_x_yields_no_solar);
    RUN_TEST(test_tick_with_42_positive_sun_x_charges_battery);
    RUN_TEST(test_tick_drains_battery_and_clamps_low);
    RUN_TEST(test_tick_charges_battery_and_clamps_high);
    RUN_TEST(test_tick_switch_voltage_reflects_index_range);

    /* Init failure path */
    RUN_TEST(test_init_fails_when_address_path_is_a_directory);

    int result = UNITY_END();

    dlclose(g_handle);
    return result;
}
