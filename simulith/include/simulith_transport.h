/*
 * Generic Simulith Transport - ZMQ based
 * Provides a reusable transport layer for simulith nodes.
 */

#ifndef SIMULITH_TRANSPORT_H
#define SIMULITH_TRANSPORT_H

#include "simulith.h"

#define SIMULITH_TRANSPORT_SUCCESS 0
#define SIMULITH_TRANSPORT_ERROR  -1
#define SIMULITH_TRANSPORT_INITIALIZED 255
#define SIMULITH_TRANSPORT_BUFFER_SIZE 4096

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[64];
    char address[128];
    int is_server;
    void* zmq_ctx;
    void* zmq_sock;
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
int simulith_transport_receive(transport_port_t *port, uint8_t *data, size_t max_len);
int simulith_transport_available(transport_port_t *port);
int simulith_transport_flush(transport_port_t *port);
int simulith_transport_close(transport_port_t *port);

#ifdef __cplusplus
}
#endif

#endif /* SIMULITH_TRANSPORT_H */
