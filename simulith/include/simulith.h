#ifndef SIMULITH_H
#define SIMULITH_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <zmq.h>

// Include interface headers
#include "simulith_transport.h"
#include "simulith_time.h"

// Defines
#define SERVER_PUB_ADDR "tcp://0.0.0.0:50000"
#define SERVER_REP_ADDR "tcp://0.0.0.0:50001"

#define CLIENT_PUB_ADDR "tcp://shire-server:50000"
#define CLIENT_REP_ADDR "tcp://shire-server:50001"

#define LOCAL_PUB_ADDR "ipc:///tmp/simulith_pub.sock"
#define LOCAL_REP_ADDR "ipc:///tmp/simulith_rep.sock"

#define INTERVAL_NS 10000000UL // 10ms tick interval

#define SIMULITH_UART_BASE_PORT 51000
#define SIMULITH_I2C_BASE_PORT  52000
#define SIMULITH_SPI_BASE_PORT  53000
#define SIMULITH_GPIO_BASE_PORT 54000

#ifdef __cplusplus
extern "C"
{
#endif

    // Logging function
    void simulith_log(const char *fmt, ...);

#ifdef SIMULITH_TESTING
    /* Test-only helper to reset logging state between tests. Only available
     * when building tests (SIMULITH_TESTING). */
    void simulith_log_reset_for_tests(void);
#endif

    // ---------- Server API ----------

    /**
     * Initialize the Simulith server.
     *
     * @param pub_bind The ZeroMQ PUB socket bind address (e.g., "tcp://0.0.0.0:5555").
     * @param rep_bind The ZeroMQ REP socket bind address (e.g., "tcp://0.0.0.0:5556").
     * @param client_count The number of clients to wait for per tick.
     * @param interval_ns The tick interval in nanoseconds.
     * @return 0 on success, -1 on error.
     */
    int simulith_server_init(const char *pub_bind, const char *rep_bind, int client_count, uint64_t interval_ns);

    /**
     * Run the main server loop. Blocks forever.
     */
    void simulith_server_run(void);

    /**
     * Cleanly shuts down the server.
     */
    void simulith_server_shutdown(void);

    // ---------- Client API ----------

    /**
     * Callback signature for a tick.
     *
     * @param tick_time_ns The time for the current tick in nanoseconds.
     */
    typedef void (*simulith_tick_callback)(uint64_t tick_time_ns);

    /**
     * Initialize a Simulith client.
     *
     * @param pub_addr The ZeroMQ SUB socket connect address (e.g., "tcp://localhost:5555").
     * @param rep_addr The ZeroMQ REQ socket connect address (e.g., "tcp://localhost:5556").
     * @param id The unique identifier string for this client.
     * @param rate_ns The update rate in nanoseconds.
     * @return 0 on success, -1 on error.
     */
    int simulith_client_init(const char *pub_addr, const char *rep_addr, const char *id, uint64_t rate_ns);

    /**
     * Handshake with the Simulith server.
     *
     * @return 0 on success, -1 on error.
     */
    int simulith_client_handshake(void);

    /**
     * Starts the client's main loop.
     *
     * @param on_tick Callback to invoke each time a new tick is received.
     */
    void simulith_client_run_loop(simulith_tick_callback on_tick);

    /**
     * Wait for next tick and send acknowledgment (non-blocking API for OSAL use).
     *
     * @param tick_time_ns Pointer to store the tick time in nanoseconds.
     * @return 0 on success, -1 on error.
     */
    int simulith_client_wait_for_tick(uint64_t* tick_time_ns);

    /**
     * Shut down the client and release resources.
     */
    void simulith_client_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // SIMULITH_H
