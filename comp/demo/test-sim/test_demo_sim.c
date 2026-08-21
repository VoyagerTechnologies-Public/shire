/*
 * Unity test for the demo component simulator (comp/demo/sim/demo_sim.c).
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

#include "demo_sim.h"
#include "demo_device.h"

#ifndef DEMO_SIM_SO_PATH
#error "DEMO_SIM_SO_PATH must be defined (path to demo_sim.so)"
#endif

static void *g_handle                       = NULL;
static const component_interface_t *g_iface = NULL;

void setUp(void) {}
void tearDown(void) {}

/* -------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/
static void demo_sim_uart_address(char *out, size_t cap)
{
    snprintf(out, cap, "ipc:///tmp/simulith_pub:%d", SIMULITH_UART_BASE_PORT + DEMO_CFG_HANDLE);
}

static int open_client_port(transport_port_t *port, const char *name)
{
    memset(port, 0, sizeof(*port));
    snprintf(port->name, sizeof(port->name), "%s", name);
    demo_sim_uart_address(port->address, sizeof(port->address));
    port->is_server = 0;
    return simulith_transport_init(port);
}

static void encode_command(uint8_t *buf, uint16_t cmd_id, uint16_t payload)
{
    buf[0] = DEMO_DEVICE_HDR_0;
    buf[1] = DEMO_DEVICE_HDR_1;
    buf[2] = (uint8_t)((cmd_id >> 8) & 0xFF);
    buf[3] = (uint8_t)(cmd_id & 0xFF);
    buf[4] = (uint8_t)((payload >> 8) & 0xFF);
    buf[5] = (uint8_t)(payload & 0xFF);
    buf[6] = DEMO_DEVICE_TRAILER_0;
    buf[7] = DEMO_DEVICE_TRAILER_1;
}

/* Read up to `cap` bytes from `port`, polling for at most ~50ms. Returns
 * total bytes drained. Multiple ZMQ messages are concatenated. */
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

/* -------------------------------------------------------------------------
 * Lifecycle / loader tests
 * -------------------------------------------------------------------------*/
static void test_dlopen_demo_sim_so(void)
{
    dlerror();
    void       *h   = dlopen(DEMO_SIM_SO_PATH, RTLD_NOW);
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
    TEST_ASSERT_NOT_NULL(g_iface->backdoor);
    TEST_ASSERT_EQUAL_STRING("demo_sim", g_iface->name);
}

static void test_init_returns_success_and_state(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    TEST_ASSERT_NOT_NULL(state);
    g_iface->cleanup(state);
}

static void test_tick_does_not_crash_with_null_42_context(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_iface->tick(state, 0ULL, NULL);
    g_iface->tick(state, 100000000ULL, NULL);
    g_iface->cleanup(state);
}

