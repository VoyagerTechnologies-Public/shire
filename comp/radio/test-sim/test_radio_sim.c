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
static simulith_gpio_state_t       *g_gpio_power = NULL;
static simulith_gpio_state_t       *g_gpio_int   = NULL;

typedef int  (*radio_init_fn)(radio_sim_state_t *);
typedef void (*radio_cleanup_fn)(radio_sim_state_t *);
static radio_init_fn    g_radio_sim_init    = NULL;
static radio_cleanup_fn g_radio_sim_cleanup = NULL;

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
    g_iface->tick(state, 0ULL, NULL);
    simulith_transport_close(&pw);
    usleep(1000);
}

/* Tracks the component state initialised by each test so tearDown() can
 * emergency-cleanup if a Unity assertion failure longjmps past cleanup(). */
static component_state_t *g_state_under_test = NULL;

void setUp(void)    { g_state_under_test = NULL; }
void tearDown(void)
{
    if (g_state_under_test)
    {
        g_iface->cleanup(g_state_under_test);
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
    TEST_ASSERT_NOT_NULL(g_iface->name);
    TEST_ASSERT_NOT_NULL(g_iface->description);
    TEST_ASSERT_NOT_NULL(g_iface->init);
    TEST_ASSERT_NOT_NULL(g_iface->tick);
    TEST_ASSERT_NOT_NULL(g_iface->cleanup);
    /* radio_sim does not define a backdoor handler */
    TEST_ASSERT_NULL(g_iface->backdoor);
    TEST_ASSERT_EQUAL_STRING("radio_sim", g_iface->name);
}

static void test_init_returns_success_and_initial_state(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    TEST_ASSERT_NOT_NULL(state);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0,                rs->hk.CommandCounter);
    TEST_ASSERT_EQUAL_UINT8(RADIO_MODE_DUPLEX, rs->hk.Mode);
    TEST_ASSERT_EQUAL_UINT8(0,                 rs->hk.GroundLock);
    TEST_ASSERT_EQUAL_UINT8(RADIO_MODE_DUPLEX, rs->config.Mode);
    TEST_ASSERT_EQUAL_UINT8(0,                 rs->interrupt_asserted);

    g_iface->cleanup(state);
}

static void test_cleanup_rebind_ok(void)
{
    /* Calling init/cleanup twice must not leak SPI or GPIO IPC sockets */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_iface->cleanup(state);

    state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_iface->cleanup(state);
}

static void test_cleanup_with_null_is_safe(void)
{
    g_iface->cleanup(NULL);
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
    g_iface->tick(NULL, 0ULL, NULL);
}

static void test_tick_does_not_crash(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_iface->tick(state, 0ULL, NULL);
    g_iface->tick(state, 200000000ULL, NULL);  /* 200 ms — crosses rate-limit threshold */
    g_iface->cleanup(state);
}

/* -------------------------------------------------------------------------
 * SPI wire-protocol tests
 * Each test powers on the radio via GPIO write before sending SPI frames so
 * that the tick does not silently discard them.
 * -------------------------------------------------------------------------*/
static void test_spi_noop_increments_command_counter(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    power_on(state);

    transport_port_t spi;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_spi_client(&spi, "test_spi"));
    usleep(2000);

    uint8_t frame[5];
    encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_NOOP_CMD, NULL, 0);
    TEST_ASSERT_EQUAL_INT(5, simulith_transport_send(&spi, frame, 5));
    usleep(2000);
    g_iface->tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(1, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->cleanup(state);
}

static void test_spi_req_hk_returns_framed_housekeeping(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    power_on(state);

    transport_port_t spi;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_spi_client(&spi, "test_spi"));
    usleep(2000);

    uint8_t frame[5];
    encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_REQ_HK_CMD, NULL, 0);
    simulith_transport_send(&spi, frame, 5);
    usleep(2000);
    g_iface->tick(state, 0ULL, NULL);

    uint8_t resp[32];
    size_t  n = drain_all(&spi, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t((size_t)RADIO_DEVICE_HK_SIZE, n);
    TEST_ASSERT_EQUAL_HEX8(RADIO_DEVICE_HDR,     resp[0]);
    TEST_ASSERT_EQUAL_HEX8(RADIO_DEVICE_TRAILER, resp[RADIO_DEVICE_HK_SIZE - 1]);

    simulith_transport_close(&spi);
    g_iface->cleanup(state);
}

