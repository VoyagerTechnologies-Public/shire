/*
 * Unity tests for the radio component simulator (comp/radio/sim/radio_sim.c).
 *
 * Port assignments (from simulith.h base constants + device_cfg.h offsets):
 *   SPI  ipc:///tmp/simulith_pub:53008  (SPI_BASE=53000 + bus=1*8 + cs=0)
 *   PWR  ipc:///tmp/simulith_pub:54010  (GPIO_BASE=54000 + power_pin=10)
 *   INT  ipc:///tmp/simulith_pub:54011  (GPIO_BASE=54000 + interrupt_pin=11)
 *   UDP  127.0.0.1:12343               (RADIO_CFG_UDP_GROUND_RX_PORT)
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "unity.h"
#include "simulith.h"
#include "simulith_component.h"
#include "simulith_transport.h"
#include "simulith_42_context.h"
#include "radio_sim.h"
#include "radio_device.h"
#include "device_cfg.h"

#ifndef RADIO_SIM_SO_PATH
#error "RADIO_SIM_SO_PATH must be defined (path to radio_sim.so)"
#endif

static void                        *g_handle     = NULL;
static const component_interface_t *g_iface      = NULL;
static component_interface_t g_phase_iface;
static int (*g_prepare_tick)(component_state_t *, uint64_t,
                             const simulith_42_context_t *) = NULL;

typedef int  (*radio_init_fn)(radio_sim_state_t *);
typedef void (*radio_cleanup_fn)(radio_sim_state_t *);
/* Link-model functions exported by the loaded simulator, used to stage packets and tampering state. */
static bool (*g_link_enqueue)(radio_link_t *, unsigned int, uint64_t, const uint8_t *, uint32_t) = NULL;
static void (*g_link_remember)(radio_link_t *, unsigned int, const uint8_t *, uint32_t) = NULL;
static radio_init_fn    g_radio_sim_init    = NULL;
static radio_cleanup_fn g_radio_sim_cleanup = NULL;

/* Exercise the same PREPARE and EXECUTE order as the director. */
static int run_tick_phases(component_state_t *state, uint64_t tick_time_ns,
                           const simulith_42_context_t *context_42)
{
    int status = g_prepare_tick(state, tick_time_ns, context_42);
    if (state && g_phase_iface.service)
        g_phase_iface.service(state, 0, NULL);
    return status;
}

#define RADIO_SPI_PORT   (SIMULITH_SPI_BASE_PORT  + RADIO_CFG_SPI_BUS * 8 + RADIO_CFG_SPI_CS)
#define RADIO_POWER_PORT (SIMULITH_GPIO_BASE_PORT + RADIO_CFG_GPIO_POWER_PIN)
#define RADIO_INT_PORT   (SIMULITH_GPIO_BASE_PORT + RADIO_CFG_GPIO_INTERRUPT_PIN)

/* -------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/
static int open_spi_client(transport_port_t *port, const char *name)
{
    memset(port, 0, sizeof(*port));
    snprintf(port->name,    sizeof(port->name),    "%s", name);
    snprintf(port->address, sizeof(port->address),
             "ipc:///tmp/simulith_pub:%d", RADIO_SPI_PORT);
    port->is_server = 0;
    return simulith_transport_init(port);
}

static int open_gpio_client(transport_port_t *port, const char *name, int gpio_port)
{
    memset(port, 0, sizeof(*port));
    snprintf(port->name,    sizeof(port->name),    "%s", name);
    snprintf(port->address, sizeof(port->address),
             "ipc:///tmp/simulith_pub:%d", gpio_port);
    port->is_server = 0;
    return simulith_transport_init(port);
}

/* Build a 5+payload_len byte SPI command frame: [HDR cmd len_hi len_lo payload TRAILER] */
static size_t encode_spi_frame(uint8_t *buf, size_t cap,
                                uint8_t cmd,
                                const uint8_t *payload, uint16_t payload_len)
{
    size_t total = (size_t)(5u + payload_len);
    if (total > cap) return 0;
    buf[0] = RADIO_DEVICE_HDR;
    buf[1] = cmd;
    buf[2] = (uint8_t)((payload_len >> 8) & 0xFFu);
    buf[3] = (uint8_t)(payload_len & 0xFFu);
    if (payload_len > 0 && payload)
        memcpy(&buf[4], payload, payload_len);
    buf[4 + payload_len] = RADIO_DEVICE_TRAILER;
    return total;
}

/* Poll for up to ~50 ms draining all pending ZMQ replies */
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
            idle   = 0;
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

/* Send a UDP datagram to the radio's ground-RX port (simulates ground→radio) */
static int inject_udp(const uint8_t *data, size_t len)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(RADIO_CFG_UDP_GROUND_RX_PORT);
    ssize_t n = sendto(sock, data, len, 0,
                       (struct sockaddr *)&addr, (socklen_t)sizeof(addr));
    close(sock);
    return (int)n;
}

/* Power the radio on by issuing a GPIO-write + one tick (exercises gpio write path) */
static void power_on(component_state_t *state)
{
    transport_port_t pw;
    open_gpio_client(&pw, "pw_helper", RADIO_POWER_PORT);
    usleep(2000);
    uint8_t msg[3] = {1, RADIO_CFG_GPIO_POWER_PIN, 1};
    simulith_transport_send(&pw, msg, sizeof(msg));
    usleep(2000);
    run_tick_phases(state, 0ULL, NULL);
    simulith_transport_close(&pw);
    usleep(1000);
}

/* Tracks the component state initialised by each test so tearDown() can
 * emergency-cleanup if a Unity assertion failure longjmps past cleanup(). */
static component_state_t *g_state_under_test = NULL;

static int g_ground_sock = -1;

void setUp(void)    { g_state_under_test = NULL; }
void tearDown(void)
{
    if (g_ground_sock >= 0)
    {
        close(g_ground_sock);
        g_ground_sock = -1;
    }
    if (g_state_under_test)
    {
        g_iface->destroy(g_state_under_test);
        g_state_under_test = NULL;
    }
}

/* -------------------------------------------------------------------------
 * Lifecycle tests
 * -------------------------------------------------------------------------*/
static void test_dlopen_radio_sim_so(void)
{
    dlerror();
    void       *h   = dlopen(RADIO_SIM_SO_PATH, RTLD_NOW);
    const char *err = dlerror();
    if (!h) TEST_FAIL_MESSAGE(err ? err : "dlopen returned NULL");
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
    TEST_ASSERT_NULL(g_iface->actuate);
    TEST_ASSERT_NOT_NULL(g_iface->destroy);
    TEST_ASSERT_NOT_NULL(g_iface->backdoor);
    TEST_ASSERT_EQUAL_STRING("radio_sim", g_iface->name);
}

static void test_init_returns_success_and_initial_state(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    TEST_ASSERT_NOT_NULL(state);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0,                rs->hk.CommandCounter);
    TEST_ASSERT_EQUAL_UINT8(RADIO_MODE_DUPLEX, rs->hk.Mode);
    TEST_ASSERT_EQUAL_UINT8(0,                 rs->hk.GroundLock);
    TEST_ASSERT_EQUAL_UINT8(RADIO_MODE_DUPLEX, rs->config.Mode);
    TEST_ASSERT_EQUAL_UINT8(0,                 rs->interrupt_asserted);

    g_iface->destroy(state);
}

static void test_lifecycle_callbacks_reject_null(void)
{
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, g_iface->create(NULL));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_prepare_tick(NULL, 0, NULL));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->service(NULL, 0, NULL));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR,
                          g_iface->wait_for_service(NULL, 0));
    g_iface->destroy(NULL);
}

static void test_cleanup_rebind_ok(void)
{
    /* Calling init/cleanup twice must not leak SPI or GPIO IPC sockets */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_iface->destroy(state);

    state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_iface->destroy(state);
}

static void test_cleanup_with_null_is_safe(void)
{
    g_iface->destroy(NULL);
}

/* -------------------------------------------------------------------------
 * Direct-function tests
 * -------------------------------------------------------------------------*/
static void test_radio_sim_init_rejects_null(void)
{
    TEST_ASSERT_NOT_NULL(g_radio_sim_init);
    TEST_ASSERT_EQUAL_INT(RADIO_SIM_ERROR, g_radio_sim_init(NULL));
}

static void test_radio_sim_cleanup_with_null_is_safe(void)
{
    TEST_ASSERT_NOT_NULL(g_radio_sim_cleanup);
    g_radio_sim_cleanup(NULL);
}

/* -------------------------------------------------------------------------
 * Tick edge-case tests
 * -------------------------------------------------------------------------*/
static void test_tick_with_null_state_returns_safely(void)
{
    g_iface->on_tick(NULL, 0ULL, NULL);
}

static void test_tick_does_not_crash(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_iface->on_tick(state, 0ULL, NULL);
    g_iface->on_tick(state, 200000000ULL, NULL);  /* 200 ms — crosses rate-limit threshold */
    g_iface->destroy(state);
}

/* -------------------------------------------------------------------------
 * SPI wire-protocol tests
 * Each test powers on the radio via GPIO write before sending SPI frames so
 * that the tick does not silently discard them.
 * -------------------------------------------------------------------------*/
static void test_spi_noop_increments_command_counter(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    power_on(state);

    transport_port_t spi;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_spi_client(&spi, "test_spi"));
    usleep(2000);

    uint8_t frame[5];
    encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_NOOP_CMD, NULL, 0);
    TEST_ASSERT_EQUAL_INT(5, simulith_transport_send(&spi, frame, 5));
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(1, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->destroy(state);
}

static void test_spi_req_hk_returns_framed_housekeeping(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    power_on(state);

    transport_port_t spi;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_spi_client(&spi, "test_spi"));
    usleep(2000);

    uint8_t frame[5];
    encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_REQ_HK_CMD, NULL, 0);
    simulith_transport_send(&spi, frame, 5);
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    uint8_t resp[32];
    size_t  n = drain_all(&spi, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t((size_t)RADIO_DEVICE_HK_SIZE, n);
    TEST_ASSERT_EQUAL_HEX8(RADIO_DEVICE_HDR,     resp[0]);
    TEST_ASSERT_EQUAL_HEX8(RADIO_DEVICE_TRAILER, resp[RADIO_DEVICE_HK_SIZE - 1]);

    simulith_transport_close(&spi);
    g_iface->destroy(state);
}

static void test_hk_initial_mode_and_counter(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    uint8_t frame[5];
    encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_REQ_HK_CMD, NULL, 0);
    simulith_transport_send(&spi, frame, 5);
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    uint8_t resp[32];
    TEST_ASSERT_EQUAL_size_t((size_t)RADIO_DEVICE_HK_SIZE,
                             drain_all(&spi, resp, sizeof(resp)));

    uint16_t counter = (uint16_t)(((uint16_t)resp[1] << 8) | resp[2]);
    TEST_ASSERT_EQUAL_UINT16(1,                counter);   /* REQ_HK increments */
    TEST_ASSERT_EQUAL_UINT8(RADIO_MODE_DUPLEX, resp[3]);   /* mode */
    /* ground lock follows the link: an ideal (always-in-view) link is locked */
    TEST_ASSERT_EQUAL_UINT8(RADIO_CFG_LINK_MODE == RADIO_LINK_MODE_ALWAYS ? 1 : 0, resp[4]);

    simulith_transport_close(&spi);
    g_iface->destroy(state);
}

static void test_spi_set_cfg_updates_mode(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    uint8_t payload[RADIO_CFG_PAYLOAD_SIZE] = {RADIO_MODE_RX, 1, 2, 3, 4};
    uint8_t frame[5 + RADIO_CFG_PAYLOAD_SIZE];
    size_t  len = encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_SET_CFG_CMD,
                                   payload, RADIO_CFG_PAYLOAD_SIZE);
    simulith_transport_send(&spi, frame, len);
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT8(RADIO_MODE_RX, rs->config.Mode);
    TEST_ASSERT_EQUAL_UINT8(RADIO_MODE_RX, rs->hk.Mode);
    TEST_ASSERT_EQUAL_UINT16(1,             rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->destroy(state);
}

static void test_spi_set_cfg_wrong_payload_size_ignored(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    /* payload_len=3 != RADIO_CFG_PAYLOAD_SIZE(5); sim logs error, ignores */
    uint8_t payload[3] = {1, 2, 3};
    uint8_t frame[5 + 3];
    size_t  len = encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_SET_CFG_CMD,
                                   payload, 3);
    simulith_transport_send(&spi, frame, len);
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0,                rs->hk.CommandCounter);
    TEST_ASSERT_EQUAL_UINT8(RADIO_MODE_DUPLEX, rs->config.Mode);  /* unchanged */

    simulith_transport_close(&spi);
    g_iface->destroy(state);
}

