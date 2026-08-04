#include "simulith.h"
#include <sched.h>

static void    *client_context = NULL;
static void    *subscriber     = NULL;
static void    *requester      = NULL;
static char     client_id[64];
static uint64_t update_rate_ns = 0;

int simulith_client_init(const char *pub_addr, const char *rep_addr, const char *id, uint64_t rate_ns)
{
    // Validate parameters
    if (!pub_addr || !rep_addr || !id)
    {
        simulith_log("Invalid parameters: addresses and id cannot be NULL\n");
        return -1;
    }

    if (strlen(id) == 0)
    {
        simulith_log("Invalid client ID: cannot be empty\n");
        return -1;
    }

    if (rate_ns == 0)
    {
        simulith_log("Invalid update rate: must be greater than 0\n");
        return -1;
    }

    strncpy(client_id, id, sizeof(client_id) - 1);
    client_id[sizeof(client_id) - 1] = '\0'; // Ensure null termination
    update_rate_ns                   = rate_ns;

    client_context = zmq_ctx_new();
    if (!client_context)
    {
        perror("zmq_ctx_new failed");
        return -1;
    }

    subscriber = zmq_socket(client_context, ZMQ_SUB);
    if (!subscriber || zmq_connect(subscriber, pub_addr) != 0)
    {
        perror("Subscriber socket setup failed");
        return -1;
    }
    zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0); // Subscribe to all messages

    requester = zmq_socket(client_context, ZMQ_REQ);
    if (!requester || zmq_connect(requester, rep_addr) != 0)
    {
        perror("Requester socket setup failed");
        return -1;
    }
    
    simulith_log("Simulith client [%s] initialized with update rate %lu ns\n", client_id, update_rate_ns);
    return 0;
}

int simulith_client_handshake(void)
{
    // Format READY message with client ID
    char ready_msg[80];
    snprintf(ready_msg, sizeof(ready_msg), "READY %s", client_id);
    const char *ack_msg    = "ACK";
    char        buffer[16] = {0};

    // Set receive timeout to 1 second
    int timeout = 1000; // milliseconds
    zmq_setsockopt(requester, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));

    // Send READY message with client ID
    if (zmq_send(requester, ready_msg, strlen(ready_msg), 0) == -1)
    {
        perror("Failed to send READY");
        return -1;
    }

    // Wait for server response
    int size = zmq_recv(requester, buffer, sizeof(buffer) - 1, 0);
    if (size == -1)
    {
        if (errno == EAGAIN)
        {
            simulith_log("Handshake timeout - server not responding\n");
        }
        else
        {
            perror("Failed to receive ACK");
        }
        return -1;
    }

    buffer[size] = '\0';

    // Check for duplicate ID rejection
    if (strcmp(buffer, "DUP_ID") == 0)
    {
        simulith_log("Handshake failed - duplicate client ID: %s\n", client_id);
        return -1;
    }

    // Check for valid ACK
    if (strcmp(buffer, ack_msg) != 0)
    {
        simulith_log("Unexpected reply to READY: %s\n", buffer);
        return -1;
    }

    // Reset timeout to infinite for normal operation
    timeout = -1;
    zmq_setsockopt(requester, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));

    simulith_log("Handshake complete with server.\n");
    return 0;
}

void simulith_client_run_loop(simulith_tick_callback on_tick)
{
    while (1)
    {
        /* Spin-wait for the next tick broadcast */
        uint64_t time_ns;
        int      recv_bytes;
        do {
            recv_bytes = zmq_recv(subscriber, &time_ns, sizeof(time_ns), ZMQ_DONTWAIT);
            if (recv_bytes != (int)sizeof(time_ns))
                sched_yield();
        } while (recv_bytes != (int)sizeof(time_ns));

        if (on_tick)
            on_tick(time_ns);

        char reply[16] = {0};
        zmq_send(requester, client_id, strlen(client_id), 0);
        zmq_recv(requester, reply, sizeof(reply) - 1, 0); // wait for server ACK
    }
}

int simulith_client_wait_for_tick(uint64_t* tick_time_ns)
{
    if (!tick_time_ns || !subscriber || !requester)
    {
        return -1;
    }

    // Spin-wait for the next tick, same rationale as simulith_client_run_loop
    int recv_bytes;
    do {
        recv_bytes = zmq_recv(subscriber, tick_time_ns, sizeof(*tick_time_ns), ZMQ_DONTWAIT);
        if (recv_bytes != (int)sizeof(*tick_time_ns))
            sched_yield();
    } while (recv_bytes != (int)sizeof(*tick_time_ns));

    // Send acknowledgment - just send the client ID
    if (zmq_send(requester, client_id, strlen(client_id), 0) == -1)
    {
        return -1;
    }

    // Wait for server ACK
    char reply[16] = {0};
    if (zmq_recv(requester, reply, sizeof(reply) - 1, 0) == -1)
    {
        return -1;
    }

    return 0;
}

void simulith_client_shutdown(void)
{
    if (subscriber)
        zmq_close(subscriber);
    if (requester)
        zmq_close(requester);
    if (client_context)
        zmq_ctx_term(client_context);
    subscriber     = NULL;
    requester      = NULL;
    client_context = NULL;
    simulith_log("Simulith client [%s] shut down\n", client_id);
}