static void test_hk_initial_mode_and_counter(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    uint8_t frame[5];
    encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_REQ_HK_CMD, NULL, 0);
    simulith_transport_send(&spi, frame, 5);
    usleep(2000);
    g_iface->tick(state, 0ULL, NULL);

    uint8_t resp[32];
    TEST_ASSERT_EQUAL_size_t((size_t)RADIO_DEVICE_HK_SIZE,
                             drain_all(&spi, resp, sizeof(resp)));

    uint16_t counter = (uint16_t)(((uint16_t)resp[1] << 8) | resp[2]);
    TEST_ASSERT_EQUAL_UINT16(1,                counter);   /* REQ_HK increments */
    TEST_ASSERT_EQUAL_UINT8(RADIO_MODE_DUPLEX, resp[3]);   /* mode */
    TEST_ASSERT_EQUAL_UINT8(0,                 resp[4]);   /* ground lock */

    simulith_transport_close(&spi);
    g_iface->cleanup(state);
}

static void test_spi_set_cfg_updates_mode(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
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
    g_iface->tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT8(RADIO_MODE_RX, rs->config.Mode);
    TEST_ASSERT_EQUAL_UINT8(RADIO_MODE_RX, rs->hk.Mode);
    TEST_ASSERT_EQUAL_UINT16(1,             rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->cleanup(state);
}

static void test_spi_set_cfg_wrong_payload_size_ignored(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
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
    g_iface->tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0,                rs->hk.CommandCounter);
    TEST_ASSERT_EQUAL_UINT8(RADIO_MODE_DUPLEX, rs->config.Mode);  /* unchanged */

    simulith_transport_close(&spi);
    g_iface->cleanup(state);
}

static void test_spi_receive_cmd_empty_buffer_returns_frame(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
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
    g_iface->tick(state, 0ULL, NULL);

    uint8_t resp[16];
    size_t  n = drain_all(&spi, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_HEX8(RADIO_DEVICE_HDR,     resp[0]);
    TEST_ASSERT_EQUAL_UINT8(0,                   resp[1]);  /* len_hi */
    TEST_ASSERT_EQUAL_UINT8(0,                   resp[2]);  /* len_lo (to_send=0) */
    TEST_ASSERT_EQUAL_HEX8(RADIO_DEVICE_TRAILER, resp[3]);

    simulith_transport_close(&spi);
    g_iface->cleanup(state);
}

static void test_spi_receive_cmd_wrong_payload_rejected(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
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
    g_iface->tick(state, 0ULL, NULL);

    uint8_t resp[16];
    TEST_ASSERT_EQUAL_size_t(0, drain_all(&spi, resp, sizeof(resp)));

    simulith_transport_close(&spi);
    g_iface->cleanup(state);
}

static void test_spi_send_cmd_in_duplex_mode(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
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
    g_iface->tick(state, 0ULL, NULL);

    /* CommandCounter increments in DUPLEX mode regardless of UDP TX result */
    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(1, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->cleanup(state);
}

static void test_spi_send_cmd_zero_payload_no_counter(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    /* SEND with payload_len=0: the if(payload_len>0) guard skips entirely */
    uint8_t frame[5];
    encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_SEND_CMD, NULL, 0);
    simulith_transport_send(&spi, frame, 5);
    usleep(2000);
    g_iface->tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->cleanup(state);
}

static void test_spi_send_cmd_in_rx_mode_not_forwarded(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
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
    g_iface->tick(state, 0ULL, NULL);  /* counter = 1 after SET_CFG */

    /* SEND in RX-only mode: mode check fails, counter does not increment */
    uint8_t data[3] = {0x01, 0x02, 0x03};
    uint8_t frame[5 + 3];
    size_t  len = encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_SEND_CMD,
                                   data, 3);
    simulith_transport_send(&spi, frame, len);
    usleep(2000);
    g_iface->tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(1, rs->hk.CommandCounter);  /* only SET_CFG counted */

    simulith_transport_close(&spi);
    g_iface->cleanup(state);
}