static void test_cleanup_releases_ipc_socket(void)
{
    /* If cleanup leaks the bound ipc:///tmp/simulith_pub:51005 socket, a
     * subsequent init in the same process will fail to rebind. This case
     * guards against that regression. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_iface->cleanup(state);

    state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_iface->cleanup(state);
}

/* -------------------------------------------------------------------------
 * Backdoor command tests
 * -------------------------------------------------------------------------*/
static void test_backdoor_set_config_writes_device_config(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    uint8_t payload[2] = {0xAB, 0xCD};
    g_iface->backdoor(state, DEMO_BD_SET_CONFIG, payload, sizeof(payload));

    demo_sim_state_t *ds = (demo_sim_state_t *)state;
    TEST_ASSERT_EQUAL_HEX16(0xABCD, ds->hk.DeviceConfig);

    g_iface->cleanup(state);
}

static void test_backdoor_rand_hk_toggles_flag(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    demo_sim_state_t *ds = (demo_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT8(0, ds->rand_hk_enabled);

    uint8_t enable = 1;
    g_iface->backdoor(state, DEMO_BD_RAND_HK, &enable, 1);
    TEST_ASSERT_EQUAL_UINT8(1, ds->rand_hk_enabled);

    uint8_t disable = 0;
    g_iface->backdoor(state, DEMO_BD_RAND_HK, &disable, 1);
    TEST_ASSERT_EQUAL_UINT8(0, ds->rand_hk_enabled);

    g_iface->cleanup(state);
}

static void test_backdoor_rand_data_toggles_flag(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    demo_sim_state_t *ds = (demo_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT8(0, ds->rand_data_enabled);

    uint8_t enable = 1;
    g_iface->backdoor(state, DEMO_BD_RAND_DATA, &enable, 1);
    TEST_ASSERT_EQUAL_UINT8(1, ds->rand_data_enabled);

    uint8_t disable = 0;
    g_iface->backdoor(state, DEMO_BD_RAND_DATA, &disable, 1);
    TEST_ASSERT_EQUAL_UINT8(0, ds->rand_data_enabled);

    g_iface->cleanup(state);
}

static void test_backdoor_unknown_cmd_is_noop(void)
{
    /* Covers the default switch arm in demo_sim_backdoor: state must be
     * unchanged for any cmd_id outside DEMO_BD_SET_CONFIG/RAND_HK/RAND_DATA. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    demo_sim_state_t *ds       = (demo_sim_state_t *)state;
    uint16_t          cfg_b4   = ds->hk.DeviceConfig;
    uint8_t           hkflag   = ds->rand_hk_enabled;
    uint8_t           dataflag = ds->rand_data_enabled;

    uint8_t payload[2] = {0xFF, 0xFF};
    g_iface->backdoor(state, 0x9999, payload, sizeof(payload));

    TEST_ASSERT_EQUAL_HEX16(cfg_b4, ds->hk.DeviceConfig);
    TEST_ASSERT_EQUAL_UINT8(hkflag, ds->rand_hk_enabled);
    TEST_ASSERT_EQUAL_UINT8(dataflag, ds->rand_data_enabled);

    g_iface->cleanup(state);
}

/* -------------------------------------------------------------------------
 * 42 sun-vector-body scaling
 * -------------------------------------------------------------------------*/
static void test_tick_with_rand_data_writes_8bit_random_channels(void)
{
    /* Covers the rand_data path in demo_sim_on_tick: when the flag is set,
     * Chan{1,2,3} are populated from rand() & 0x00FF, so the high byte must
     * always be zero. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    uint8_t enable = 1;
    g_iface->backdoor(state, DEMO_BD_RAND_DATA, &enable, 1);

    /* Tick past the 100ms update gate. */
    g_iface->tick(state, 200000000ULL, NULL);

    demo_sim_state_t *ds = (demo_sim_state_t *)state;
    TEST_ASSERT_EQUAL_HEX16(0x0000, ds->data.Chan1 & 0xFF00);
    TEST_ASSERT_EQUAL_HEX16(0x0000, ds->data.Chan2 & 0xFF00);
    TEST_ASSERT_EQUAL_HEX16(0x0000, ds->data.Chan3 & 0xFF00);

    g_iface->cleanup(state);
}

static void test_tick_with_rand_hk_writes_high_byte_only_hk(void)
{
    /* Covers the rand_hk path in demo_sim_on_tick: when the flag is set,
     * DeviceConfig and DeviceCounter are set to rand() & 0xFF00, so the
     * low byte must always be zero. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    uint8_t enable = 1;
    g_iface->backdoor(state, DEMO_BD_RAND_HK, &enable, 1);

    g_iface->tick(state, 200000000ULL, NULL);

    demo_sim_state_t *ds = (demo_sim_state_t *)state;
    TEST_ASSERT_EQUAL_HEX16(0x0000, ds->hk.DeviceConfig & 0x00FF);
    TEST_ASSERT_EQUAL_HEX16(0x0000, ds->hk.DeviceCounter & 0x00FF);

    g_iface->cleanup(state);
}

static void test_tick_with_42_svb_scales_channels(void)
{
    /* When a valid 42 context is supplied and rand_data is off (the default
     * after init), demo_sim maps the SVB components into Chan{1,2,3} as:
     *   chan = (uint16_t)(svb * 10000.0 + 32768.0)
     * (see comp/demo/sim/demo_sim.c). */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    simulith_42_context_t ctx = {0};
    ctx.valid                 = 1;
    ctx.sim_time              = 0.0;
    ctx.sun_vector_body[0]    = 0.5;   /* -> 5000 + 32768 = 37768 */
    ctx.sun_vector_body[1]    = -0.25; /* -> -2500 + 32768 = 30268 */
    ctx.sun_vector_body[2]    = 0.0;   /* -> 32768 */

    /* Update gate fires when (current - last_update) >= 1/10s. last_update
     * starts at 0, so any tick >= 100ms passes the gate. */
    g_iface->tick(state, 200000000ULL, &ctx);

    demo_sim_state_t *ds = (demo_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(37768, ds->data.Chan1);
    TEST_ASSERT_EQUAL_UINT16(30268, ds->data.Chan2);
    TEST_ASSERT_EQUAL_UINT16(32768, ds->data.Chan3);

    g_iface->cleanup(state);
}

/* -------------------------------------------------------------------------
 * Wire-protocol / transport tests
 *
 * Each test brings up the simulator (UART server bind), connects a client
 * transport_port_t to the same IPC address, exchanges framed device packets,
 * and verifies the simulator's responses byte-for-byte against the protocol
 * documented in comp/demo/shared/demo_device.h.
 * -------------------------------------------------------------------------*/
static void test_wire_protocol_noop_echoes_command(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000); /* Let ZMQ_PAIR connect. */

    uint8_t cmd[DEMO_DEVICE_CMD_SIZE];
    encode_command(cmd, DEMO_DEVICE_NOOP_CMD, 0);
    TEST_ASSERT_EQUAL_INT((int)sizeof(cmd),
                          simulith_transport_send(&client, cmd, sizeof(cmd)));
    usleep(2000); /* Let the message arrive at the simulator. */

    g_iface->tick(state, 100000000ULL, NULL);

    uint8_t rx[DEMO_DEVICE_CMD_SIZE];
    size_t  n = drain_all(&client, rx, sizeof(rx));
    TEST_ASSERT_EQUAL_size_t(sizeof(cmd), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(cmd, rx, sizeof(cmd));

    simulith_transport_close(&client);
    g_iface->cleanup(state);
}

static void test_wire_protocol_req_hk_returns_framed_hk(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    /* Seed a known DeviceConfig via backdoor so we can spot it in the
     * housekeeping response. DeviceCounter increments in handle_command, so
     * after one REQ_HK it will read 1 (zero-initialized at init, ++ at end
     * of handle_command). */
    demo_sim_state_t *ds         = (demo_sim_state_t *)state;
    uint8_t           cfg_payload[2] = {0x12, 0x34};
    g_iface->backdoor(state, DEMO_BD_SET_CONFIG, cfg_payload, sizeof(cfg_payload));
    TEST_ASSERT_EQUAL_HEX16(0x1234, ds->hk.DeviceConfig);

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd[DEMO_DEVICE_CMD_SIZE];
    encode_command(cmd, DEMO_DEVICE_REQ_HK_CMD, 0);
    TEST_ASSERT_EQUAL_INT((int)sizeof(cmd),
                          simulith_transport_send(&client, cmd, sizeof(cmd)));
    usleep(2000);

    g_iface->tick(state, 100000000ULL, NULL);

    /* Expect: 8-byte echo, then 8-byte HK frame:
     *   [C0 FF] [counter_hi counter_lo] [config_hi config_lo] [FE FE] */
    uint8_t rx[DEMO_DEVICE_CMD_SIZE + DEMO_DEVICE_HK_SIZE];
    size_t  n = drain_all(&client, rx, sizeof(rx));
    TEST_ASSERT_EQUAL_size_t(sizeof(rx), n);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(cmd, rx, sizeof(cmd));

    const uint8_t *hk = rx + sizeof(cmd);
    TEST_ASSERT_EQUAL_HEX8(DEMO_DEVICE_HDR_0, hk[0]);
    TEST_ASSERT_EQUAL_HEX8(DEMO_DEVICE_HDR_1, hk[1]);
    uint16_t counter = (uint16_t)((hk[2] << 8) | hk[3]);
    uint16_t config  = (uint16_t)((hk[4] << 8) | hk[5]);
    TEST_ASSERT_EQUAL_HEX8(DEMO_DEVICE_TRAILER_0, hk[6]);
    TEST_ASSERT_EQUAL_HEX8(DEMO_DEVICE_TRAILER_1, hk[7]);
    /* Counter was 0 at start of handle_command; incremented to 1 after. The
     * HK frame is sent BEFORE that increment, so we see 0 here. */
    TEST_ASSERT_EQUAL_UINT16(0, counter);
    TEST_ASSERT_EQUAL_HEX16(0x1234, config);

    simulith_transport_close(&client);
    g_iface->cleanup(state);
}

static void test_wire_protocol_req_data_returns_framed_data(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    /* Drive the SVB-scaling path so the channel values are deterministic. */
    simulith_42_context_t ctx = {0};
    ctx.valid                 = 1;
    ctx.sun_vector_body[0]    = 0.5;
    ctx.sun_vector_body[1]    = -0.25;
    ctx.sun_vector_body[2]    = 0.0;
    g_iface->tick(state, 200000000ULL, &ctx);

    uint8_t cmd[DEMO_DEVICE_CMD_SIZE];
    encode_command(cmd, DEMO_DEVICE_REQ_DATA_CMD, 0);
    TEST_ASSERT_EQUAL_INT((int)sizeof(cmd),
                          simulith_transport_send(&client, cmd, sizeof(cmd)));
    usleep(2000);

    /* Tick at the same time as the previous tick so the SVB-update gate does
     * NOT re-fire — channel values must remain at the values we just
     * computed when the device frame is built. */
    g_iface->tick(state, 200000000ULL, NULL);

    /* Expect: 8-byte echo, then 10-byte data frame:
     *   [C0 FF] [c1_hi c1_lo c2_hi c2_lo c3_hi c3_lo] [FE FE] */
    uint8_t rx[DEMO_DEVICE_CMD_SIZE + DEMO_DEVICE_DATA_SIZE];
    size_t  n = drain_all(&client, rx, sizeof(rx));
    TEST_ASSERT_EQUAL_size_t(sizeof(rx), n);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(cmd, rx, sizeof(cmd));

    const uint8_t *d = rx + sizeof(cmd);
    TEST_ASSERT_EQUAL_HEX8(DEMO_DEVICE_HDR_0, d[0]);
    TEST_ASSERT_EQUAL_HEX8(DEMO_DEVICE_HDR_1, d[1]);
    uint16_t c1 = (uint16_t)((d[2] << 8) | d[3]);
    uint16_t c2 = (uint16_t)((d[4] << 8) | d[5]);
    uint16_t c3 = (uint16_t)((d[6] << 8) | d[7]);
    TEST_ASSERT_EQUAL_HEX8(DEMO_DEVICE_TRAILER_0, d[8]);
    TEST_ASSERT_EQUAL_HEX8(DEMO_DEVICE_TRAILER_1, d[9]);
    TEST_ASSERT_EQUAL_UINT16(37768, c1);
    TEST_ASSERT_EQUAL_UINT16(30268, c2);
    TEST_ASSERT_EQUAL_UINT16(32768, c3);

    simulith_transport_close(&client);
    g_iface->cleanup(state);
}

static void test_wire_protocol_short_packet_is_rejected(void)
{
    /* Covers the length < DEMO_DEVICE_CMD_SIZE guard in handle_command:
     * the simulator must NOT echo and must NOT increment its counter. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t partial[4] = {DEMO_DEVICE_HDR_0, DEMO_DEVICE_HDR_1, 0, DEMO_DEVICE_NOOP_CMD};
    TEST_ASSERT_EQUAL_INT((int)sizeof(partial),
                          simulith_transport_send(&client, partial, sizeof(partial)));
    usleep(2000);
    g_iface->tick(state, 100000000ULL, NULL);

    uint8_t rx[16];
    TEST_ASSERT_EQUAL_size_t(0, drain_all(&client, rx, sizeof(rx)));

    demo_sim_state_t *ds = (demo_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, ds->hk.DeviceCounter);

    simulith_transport_close(&client);
    g_iface->cleanup(state);
}

static void test_wire_protocol_bad_header_is_rejected(void)
{
    /* Covers the header-validation branch in handle_command. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd[DEMO_DEVICE_CMD_SIZE] = {
        0xAA, 0xBB,                /* wrong header */
        0x00, DEMO_DEVICE_NOOP_CMD,
        0x00, 0x00,
        DEMO_DEVICE_TRAILER_0, DEMO_DEVICE_TRAILER_1
    };
    simulith_transport_send(&client, cmd, sizeof(cmd));
    usleep(2000);
    g_iface->tick(state, 100000000ULL, NULL);

    uint8_t rx[16];
    TEST_ASSERT_EQUAL_size_t(0, drain_all(&client, rx, sizeof(rx)));

    demo_sim_state_t *ds = (demo_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, ds->hk.DeviceCounter);

    simulith_transport_close(&client);
    g_iface->cleanup(state);
}

static void test_wire_protocol_bad_trailer_is_rejected(void)
{
    /* Covers the trailer-validation branch in handle_command. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd[DEMO_DEVICE_CMD_SIZE] = {
        DEMO_DEVICE_HDR_0, DEMO_DEVICE_HDR_1,
        0x00, DEMO_DEVICE_NOOP_CMD,
        0x00, 0x00,
        0xDE, 0xAD                 /* wrong trailer */
    };
    simulith_transport_send(&client, cmd, sizeof(cmd));
    usleep(2000);
    g_iface->tick(state, 100000000ULL, NULL);

    uint8_t rx[16];
    TEST_ASSERT_EQUAL_size_t(0, drain_all(&client, rx, sizeof(rx)));

    demo_sim_state_t *ds = (demo_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, ds->hk.DeviceCounter);

    simulith_transport_close(&client);
    g_iface->cleanup(state);
}

static void test_wire_protocol_unknown_cmd_is_echoed_only(void)
{
    /* Covers the default switch arm in handle_command: a well-framed packet
     * with an unknown cmd_id is echoed and counted, but no telemetry frame
     * is appended. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd[DEMO_DEVICE_CMD_SIZE];
    encode_command(cmd, 0x00FF, 0); /* outside 0..3 */
    simulith_transport_send(&client, cmd, sizeof(cmd));
    usleep(2000);
    g_iface->tick(state, 100000000ULL, NULL);

    uint8_t rx[DEMO_DEVICE_CMD_SIZE * 2];
    size_t  n = drain_all(&client, rx, sizeof(rx));
    TEST_ASSERT_EQUAL_size_t(sizeof(cmd), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(cmd, rx, sizeof(cmd));

    demo_sim_state_t *ds = (demo_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(1, ds->hk.DeviceCounter);

    simulith_transport_close(&client);
    g_iface->cleanup(state);
}

static void test_wire_protocol_set_config_updates_state(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    transport_port_t client;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS, open_client_port(&client, "test_client"));
    usleep(2000);

    uint8_t cmd[DEMO_DEVICE_CMD_SIZE];
    encode_command(cmd, DEMO_DEVICE_CFG_CMD, 0xBEEF);
    TEST_ASSERT_EQUAL_INT((int)sizeof(cmd),
                          simulith_transport_send(&client, cmd, sizeof(cmd)));
    usleep(2000);

    g_iface->tick(state, 100000000ULL, NULL);

    /* CFG cmd echoes only — no telemetry frame appended. */
    uint8_t rx[DEMO_DEVICE_CMD_SIZE];
    size_t  n = drain_all(&client, rx, sizeof(rx));
    TEST_ASSERT_EQUAL_size_t(sizeof(cmd), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(cmd, rx, sizeof(cmd));

    /* The simulator's stored config must now match the payload we sent. */
    demo_sim_state_t *ds = (demo_sim_state_t *)state;
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, ds->hk.DeviceConfig);

    simulith_transport_close(&client);
    g_iface->cleanup(state);
}

/* -------------------------------------------------------------------------
 * Init failure path
 * -------------------------------------------------------------------------*/
static void test_init_fails_when_address_path_is_a_directory(void)
{
    /* Covers the bind-failure path in demo_sim_init (and the matching error
     * rollback in demo_sim_component_init). ZMQ allows two binds to the
     * same IPC address, but bind() fails with EADDRINUSE if a directory
     * already occupies the path. */
    char path[256];
    snprintf(path, sizeof(path), "/tmp/simulith_pub:%d", SIMULITH_UART_BASE_PORT + DEMO_CFG_HANDLE);
    (void)unlink(path); /* drop any stray socket file from prior runs */
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mkdir(path, 0755), "could not stage path squat");

    component_state_t *state = NULL;
    int                rc    = g_iface->init(&state);

    /* Always remove the squat dir before asserting so failures don't leak it. */
    (void)rmdir(path);

    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, rc);
}

/* -------------------------------------------------------------------------
 * REGISTER_COMPONENT alias
 * -------------------------------------------------------------------------*/
static void test_get_demo_sim_component_interface_alias(void)
{
    /* The REGISTER_COMPONENT(demo_sim) macro emits a function named
     * get_demo_sim_component_interface(). Resolve it and confirm it returns
     * the same struct that get_component_interface() returns. */
    dlerror();
    get_component_interface_fn fn =
        (get_component_interface_fn)dlsym(g_handle, "get_demo_sim_component_interface");
    const char *err = dlerror();
    TEST_ASSERT_NULL_MESSAGE(err, err ? err : "");
    TEST_ASSERT_NOT_NULL(fn);
    TEST_ASSERT_EQUAL_PTR(g_iface, fn());
}

/* -------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------*/
int main(void)
{
    g_handle = dlopen(DEMO_SIM_SO_PATH, RTLD_NOW);
    if (!g_handle)
    {
        fprintf(stderr, "Failed to dlopen %s: %s\n", DEMO_SIM_SO_PATH, dlerror());
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

    /* Lifecycle / loader */
    RUN_TEST(test_dlopen_demo_sim_so);
    RUN_TEST(test_get_component_interface_symbol);
    RUN_TEST(test_init_returns_success_and_state);
    RUN_TEST(test_tick_does_not_crash_with_null_42_context);
    RUN_TEST(test_cleanup_releases_ipc_socket);

    /* Backdoor */
    RUN_TEST(test_backdoor_set_config_writes_device_config);
    RUN_TEST(test_backdoor_rand_hk_toggles_flag);
    RUN_TEST(test_backdoor_rand_data_toggles_flag);
    RUN_TEST(test_backdoor_unknown_cmd_is_noop);

    /* Tick paths */
    RUN_TEST(test_tick_with_rand_data_writes_8bit_random_channels);
    RUN_TEST(test_tick_with_rand_hk_writes_high_byte_only_hk);
    RUN_TEST(test_tick_with_42_svb_scales_channels);

    /* Wire protocol */
    RUN_TEST(test_wire_protocol_noop_echoes_command);
    RUN_TEST(test_wire_protocol_req_hk_returns_framed_hk);
    RUN_TEST(test_wire_protocol_req_data_returns_framed_data);
    RUN_TEST(test_wire_protocol_set_config_updates_state);
    RUN_TEST(test_wire_protocol_short_packet_is_rejected);
    RUN_TEST(test_wire_protocol_bad_header_is_rejected);
    RUN_TEST(test_wire_protocol_bad_trailer_is_rejected);
    RUN_TEST(test_wire_protocol_unknown_cmd_is_echoed_only);

    /* Init failure path & alias symbol */
    RUN_TEST(test_init_fails_when_address_path_is_a_directory);
    RUN_TEST(test_get_demo_sim_component_interface_alias);

    int result = UNITY_END();

    dlclose(g_handle);
    return result;
}
