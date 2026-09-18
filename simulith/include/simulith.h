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

/* Ground-command interface: a director-backdoor-style UDP listener for
 * pause/play/speed control, and a UDP status-telemetry sender reporting the
 * result back to YAMCS. */
#define SERVER_BACKDOOR_PORT 50061
#define SERVER_STATUS_PORT   50043

#define INTERVAL_NS 10000000UL // 10ms tick interval

#define SIMULITH_PROTOCOL_MAGIC   0x53484D54U /* "SHMT" */
#define SIMULITH_PROTOCOL_VERSION 1U

typedef enum
{
    SIMULITH_PHASE_PREPARE = 1,
    SIMULITH_PHASE_EXECUTE = 2,
    SIMULITH_PHASE_COMMIT  = 3,
    SIMULITH_PHASE_STOP    = 4
} simulith_phase_t;

#define SIMULITH_PHASE_BIT(phase) (1U << ((unsigned)(phase) - 1U))
#define SIMULITH_PHASE_MASK_PREPARE SIMULITH_PHASE_BIT(SIMULITH_PHASE_PREPARE)
#define SIMULITH_PHASE_MASK_EXECUTE SIMULITH_PHASE_BIT(SIMULITH_PHASE_EXECUTE)
#define SIMULITH_PHASE_MASK_COMMIT  SIMULITH_PHASE_BIT(SIMULITH_PHASE_COMMIT)

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t phase;
    uint64_t sequence;
    uint64_t time_ns;
} simulith_tick_message_t;

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

    /** Configure pacing and a finite run. A speed of zero means unbounded. */
    int simulith_server_configure(double speed, uint64_t duration_ns, const char *metrics_json_path);

    /** Exclude an initial simulated-time interval from steady-state metrics. */
    int simulith_server_configure_warmup(uint64_t warmup_ns);

    /**
     * Run the main server loop. Blocks forever.
     */
    void simulith_server_run(void);

    /** Request that a server loop running on another thread return. */
    void simulith_server_request_stop(void);

#ifdef SIMULITH_TESTING
    /** Exercise the server's interactive command parser without running its loop. */
    int simulith_server_process_cli_command_for_test(const char *command, int *paused, double *speed);
    /** Exercise the server's ground-command backdoor parser without a real socket. */
    void simulith_server_process_backdoor_command_for_test(const uint8_t *frame, size_t frame_len,
                                                           int *paused, double *speed);
    /** Force a broadcast timestamp to exercise periodic reporting deterministically. */
    void simulith_server_broadcast_for_test(uint64_t time_ns);
    /** Exercise the server's ground-command backdoor socket setup without running its loop. */
    int simulith_server_ensure_backdoor_socket_for_test(void);
    /** Exercise the server's status-telemetry socket setup without running its loop. */
    void simulith_server_ensure_status_socket_for_test(void);
    /** Exercise the server's status-telemetry packet send without running its loop. */
    void simulith_server_send_status_update_for_test(int paused, double speed);
    /** Exercise the combined stdin/backdoor poll-and-dispatch step without running its loop. */
    int simulith_server_poll_and_dispatch_commands_for_test(int *paused, double *speed);
#endif

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
     * Callback signature for one explicitly sequenced synchronization phase.
     * Returning nonzero fails the phase; the client will not acknowledge it.
     */
    typedef int (*simulith_phase_callback)(uint64_t sequence,
                                           uint64_t tick_time_ns);

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

    /** Declare the phases this client must explicitly complete. */
    int simulith_client_configure_phases(uint32_t phase_mask);

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

    /** Run a director-style PREPARE/EXECUTE/COMMIT loop. */
    void simulith_client_run_phased_loop(simulith_phase_callback on_prepare,
                                         simulith_phase_callback on_execute,
                                         simulith_phase_callback on_commit);

    /** Request that a client loop running on another thread return. */
    void simulith_client_request_stop(void);

    /**
     * Wait for next tick and send acknowledgment (non-blocking API for OSAL use).
     *
     * @param tick_time_ns Pointer to store the tick time in nanoseconds.
     * @return 0 on success, -1 on error.
     */
    int simulith_client_wait_for_tick(uint64_t* tick_time_ns);

    /** Receive a tick without acknowledging it. */
    int simulith_client_receive_tick(uint64_t *tick_time_ns, uint64_t *sequence);

    /** Receive the next sequence/phase frame without completing it. */
    int simulith_client_receive_phase(uint64_t *tick_time_ns, uint64_t *sequence,
                                      simulith_phase_t *phase);

    /** Explicitly report completion of work for the received sequence. */
    int simulith_client_complete_tick(uint64_t sequence, simulith_phase_t phase);

    /**
     * Shut down the client and release resources.
     */
    void simulith_client_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // SIMULITH_H