static void test_spi_send_cmd_fails_on_bad_socket(void)
{
    /* Close the TX socket before SEND so sendto() returns -1, covering the
     * failure-log branch.  cleanup() then exercises the udp_tx_socket < 0 guard. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
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
    g_iface->tick(state, 0ULL, NULL);

    /* BytesReceived always incremented; CommandCounter incremented in TX/DUPLEX
     * even when sendto fails; BytesSent stays 0 because sendto returned <= 0. */
    TEST_ASSERT_EQUAL_UINT32(4, rs->hk.BytesReceived);
    TEST_ASSERT_EQUAL_UINT16(1, rs->hk.CommandCounter);
    TEST_ASSERT_EQUAL_UINT32(0, rs->hk.BytesSent);

    simulith_transport_close(&spi);
    g_iface->cleanup(state);  /* cleanup skips close(udp_tx_socket) since it's -1 */
}

static void test_spi_short_packet_rejected(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    /* 4 bytes < minimum 5: length guard in handle_spi_command rejects it */
    uint8_t short_frame[4] = {RADIO_DEVICE_HDR, RADIO_DEVICE_NOOP_CMD, 0x00, 0x11};
    simulith_transport_send(&spi, short_frame, 4);
    usleep(2000);
    g_iface->tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->cleanup(state);
}

static void test_spi_bad_header_rejected(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    uint8_t frame[5] = {0xBB, RADIO_DEVICE_NOOP_CMD, 0x00, 0x00, RADIO_DEVICE_TRAILER};
    simulith_transport_send(&spi, frame, sizeof(frame));
    usleep(2000);
    g_iface->tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->cleanup(state);
}

static void test_spi_bad_trailer_rejected(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    uint8_t frame[5] = {RADIO_DEVICE_HDR, RADIO_DEVICE_NOOP_CMD, 0x00, 0x00, 0xBB};
    simulith_transport_send(&spi, frame, sizeof(frame));
    usleep(2000);
    g_iface->tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->cleanup(state);
}

static void test_spi_truncated_payload_rejected(void)
{
    /* Header declares payload_len=5 but only 4 payload bytes are included (9 total < 10
     * expected), hitting the `length < expected_len` guard at radio_sim_handle_spi_command. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
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
    g_iface->tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->cleanup(state);
}

static void test_spi_unknown_command_handled(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    power_on(state);

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    uint8_t frame[5];
    encode_spi_frame(frame, sizeof(frame), 0x99, NULL, 0);
    simulith_transport_send(&spi, frame, 5);
    usleep(2000);
    g_iface->tick(state, 0ULL, NULL);

    /* Default arm: logs error, no crash, counter unchanged */
    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->cleanup(state);
}

static void test_spi_cmd_dropped_when_powered_off(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    /* Explicitly ensure power is off regardless of prior test state */
    if (g_gpio_power) g_gpio_power->value = 0;

    transport_port_t spi;
    open_spi_client(&spi, "test_spi");
    usleep(2000);

    uint8_t frame[5];
    encode_spi_frame(frame, sizeof(frame), RADIO_DEVICE_NOOP_CMD, NULL, 0);
    simulith_transport_send(&spi, frame, 5);
    usleep(2000);
    g_iface->tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT16(0, rs->hk.CommandCounter);

    simulith_transport_close(&spi);
    g_iface->cleanup(state);
}

/* -------------------------------------------------------------------------
 * GPIO tests
 * -------------------------------------------------------------------------*/