static void test_spi_receive_cmd_empty_buffer_returns_frame(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    /* RECEIVE with requested=0: response is always requested+4=4 bytes */
    uint8_t payload[2] = {0x00, 0x00};
    uint8_t frame[7];
    size_t  len = encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_RECEIVE_CMD,
                                   payload, 2);
    simulith_transport_send(&spi, frame, len);
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    uint8_t resp[16];
    size_t  n = drain_all(&spi, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_HEX8(RADIO_DEVICE_HDR,     resp[0]);
    TEST_ASSERT_EQUAL_UINT8(0,                   resp[1]);  /* len_hi */
    TEST_ASSERT_EQUAL_UINT8(0,                   resp[2]);  /* len_lo (to_send=0) */
    TEST_ASSERT_EQUAL_HEX8(RADIO_DEVICE_TRAILER, resp[3]);

    simulith_transport_close(&spi);
    g_iface->destroy(state);
}

static void test_spi_receive_cmd_wrong_payload_rejected(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    /* payload_len=1 != 2: sim prints error, sends nothing */
    uint8_t payload[1] = {0x0A};
    uint8_t frame[6];
    size_t  len = encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_RECEIVE_CMD,
                                   payload, 1);
    simulith_transport_send(&spi, frame, len);
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    uint8_t resp[16];
    TEST_ASSERT_EQUAL_size_t(0, drain_all(&spi, resp, sizeof(resp)));

    simulith_transport_close(&spi);
    g_iface->destroy(state);
}

static void test_spi_send_cmd_in_duplex_mode(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    uint8_t payload[5] = {'h', 'e', 'l', 'l', 'o'};
    uint8_t frame[5 + 5];
    size_t  len = encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_SEND_CMD,
                                   payload, 5);
    simulith_transport_send(&spi, frame, len);
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    /* CommandCounter increments in DUPLEX mode regardless of UDP TX result */
    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(1, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->destroy(state);
}

static void test_spi_send_cmd_zero_payload_no_counter(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    /* SEND with payload_len=0: the if(payload_len>0) guard skips entirely */
    uint8_t frame[5];
    encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_SEND_CMD, NULL, 0);
    simulith_transport_send(&spi, frame, 5);
    usleep(2000);
    g_prepare_tick(state, 0ULL, NULL);
    TEST_ASSERT_EQUAL_INT(COMPONENT_WORK, g_iface->service(state, 0ULL, NULL));

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT64(1, rs->power.rejected_requests);
    TEST_ASSERT_EQUAL_UINT16(0, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->destroy(state);
}

static void test_spi_send_cmd_in_rx_mode_not_forwarded(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    /* First switch to RX-only mode */
    uint8_t cfg[RADIO_CFG_PAYLOAD_SIZE] = {RADIO_MODE_RX, 0, 0, 0, 0};
    uint8_t cfg_frame[5 + RADIO_CFG_PAYLOAD_SIZE];
    size_t  cfg_len = encode_spi_frame(cfg_frame, sizeof(cfg_frame),
                                       RADIO_DEVICE_SET_CFG_CMD, cfg,
                                       RADIO_CFG_PAYLOAD_SIZE);
    simulith_transport_send(&spi, cfg_frame, cfg_len);
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);  /* counter = 1 after SET_CFG */

    /* SEND in RX-only mode: mode check fails, counter does not increment */
    uint8_t data[3] = {0x01, 0x02, 0x03};
    uint8_t frame[5 + 3];
    size_t  len = encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_SEND_CMD,
                                   data, 3);
    simulith_transport_send(&spi, frame, len);
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(1, rs->hk.CommandCounter);  /* only SET_CFG counted */

    simulith_transport_close(&spi);
    g_iface->destroy(state);
}

static void test_spi_send_cmd_in_tx_mode_forwarded(void)
{
    /* mode == RADIO_MODE_TX short-circuits the `mode == TX || mode == DUPLEX`
     * OR without evaluating the DUPLEX comparison -- covers that branch
     * outcome, complementary to the DUPLEX and RX mode SEND_CMD tests. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    uint8_t cfg[RADIO_CFG_PAYLOAD_SIZE] = {RADIO_MODE_TX, 0, 0, 0, 0};
    uint8_t cfg_frame[5 + RADIO_CFG_PAYLOAD_SIZE];
    size_t  cfg_len = encode_spi_frame(cfg_frame, sizeof(cfg_frame),
                                       RADIO_DEVICE_SET_CFG_CMD, cfg,
                                       RADIO_CFG_PAYLOAD_SIZE);
    simulith_transport_send(&spi, cfg_frame, cfg_len);
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);  /* counter = 1 after SET_CFG */

    uint8_t data[3] = {0x0A, 0x0B, 0x0C};
    uint8_t frame[5 + 3];
    size_t  len = encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_SEND_CMD,
                                   data, 3);
    simulith_transport_send(&spi, frame, len);
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(2, rs->hk.CommandCounter);  /* SET_CFG + forwarded SEND */

    simulith_transport_close(&spi);
    g_iface->destroy(state);
}

static void test_spi_send_cmd_fails_on_bad_socket(void)
{
    /* Close the TX socket before SEND so sendto() returns -1, covering the
     * failure-log branch.  cleanup() then exercises the udp_tx_socket < 0 guard. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    power_on(state);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    close(rs->udp_tx_socket);
    rs->udp_tx_socket = -1;

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t frame[5 + 4];
    size_t  len = encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_SEND_CMD, payload, 4);
    simulith_transport_send(&spi, frame, len);
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    /* BytesReceived always incremented; CommandCounter incremented in TX/DUPLEX
     * even when sendto fails; BytesSent stays 0 because sendto returned <= 0. */
    TEST_ASSERT_EQUAL_UINT32(4, rs->hk.BytesReceived);
    TEST_ASSERT_EQUAL_UINT16(1, rs->hk.CommandCounter);
    TEST_ASSERT_EQUAL_UINT32(0, rs->hk.BytesSent);

    simulith_transport_close(&spi);
    g_iface->destroy(state);  /* cleanup skips close(udp_tx_socket) since it's -1 */
}

static void test_spi_short_packet_rejected(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    /* 4 bytes < minimum 5: length guard in handle_spi_command rejects it */
    uint8_t short_frame[4] = {RADIO_DEVICE_HDR, RADIO_DEVICE_NOOP_CMD, 0x00, 0x11};
    simulith_transport_send(&spi, short_frame, 4);
    usleep(2000);
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS,
                          g_prepare_tick(state, 0ULL, NULL));
    TEST_ASSERT_EQUAL_INT(COMPONENT_WORK,
                          g_phase_iface.service(state, 0ULL, NULL));

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->destroy(state);
}

static void test_spi_bad_header_rejected(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    uint8_t frame[5] = {0xBB, RADIO_DEVICE_NOOP_CMD, 0x00, 0x00, RADIO_DEVICE_TRAILER};
    simulith_transport_send(&spi, frame, sizeof(frame));
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->destroy(state);
}

static void test_spi_bad_trailer_rejected(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    uint8_t frame[5] = {RADIO_DEVICE_HDR, RADIO_DEVICE_NOOP_CMD, 0x00, 0x00, 0xBB};
    simulith_transport_send(&spi, frame, sizeof(frame));
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->destroy(state);
}

static void test_spi_truncated_payload_rejected(void)
{
    /* Header declares payload_len=5 but only 4 payload bytes are included (9 total < 10
     * expected), hitting the `length < expected_len` guard at radio_sim_handle_spi_command. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    uint8_t frame[9] = {
        RADIO_DEVICE_HDR, RADIO_DEVICE_NOOP_CMD,
        0x00, 0x05,                              /* payload_len = 5 → expected = 10 */
        0xAA, 0xBB, 0xCC, 0xDD,                 /* only 4 payload bytes */
        RADIO_DEVICE_TRAILER                     /* trailer at byte 8, not 9 */
    };
    simulith_transport_send(&spi, frame, sizeof(frame));  /* 9 < 10 → rejected */
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->destroy(state);
}

static void test_spi_unknown_command_handled(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    uint8_t frame[5];
    encode_spi_frame(frame, sizeof(frame), 0x99, NULL, 0);
    simulith_transport_send(&spi, frame, 5);
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    /* Default arm: logs error, no crash, counter unchanged */
    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->destroy(state);
}

static void test_spi_cmd_dropped_when_powered_off(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));

    /* Explicitly ensure power is off regardless of prior test state */
    ((radio_sim_state_t *)state)->power_gpio.value = 0;

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    uint8_t frame[5];
    encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_NOOP_CMD, NULL, 0);
    simulith_transport_send(&spi, frame, 5);
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->destroy(state);
}

/* -------------------------------------------------------------------------
 * GPIO tests
 * -------------------------------------------------------------------------*/
static void test_gpio_power_read_returns_on(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_state_under_test = state;
    /* Set the power state directly — avoids a ZMQ PAIR close/reopen race that
     * occurs when power_on() opens and closes pw_helper before we connect test_pw. */
    ((radio_sim_state_t *)state)->power_gpio.value = 1;

    transport_port_t pw;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_gpio_client(&pw, "test_pw", RADIO_POWER_PORT));
    usleep(5000); /* give PAIR handshake time to complete */

    uint8_t req[2] = {0, RADIO_CFG_GPIO_POWER_PIN};  /* read command */
    simulith_transport_send(&pw, req, sizeof(req));
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    uint8_t resp[8];
    size_t  n = drain_all(&pw, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_UINT8(0,                        resp[0]);  /* cmd=read */
    TEST_ASSERT_EQUAL_UINT8(RADIO_CFG_GPIO_POWER_PIN, resp[1]);
    TEST_ASSERT_EQUAL_UINT8(1,                        resp[2]);  /* powered on */

    simulith_transport_close(&pw);
    g_state_under_test = NULL;
    g_iface->destroy(state);
}

static void test_gpio_interrupt_read_initial(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));

    transport_port_t irq;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_gpio_client(&irq, "test_irq", RADIO_INT_PORT));
    usleep(2000);

    uint8_t req[2] = {0, RADIO_CFG_GPIO_INTERRUPT_PIN};
    simulith_transport_send(&irq, req, sizeof(req));
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    uint8_t resp[8];
    size_t  n = drain_all(&irq, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_UINT8(0,                             resp[0]);
    TEST_ASSERT_EQUAL_UINT8(RADIO_CFG_GPIO_INTERRUPT_PIN,  resp[1]);
    TEST_ASSERT_EQUAL_UINT8(0,                             resp[2]);  /* not asserted */

    simulith_transport_close(&irq);
    g_iface->destroy(state);
}

static void test_gpio_power_write_same_value_no_op(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_state_under_test = state;
    ((radio_sim_state_t *)state)->power_gpio.value = 1;

    transport_port_t pw;
    open_gpio_client(&pw, "test_pw", RADIO_POWER_PORT);
    usleep(5000);

    /* Write 1 when already 1: value==current, no state change */
    uint8_t req[3] = {1, RADIO_CFG_GPIO_POWER_PIN, 1};
    simulith_transport_send(&pw, req, sizeof(req));
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT8(RADIO_MODE_DUPLEX, rs->config.Mode);  /* unchanged */

    simulith_transport_close(&pw);
    g_state_under_test = NULL;
    g_iface->destroy(state);
}

static void test_gpio_power_write_off_clears_state(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_state_under_test = state;
    /* Set power on directly — avoids PAIR reconnect race from power_on() open/close. */
    ((radio_sim_state_t *)state)->power_gpio.value = 1;

    transport_port_t pw;
    open_gpio_client(&pw, "test_pw", RADIO_POWER_PORT);
    usleep(5000);

    /* Write 0: power off — radio clears buffers and sets mode to SLEEP */
    uint8_t req[3] = {1, RADIO_CFG_GPIO_POWER_PIN, 0};
    simulith_transport_send(&pw, req, sizeof(req));
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT8(RADIO_MODE_SLEEP, rs->hk.Mode);
    TEST_ASSERT_EQUAL_UINT32(0, rs->rx_buffer_head);
    TEST_ASSERT_EQUAL_UINT32(0, rs->rx_buffer_tail);

    simulith_transport_close(&pw);
    g_state_under_test = NULL;
    g_iface->destroy(state);
}

static void test_gpio_power_write_on_from_off_does_not_reset_state(void)
{
    /* value != current (0 -> 1) takes the state-change branch, but !value is
     * false so the buffer/config reset block is skipped -- covers the false
     * outcome of the `if (!value)` guard that only the power-off write hits
     * elsewhere. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_state_under_test = state;
    ((radio_sim_state_t *)state)->power_gpio.value = 0;

    transport_port_t pw;
    open_gpio_client(&pw, "test_pw", RADIO_POWER_PORT);
    usleep(5000);

    uint8_t req[3] = {1, RADIO_CFG_GPIO_POWER_PIN, 1};  /* write ON */
    simulith_transport_send(&pw, req, sizeof(req));
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT8(1, rs->power_gpio.value);

    simulith_transport_close(&pw);
    g_state_under_test = NULL;
    g_iface->destroy(state);
}

static void test_gpio_interrupt_write(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));

    transport_port_t irq;
    open_gpio_client(&irq, "test_irq", RADIO_INT_PORT);
    usleep(2000);

    /* Write interrupt line to 1 — no response expected for GPIO writes */
    uint8_t req[3] = {1, RADIO_CFG_GPIO_INTERRUPT_PIN, 1};
    simulith_transport_send(&irq, req, sizeof(req));
    usleep(2000);
    g_iface->on_tick(state, 0ULL, NULL);

    simulith_transport_close(&irq);
    g_iface->destroy(state);
}

