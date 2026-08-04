/*
 * Generic Simulith Transport implementation using ZMQ
 */

#include "simulith_transport.h"

int simulith_transport_init(transport_port_t *port)
{
    if (!port) return SIMULITH_TRANSPORT_ERROR;
    if (port->init == SIMULITH_TRANSPORT_INITIALIZED) return SIMULITH_TRANSPORT_SUCCESS;

    port->zmq_ctx = zmq_ctx_new();
    if (!port->zmq_ctx) {
        simulith_log("simulith_transport_init: Failed to create ZMQ context\n");
        return SIMULITH_TRANSPORT_ERROR;
    }
    port->zmq_sock = zmq_socket(port->zmq_ctx, ZMQ_PAIR);
    if (!port->zmq_sock) {
        simulith_log("simulith_transport_init: Failed to create ZMQ socket\n");
        zmq_ctx_term(port->zmq_ctx);
        return SIMULITH_TRANSPORT_ERROR;
    }

    /* Raise HWM to prevent silent drops at high FTRT speeds. Default is 1000
     * which fills in tens of milliseconds at 256× real-time rates. */
    int hwm = 65536;
    zmq_setsockopt(port->zmq_sock, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(port->zmq_sock, ZMQ_RCVHWM, &hwm, sizeof(hwm));
    if (strlen(port->name) > 0) {
        zmq_setsockopt(port->zmq_sock, ZMQ_IDENTITY, port->name, strlen(port->name));
    }
    int rc;
    if (port->is_server) {
        rc = zmq_bind(port->zmq_sock, port->address);
        if (rc != 0) {
            simulith_log("simulith_transport_init: Failed to bind to %s\n", port->address);
            zmq_close(port->zmq_sock);
            zmq_ctx_term(port->zmq_ctx);
            return SIMULITH_TRANSPORT_ERROR;
        }
        simulith_log("simulith_transport_init: Bound to %s as '%s'\n", port->address, port->name);
    } else {
        rc = zmq_connect(port->zmq_sock, port->address);
        if (rc != 0) {
            simulith_log("simulith_transport_init: Failed to connect to %s\n", port->address);
            zmq_close(port->zmq_sock);
            zmq_ctx_term(port->zmq_ctx);
            return SIMULITH_TRANSPORT_ERROR;
        }
        simulith_log("simulith_transport_init: Connected to %s as '%s'\n", port->address, port->name);
    }
    port->init = SIMULITH_TRANSPORT_INITIALIZED;

    /* Initialize RX buffer */
    port->rx_buf_len = 0;
    return SIMULITH_TRANSPORT_SUCCESS;
}

int simulith_transport_send(transport_port_t *port, const uint8_t *data, size_t len)
{
    if (!port || port->init != SIMULITH_TRANSPORT_INITIALIZED) {
        simulith_log("simulith_transport_send: Uninitialized transport port\n");
        return SIMULITH_TRANSPORT_ERROR;
    }
    int rc = zmq_send(port->zmq_sock, data, len, ZMQ_DONTWAIT);
    if (rc < 0) {
        simulith_log("simulith_transport_send: zmq_send failed (peer may be unavailable)\n");
        return SIMULITH_TRANSPORT_ERROR;
    }
    simulith_log("  TX[%s]: %zu bytes\n", port->name, len);
    return (int)len;
}

int simulith_transport_receive(transport_port_t *port, uint8_t *data, size_t max_len)
{
    if (!port || port->init != SIMULITH_TRANSPORT_INITIALIZED) {
        simulith_log("simulith_transport_receive: Uninitialized transport port\n");
        return SIMULITH_TRANSPORT_ERROR;
    }
    if (port->rx_buf_len == 0) {
        /* No buffered data */
        return 0;
    }
    size_t to_copy = (port->rx_buf_len < max_len) ? port->rx_buf_len : max_len;
    memcpy(data, port->rx_buf, to_copy);
    /* Shift remaining data in buffer */
    if (to_copy < port->rx_buf_len) {
        memmove(port->rx_buf, port->rx_buf + to_copy, port->rx_buf_len - to_copy);
    }
    port->rx_buf_len -= to_copy;
    simulith_log("  RX[%s]: %zu bytes (from buffer)\n", port->name, to_copy);
    return (int)to_copy;
}

int simulith_transport_available(transport_port_t *port)
{
    if (!port || port->init != SIMULITH_TRANSPORT_INITIALIZED) {
        simulith_log("simulith_transport_available: Uninitialized transport port\n");
        return SIMULITH_TRANSPORT_ERROR;
    }
    /* If buffer already has data, report available */
    if (port->rx_buf_len > 0) {
        return 1;
    }
    zmq_pollitem_t items[] = { { port->zmq_sock, 0, ZMQ_POLLIN, 0 } };
    int rc = zmq_poll(items, 1, 0);
    if (rc > 0 && (items[0].revents & ZMQ_POLLIN)) {
        zmq_msg_t msg;
        zmq_msg_init(&msg);
        int rcv_size = zmq_msg_recv(&msg, port->zmq_sock, ZMQ_DONTWAIT);
        if (rcv_size > 0) {
            size_t size = (size_t)rcv_size;
            size_t space = sizeof(port->rx_buf) - port->rx_buf_len;
            if (size > space) {
                simulith_log("  RX[%s]: Buffer overflow, dropping %zu bytes\n", port->name, size);
                zmq_msg_close(&msg);
                return 0;
            }
            memcpy(port->rx_buf + port->rx_buf_len, zmq_msg_data(&msg), size);
            port->rx_buf_len += size;
            zmq_msg_close(&msg);
            simulith_log("  RX[%s]: %zu bytes buffered\n", port->name, size);
            return 1;
        }
        zmq_msg_close(&msg);
    }
    return 0;
}

int simulith_transport_flush(transport_port_t *port)
{
    if (!port || port->init != SIMULITH_TRANSPORT_INITIALIZED) {
        simulith_log("simulith_transport_flush: Uninitialized transport port\n");
        return SIMULITH_TRANSPORT_ERROR;
    }
    return SIMULITH_TRANSPORT_SUCCESS;
}

int simulith_transport_close(transport_port_t *port)
{
    if (!port || port->init != SIMULITH_TRANSPORT_INITIALIZED) {
        return SIMULITH_TRANSPORT_ERROR;
    }
    zmq_close(port->zmq_sock);
    zmq_ctx_term(port->zmq_ctx);
    port->init = 0;
    simulith_log("Transport port %s closed\n", port->name);
    return SIMULITH_TRANSPORT_SUCCESS;
}