static void test_gpio_power_read_returns_on(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;
    /* Set the power state directly — avoids a ZMQ PAIR close/reopen race that
     * occurs when power_on() opens and closes pw_helper before we connect test_pw. */
    if (g_gpio_power) g_gpio_power->value = 1;

    transport_port_t pw;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_gpio_client(&pw, "test_pw", RADIO_POWER_PORT));
    usleep(5000); /* give PAIR handshake time to complete */

    uint8_t req[2] = {0, RADIO_CFG_GPIO_POWER_PIN};  /* read command */
    simulith_transport_send(&pw, req, sizeof(req));
    usleep(2000);
    g_iface->tick(state, 0ULL, NULL);

    uint8_t resp[8];
    size_t  n = drain_all(&pw, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_UINT8(0,                        resp[0]);  /* cmd=read */
    TEST_ASSERT_EQUAL_UINT8(RADIO_CFG_GPIO_POWER_PIN, resp[1]);
    TEST_ASSERT_EQUAL_UINT8(1,                        resp[2]);  /* powered on */

    simulith_transport_close(&pw);
    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_gpio_interrupt_read_initial(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    transport_port_t irq;
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,
                          open_gpio_client(&irq, "test_irq", RADIO_INT_PORT));
    usleep(2000);

    uint8_t req[2] = {0, RADIO_CFG_GPIO_INTERRUPT_PIN};
    simulith_transport_send(&irq, req, sizeof(req));
    usleep(2000);
    g_iface->tick(state, 0ULL, NULL);

    uint8_t resp[8];
    size_t  n = drain_all(&irq, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_UINT8(0,                             resp[0]);
    TEST_ASSERT_EQUAL_UINT8(RADIO_CFG_GPIO_INTERRUPT_PIN,  resp[1]);
    TEST_ASSERT_EQUAL_UINT8(0,                             resp[2]);  /* not asserted */

    simulith_transport_close(&irq);
    g_iface->cleanup(state);
}

static void test_gpio_power_write_same_value_no_op(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;
    if (g_gpio_power) g_gpio_power->value = 1;

    transport_port_t pw;
    open_gpio_client(&pw, "test_pw", RADIO_POWER_PORT);
    usleep(5000);

    /* Write 1 when already 1: value==current, no state change */
    uint8_t req[3] = {1, RADIO_CFG_GPIO_POWER_PIN, 1};
    simulith_transport_send(&pw, req, sizeof(req));
    usleep(2000);
    g_iface->tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT8(RADIO_MODE_DUPLEX, rs->config.Mode);  /* unchanged */

    simulith_transport_close(&pw);
    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_gpio_power_write_off_clears_state(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;
    /* Set power on directly — avoids PAIR reconnect race from power_on() open/close. */
    if (g_gpio_power) g_gpio_power->value = 1;

    transport_port_t pw;
    open_gpio_client(&pw, "test_pw", RADIO_POWER_PORT);
    usleep(5000);

    /* Write 0: power off — radio clears buffers and sets mode to SLEEP */
    uint8_t req[3] = {1, RADIO_CFG_GPIO_POWER_PIN, 0};
    simulith_transport_send(&pw, req, sizeof(req));
    usleep(2000);
    g_iface->tick(state, 0ULL, NULL);

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    TEST_ASSERT_EQUAL_UINT8(RADIO_MODE_SLEEP, rs->hk.Mode);
    TEST_ASSERT_EQUAL_UINT32(0, rs->rx_buffer_head);
    TEST_ASSERT_EQUAL_UINT32(0, rs->rx_buffer_tail);

    simulith_transport_close(&pw);
    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_gpio_interrupt_write(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    transport_port_t irq;
    open_gpio_client(&irq, "test_irq", RADIO_INT_PORT);
    usleep(2000);

    /* Write interrupt line to 1 — no response expected for GPIO writes */
    uint8_t req[3] = {1, RADIO_CFG_GPIO_INTERRUPT_PIN, 1};
    simulith_transport_send(&irq, req, sizeof(req));
    usleep(2000);
    g_iface->tick(state, 0ULL, NULL);

    simulith_transport_close(&irq);
    g_iface->cleanup(state);
}

/* -------------------------------------------------------------------------
 * Interrupt-flag tests (direct state manipulation, no transport overhead)
 * -------------------------------------------------------------------------*/
static void test_interrupt_asserted_when_buffer_fills(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    pthread_mutex_lock(&rs->buffer_mutex);
    rs->rx_buffer_head    = RADIO_SIM_INTERRUPT_THRESHOLD;  /* 32768 bytes filled */
    rs->rx_buffer_tail    = 0;
    rs->interrupt_asserted = 0;
    pthread_mutex_unlock(&rs->buffer_mutex);

    /* 200 ms tick crosses the rate-limit gate so radio_sim_update_interrupt fires */
    g_iface->tick(state, 200000000ULL, NULL);

    TEST_ASSERT_EQUAL_UINT8(1, rs->interrupt_asserted);

    g_iface->cleanup(state);
}

static void test_interrupt_cleared_when_buffer_drains(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    pthread_mutex_lock(&rs->buffer_mutex);
    rs->interrupt_asserted = 1;  /* was asserted */
    rs->rx_buffer_head     = 0;  /* buffer now empty */
    rs->rx_buffer_tail     = 0;
    pthread_mutex_unlock(&rs->buffer_mutex);

    g_iface->tick(state, 200000000ULL, NULL);

    TEST_ASSERT_EQUAL_UINT8(0, rs->interrupt_asserted);

    g_iface->cleanup(state);
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
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    pthread_mutex_lock(&rs->buffer_mutex);
    rs->rx_buffer_head    = 1;
    rs->rx_buffer_tail    = (uint32_t)(RADIO_SIM_RX_BUFFER_SIZE - RADIO_SIM_INTERRUPT_THRESHOLD);
    rs->interrupt_asserted = 0;
    pthread_mutex_unlock(&rs->buffer_mutex);

    g_iface->tick(state, 200000000ULL, NULL);  /* 200 ms crosses rate-limit gate */

    TEST_ASSERT_EQUAL_UINT8(1, rs->interrupt_asserted);

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_rx_buffer_overflow_clips_to_available(void)
{
    /* Fill the circular buffer to within 4 bytes of capacity, then inject 20 bytes
     * via UDP.  radio_sim_write_to_rx_buffer must clip and log the overflow. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
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
    g_iface->cleanup(state);
}

static void test_cleanup_skips_closed_rx_socket(void)
{
    /* Stop the UDP thread manually, then pre-close udp_rx_socket so cleanup hits
     * the `socket < 0` guard rather than calling close() again. */
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    rs->udp_thread_running = 0;
    pthread_join(rs->udp_thread, NULL);

    close(rs->udp_rx_socket);
    rs->udp_rx_socket = -1;

    /* cleanup must not double-close or crash; it also skips pthread_join
     * because udp_thread_running is already 0. */
    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

/* -------------------------------------------------------------------------
 * UDP / receive-buffer tests
 * -------------------------------------------------------------------------*/
static void test_udp_thread_ignores_data_when_powered_off(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
    g_state_under_test = state;
    /* Leave radio powered off (initial state) */
    if (g_gpio_power) g_gpio_power->value = 0;

    static const uint8_t payload[8] = {0xAB, 0xCD, 0xEF, 0x01, 0x02, 0x03, 0x04, 0x05};
    inject_udp(payload, sizeof(payload));
    usleep(50000);  /* let UDP thread run */

    radio_sim_state_t *rs = (radio_sim_state_t *)state;
    pthread_mutex_lock(&rs->buffer_mutex);
    uint32_t count = rs->rx_buffer_head - rs->rx_buffer_tail;
    pthread_mutex_unlock(&rs->buffer_mutex);

    TEST_ASSERT_EQUAL_UINT32(0, count);  /* no bytes written when powered off */

    g_state_under_test = NULL;
    g_iface->cleanup(state);
}

static void test_udp_inject_populates_rx_buffer(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
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
    g_iface->cleanup(state);
}

static void test_spi_receive_cmd_with_udp_data(void)
{
    component_state_t *state = NULL;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
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
    g_iface->tick(state, 200000000ULL, NULL);

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
    g_iface->cleanup(state);
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
    int                rc    = g_iface->init(&state);

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
    int rc = g_iface->init(&state);

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
    int rc = g_iface->init(&state);

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
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, g_iface->init(&state));
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
    g_iface->cleanup(state);
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
    g_iface = get_iface();
    if (!g_iface)
    {
        fprintf(stderr, "get_component_interface() returned NULL\n");
        dlclose(g_handle);
        return 1;
    }

    g_radio_sim_init    = (radio_init_fn)dlsym(g_handle,    "radio_sim_init");
    g_radio_sim_cleanup = (radio_cleanup_fn)dlsym(g_handle, "radio_sim_cleanup");
    g_gpio_power        = (simulith_gpio_state_t *)dlsym(g_handle, "gpio_power_state");
    g_gpio_int          = (simulith_gpio_state_t *)dlsym(g_handle, "gpio_interrupt_state");

    UNITY_BEGIN();

    /* Lifecycle */
    RUN_TEST(test_dlopen_radio_sim_so);
    RUN_TEST(test_get_component_interface_symbol);
    RUN_TEST(test_init_returns_success_and_initial_state);
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
    RUN_TEST(test_gpio_interrupt_write);

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
    RUN_TEST(test_spi_receive_cmd_with_udp_data);
    RUN_TEST(test_udp_select_error_stops_thread);

    /* Init failure */
    RUN_TEST(test_init_fails_when_spi_address_is_dir);
    RUN_TEST(test_init_fails_when_power_gpio_address_is_dir);
    RUN_TEST(test_init_fails_when_interrupt_gpio_address_is_dir);

    /* Symbol alias (also calls the REGISTER_COMPONENT body) */
    RUN_TEST(test_register_component_alias);

    int result = UNITY_END();
    dlclose(g_handle);
    return result;
}