static void test_gpio_rejects_short_wrong_pin_and_invalid_commands(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_state_under_test = state;
    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    rs->power_gpio.value = 0;
    rs->interrupt_gpio.value = 0;

    transport_port_t power;
    transport_port_t interrupt;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_gpio_client(&power, "test_pw_invalid", RADIO_POWER_PORT));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_gpio_client(&interrupt, "test_irq_invalid", RADIO_INT_PORT));
    usleep(5000);

    const uint8_t short_request[] = {0};
    const uint8_t wrong_power_pin[] = {0, (uint8_t)(RADIO_CFG_GPIO_POWER_PIN + 1)};
    const uint8_t invalid_power_command[] = {2, RADIO_CFG_GPIO_POWER_PIN, 1};
    const uint8_t short_power_write[] = {1, RADIO_CFG_GPIO_POWER_PIN};
    /* cmd==0 (read) but 3 bytes instead of 2 -- covers the `gpio_bytes == 2`
     * false outcome of the read-command guard. */
    const uint8_t bad_length_power_read[] = {0, RADIO_CFG_GPIO_POWER_PIN, 0xFF};
    /* cmd==1, 3 bytes, but value > 1 -- covers the `gpio_rx_buf[2] <= 1U`
     * false outcome of the write-command guard. */
    const uint8_t invalid_power_value[] = {1, RADIO_CFG_GPIO_POWER_PIN, 2};
    const uint8_t wrong_interrupt_pin[] = {0, (uint8_t)(RADIO_CFG_GPIO_INTERRUPT_PIN + 1)};
    const uint8_t invalid_interrupt_command[] = {2, RADIO_CFG_GPIO_INTERRUPT_PIN, 1};
    const uint8_t short_interrupt_write[] = {1, RADIO_CFG_GPIO_INTERRUPT_PIN};
    const uint8_t bad_length_interrupt_read[] = {0, RADIO_CFG_GPIO_INTERRUPT_PIN, 0xFF};
    const uint8_t invalid_interrupt_value[] = {1, RADIO_CFG_GPIO_INTERRUPT_PIN, 2};

    simulith_transport_send(&power, short_request, sizeof(short_request));
    usleep(1000);
    g_iface->on_tick(state, 0ULL, NULL);
    simulith_transport_send(&power, wrong_power_pin, sizeof(wrong_power_pin));
    usleep(1000);
    g_iface->on_tick(state, 0ULL, NULL);
    simulith_transport_send(&power, invalid_power_command, sizeof(invalid_power_command));
    usleep(1000);
    g_iface->on_tick(state, 0ULL, NULL);
    simulith_transport_send(&power, short_power_write, sizeof(short_power_write));
    usleep(1000);
    g_iface->on_tick(state, 0ULL, NULL);
    simulith_transport_send(&power, bad_length_power_read, sizeof(bad_length_power_read));
    usleep(1000);
    g_iface->on_tick(state, 0ULL, NULL);
    simulith_transport_send(&power, invalid_power_value, sizeof(invalid_power_value));
    usleep(1000);
    g_iface->on_tick(state, 0ULL, NULL);

    simulith_transport_send(&interrupt, short_request, sizeof(short_request));
    usleep(1000);
    g_iface->on_tick(state, 0ULL, NULL);
    simulith_transport_send(&interrupt, wrong_interrupt_pin, sizeof(wrong_interrupt_pin));
    usleep(1000);
    g_iface->on_tick(state, 0ULL, NULL);
    simulith_transport_send(&interrupt, invalid_interrupt_command,
                            sizeof(invalid_interrupt_command));
    usleep(1000);
    g_iface->on_tick(state, 0ULL, NULL);
    simulith_transport_send(&interrupt, short_interrupt_write,
                            sizeof(short_interrupt_write));
    usleep(1000);
    g_iface->on_tick(state, 0ULL, NULL);
    simulith_transport_send(&interrupt, bad_length_interrupt_read,
                            sizeof(bad_length_interrupt_read));
    usleep(1000);
    g_iface->on_tick(state, 0ULL, NULL);
    simulith_transport_send(&interrupt, invalid_interrupt_value,
                            sizeof(invalid_interrupt_value));
    usleep(1000);
    g_iface->on_tick(state, 0ULL, NULL);

    uint8_t response[8];
    TEST_ASSERT_EQUAL_size_t(0, drain_all(&power, response, sizeof(response)));
    TEST_ASSERT_EQUAL_size_t(0, drain_all(&interrupt, response, sizeof(response)));
    TEST_ASSERT_EQUAL_INT(0, rs->power_gpio.value);
    TEST_ASSERT_EQUAL_INT(0, rs->interrupt_gpio.value);

    simulith_transport_close(&interrupt);
    simulith_transport_close(&power);
    g_state_under_test = NULL;
    g_iface->destroy(state);
}

/* -------------------------------------------------------------------------
 * Interrupt-flag tests (direct state manipulation, no transport overhead)
 * -------------------------------------------------------------------------*/
static void test_interrupt_asserted_when_buffer_fills(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    pthread_mutex_lock(&rs->buffer_mutex);
    rs->rx_buffer_head    = RADIO_SIM_INTERRUPT_THRESHOLD;  /* 32768 bytes filled */
    rs->rx_buffer_tail    = 0;
    rs->interrupt_asserted = 0;
    pthread_mutex_unlock(&rs->buffer_mutex);

    /* 200 ms tick crosses the rate-limit gate so radio_sim_update_interrupt fires */
    g_iface->on_tick(state, 200000000ULL, NULL);

    TEST_ASSERT_EQUAL_UINT8(1, rs->interrupt_asserted);

    g_iface->destroy(state);
}

static void test_interrupt_cleared_when_buffer_drains(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    pthread_mutex_lock(&rs->buffer_mutex);
    rs->interrupt_asserted = 1;  /* was asserted */
    rs->rx_buffer_head     = 0;  /* buffer now empty */
    rs->rx_buffer_tail     = 0;
    pthread_mutex_unlock(&rs->buffer_mutex);

    g_iface->on_tick(state, 200000000ULL, NULL);

    TEST_ASSERT_EQUAL_UINT8(0, rs->interrupt_asserted);

    g_iface->destroy(state);
}

/* -------------------------------------------------------------------------
 * Circular-buffer internals (wrap-around and overflow paths)
 * -------------------------------------------------------------------------*/
static void test_rx_buffer_count_wraps_correctly(void)
{
    /* Verify the (head < tail) wrap-around branch in radio_sim_get_rx_buffer_count.
     * Formula: count = (BUFFER_SIZE - tail) + head when head < tail.
     * Set tail = BUFFER_SIZE - THRESHOLD, head = 1
     *   → count = THRESHOLD + 1 > THRESHOLD, and head(1) < tail ✓
     * The tick's update_interrupt call then fires because count >= THRESHOLD. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_state_under_test = state;

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    pthread_mutex_lock(&rs->buffer_mutex);
    rs->rx_buffer_head    = 1;
    rs->rx_buffer_tail    = (uint32_t)(RADIO_SIM_RX_BUFFER_SIZE - RADIO_SIM_INTERRUPT_THRESHOLD);
    rs->interrupt_asserted = 0;
    pthread_mutex_unlock(&rs->buffer_mutex);

    g_iface->on_tick(state, 200000000ULL, NULL);  /* 200 ms crosses rate-limit gate */

    TEST_ASSERT_EQUAL_UINT8(1, rs->interrupt_asserted);

    g_state_under_test = NULL;
    g_iface->destroy(state);
}

static void test_rx_buffer_overflow_clips_to_available(void)
{
    /* Fill the circular buffer to within 4 bytes of capacity, then inject 20 bytes
     * via UDP.  radio_sim_write_to_rx_buffer must clip and log the overflow. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_state_under_test = state;
    power_on(state);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    /* head=BUFFER_SIZE-5, tail=0 → count=BUFFER_SIZE-5, available=4 */
    pthread_mutex_lock(&rs->buffer_mutex);
    rs->rx_buffer_head = (uint32_t)(RADIO_SIM_RX_BUFFER_SIZE - 5);
    rs->rx_buffer_tail = 0;
    pthread_mutex_unlock(&rs->buffer_mutex);

    uint8_t data[20];
    memset(data, 0xAB, sizeof(data));
    TEST_ASSERT_TRUE(inject_udp(data, sizeof(data)) > 0);
    usleep(50000);  /* let UDP thread process */

    pthread_mutex_lock(&rs->buffer_mutex);
    uint32_t head = rs->rx_buffer_head;
    uint32_t tail = rs->rx_buffer_tail;
    uint32_t count = (head >= tail) ? head - tail
                                    : (RADIO_SIM_RX_BUFFER_SIZE - tail) + head;
    pthread_mutex_unlock(&rs->buffer_mutex);

    /* Exactly 4 bytes written (clipped): head advances to BUFFER_SIZE-1, tail stays 0 */
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(RADIO_SIM_RX_BUFFER_SIZE - 1), count);

    g_state_under_test = NULL;
    g_iface->destroy(state);
}

static void test_cleanup_skips_closed_rx_socket(void)
{
    /* Stop the UDP thread manually, then pre-close udp_rx_socket so cleanup hits
     * the `socket < 0` guard rather than calling close() again. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_state_under_test = state;

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    rs->udp_thread_running = 0;
    pthread_join(rs->udp_thread, NULL);

    close(rs->udp_rx_socket);
    rs->udp_rx_socket = -1;

    /* cleanup must not double-close or crash; it also skips pthread_join
     * because udp_thread_running is already 0. */
    g_state_under_test = NULL;
    g_iface->destroy(state);
}

/* -------------------------------------------------------------------------
 * UDP / receive-buffer tests
 * -------------------------------------------------------------------------*/
static void test_udp_thread_ignores_data_when_powered_off(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_state_under_test = state;
    /* Leave radio powered off (initial state) */
    ((radio_sim_state_t *)state)->power_gpio.value = 0;

    static const uint8_t payload[8] = {0xAB, 0xCD, 0xEF, 0x01, 0x02, 0x03, 0x04, 0x05};
    inject_udp(payload, sizeof(payload));
    usleep(50000);  /* let UDP thread run */

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    pthread_mutex_lock(&rs->buffer_mutex);
    uint32_t count = rs->rx_buffer_head - rs->rx_buffer_tail;
    pthread_mutex_unlock(&rs->buffer_mutex);

    TEST_ASSERT_EQUAL_UINT32(0, count);  /* no bytes written when powered off */

    g_state_under_test = NULL;
    g_iface->destroy(state);
}

static void test_udp_inject_populates_rx_buffer(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_state_under_test = state;
    power_on(state);  /* radio must be on for UDP thread to write to buffer */

    static const uint8_t payload[64];
    TEST_ASSERT_TRUE(inject_udp(payload, sizeof(payload)) > 0);
    usleep(50000);  /* wait for UDP thread to populate the circular buffer */

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    pthread_mutex_lock(&rs->buffer_mutex);
    uint32_t count = (rs->rx_buffer_head >= rs->rx_buffer_tail)
                         ? rs->rx_buffer_head - rs->rx_buffer_tail
                         : (RADIO_SIM_RX_BUFFER_SIZE - rs->rx_buffer_tail)
                           + rs->rx_buffer_head;
    pthread_mutex_unlock(&rs->buffer_mutex);

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32((uint32_t)sizeof(payload), count);

    g_state_under_test = NULL;
    g_iface->destroy(state);
}

