/*
 * Generic Simulith Transport - ZMQ based
 * Provides a reusable transport layer for simulith nodes.
 */

#ifndef SIMULITH_TRANSPORT_H
#define SIMULITH_TRANSPORT_H

#include <stdio.h>
#include "simulith.h"

#define SIMULITH_TRANSPORT_SUCCESS 0
#define SIMULITH_TRANSPORT_ERROR  -1
#define SIMULITH_TRANSPORT_INTERRUPTED 0
#define SIMULITH_TRANSPORT_READY 1
#define SIMULITH_TRANSPORT_INITIALIZED 255
#define SIMULITH_TRANSPORT_BUFFER_SIZE 4096
#define SIMULITH_TRANSPORT_DEFAULT_TIMEOUT_MS 1000
#define SIMULITH_TRANSPORT_MAX_WAIT_PORTS 32

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[64];
    char address[128];
    int is_server;
    void* zmq_ctx;
    void* zmq_sock;
    uint64_t next_transaction_id;
    int init;
    /* RX buffer for incoming data */
    uint8_t rx_buf[SIMULITH_TRANSPORT_BUFFER_SIZE];
    size_t rx_buf_len;
} transport_port_t;

typedef struct {
    int pin;
    int direction; // 0=input to FSW, 1=output from FSW
    int value;     // 0=low, 1=high
} simulith_gpio_state_t;

int simulith_transport_init(transport_port_t *port);
int simulith_transport_send(transport_port_t *port, const uint8_t *data, size_t len);
/*
 * Send a device request and wait until the simulator reports that the request
 * has been processed.  The returned byte count is the payload length; device
 * response data, if any, remains on the normal receive path.
 */
int simulith_transport_request(transport_port_t *port, const uint8_t *data, size_t len,
                               int timeout_ms);
/* Receive one request without blocking and return its transaction identifier. */
int simulith_transport_receive_request(transport_port_t *port, uint8_t *data, size_t max_len,
                                       uint64_t *transaction_id);
/* Block until a request is ready on any port or interrupt_fd becomes readable.
 * Returns SIMULITH_TRANSPORT_READY for transport work,
 * SIMULITH_TRANSPORT_INTERRUPTED for interruption, and ERROR on failure. The
 * interrupt descriptor is consumed, but transport data remains queued. */
int simulith_transport_wait_for_request(transport_port_t *const ports[],
                                        size_t port_count, int interrupt_fd);
/* Acknowledge a request only after the simulated device has processed it. */
int simulith_transport_complete_request(transport_port_t *port, uint64_t transaction_id,
                                        int status);
int simulith_transport_receive(transport_port_t *port, uint8_t *data, size_t max_len);
/* Block until exactly len response bytes are available or a monotonic timeout expires. */
int simulith_transport_receive_exact(transport_port_t *port, uint8_t *data,
                                     size_t len, int timeout_ms);
int simulith_transport_available(transport_port_t *port);
int simulith_transport_flush(transport_port_t *port);
int simulith_transport_close(transport_port_t *port);
/* Append a named JSON member containing per-port request round-trip metrics. */
void simulith_transport_write_metrics_json(FILE *stream);

#ifdef __cplusplus
}
#endif

#endif /* SIMULITH_TRANSPORT_H */