static void test_udp_thread_writes_to_buffer_in_rx_mode(void)
{
    /* config.Mode == RADIO_MODE_RX short-circuits the udp thread's
     * `mode == RX || mode == DUPLEX` OR to true without evaluating the
     * DUPLEX comparison -- covers that branch outcome. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_state_under_test = state;
    power_on(state);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    pthread_mutex_lock(&rs->buffer_mutex);
    rs->config.Mode = RADIO_MODE_RX;
    pthread_mutex_unlock(&rs->buffer_mutex);

    static const uint8_t payload[16] = {0};
    TEST_ASSERT_TRUE(inject_udp(payload, sizeof(payload)) > 0);
    usleep(50000);

    pthread_mutex_lock(&rs->buffer_mutex);
    uint32_t count = rs->rx_buffer_head - rs->rx_buffer_tail;
    pthread_mutex_unlock(&rs->buffer_mutex);

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32((uint32_t)sizeof(payload), count);

    g_state_under_test = NULL;
    g_iface->destroy(state);
}

static void test_udp_thread_drops_data_in_tx_mode(void)
{
    /* Powered on but in TX-only mode: neither RX nor DUPLEX, so the udp
     * thread's OR condition is fully false and the data is dropped. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_state_under_test = state;
    power_on(state);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    pthread_mutex_lock(&rs->buffer_mutex);
    rs->config.Mode = RADIO_MODE_TX;
    pthread_mutex_unlock(&rs->buffer_mutex);

    static const uint8_t payload[16] = {0};
    TEST_ASSERT_TRUE(inject_udp(payload, sizeof(payload)) > 0);
    usleep(50000);

    pthread_mutex_lock(&rs->buffer_mutex);
    uint32_t count = rs->rx_buffer_head - rs->rx_buffer_tail;
    pthread_mutex_unlock(&rs->buffer_mutex);

    TEST_ASSERT_EQUAL_UINT32(0, count);

    g_state_under_test = NULL;
    g_iface->destroy(state);
}

static void test_spi_receive_cmd_with_udp_data(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_state_under_test = state;
    power_on(state);

    static const uint8_t ground_data[8] = {0x01, 0x02, 0x03, 0x04,
                                             0x05, 0x06, 0x07, 0x08};
    TEST_ASSERT_TRUE(inject_udp(ground_data, sizeof(ground_data)) > 0);
    usleep(50000);  /* let UDP thread place bytes into the circular buffer */

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    uint16_t requested  = (uint16_t)sizeof(ground_data);
    uint8_t  pl[2]      = {(uint8_t)(requested >> 8), (uint8_t)(requested & 0xFFu)};
    uint8_t  frame[7];
    encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_RECEIVE_CMD, pl, 2);
    simulith_transport_send(&spi, frame, sizeof(frame));
    usleep(2000);
    g_iface->on_tick(state, 200000000ULL, NULL);

    /* Response: [HDR len_hi len_lo data[0..7] TRAILER] = requested+4 bytes */
    uint8_t resp[32];
    size_t  n = drain_all(&spi, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t((size_t)(requested + 4u), n);
    TEST_ASSERT_EQUAL_HEX8(RADIO_DEVICE_HDR,                   resp[0]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(requested >> 8),          resp[1]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(requested & 0xFFu),       resp[2]);
    TEST_ASSERT_EQUAL_MEMORY(ground_data, &resp[3], sizeof(ground_data));
    TEST_ASSERT_EQUAL_HEX8(RADIO_DEVICE_TRAILER, resp[3 + requested]);

    simulith_transport_close(&spi);
    g_state_under_test = NULL;
    g_iface->destroy(state);
}

static void test_spi_receive_cmd_leaves_unrequested_udp_data_buffered(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_state_under_test = state;
    power_on(state);

    static const uint8_t ground_data[8] = {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    TEST_ASSERT_TRUE(inject_udp(ground_data, sizeof(ground_data)) > 0);
    usleep(50000);

    transport_port_t spi;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_spi_client(&spi, "test_spi_partial"));
    usleep(2000);

    const uint16_t requested = 4;
    uint8_t payload[2] = {0, (uint8_t)requested};
    uint8_t frame[7];
    encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_RECEIVE_CMD, payload, 2);
    simulith_transport_send(&spi, frame, sizeof(frame));
    usleep(2000);
    g_iface->on_tick(state, 200000000ULL, NULL);

    uint8_t response[16];
    TEST_ASSERT_EQUAL_size_t(requested + 4u,
                             drain_all(&spi, response, sizeof(response)));
    TEST_ASSERT_EQUAL_MEMORY(ground_data, &response[3], requested);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    pthread_mutex_lock(&rs->buffer_mutex);
    uint32_t remaining = (rs->rx_buffer_head >= rs->rx_buffer_tail)
                             ? rs->rx_buffer_head - rs->rx_buffer_tail
                             : (RADIO_SIM_RX_BUFFER_SIZE - rs->rx_buffer_tail) +
                                   rs->rx_buffer_head;
    pthread_mutex_unlock(&rs->buffer_mutex);
    TEST_ASSERT_EQUAL_UINT32(sizeof(ground_data) - requested, remaining);

    simulith_transport_close(&spi);
    g_state_under_test = NULL;
    g_iface->destroy(state);
}

/* -------------------------------------------------------------------------
 * Init failure path
 * -------------------------------------------------------------------------*/
static void test_init_fails_when_spi_address_is_dir(void)
{
    /* Pre-create a directory at the SPI bind path so ZMQ bind() returns
     * EADDRINUSE, covering the init-error branch in radio_sim_component_init. */
    char path[256];
    snprintf(path, sizeof(path), "/tmp/simulith_pub:%d", RADIO_SPI_PORT);
    (void)unlink(path);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mkdir(path, 0755), "could not stage path squat");

    component_state_t *state = NULL;
    int                rc    = g_iface->create(&state);

    (void)rmdir(path);

    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, rc);
}

static void test_init_fails_when_power_gpio_address_is_dir(void)
{
    /* SPI binds successfully, then power GPIO fails → covers the cleanup path at
     * radio_sim_init lines that close SPI and destroy the mutex on GPIO error. */
    char path[256];
    snprintf(path, sizeof(path), "/tmp/simulith_pub:%d", RADIO_POWER_PORT);
    (void)unlink(path);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mkdir(path, 0755), "could not stage path squat");

    component_state_t *state = NULL;
    int rc = g_iface->create(&state);

    (void)rmdir(path);

    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, rc);
    TEST_ASSERT_NULL(state);
}

static void test_init_fails_when_interrupt_gpio_address_is_dir(void)
{
    /* SPI and power GPIO bind, interrupt GPIO fails → covers the third cleanup path. */
    char path[256];
    snprintf(path, sizeof(path), "/tmp/simulith_pub:%d", RADIO_INT_PORT);
    (void)unlink(path);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mkdir(path, 0755), "could not stage path squat");

    component_state_t *state = NULL;
    int rc = g_iface->create(&state);

    (void)rmdir(path);

    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, rc);
    TEST_ASSERT_NULL(state);
}

/* -------------------------------------------------------------------------
 * UDP thread exits cleanly on select() error
 * -------------------------------------------------------------------------*/
static void test_udp_select_error_stops_thread(void)
{
    /* Close udp_rx_socket while the UDP thread is blocked in select().
     * After the current 100 ms timeout the thread re-enters the while loop,
     * calls select() on the now-invalid FD, receives EBADF, and breaks —
     * covering the error branch at radio_sim.c lines 68-69. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_state_under_test = state;

    radio_sim_state_t *rs = (radio_sim_state_t *)state;

    /* Let the UDP thread enter its first select() call */
    usleep(20000);

    /* Invalidate the socket FD — keep the stale fd number in rs->udp_rx_socket
     * so the thread calls select() on an invalid fd and gets EBADF. */
    int stale_fd = rs->udp_rx_socket;
    close(stale_fd);

    /* Wait for current 100 ms select() to timeout, then for the thread to
     * re-enter and fail with EBADF and exit via break. */
    usleep(250000);

    /* Synchronize with the exited thread before cleanup.
     * cleanup will see udp_thread_running==1, join (thread already done),
     * then skip close because we set udp_rx_socket=-1. */
    pthread_join(rs->udp_thread, NULL);
    rs->udp_thread_running = 0;  /* prevent cleanup from joining again (double-join is UB) */
    rs->udp_rx_socket      = -1; /* prevent double-close in cleanup */

    g_state_under_test = NULL;
    g_iface->destroy(state);
}

/* -------------------------------------------------------------------------
 * Symbol alias
 * -------------------------------------------------------------------------*/
static void test_register_component_alias(void)
{
    /* Verify the REGISTER_COMPONENT expansion is exported and callable. */
    dlerror();
    typedef const component_interface_t *(*get_iface_fn)(void);
    get_iface_fn fn  = (get_iface_fn)dlsym(g_handle, "get_radio_sim_component_interface");
    const char  *err = dlerror();
    TEST_ASSERT_NULL_MESSAGE((void *)err, err ? err : "");
    TEST_ASSERT_NOT_NULL((void *)fn);

    /* Call the function — covers its body (REGISTER_COMPONENT lines) */
    const component_interface_t *iface = fn();
    TEST_ASSERT_NOT_NULL(iface);
    TEST_ASSERT_EQUAL_STRING("radio_sim", iface->name);
}

/* -------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------*/
static void test_power_callback_contract_and_off_prepare(void)
{
    simulith_power_load_config_t cfg = {0};
    simulith_power_snapshot_t snapshot;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, g_iface->power_configure(NULL, &cfg));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, g_iface->power_reset(NULL, 0));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, g_iface->power_set(NULL, 1, 0));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, g_iface->power_snapshot(NULL, 0, &snapshot));
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, g_iface->power_configure(state, NULL));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, g_iface->power_snapshot(state, 0, NULL));
    cfg.boot_delay_s = 1; cfg.boot_w = 4; cfg.mode_w[0] = 2; cfg.scale = 1;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->power_configure(state, &cfg));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, g_iface->power_set(state, -1, 0));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, g_iface->power_set(state, 2, 0));
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_prepare_tick(state, 0, NULL));
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->power_snapshot(state, 0, &snapshot));
    TEST_ASSERT_FALSE(snapshot.supplied);
    TEST_ASSERT_FALSE(snapshot.ready);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.cycles);
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->power_reset(state, 100));
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->power_snapshot(state, 100, &snapshot));
    TEST_ASSERT_FALSE(snapshot.supplied);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.cycles);
    g_iface->destroy(state);
}

static void test_eps_power_cycle_boot_and_mode_demand(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    radio_sim_state_t *s = (radio_sim_state_t *)state;
    simulith_power_load_config_t cfg = {0};
    cfg.scale = 0.5; cfg.boot_delay_s = 2; cfg.boot_w = 10;
    for (unsigned int m = 0; m < SIMULITH_POWER_MODES; m++) cfg.mode_w[m] = 1 + m;
    TEST_ASSERT_EQUAL_INT(0, g_iface->power_configure(state, &cfg));
    TEST_ASSERT_EQUAL_INT(0, g_iface->power_set(state, 1, 1000000000ULL));
    simulith_power_snapshot_t snap;
    TEST_ASSERT_EQUAL_INT(0, g_iface->power_snapshot(state, 2000000000ULL, &snap));
    TEST_ASSERT_EQUAL_UINT8(0, snap.ready);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 5, snap.watts);
    TEST_ASSERT_EQUAL_INT(0, g_iface->power_set(state, 1, 2000000000ULL));
    TEST_ASSERT_EQUAL_UINT64(1, s->power.cycles);
    for (int mode = 0; mode < 4; mode++) {
        s->config.Mode = (uint8_t)mode;
        TEST_ASSERT_EQUAL_INT(0, g_iface->power_snapshot(state, 3000000000ULL, &snap));
        TEST_ASSERT_EQUAL_UINT8(1, snap.ready);
        TEST_ASSERT_FLOAT_WITHIN(1e-6, (1 + mode) * 0.5, snap.watts);
    }
    TEST_ASSERT_EQUAL_INT(0, g_iface->power_set(state, 0, 3000000000ULL));
    TEST_ASSERT_EQUAL_INT(0, g_iface->power_snapshot(state, 4000000000ULL, &snap));
    TEST_ASSERT_EQUAL_UINT8(0, snap.ready);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 0, snap.watts);
    TEST_ASSERT_EQUAL_UINT32(0, s->rx_buffer_head);
    TEST_ASSERT_EQUAL_INT(0, g_iface->power_set(state, 1, 4000000000ULL));
    TEST_ASSERT_EQUAL_UINT64(2, s->power.cycles);
    g_iface->destroy(state);
}

static void test_rf_input_requires_eps_supply_and_completed_boot(void)
{
    component_state_t *state=NULL;
    TEST_ASSERT_EQUAL_INT(0,g_iface->create(&state));
    g_state_under_test=state;
    radio_sim_state_t *s=(radio_sim_state_t *)state;
    simulith_power_load_config_t cfg={.scale=1,.boot_delay_s=1};
    TEST_ASSERT_EQUAL_INT(0,g_iface->power_configure(state,&cfg));
    TEST_ASSERT_EQUAL_INT(0,g_iface->power_set(state,1,0));
    const uint8_t payload[]={1,2,3,4};
    TEST_ASSERT_EQUAL_INT(sizeof(payload),inject_udp(payload,sizeof(payload)));
    int queued=0;
    for (unsigned int i=0; i<200; i++) {
        uint8_t peek[8];
        pthread_mutex_lock(&s->buffer_mutex);
        queued=(int)recv(s->udp_rx_socket,peek,sizeof(peek),MSG_PEEK|MSG_DONTWAIT);
        pthread_mutex_unlock(&s->buffer_mutex);
        if (queued<0) break;
        usleep(1000);
    }
    TEST_ASSERT_LESS_THAN_INT(0,queued);
    simulith_power_snapshot_t snapshot;
    TEST_ASSERT_EQUAL_INT(0,g_iface->power_snapshot(state,0,&snapshot));
    TEST_ASSERT_FALSE(snapshot.ready);
    TEST_ASSERT_EQUAL_UINT64(0,snapshot.rf_received);
    TEST_ASSERT_EQUAL_INT(0,g_iface->power_snapshot(state,1000000000ULL,&snapshot));
    TEST_ASSERT_TRUE(snapshot.ready);
    TEST_ASSERT_EQUAL_INT(sizeof(payload),inject_udp(payload,sizeof(payload)));
    for (unsigned int i=0; i<200; i++) {
        TEST_ASSERT_EQUAL_INT(0,g_iface->power_snapshot(state,1000000000ULL,&snapshot));
        if (snapshot.rf_received==sizeof(payload)) break;
        usleep(1000);
    }
    TEST_ASSERT_EQUAL_UINT64(sizeof(payload),snapshot.rf_received);
    TEST_ASSERT_EQUAL_INT(0,g_iface->power_set(state,0,2000000000ULL));
    TEST_ASSERT_EQUAL_INT(sizeof(payload),inject_udp(payload,sizeof(payload)));
    usleep(50000);
    TEST_ASSERT_EQUAL_INT(0,g_iface->power_snapshot(state,2000000000ULL,&snapshot));
    TEST_ASSERT_FALSE(snapshot.ready);
    TEST_ASSERT_EQUAL_UINT64(sizeof(payload),snapshot.rf_received);
    g_state_under_test=NULL;
    g_iface->destroy(state);
}

static void test_eps_power_cycle_discards_queued_udp_datagrams(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    radio_sim_state_t *s = (radio_sim_state_t *)state;
    /* Stop only the receiver so input remains queued at the power boundary. */
    pthread_mutex_lock(&s->buffer_mutex);
    s->udp_thread_running = 0;
    pthread_mutex_unlock(&s->buffer_mutex);
    pthread_join(s->udp_thread, NULL);
    simulith_power_load_config_t cfg = {0};
    cfg.scale = 1;
    TEST_ASSERT_EQUAL_INT(0, g_iface->power_configure(state, &cfg));
    TEST_ASSERT_EQUAL_INT(0, g_iface->power_set(state, 1, 0));
    const uint8_t payload[] = {1,2,3,4};
    TEST_ASSERT_EQUAL_INT(sizeof(payload), inject_udp(payload, sizeof(payload)));
    uint8_t received[8];
    TEST_ASSERT_EQUAL_INT(sizeof(payload), recv(s->udp_rx_socket, received, sizeof(received), MSG_PEEK|MSG_DONTWAIT));
    TEST_ASSERT_EQUAL_INT(0, g_iface->power_set(state, 0, 1000000000ULL));
    TEST_ASSERT_EQUAL_INT(0, g_iface->power_set(state, 1, 1000000000ULL));
    int stale_bytes = recv(s->udp_rx_socket, received, sizeof(received), MSG_DONTWAIT);
    TEST_ASSERT_EQUAL_UINT64(2, s->power.cycles);
    /* The endpoint remains usable for fresh input after recovery. */
    TEST_ASSERT_EQUAL_INT(sizeof(payload), inject_udp(payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_INT(sizeof(payload), recv(s->udp_rx_socket, received, sizeof(received), MSG_DONTWAIT));
    g_iface->destroy(state);
    TEST_ASSERT_EQUAL_INT(-1, stale_bytes);
}

/* -------------------------------------------------------------------------
 * RF link model: backdoor, visibility, delay, and loss
 * -------------------------------------------------------------------------*/
#define LINK_ALWAYS_MODE 1
#define LINK_NEVER_MODE  2

static void be32(uint8_t *out, uint32_t v)
{
    out[0] = (uint8_t)(v >> 24); out[1] = (uint8_t)(v >> 16);
    out[2] = (uint8_t)(v >> 8);  out[3] = (uint8_t)v;
}

/* Bind the ground end of the downlink (where the radio sends its TX packets). */
static void open_ground_listener(void)
{
    g_ground_sock = socket(AF_INET, SOCK_DGRAM, 0);
    TEST_ASSERT_TRUE(g_ground_sock >= 0);
    int one = 1;
    setsockopt(g_ground_sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct timeval tv = {0, 100000};
    setsockopt(g_ground_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(RADIO_CFG_UDP_GROUND_TX_PORT);
    TEST_ASSERT_EQUAL_INT(0, bind(g_ground_sock, (struct sockaddr *)&addr, sizeof(addr)));
}

/* Waits up to the socket timeout for one downlink packet; <= 0 if none. */
static ssize_t ground_recv(uint8_t *buf, size_t cap)
{
    return recv(g_ground_sock, buf, cap, 0);
}

static radio_sim_state_t *start_radio(transport_port_t *spi)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->create(&state));
    g_state_under_test = state;
    power_on(state);
    open_spi_client(spi, "test_spi");
    open_ground_listener();
    usleep(2000);
    return (radio_sim_state_t *)state;
}

static void finish_radio(transport_port_t *spi)
{
    simulith_transport_close(spi);
    component_state_t *state = g_state_under_test;
    g_state_under_test = NULL;
    g_iface->destroy(state);
}

/* Host -> radio SEND_CMD, serviced by a tick at tick_ns. */
static void host_send(radio_sim_state_t *rs, transport_port_t *spi, uint64_t tick_ns,
                      const uint8_t *data, uint16_t len)
{
    uint8_t frame[5 + 2048];
    size_t n = encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_SEND_CMD, data, len);
    simulith_transport_send(spi, frame, n);
    usleep(2000);
    g_iface->on_tick((component_state_t *)rs, tick_ns, NULL);
}

static uint32_t rx_count(radio_sim_state_t *rs)
{
    pthread_mutex_lock(&rs->buffer_mutex);
    uint32_t n = rs->rx_buffer_head - rs->rx_buffer_tail;
    pthread_mutex_unlock(&rs->buffer_mutex);
    return n;
}

static void backdoor(radio_sim_state_t *rs, uint16_t id, const uint8_t *p, uint16_t n)
{
    g_iface->backdoor((component_state_t *)rs, id, p, n);
}

static void bd_mode(radio_sim_state_t *rs, uint8_t mode) { backdoor(rs, RADIO_BD_SET_LINK_MODE, &mode, 1); }

static void bd_u8_u32(radio_sim_state_t *rs, uint16_t id, uint8_t dir, uint32_t value)
{
    uint8_t p[5] = {dir};
    be32(p + 1, value);
    backdoor(rs, id, p, sizeof(p));
}

/* Spacecraft over lon 0 / lat 0, Earth-fixed frame = inertial frame. */
static simulith_42_context_t over_station_context(int flipped)
{
    simulith_42_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.valid = 1;
    ctx.pos_n[0] = 6378145.0 + 500000.0;
    ctx.cwn[0][0] = flipped ? -1.0 : 1.0;
    ctx.cwn[1][1] = flipped ? -1.0 : 1.0;
    ctx.cwn[2][2] = 1.0;
    return ctx;
}

/* One station at lat/lon 0 with a 5 degree mask, regardless of generated config. */
static void use_single_station(radio_sim_state_t *rs)
{
    pthread_mutex_lock(&rs->buffer_mutex);
    rs->link.num_stations = 1;
    rs->link.stations[0].lat_deg = 0.0;
    rs->link.stations[0].lon_deg = 0.0;
    rs->link.stations[0].min_elev_deg = 5.0;
    rs->link.stations[0].enabled = 1;
    pthread_mutex_unlock(&rs->buffer_mutex);
}

static void test_link_defaults_leave_both_directions_transparent(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);

    uint8_t data[3] = {0xA1, 0xA2, 0xA3}, got[16];
    host_send(rs, &spi, 0ULL, data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(3, (int)ground_recv(got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, got, 3);
    TEST_ASSERT_EQUAL_UINT32(3, rs->hk.BytesSent);

    uint8_t up[4] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(inject_udp(up, sizeof(up)) > 0);
    usleep(50000);
    TEST_ASSERT_EQUAL_UINT32(4, rx_count(rs));

    finish_radio(&spi);
}

static void test_backdoor_commands_validate_and_apply(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    radio_link_t *l = &rs->link;

    bd_mode(rs, LINK_NEVER_MODE);
    TEST_ASSERT_EQUAL_INT(RADIO_LINK_MODE_NEVER, l->mode);
    TEST_ASSERT_EQUAL_UINT8(0, l->up);
    TEST_ASSERT_EQUAL_UINT8(0, rs->hk.GroundLock); /* visible without waiting for a tick */
    bd_mode(rs, LINK_ALWAYS_MODE);
    TEST_ASSERT_EQUAL_UINT8(1, l->up);
    TEST_ASSERT_EQUAL_UINT8(1, rs->hk.GroundLock);
    bd_mode(rs, 3); /* out of range */
    TEST_ASSERT_EQUAL_INT(RADIO_LINK_MODE_ALWAYS, l->mode);
    backdoor(rs, RADIO_BD_SET_LINK_MODE, NULL, 0);
    uint8_t two[2] = {0, 0};
    backdoor(rs, RADIO_BD_SET_LINK_MODE, two, 2);

    bd_u8_u32(rs, RADIO_BD_SET_DELAY, RADIO_LINK_UP, 1500);
    TEST_ASSERT_EQUAL_UINT64(1500000000ULL, l->delay_ns[RADIO_LINK_UP]);
    TEST_ASSERT_EQUAL_UINT64(0, l->delay_ns[RADIO_LINK_DOWN]);
    bd_u8_u32(rs, RADIO_BD_SET_DELAY, RADIO_LINK_DIR_BOTH, 20);
    TEST_ASSERT_EQUAL_UINT64(20000000ULL, l->delay_ns[RADIO_LINK_UP]);
    TEST_ASSERT_EQUAL_UINT64(20000000ULL, l->delay_ns[RADIO_LINK_DOWN]);
    bd_u8_u32(rs, RADIO_BD_SET_DELAY, 3, 99); /* bad direction */
    TEST_ASSERT_EQUAL_UINT64(20000000ULL, l->delay_ns[RADIO_LINK_UP]);

    bd_u8_u32(rs, RADIO_BD_SET_DROP_RATE, RADIO_LINK_DOWN, 250000);
    TEST_ASSERT_EQUAL_UINT32(250000, l->drop_ppm[RADIO_LINK_DOWN]);
    TEST_ASSERT_EQUAL_UINT32(0, l->drop_ppm[RADIO_LINK_UP]);
    bd_u8_u32(rs, RADIO_BD_SET_DROP_RATE, RADIO_LINK_DIR_BOTH, 1000001); /* > 100 % */
    TEST_ASSERT_EQUAL_UINT32(250000, l->drop_ppm[RADIO_LINK_DOWN]);
    backdoor(rs, RADIO_BD_SET_DROP_RATE, two, 2);

    uint8_t off = 0, bad = 2;
    backdoor(rs, RADIO_BD_SET_IMPAIRMENTS_ENABLED, &off, 1);
    TEST_ASSERT_EQUAL_UINT8(0, l->impairments_enabled);
    backdoor(rs, RADIO_BD_SET_IMPAIRMENTS_ENABLED, &bad, 1);
    TEST_ASSERT_EQUAL_UINT8(0, l->impairments_enabled);

    uint8_t seed[4];
    be32(seed, 0xCAFEF00DU);
    backdoor(rs, RADIO_BD_SET_SEED, seed, 4);
    TEST_ASSERT_EQUAL_UINT32(0xCAFEF00DU, l->prng);
    backdoor(rs, RADIO_BD_SET_SEED, seed, 3);

    use_single_station(rs);
    uint8_t st[2] = {0, 0};
    backdoor(rs, RADIO_BD_SET_STATION_ENABLED, st, 2);
    TEST_ASSERT_EQUAL_UINT8(0, l->stations[0].enabled);
    st[0] = 1; st[1] = 1; /* no station 1 */
    backdoor(rs, RADIO_BD_SET_STATION_ENABLED, st, 2);
    TEST_ASSERT_EQUAL_UINT8(0, l->stations[0].enabled);
    st[0] = RADIO_BD_ALL_STATIONS;
    backdoor(rs, RADIO_BD_SET_STATION_ENABLED, st, 2);
    TEST_ASSERT_EQUAL_UINT8(1, l->stations[0].enabled);
    st[1] = 2;
    backdoor(rs, RADIO_BD_SET_STATION_ENABLED, st, 2);
    TEST_ASSERT_EQUAL_UINT8(1, l->stations[0].enabled);

    backdoor(rs, 99, NULL, 0); /* unknown command */
    backdoor(rs, RADIO_BD_RESET_LINK, two, 2); /* takes no payload */
    TEST_ASSERT_EQUAL_UINT64(20000000ULL, l->delay_ns[RADIO_LINK_UP]); /* so nothing was reset */

    TEST_ASSERT_EQUAL_UINT32(9, rs->backdoor_accepted);
    TEST_ASSERT_EQUAL_UINT32(12, rs->backdoor_rejected);

    finish_radio(&spi);
}

static void test_backdoor_reset_restores_configured_defaults(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    radio_link_t *l = &rs->link;

    bd_mode(rs, LINK_NEVER_MODE);
    bd_u8_u32(rs, RADIO_BD_SET_DELAY, RADIO_LINK_DIR_BOTH, 777);
    bd_u8_u32(rs, RADIO_BD_SET_DROP_RATE, RADIO_LINK_DIR_BOTH, 123456);
    backdoor(rs, RADIO_BD_RESET_LINK, NULL, 0);

    TEST_ASSERT_EQUAL_INT(RADIO_CFG_LINK_MODE, l->mode);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)RADIO_CFG_LINK_UPLINK_DELAY_MS * 1000000ULL, l->delay_ns[RADIO_LINK_UP]);
    TEST_ASSERT_EQUAL_UINT32(RADIO_CFG_LINK_DOWNLINK_DROP_PPM, l->drop_ppm[RADIO_LINK_DOWN]);
    TEST_ASSERT_EQUAL_UINT8(rs->link.up, rs->hk.GroundLock);

    finish_radio(&spi);
}

static void test_downlink_discarded_when_never_in_view(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    bd_mode(rs, LINK_NEVER_MODE);

    uint8_t data[3] = {1, 2, 3}, got[16];
    host_send(rs, &spi, 0ULL, data, sizeof(data));

    TEST_ASSERT_TRUE(ground_recv(got, sizeof(got)) <= 0);
    TEST_ASSERT_EQUAL_UINT32(1, rs->link.out_of_view[RADIO_LINK_DOWN]);
    TEST_ASSERT_EQUAL_UINT32(0, rs->hk.BytesSent);
    TEST_ASSERT_EQUAL_UINT16(1, rs->hk.CommandCounter); /* the host's transmit still succeeded */

    bd_mode(rs, LINK_ALWAYS_MODE);
    host_send(rs, &spi, 100000000ULL, data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(3, (int)ground_recv(got, sizeof(got)));

    finish_radio(&spi);
}

static void test_downlink_total_loss_and_impairment_bypass(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    bd_u8_u32(rs, RADIO_BD_SET_DROP_RATE, RADIO_LINK_DOWN, 1000000);

    uint8_t data[2] = {7, 8}, got[16];
    host_send(rs, &spi, 0ULL, data, sizeof(data));
    TEST_ASSERT_TRUE(ground_recv(got, sizeof(got)) <= 0);
    TEST_ASSERT_EQUAL_UINT32(1, rs->link.dropped[RADIO_LINK_DOWN]);

    uint8_t off = 0;
    backdoor(rs, RADIO_BD_SET_IMPAIRMENTS_ENABLED, &off, 1);
    host_send(rs, &spi, 100000000ULL, data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(2, (int)ground_recv(got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT32(1, rs->link.dropped[RADIO_LINK_DOWN]);

    finish_radio(&spi);
}

static void test_downlink_delay_is_held_until_sim_time_elapses(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    bd_u8_u32(rs, RADIO_BD_SET_DELAY, RADIO_LINK_DOWN, 1000);

    uint8_t data[4] = {0xDE, 0xAD, 0xBE, 0xEF}, got[16];
    host_send(rs, &spi, 0ULL, data, sizeof(data));
    TEST_ASSERT_EQUAL_UINT32(1, rs->link.queue[RADIO_LINK_DOWN].count);
    TEST_ASSERT_EQUAL_UINT32(0, rs->hk.BytesSent);
    TEST_ASSERT_TRUE(ground_recv(got, sizeof(got)) <= 0);

    g_iface->on_tick((component_state_t *)rs, 999000000ULL, NULL);
    TEST_ASSERT_TRUE(ground_recv(got, sizeof(got)) <= 0);

    g_iface->on_tick((component_state_t *)rs, 1000000000ULL, NULL);
    TEST_ASSERT_EQUAL_INT(4, (int)ground_recv(got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, got, 4);
    TEST_ASSERT_EQUAL_UINT32(4, rs->hk.BytesSent);
    TEST_ASSERT_EQUAL_UINT32(0, rs->link.queue[RADIO_LINK_DOWN].count);

    finish_radio(&spi);
}

static void test_downlink_delay_keeps_packet_order(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    bd_u8_u32(rs, RADIO_BD_SET_DELAY, RADIO_LINK_DOWN, 500);

    uint8_t first = 0x11, second = 0x22, got[16];
    host_send(rs, &spi, 0ULL, &first, 1);                /* due at 500 ms */
    host_send(rs, &spi, 100000000ULL, &second, 1);       /* due at 600 ms */

    g_iface->on_tick((component_state_t *)rs, 500000000ULL, NULL);
    TEST_ASSERT_EQUAL_INT(1, (int)ground_recv(got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8(0x11, got[0]);
    TEST_ASSERT_TRUE(ground_recv(got, sizeof(got)) <= 0);

    g_iface->on_tick((component_state_t *)rs, 600000000ULL, NULL);
    TEST_ASSERT_EQUAL_INT(1, (int)ground_recv(got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8(0x22, got[0]);

    finish_radio(&spi);
}

static void bd_corrupt(radio_sim_state_t *rs, uint8_t dir, uint16_t count, uint16_t offset)
{
    uint8_t p[5] = {dir, (uint8_t)(count >> 8), (uint8_t)count, (uint8_t)(offset >> 8), (uint8_t)offset};
    backdoor(rs, RADIO_BD_CORRUPT_PACKETS, p, sizeof(p));
}

static void test_delayed_downlink_carries_a_full_telemetry_frame(void)
{
    /* A TM CADU (ASM + 1786 byte frame) crosses as one packet, larger than a command payload. */
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    bd_u8_u32(rs, RADIO_BD_SET_DELAY, RADIO_LINK_DOWN, 100);

    uint8_t frame[1790], got[2048];
    for (size_t i = 0; i < sizeof(frame); i++) frame[i] = (uint8_t)(i * 7u);
    host_send(rs, &spi, 0ULL, frame, sizeof(frame));
    g_iface->on_tick((component_state_t *)rs, 100000000ULL, NULL);

    TEST_ASSERT_EQUAL_INT((int)sizeof(frame), (int)ground_recv(got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame, got, sizeof(frame));

    finish_radio(&spi);
}

static void test_corrupt_downlink_flips_a_bit_in_the_next_packets_only(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    bd_corrupt(rs, RADIO_LINK_DOWN, 1, 1);

    uint8_t data[3] = {0x10, 0x20, 0x30}, got[16];
    host_send(rs, &spi, 0ULL, data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(3, (int)ground_recv(got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8(0x10, got[0]);
    TEST_ASSERT_EQUAL_UINT8(0x21, got[1]);
    TEST_ASSERT_EQUAL_UINT8(0x30, got[2]);

    host_send(rs, &spi, 100000000ULL, data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(3, (int)ground_recv(got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, got, 3);
    TEST_ASSERT_EQUAL_UINT32(1, rs->link.corrupted[RADIO_LINK_DOWN]);

    finish_radio(&spi);
}

static void test_corrupt_uplink_flips_a_bit_in_what_the_host_receives(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    bd_corrupt(rs, RADIO_LINK_UP, 1, 2);

    uint8_t up[4] = {0, 0, 0, 0};
    TEST_ASSERT_TRUE(inject_udp(up, sizeof(up)) > 0);
    usleep(50000);
    TEST_ASSERT_EQUAL_UINT32(4, rx_count(rs));
    TEST_ASSERT_EQUAL_UINT8(1, rs->rx_buffer[2]);
    TEST_ASSERT_EQUAL_UINT8(0, rs->rx_buffer[0] | rs->rx_buffer[1] | rs->rx_buffer[3]);

    finish_radio(&spi);
}

static void test_corrupt_does_not_touch_packets_that_never_cross_the_link(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    bd_mode(rs, LINK_NEVER_MODE);
    bd_corrupt(rs, RADIO_LINK_DOWN, 1, 0);

    uint8_t data[2] = {1, 2}, got[8];
    host_send(rs, &spi, 0ULL, data, sizeof(data));
    TEST_ASSERT_TRUE(ground_recv(got, sizeof(got)) <= 0);
    TEST_ASSERT_EQUAL_UINT32(0, rs->link.corrupted[RADIO_LINK_DOWN]);
    TEST_ASSERT_EQUAL_UINT16(1, rs->link.corrupt_remaining[RADIO_LINK_DOWN]);

    finish_radio(&spi);
}

static void test_replay_downlink_resends_the_last_packet_even_out_of_view(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);

    uint8_t dir = RADIO_LINK_DOWN, data[3] = {0xA0, 0xA1, 0xA2}, got[16];
    backdoor(rs, RADIO_BD_REPLAY_PACKET, &dir, 1); /* nothing has crossed yet */
    TEST_ASSERT_EQUAL_UINT32(1, rs->backdoor_rejected);

    host_send(rs, &spi, 0ULL, data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(3, (int)ground_recv(got, sizeof(got)));

    bd_mode(rs, LINK_NEVER_MODE);
    backdoor(rs, RADIO_BD_REPLAY_PACKET, &dir, 1);
    TEST_ASSERT_EQUAL_INT(3, (int)ground_recv(got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, got, 3);
    TEST_ASSERT_EQUAL_UINT32(1, rs->link.replayed[RADIO_LINK_DOWN]);

    finish_radio(&spi);
}

static void test_replay_uplink_redelivers_the_last_packet_when_the_radio_can_hear_it(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);

    uint8_t up[4] = {5, 6, 7, 8}, dir = RADIO_LINK_UP;
    TEST_ASSERT_TRUE(inject_udp(up, sizeof(up)) > 0);
    usleep(50000);
    TEST_ASSERT_EQUAL_UINT32(4, rx_count(rs));

    backdoor(rs, RADIO_BD_REPLAY_PACKET, &dir, 1);
    TEST_ASSERT_EQUAL_UINT32(8, rx_count(rs));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(up, &rs->rx_buffer[4], 4);

    pthread_mutex_lock(&rs->buffer_mutex);
    rs->config.Mode = RADIO_MODE_TX;
    pthread_mutex_unlock(&rs->buffer_mutex);
    backdoor(rs, RADIO_BD_REPLAY_PACKET, &dir, 1);
    TEST_ASSERT_EQUAL_UINT32(8, rx_count(rs));
    TEST_ASSERT_EQUAL_UINT32(1, rs->backdoor_rejected);

    finish_radio(&spi);
}

static void test_tamper_backdoors_validate_payloads_and_reset_clears_them(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);

    uint8_t two[2] = {0, 0}, bad_dir[5] = {3, 0, 1, 0, 0}, dir = 2;
    backdoor(rs, RADIO_BD_CORRUPT_PACKETS, two, 2);
    backdoor(rs, RADIO_BD_CORRUPT_PACKETS, bad_dir, 5);
    backdoor(rs, RADIO_BD_REPLAY_PACKET, &dir, 1); /* both is not a replay direction */
    backdoor(rs, RADIO_BD_REPLAY_PACKET, two, 2);
    TEST_ASSERT_EQUAL_UINT32(0, rs->backdoor_accepted);
    TEST_ASSERT_EQUAL_UINT32(4, rs->backdoor_rejected);

    bd_corrupt(rs, RADIO_LINK_DIR_BOTH, 4, 9);
    TEST_ASSERT_EQUAL_UINT16(4, rs->link.corrupt_remaining[RADIO_LINK_UP]);
    TEST_ASSERT_EQUAL_UINT16(4, rs->link.corrupt_remaining[RADIO_LINK_DOWN]);
    TEST_ASSERT_EQUAL_UINT16(9, rs->link.corrupt_offset[RADIO_LINK_DOWN]);
    bd_corrupt(rs, RADIO_LINK_DIR_BOTH, 0, 9); /* a count of zero cancels */
    TEST_ASSERT_EQUAL_UINT16(0, rs->link.corrupt_remaining[RADIO_LINK_UP]);

    bd_corrupt(rs, RADIO_LINK_UP, 2, 0);
    backdoor(rs, RADIO_BD_RESET_LINK, NULL, 0);
    TEST_ASSERT_EQUAL_UINT16(0, rs->link.corrupt_remaining[RADIO_LINK_UP]);

    finish_radio(&spi);
}

/* Queues a packet straight onto the uplink delay queue, already due, to exercise delivery conditions. */
static void queue_due_uplink(radio_sim_state_t *rs, const uint8_t *data, uint32_t len)
{
    pthread_mutex_lock(&rs->buffer_mutex);
    TEST_ASSERT_TRUE(g_link_enqueue(&rs->link, RADIO_LINK_UP, 0, data, len));
    pthread_mutex_unlock(&rs->buffer_mutex);
}

static void set_mode(radio_sim_state_t *rs, uint8_t mode)
{
    pthread_mutex_lock(&rs->buffer_mutex);
    rs->config.Mode = mode;
    pthread_mutex_unlock(&rs->buffer_mutex);
}

static void remember_packet(radio_sim_state_t *rs, unsigned int dir, const uint8_t *data, uint32_t len)
{
    pthread_mutex_lock(&rs->buffer_mutex);
    g_link_remember(&rs->link, dir, data, len);
    pthread_mutex_unlock(&rs->buffer_mutex);
}

static void test_delayed_uplink_delivery_depends_on_power_and_mode(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    const uint8_t data[4] = {1, 2, 3, 4};

    /* Powered off when the delay elapses: lost. */
    queue_due_uplink(rs, data, sizeof(data));
    pthread_mutex_lock(&rs->buffer_mutex);
    rs->power_gpio.value = 0;
    pthread_mutex_unlock(&rs->buffer_mutex);
    g_iface->on_tick((component_state_t *)rs, 1000000ULL, NULL);
    TEST_ASSERT_EQUAL_UINT32(0, rx_count(rs));
    TEST_ASSERT_EQUAL_UINT32(0, rs->link.queue[RADIO_LINK_UP].count);

    /* Powered but still booting: lost. */
    pthread_mutex_lock(&rs->buffer_mutex);
    rs->power_gpio.value = 1;
    rs->power.managed = 1;
    rs->power.supply_on = 1;
    rs->power.enabled = 1;
    rs->power.boot_ready_ns = 5000000000ULL;
    pthread_mutex_unlock(&rs->buffer_mutex);
    queue_due_uplink(rs, data, sizeof(data));
    g_iface->on_tick((component_state_t *)rs, 2000000ULL, NULL);
    TEST_ASSERT_EQUAL_UINT32(0, rx_count(rs));

    /* Booted, and receive-only is enough to hear it. */
    pthread_mutex_lock(&rs->buffer_mutex);
    rs->power.boot_ready_ns = 0;
    pthread_mutex_unlock(&rs->buffer_mutex);
    set_mode(rs, RADIO_MODE_RX);
    queue_due_uplink(rs, data, sizeof(data));
    g_iface->on_tick((component_state_t *)rs, 3000000ULL, NULL);
    TEST_ASSERT_EQUAL_UINT32(4, rx_count(rs));

    finish_radio(&spi);
}

static void test_backdoor_rejects_malformed_payloads_for_every_command(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    radio_link_t *l = &rs->link;
    const radio_link_mode_t mode_before = l->mode;

    uint8_t five[5] = {0, 0, 0, 0, 1}, bad_dir[5] = {7, 0, 0, 0, 1}, bad_ppm[5] = {0, 0, 0xF4, 0x24, 0x1};
    uint8_t one = 0, bad_one = 9, two[2] = {0, 1}, four[4] = {0, 0, 0, 1}, three[3] = {0, 0, 0};
    unsigned int expected_rejected = 0;

    struct { uint16_t id; const uint8_t *p; uint16_t n; } cases[] = {
        {RADIO_BD_SET_LINK_MODE, two, 2},              {RADIO_BD_SET_LINK_MODE, NULL, 0},
        {RADIO_BD_SET_DELAY, four, 4},                 {RADIO_BD_SET_DELAY, bad_dir, 5},
        {RADIO_BD_SET_DROP_RATE, four, 4},             {RADIO_BD_SET_DROP_RATE, bad_dir, 5},
        {RADIO_BD_SET_DROP_RATE, bad_ppm, 5},          {RADIO_BD_SET_IMPAIRMENTS_ENABLED, two, 2},
        {RADIO_BD_SET_IMPAIRMENTS_ENABLED, &bad_one, 1}, {RADIO_BD_SET_STATION_ENABLED, &one, 1},
        {RADIO_BD_SET_STATION_ENABLED, three, 3},      {RADIO_BD_SET_SEED, three, 3},
        {RADIO_BD_SET_SEED, five, 5},                  {RADIO_BD_RESET_LINK, &one, 1},
        {RADIO_BD_CORRUPT_PACKETS, four, 4},           {RADIO_BD_CORRUPT_PACKETS, bad_dir, 5},
        {RADIO_BD_REPLAY_PACKET, two, 2},              {RADIO_BD_REPLAY_PACKET, &bad_one, 1},
        {0xFFFF, NULL, 0},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        backdoor(rs, cases[i].id, cases[i].p, cases[i].n);
        expected_rejected++;
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected_rejected, rs->backdoor_rejected, "command was not rejected");
    }
    TEST_ASSERT_EQUAL_UINT32(0, rs->backdoor_accepted);
    TEST_ASSERT_EQUAL_INT(mode_before, l->mode);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)RADIO_CFG_LINK_UPLINK_DELAY_MS * 1000000ULL, l->delay_ns[RADIO_LINK_UP]);
    TEST_ASSERT_EQUAL_UINT16(0, l->corrupt_remaining[RADIO_LINK_UP]);

    /* A payload length with no payload pointer cannot be read and is rejected too. */
    backdoor(rs, RADIO_BD_SET_LINK_MODE, NULL, 1);
    TEST_ASSERT_EQUAL_UINT32(expected_rejected + 1, rs->backdoor_rejected);

    /* A missing component state is ignored. */
    g_iface->backdoor(NULL, RADIO_BD_SET_LINK_MODE, &one, 1);

    finish_radio(&spi);
}

static void test_station_backdoor_addresses_one_station_or_all(void)
{
    if (RADIO_LINK_MAX_STATIONS < 2) TEST_IGNORE_MESSAGE("needs a build with two ground stations");

    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    pthread_mutex_lock(&rs->buffer_mutex);
    rs->link.num_stations = 2;
    for (int i = 0; i < 2; i++) rs->link.stations[i].enabled = 1;
    pthread_mutex_unlock(&rs->buffer_mutex);

    uint8_t second_off[2] = {1, 0}, first_off[2] = {0, 0}, all_on[2] = {RADIO_BD_ALL_STATIONS, 1};
    backdoor(rs, RADIO_BD_SET_STATION_ENABLED, second_off, 2);
    TEST_ASSERT_EQUAL_UINT8(1, rs->link.stations[0].enabled);
    TEST_ASSERT_EQUAL_UINT8(0, rs->link.stations[1].enabled);
    backdoor(rs, RADIO_BD_SET_STATION_ENABLED, first_off, 2);
    TEST_ASSERT_EQUAL_UINT8(0, rs->link.stations[0].enabled);
    backdoor(rs, RADIO_BD_SET_STATION_ENABLED, all_on, 2);
    TEST_ASSERT_EQUAL_UINT8(1, rs->link.stations[0].enabled);
    TEST_ASSERT_EQUAL_UINT8(1, rs->link.stations[1].enabled);

    finish_radio(&spi);
}

static void test_replay_uplink_requires_power_boot_and_a_receiving_mode(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    const uint8_t data[4] = {9, 8, 7, 6};
    uint8_t dir = RADIO_LINK_UP;
    remember_packet(rs, RADIO_LINK_UP, data, sizeof(data));

    pthread_mutex_lock(&rs->buffer_mutex);
    rs->power_gpio.value = 0;
    pthread_mutex_unlock(&rs->buffer_mutex);
    backdoor(rs, RADIO_BD_REPLAY_PACKET, &dir, 1);
    TEST_ASSERT_EQUAL_UINT32(0, rx_count(rs));

    pthread_mutex_lock(&rs->buffer_mutex);
    rs->power_gpio.value = 1;
    rs->power.managed = 1;
    rs->power.supply_on = 1;
    rs->power.enabled = 1;
    rs->power.boot_ready_ns = 5000000000ULL;
    pthread_mutex_unlock(&rs->buffer_mutex);
    backdoor(rs, RADIO_BD_REPLAY_PACKET, &dir, 1);
    TEST_ASSERT_EQUAL_UINT32(0, rx_count(rs));

    pthread_mutex_lock(&rs->buffer_mutex);
    rs->power.boot_ready_ns = 0;
    pthread_mutex_unlock(&rs->buffer_mutex);
    set_mode(rs, RADIO_MODE_RX); /* receive-only still hears the replay */
    backdoor(rs, RADIO_BD_REPLAY_PACKET, &dir, 1);
    TEST_ASSERT_EQUAL_UINT32(4, rx_count(rs));
    TEST_ASSERT_EQUAL_UINT32(1, rs->backdoor_accepted);

    finish_radio(&spi);
}

static void test_replay_downlink_requires_a_transmitting_mode(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    const uint8_t data[3] = {4, 5, 6};
    uint8_t dir = RADIO_LINK_DOWN, got[8];
    remember_packet(rs, RADIO_LINK_DOWN, data, sizeof(data));

    set_mode(rs, RADIO_MODE_RX);
    backdoor(rs, RADIO_BD_REPLAY_PACKET, &dir, 1);
    TEST_ASSERT_TRUE(ground_recv(got, sizeof(got)) <= 0);
    TEST_ASSERT_EQUAL_UINT32(1, rs->backdoor_rejected);

    set_mode(rs, RADIO_MODE_TX); /* transmit-only is enough to send it */
    backdoor(rs, RADIO_BD_REPLAY_PACKET, &dir, 1);
    TEST_ASSERT_EQUAL_INT(3, (int)ground_recv(got, sizeof(got)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, got, 3);

    finish_radio(&spi);
}

static void ignore_signal(int signal) { (void)signal; }

static void test_udp_thread_survives_interrupted_select(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = ignore_signal; /* no SA_RESTART: select() must return EINTR */
    sigemptyset(&action.sa_mask);
    struct sigaction previous;
    TEST_ASSERT_EQUAL_INT(0, sigaction(SIGUSR1, &action, &previous));
    for (int i = 0; i < 20; i++)
    {
        pthread_kill(rs->udp_thread, SIGUSR1);
        usleep(2000);
    }
    sigaction(SIGUSR1, &previous, NULL);

    /* The thread keeps serving the ground interface afterwards. */
    uint8_t up[3] = {1, 2, 3};
    TEST_ASSERT_TRUE(inject_udp(up, sizeof(up)) > 0);
    usleep(50000);
    TEST_ASSERT_EQUAL_UINT32(3, rx_count(rs));

    finish_radio(&spi);
}

static void test_delayed_downlink_send_failure_is_tolerated(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    bd_u8_u32(rs, RADIO_BD_SET_DELAY, RADIO_LINK_DOWN, 100);

    uint8_t data = 0x55;
    host_send(rs, &spi, 0ULL, &data, 1);
    TEST_ASSERT_EQUAL_UINT32(1, rs->link.queue[RADIO_LINK_DOWN].count);

    close(rs->udp_tx_socket); /* the transmit will fail when the delay elapses */
    rs->udp_tx_socket = -1;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS,
                          g_iface->on_tick((component_state_t *)rs, 200000000ULL, NULL));
    TEST_ASSERT_EQUAL_UINT32(0, rs->link.queue[RADIO_LINK_DOWN].count);
    TEST_ASSERT_EQUAL_UINT32(0, rs->hk.BytesSent);

    finish_radio(&spi);
}

static void test_downlink_delay_queue_overflow_drops_and_counts(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    bd_u8_u32(rs, RADIO_BD_SET_DELAY, RADIO_LINK_DOWN, 60000);

    uint8_t data = 0x66;
    for (int i = 0; i < RADIO_CFG_LINK_QUEUE_DEPTH + 2; i++)
        host_send(rs, &spi, (uint64_t)i * 1000000ULL, &data, 1);

    TEST_ASSERT_EQUAL_UINT32(RADIO_CFG_LINK_QUEUE_DEPTH, rs->link.queue[RADIO_LINK_DOWN].count);
    TEST_ASSERT_EQUAL_UINT32(2, rs->link.overflowed[RADIO_LINK_DOWN]);

    finish_radio(&spi);
}

static void test_uplink_discarded_when_never_in_view(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    bd_mode(rs, LINK_NEVER_MODE);

    uint8_t up[5] = {1, 2, 3, 4, 5};
    TEST_ASSERT_TRUE(inject_udp(up, sizeof(up)) > 0);
    usleep(50000);
    TEST_ASSERT_EQUAL_UINT32(0, rx_count(rs));
    TEST_ASSERT_EQUAL_UINT32(1, rs->link.out_of_view[RADIO_LINK_UP]);

    bd_mode(rs, LINK_ALWAYS_MODE);
    TEST_ASSERT_TRUE(inject_udp(up, sizeof(up)) > 0);
    usleep(50000);
    TEST_ASSERT_EQUAL_UINT32(5, rx_count(rs));

    finish_radio(&spi);
}

static void test_uplink_total_loss(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    bd_u8_u32(rs, RADIO_BD_SET_DROP_RATE, RADIO_LINK_UP, 1000000);

    uint8_t up[5] = {1, 2, 3, 4, 5};
    TEST_ASSERT_TRUE(inject_udp(up, sizeof(up)) > 0);
    usleep(50000);
    TEST_ASSERT_EQUAL_UINT32(0, rx_count(rs));
    TEST_ASSERT_EQUAL_UINT32(1, rs->link.dropped[RADIO_LINK_UP]);

    finish_radio(&spi);
}

static void test_uplink_delay_is_held_until_sim_time_elapses(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    bd_u8_u32(rs, RADIO_BD_SET_DELAY, RADIO_LINK_UP, 500);

    uint8_t up[6] = {9, 8, 7, 6, 5, 4};
    TEST_ASSERT_TRUE(inject_udp(up, sizeof(up)) > 0);
    usleep(50000);
    TEST_ASSERT_EQUAL_UINT32(0, rx_count(rs));
    TEST_ASSERT_EQUAL_UINT32(1, rs->link.queue[RADIO_LINK_UP].count);

    g_iface->on_tick((component_state_t *)rs, 499000000ULL, NULL);
    TEST_ASSERT_EQUAL_UINT32(0, rx_count(rs));

    g_iface->on_tick((component_state_t *)rs, 500000000ULL, NULL);
    TEST_ASSERT_EQUAL_UINT32(6, rx_count(rs));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(up, rs->rx_buffer, 6);
    TEST_ASSERT_EQUAL_UINT32(6, rs->bytes_received);

    finish_radio(&spi);
}

static void test_delayed_uplink_is_lost_if_radio_leaves_rx_mode(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    bd_u8_u32(rs, RADIO_BD_SET_DELAY, RADIO_LINK_UP, 100);

    uint8_t up[2] = {1, 2};
    TEST_ASSERT_TRUE(inject_udp(up, sizeof(up)) > 0);
    usleep(50000);
    pthread_mutex_lock(&rs->buffer_mutex);
    rs->config.Mode = RADIO_MODE_TX;
    pthread_mutex_unlock(&rs->buffer_mutex);

    g_iface->on_tick((component_state_t *)rs, 200000000ULL, NULL);
    TEST_ASSERT_EQUAL_UINT32(0, rx_count(rs));
    TEST_ASSERT_EQUAL_UINT32(0, rs->link.queue[RADIO_LINK_UP].count);

    finish_radio(&spi);
}

static void test_power_cycle_discards_packets_in_flight_but_keeps_link_settings(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    bd_u8_u32(rs, RADIO_BD_SET_DELAY, RADIO_LINK_DIR_BOTH, 5000);

    uint8_t data = 0x42;
    host_send(rs, &spi, 0ULL, &data, 1);
    TEST_ASSERT_TRUE(inject_udp(&data, 1) > 0);
    usleep(50000);
    TEST_ASSERT_EQUAL_UINT32(1, rs->link.queue[RADIO_LINK_DOWN].count);
    TEST_ASSERT_EQUAL_UINT32(1, rs->link.queue[RADIO_LINK_UP].count);

    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->power_reset((component_state_t *)rs, 1000000000ULL));
    TEST_ASSERT_EQUAL_UINT32(0, rs->link.queue[RADIO_LINK_DOWN].count);
    TEST_ASSERT_EQUAL_UINT32(0, rs->link.queue[RADIO_LINK_UP].count);
    TEST_ASSERT_EQUAL_UINT64(5000000000ULL, rs->link.delay_ns[RADIO_LINK_UP]);

    finish_radio(&spi);
}

static void test_auto_mode_follows_spacecraft_geometry(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    use_single_station(rs);
    uint8_t auto_mode = 0;
    backdoor(rs, RADIO_BD_SET_LINK_MODE, &auto_mode, 1);

    simulith_42_context_t overhead = over_station_context(0);
    simulith_42_context_t far_side = over_station_context(1);

    g_iface->on_tick((component_state_t *)rs, 100000000ULL, &overhead);
    TEST_ASSERT_EQUAL_UINT8(1, rs->hk.GroundLock);
    TEST_ASSERT_EQUAL_UINT8(1, rs->link.up);

    g_iface->on_tick((component_state_t *)rs, 200000000ULL, &far_side);
    TEST_ASSERT_EQUAL_UINT8(0, rs->hk.GroundLock);

    /* An invalid context holds the last answer rather than guessing. */
    g_iface->on_tick((component_state_t *)rs, 300000000ULL, &overhead);
    TEST_ASSERT_EQUAL_UINT8(1, rs->hk.GroundLock);
    overhead.valid = 0;
    g_iface->on_tick((component_state_t *)rs, 400000000ULL, &overhead);
    TEST_ASSERT_EQUAL_UINT8(1, rs->hk.GroundLock);
    g_iface->on_tick((component_state_t *)rs, 500000000ULL, NULL);
    TEST_ASSERT_EQUAL_UINT8(1, rs->hk.GroundLock);

    finish_radio(&spi);
}

static void test_station_backdoor_gates_auto_visibility(void)
{
    transport_port_t spi;
    radio_sim_state_t *rs = start_radio(&spi);
    use_single_station(rs);
    uint8_t auto_mode = 0;
    backdoor(rs, RADIO_BD_SET_LINK_MODE, &auto_mode, 1);
    simulith_42_context_t overhead = over_station_context(0);

    uint8_t disable[2] = {0, 0};
    backdoor(rs, RADIO_BD_SET_STATION_ENABLED, disable, 2);
    g_iface->on_tick((component_state_t *)rs, 100000000ULL, &overhead);
    TEST_ASSERT_EQUAL_UINT8(0, rs->hk.GroundLock);

    uint8_t got[8];
    uint8_t data[2] = {3, 4};
    host_send(rs, &spi, 200000000ULL, data, sizeof(data));
    TEST_ASSERT_TRUE(ground_recv(got, sizeof(got)) <= 0);
    TEST_ASSERT_EQUAL_UINT32(1, rs->link.out_of_view[RADIO_LINK_DOWN]);

    uint8_t enable[2] = {RADIO_BD_ALL_STATIONS, 1};
    backdoor(rs, RADIO_BD_SET_STATION_ENABLED, enable, 2);
    g_iface->on_tick((component_state_t *)rs, 300000000ULL, &overhead);
    TEST_ASSERT_EQUAL_UINT8(1, rs->hk.GroundLock);
    host_send(rs, &spi, 400000000ULL, data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(2, (int)ground_recv(got, sizeof(got)));

    finish_radio(&spi);
}

int main(void)
{
    /* Bypass gethostbyname("shire-cryptolib") which blocks for DNS timeout in CI/test envs. */
    setenv("RADIO_GROUND_HOST", "127.0.0.1", 0 /* don't overwrite if caller set it */);

    g_handle = dlopen(RADIO_SIM_SO_PATH, RTLD_NOW);
    if (!g_handle)
    {
        fprintf(stderr, "Failed to dlopen %s: %s\n", RADIO_SIM_SO_PATH, dlerror());
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

    g_radio_sim_init    = (radio_init_fn)dlsym(g_handle,    "radio_sim_init");
    g_radio_sim_cleanup = (radio_cleanup_fn)dlsym(g_handle, "radio_sim_cleanup");
    *(void **)(&g_link_enqueue)  = dlsym(g_handle, "radio_link_enqueue");
    *(void **)(&g_link_remember) = dlsym(g_handle, "radio_link_remember");

    UNITY_BEGIN();

    /* Lifecycle */
    RUN_TEST(test_power_callback_contract_and_off_prepare);
    RUN_TEST(test_eps_power_cycle_boot_and_mode_demand);
    RUN_TEST(test_rf_input_requires_eps_supply_and_completed_boot);
    RUN_TEST(test_eps_power_cycle_discards_queued_udp_datagrams);
    RUN_TEST(test_dlopen_radio_sim_so);
    RUN_TEST(test_get_component_interface_symbol);
    RUN_TEST(test_init_returns_success_and_initial_state);
    RUN_TEST(test_lifecycle_callbacks_reject_null);
    RUN_TEST(test_cleanup_rebind_ok);
    RUN_TEST(test_cleanup_with_null_is_safe);

    /* Direct helpers */
    RUN_TEST(test_radio_sim_init_rejects_null);
    RUN_TEST(test_radio_sim_cleanup_with_null_is_safe);

    /* Tick edge cases */
    RUN_TEST(test_tick_with_null_state_returns_safely);
    RUN_TEST(test_tick_does_not_crash);

    /* SPI wire protocol */
    RUN_TEST(test_spi_noop_increments_command_counter);
    RUN_TEST(test_spi_req_hk_returns_framed_housekeeping);
    RUN_TEST(test_hk_initial_mode_and_counter);
    RUN_TEST(test_spi_set_cfg_updates_mode);
    RUN_TEST(test_spi_set_cfg_wrong_payload_size_ignored);
    RUN_TEST(test_spi_receive_cmd_empty_buffer_returns_frame);
    RUN_TEST(test_spi_receive_cmd_wrong_payload_rejected);
    RUN_TEST(test_spi_send_cmd_in_duplex_mode);
    RUN_TEST(test_spi_send_cmd_zero_payload_no_counter);
    RUN_TEST(test_spi_send_cmd_in_rx_mode_not_forwarded);
    RUN_TEST(test_spi_send_cmd_in_tx_mode_forwarded);
    RUN_TEST(test_spi_send_cmd_fails_on_bad_socket);
    RUN_TEST(test_spi_short_packet_rejected);
    RUN_TEST(test_spi_bad_header_rejected);
    RUN_TEST(test_spi_bad_trailer_rejected);
    RUN_TEST(test_spi_truncated_payload_rejected);
    RUN_TEST(test_spi_unknown_command_handled);
    RUN_TEST(test_spi_cmd_dropped_when_powered_off);

    /* GPIO */
    RUN_TEST(test_gpio_power_read_returns_on);
    RUN_TEST(test_gpio_interrupt_read_initial);
    RUN_TEST(test_gpio_power_write_same_value_no_op);
    RUN_TEST(test_gpio_power_write_off_clears_state);
    RUN_TEST(test_gpio_power_write_on_from_off_does_not_reset_state);
    RUN_TEST(test_gpio_interrupt_write);
    RUN_TEST(test_gpio_rejects_short_wrong_pin_and_invalid_commands);

    /* Interrupt flag (state manipulation) */
    RUN_TEST(test_interrupt_asserted_when_buffer_fills);
    RUN_TEST(test_interrupt_cleared_when_buffer_drains);

    /* Circular-buffer internals */
    RUN_TEST(test_rx_buffer_count_wraps_correctly);
    RUN_TEST(test_rx_buffer_overflow_clips_to_available);
    RUN_TEST(test_cleanup_skips_closed_rx_socket);

    /* UDP / buffer */
    RUN_TEST(test_udp_thread_ignores_data_when_powered_off);
    RUN_TEST(test_udp_inject_populates_rx_buffer);
    RUN_TEST(test_udp_thread_writes_to_buffer_in_rx_mode);
    RUN_TEST(test_udp_thread_drops_data_in_tx_mode);
    RUN_TEST(test_spi_receive_cmd_with_udp_data);
    RUN_TEST(test_spi_receive_cmd_leaves_unrequested_udp_data_buffered);
    RUN_TEST(test_udp_select_error_stops_thread);

    /* Init failure */
    RUN_TEST(test_init_fails_when_spi_address_is_dir);
    RUN_TEST(test_init_fails_when_power_gpio_address_is_dir);
    RUN_TEST(test_init_fails_when_interrupt_gpio_address_is_dir);

    /* RF link model */
    RUN_TEST(test_link_defaults_leave_both_directions_transparent);
    RUN_TEST(test_backdoor_commands_validate_and_apply);
    RUN_TEST(test_backdoor_reset_restores_configured_defaults);
    RUN_TEST(test_downlink_discarded_when_never_in_view);
    RUN_TEST(test_downlink_total_loss_and_impairment_bypass);
    RUN_TEST(test_downlink_delay_is_held_until_sim_time_elapses);
    RUN_TEST(test_downlink_delay_keeps_packet_order);
    RUN_TEST(test_delayed_downlink_carries_a_full_telemetry_frame);
    RUN_TEST(test_corrupt_downlink_flips_a_bit_in_the_next_packets_only);
    RUN_TEST(test_corrupt_uplink_flips_a_bit_in_what_the_host_receives);
    RUN_TEST(test_corrupt_does_not_touch_packets_that_never_cross_the_link);
    RUN_TEST(test_replay_downlink_resends_the_last_packet_even_out_of_view);
    RUN_TEST(test_replay_uplink_redelivers_the_last_packet_when_the_radio_can_hear_it);
    RUN_TEST(test_tamper_backdoors_validate_payloads_and_reset_clears_them);
    RUN_TEST(test_delayed_uplink_delivery_depends_on_power_and_mode);
    RUN_TEST(test_backdoor_rejects_malformed_payloads_for_every_command);
    RUN_TEST(test_station_backdoor_addresses_one_station_or_all);
    RUN_TEST(test_replay_uplink_requires_power_boot_and_a_receiving_mode);
    RUN_TEST(test_replay_downlink_requires_a_transmitting_mode);
    RUN_TEST(test_udp_thread_survives_interrupted_select);
    RUN_TEST(test_delayed_downlink_send_failure_is_tolerated);
    RUN_TEST(test_downlink_delay_queue_overflow_drops_and_counts);
    RUN_TEST(test_uplink_discarded_when_never_in_view);
    RUN_TEST(test_uplink_total_loss);
    RUN_TEST(test_uplink_delay_is_held_until_sim_time_elapses);
    RUN_TEST(test_delayed_uplink_is_lost_if_radio_leaves_rx_mode);
    RUN_TEST(test_power_cycle_discards_packets_in_flight_but_keeps_link_settings);
    RUN_TEST(test_auto_mode_follows_spacecraft_geometry);
    RUN_TEST(test_station_backdoor_gates_auto_visibility);

    /* Symbol alias (also calls the REGISTER_COMPONENT body) */
    RUN_TEST(test_register_component_alias);

    int result = UNITY_END();
    dlclose(g_handle);
    return result;
}
