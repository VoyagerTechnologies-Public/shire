/*
 * Generic Simulith Transport implementation using ZMQ
 */

#include "simulith_transport.h"
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <unistd.h>

#define TRANSPORT_REQUEST_MAGIC   0x53525154U /* "SRQT" */
#define TRANSPORT_REQUEST_VERSION 1U
#define TRANSPORT_REQUEST_TYPE    1U
#define TRANSPORT_ACK_TYPE        2U
#define TRANSPORT_REQUEST_HEADER_SIZE 20U
#define TRANSPORT_ACK_SIZE            20U

static pthread_mutex_t transport_context_mutex = PTHREAD_MUTEX_INITIALIZER;
static void *transport_shared_context = NULL;
static size_t transport_context_references = 0;

#define TRANSPORT_METRIC_COUNT 64
#define TRANSPORT_LATENCY_BUCKET_WIDTH_NS UINT64_C(5000)
#define TRANSPORT_LATENCY_BUCKETS 2048
typedef struct
{
    char name[64];
    uint64_t count;
    uint64_t errors;
    uint64_t total_ns;
    uint64_t maximum_ns;
    uint64_t histogram_overflows;
    uint64_t histogram[TRANSPORT_LATENCY_BUCKETS];
} transport_metric_t;

static pthread_mutex_t transport_metric_mutex = PTHREAD_MUTEX_INITIALIZER;
static transport_metric_t transport_metrics[TRANSPORT_METRIC_COUNT];
static size_t transport_metrics_count = 0;

static uint64_t monotonic_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static unsigned int transport_latency_bucket(uint64_t latency_ns)
{
    uint64_t bucket = latency_ns / TRANSPORT_LATENCY_BUCKET_WIDTH_NS;
    return bucket < TRANSPORT_LATENCY_BUCKETS ? (unsigned int)bucket :
        TRANSPORT_LATENCY_BUCKETS - 1U;
}

static void transport_record_request(const char *name, uint64_t start_ns, int success)
{
    uint64_t latency_ns = monotonic_ns() - start_ns;
    pthread_mutex_lock(&transport_metric_mutex);
    size_t index = 0;
    while (index < transport_metrics_count &&
           strcmp(transport_metrics[index].name, name) != 0)
        index++;
    if (index == transport_metrics_count && index < TRANSPORT_METRIC_COUNT)
    {
        snprintf(transport_metrics[index].name,
                 sizeof(transport_metrics[index].name), "%s", name);
        transport_metrics_count++;
    }
    if (index < TRANSPORT_METRIC_COUNT)
    {
        transport_metric_t *metric = &transport_metrics[index];
        metric->count++;
        metric->errors += success ? 0U : 1U;
        metric->total_ns += latency_ns;
        if (latency_ns > metric->maximum_ns) metric->maximum_ns = latency_ns;
        if (latency_ns >= TRANSPORT_LATENCY_BUCKET_WIDTH_NS *
                          TRANSPORT_LATENCY_BUCKETS)
            metric->histogram_overflows++;
        metric->histogram[transport_latency_bucket(latency_ns)]++;
    }
    pthread_mutex_unlock(&transport_metric_mutex);
}

static uint64_t transport_metric_percentile(const transport_metric_t *metric,
                                            uint64_t numerator)
{
    uint64_t target = (metric->count * numerator + 99U) / 100U;
    uint64_t accumulated = 0;
    for (unsigned int bucket = 0; bucket < TRANSPORT_LATENCY_BUCKETS; ++bucket)
    {
        accumulated += metric->histogram[bucket];
        if (accumulated >= target)
            return ((uint64_t)bucket + 1U) *
                TRANSPORT_LATENCY_BUCKET_WIDTH_NS;
    }
    return 0;
}

void simulith_transport_write_metrics_json(FILE *stream)
{
    if (!stream) return;
    pthread_mutex_lock(&transport_metric_mutex);
    fprintf(stream, "\"device_transactions\":[");
    for (size_t index = 0; index < transport_metrics_count; ++index)
    {
        const transport_metric_t *metric = &transport_metrics[index];
        fprintf(stream, "%s{\"name\":\"", index == 0 ? "" : ",");
        for (const unsigned char *character = (const unsigned char *)metric->name;
             *character; ++character)
        {
            if (*character == '\"' || *character == '\\') fputc('\\', stream);
            fputc(*character, stream);
        }
        fprintf(stream, "\",\"count\":%" PRIu64 ",\"errors\":%" PRIu64
                ",\"latency_us\":{\"mean\":%.3f,\"p50\":%" PRIu64
                ",\"p95\":%" PRIu64 ",\"max\":%.3f,"
                "\"resolution\":5,\"histogram_max\":10240,"
                "\"histogram_overflows\":%" PRIu64 "}}",
                metric->count, metric->errors,
                (double)metric->total_ns / (double)metric->count / 1000.0,
                transport_metric_percentile(metric, 50) / 1000,
                transport_metric_percentile(metric, 95) / 1000,
                (double)metric->maximum_ns / 1000.0,
                metric->histogram_overflows);
    }
    fprintf(stream, "]");
    pthread_mutex_unlock(&transport_metric_mutex);
}

static void *transport_context_acquire(void)
{
    pthread_mutex_lock(&transport_context_mutex);
    if (!transport_shared_context)
        transport_shared_context = zmq_ctx_new();
    void *context = transport_shared_context;
    if (context)
        transport_context_references++;
    pthread_mutex_unlock(&transport_context_mutex);
    return context;
}

static void transport_context_release(void *context)
{
    if (!context) return;
    pthread_mutex_lock(&transport_context_mutex);
    if (context == transport_shared_context && transport_context_references != 0U)
    {
        transport_context_references--;
        if (transport_context_references == 0U)
        {
            (void)zmq_ctx_term(transport_shared_context);
            transport_shared_context = NULL;
        }
    }
    pthread_mutex_unlock(&transport_context_mutex);
}

static void put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

static uint32_t get_u32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) | (uint32_t)src[3];
}

static void put_u64(uint8_t *dst, uint64_t value)
{
    put_u32(dst, (uint32_t)(value >> 32));
    put_u32(dst + 4, (uint32_t)value);
}

static uint64_t get_u64(const uint8_t *src)
{
    return ((uint64_t)get_u32(src) << 32) | get_u32(src + 4);
}

static int64_t monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void transport_cleanup_sockets(transport_port_t *port)
{
    if (port->zmq_sock)
    {
        zmq_close(port->zmq_sock);
        port->zmq_sock = NULL;
    }
    if (port->zmq_ctx)
    {
        transport_context_release(port->zmq_ctx);
        port->zmq_ctx = NULL;
    }
}

int simulith_transport_init(transport_port_t *port)
{
    if (!port) return SIMULITH_TRANSPORT_ERROR;
    if (port->init == SIMULITH_TRANSPORT_INITIALIZED) return SIMULITH_TRANSPORT_SUCCESS;

    port->zmq_ctx = transport_context_acquire();
    if (!port->zmq_ctx) {
        simulith_log("simulith_transport_init: Failed to create ZMQ context\n");
        return SIMULITH_TRANSPORT_ERROR;
    }
    port->zmq_sock = zmq_socket(port->zmq_ctx, ZMQ_PAIR);
    if (!port->zmq_sock) {
        simulith_log("simulith_transport_init: Failed to create ZMQ socket\n");
        transport_cleanup_sockets(port);
        return SIMULITH_TRANSPORT_ERROR;
    }

    /* Raise HWM to prevent silent drops at high FTRT speeds. Default is 1000
     * which fills in tens of milliseconds at 256× real-time rates. */
    int hwm = 65536;
    int linger = 0;
    zmq_setsockopt(port->zmq_sock, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(port->zmq_sock, ZMQ_RCVHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(port->zmq_sock, ZMQ_LINGER, &linger, sizeof(linger));
    if (strlen(port->name) > 0) {
        zmq_setsockopt(port->zmq_sock, ZMQ_IDENTITY, port->name, strlen(port->name));
    }
    int rc;
    if (port->is_server) {
        rc = zmq_bind(port->zmq_sock, port->address);
        if (rc != 0) {
            simulith_log("simulith_transport_init: Failed to bind to %s\n", port->address);
            transport_cleanup_sockets(port);
            return SIMULITH_TRANSPORT_ERROR;
        }
        simulith_log("simulith_transport_init: Bound to %s as '%s'\n", port->address, port->name);
    } else {
        rc = zmq_connect(port->zmq_sock, port->address);
        if (rc != 0) {
            simulith_log("simulith_transport_init: Failed to connect to %s\n", port->address);
            transport_cleanup_sockets(port);
            return SIMULITH_TRANSPORT_ERROR;
        }
        simulith_log("simulith_transport_init: Connected to %s as '%s'\n", port->address, port->name);
    }
    port->init = SIMULITH_TRANSPORT_INITIALIZED;
    port->next_transaction_id = 0;

    /* Initialize RX buffer */
    port->rx_buf_len = 0;
    return SIMULITH_TRANSPORT_SUCCESS;
}

int simulith_transport_request(transport_port_t *port, const uint8_t *data, size_t len,
                               int timeout_ms)
{
    uint8_t frame[TRANSPORT_REQUEST_HEADER_SIZE + SIMULITH_TRANSPORT_BUFFER_SIZE];
    int64_t start_ms;

    if (!port || !data || len == 0 || port->init != SIMULITH_TRANSPORT_INITIALIZED ||
        len > SIMULITH_TRANSPORT_BUFFER_SIZE || timeout_ms < 0) {
        simulith_log("simulith_transport_request: Invalid request\n");
        return SIMULITH_TRANSPORT_ERROR;
    }
    uint64_t request_start_ns = monotonic_ns();

    uint64_t transaction_id = ++port->next_transaction_id;
    if (transaction_id == 0)
        transaction_id = ++port->next_transaction_id;

    put_u32(frame, TRANSPORT_REQUEST_MAGIC);
    frame[4] = TRANSPORT_REQUEST_VERSION;
    frame[5] = TRANSPORT_REQUEST_TYPE;
    frame[6] = 0;
    frame[7] = 0;
    put_u64(frame + 8, transaction_id);
    put_u32(frame + 16, (uint32_t)len);
    memcpy(frame + TRANSPORT_REQUEST_HEADER_SIZE, data, len);

    int sent = zmq_send(port->zmq_sock, frame, TRANSPORT_REQUEST_HEADER_SIZE + len, ZMQ_DONTWAIT);
    if (sent != (int)(TRANSPORT_REQUEST_HEADER_SIZE + len)) {
        simulith_log("simulith_transport_request: Failed to send request on %s\n", port->name);
        transport_record_request(port->name, request_start_ns, 0);
        return SIMULITH_TRANSPORT_ERROR;
    }

    start_ms = monotonic_ms();
    for (;;) {
        int64_t elapsed_ms = monotonic_ms() - start_ms;
        int remaining_ms = timeout_ms - (int)elapsed_ms;
        if (remaining_ms < 0)
            break;

        zmq_pollitem_t item = {port->zmq_sock, 0, ZMQ_POLLIN, 0};
        int rc = zmq_poll(&item, 1, remaining_ms);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (rc == 0)
            break;

        zmq_msg_t message;
        zmq_msg_init(&message);
        int received = zmq_msg_recv(&message, port->zmq_sock, ZMQ_DONTWAIT);
        if (received < 0) {
            zmq_msg_close(&message);
            continue;
        }

        const uint8_t *reply = zmq_msg_data(&message);
        size_t reply_len = (size_t)received;
        int is_ack = reply_len == TRANSPORT_ACK_SIZE &&
            get_u32(reply) == TRANSPORT_REQUEST_MAGIC &&
            reply[4] == TRANSPORT_REQUEST_VERSION && reply[5] == TRANSPORT_ACK_TYPE;
        if (!is_ack) {
            size_t available = sizeof(port->rx_buf) - port->rx_buf_len;
            if (reply_len > available) {
                simulith_log("simulith_transport_request: Response buffer overflow on %s\n",
                             port->name);
                zmq_msg_close(&message);
                transport_record_request(port->name, request_start_ns, 0);
                return SIMULITH_TRANSPORT_ERROR;
            }
            memcpy(port->rx_buf + port->rx_buf_len, reply, reply_len);
            port->rx_buf_len += reply_len;
            zmq_msg_close(&message);
            continue;
        }
        if (get_u64(reply + 8) != transaction_id) {
            simulith_log("simulith_transport_request: Stale acknowledgement on %s\n", port->name);
            zmq_msg_close(&message);
            continue;
        }
        int32_t completion_status = (int32_t)get_u32(reply + 16);
        zmq_msg_close(&message);
        if (completion_status != SIMULITH_TRANSPORT_SUCCESS) {
            simulith_log("simulith_transport_request: Device rejected request on %s\n", port->name);
            transport_record_request(port->name, request_start_ns, 0);
            return SIMULITH_TRANSPORT_ERROR;
        }
        simulith_log("  REQUEST[%s]: %zu bytes completed\n", port->name, len);
        transport_record_request(port->name, request_start_ns, 1);
        return (int)len;
    }

    simulith_log("simulith_transport_request: Timed out waiting for %s completion\n", port->name);
    transport_record_request(port->name, request_start_ns, 0);
    return SIMULITH_TRANSPORT_ERROR;
}

int simulith_transport_receive_request(transport_port_t *port, uint8_t *data, size_t max_len,
                                       uint64_t *transaction_id)
{
    if (!port || !data || !transaction_id || port->init != SIMULITH_TRANSPORT_INITIALIZED) {
        simulith_log("simulith_transport_receive_request: Invalid transport port\n");
        return SIMULITH_TRANSPORT_ERROR;
    }

    zmq_msg_t msg;
    zmq_msg_init(&msg);
    int received = zmq_msg_recv(&msg, port->zmq_sock, ZMQ_DONTWAIT);
    if (received < 0) {
        zmq_msg_close(&msg);
        return errno == EAGAIN ? 0 : SIMULITH_TRANSPORT_ERROR;
    }

    const uint8_t *frame = zmq_msg_data(&msg);
    size_t frame_len = (size_t)received;
    *transaction_id = 0;

    if (frame_len >= TRANSPORT_REQUEST_HEADER_SIZE &&
        get_u32(frame) == TRANSPORT_REQUEST_MAGIC &&
        frame[4] == TRANSPORT_REQUEST_VERSION && frame[5] == TRANSPORT_REQUEST_TYPE) {
        size_t payload_len = get_u32(frame + 16);
        uint64_t id = get_u64(frame + 8);
        if (id == 0 || payload_len != frame_len - TRANSPORT_REQUEST_HEADER_SIZE ||
            payload_len > max_len) {
            simulith_log("simulith_transport_receive_request: Malformed request on %s\n", port->name);
            zmq_msg_close(&msg);
            return SIMULITH_TRANSPORT_ERROR;
        }
        memcpy(data, frame + TRANSPORT_REQUEST_HEADER_SIZE, payload_len);
        *transaction_id = id;
        zmq_msg_close(&msg);
        return (int)payload_len;
    }

    /* Raw messages remain supported for test tools and transport compatibility. */
    if (frame_len > max_len) {
        simulith_log("simulith_transport_receive_request: Request is too large on %s\n", port->name);
        zmq_msg_close(&msg);
        return SIMULITH_TRANSPORT_ERROR;
    }
    memcpy(data, frame, frame_len);
    zmq_msg_close(&msg);
    return (int)frame_len;
}

int simulith_transport_wait_for_request(transport_port_t *const ports[],
                                        size_t port_count, int interrupt_fd)
{
    zmq_pollitem_t items[SIMULITH_TRANSPORT_MAX_WAIT_PORTS + 1U];

    if (!ports || port_count == 0U ||
        port_count > SIMULITH_TRANSPORT_MAX_WAIT_PORTS || interrupt_fd < 0)
        return SIMULITH_TRANSPORT_ERROR;

    memset(items, 0, sizeof(items));
    for (size_t index = 0; index < port_count; ++index)
    {
        if (!ports[index] ||
            ports[index]->init != SIMULITH_TRANSPORT_INITIALIZED ||
            !ports[index]->zmq_sock)
            return SIMULITH_TRANSPORT_ERROR;
        items[index].socket = ports[index]->zmq_sock;
        items[index].events = ZMQ_POLLIN;
    }
    items[port_count].fd = interrupt_fd;
    items[port_count].events = ZMQ_POLLIN;

    int status;
    do
    {
        status = zmq_poll(items, (int)port_count + 1, -1);
    } while (status < 0 && errno == EINTR);
    if (status < 0)
        return SIMULITH_TRANSPORT_ERROR;

    /* COMMIT wins if transport and interruption become ready together. The
     * request stays queued and cannot be consumed outside EXECUTE. eventfd
     * reads clear its accumulated wake count. */
    if ((items[port_count].revents & ZMQ_POLLIN) != 0)
    {
        uint64_t wake_count;
        while (read(interrupt_fd, &wake_count, sizeof(wake_count)) < 0 &&
               errno == EINTR)
        {
        }
        return 0;
    }
    for (size_t index = 0; index < port_count; ++index)
    {
        if ((items[index].revents & ZMQ_POLLIN) != 0)
            return 1;
        if ((items[index].revents & ZMQ_POLLERR) != 0)
            return SIMULITH_TRANSPORT_ERROR;
    }
    return SIMULITH_TRANSPORT_ERROR;
}

int simulith_transport_complete_request(transport_port_t *port, uint64_t transaction_id,
                                        int status)
{
    uint8_t ack[TRANSPORT_ACK_SIZE] = {0};

    if (!port || port->init != SIMULITH_TRANSPORT_INITIALIZED)
        return SIMULITH_TRANSPORT_ERROR;
    if (transaction_id == 0)
        return SIMULITH_TRANSPORT_SUCCESS;

    put_u32(ack, TRANSPORT_REQUEST_MAGIC);
    ack[4] = TRANSPORT_REQUEST_VERSION;
    ack[5] = TRANSPORT_ACK_TYPE;
    put_u64(ack + 8, transaction_id);
    put_u32(ack + 16, (uint32_t)status);
    int sent = zmq_send(port->zmq_sock, ack, sizeof(ack), ZMQ_DONTWAIT);
    if (sent != (int)sizeof(ack)) {
        simulith_log("simulith_transport_complete_request: Failed on %s\n", port->name);
        return SIMULITH_TRANSPORT_ERROR;
    }
    return SIMULITH_TRANSPORT_SUCCESS;
}

int simulith_transport_send(transport_port_t *port, const uint8_t *data, size_t len)
{
    if (!port || (!data && len > 0) || port->init != SIMULITH_TRANSPORT_INITIALIZED) {
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

static int transport_buffer_next_message(transport_port_t *port, int timeout_ms)
{
    int64_t deadline_ms = monotonic_ms() + timeout_ms;
    for (;;)
    {
        int64_t remaining = deadline_ms - monotonic_ms();
        int poll_timeout = remaining > 0 ? (int)remaining : 0;
        zmq_pollitem_t item = {port->zmq_sock, 0, ZMQ_POLLIN, 0};
        int rc = zmq_poll(&item, 1, poll_timeout);
        if (rc < 0)
        {
            if (errno == EINTR) continue;
            return SIMULITH_TRANSPORT_ERROR;
        }
        if (rc == 0 || !(item.revents & ZMQ_POLLIN)) return 0;

        zmq_msg_t message;
        zmq_msg_init(&message);
        int received = zmq_msg_recv(&message, port->zmq_sock, ZMQ_DONTWAIT);
        if (received < 0)
        {
            zmq_msg_close(&message);
            if (errno == EAGAIN && monotonic_ms() <= deadline_ms) continue;
            return SIMULITH_TRANSPORT_ERROR;
        }
        const uint8_t *data = zmq_msg_data(&message);
        size_t size = (size_t)received;
        int is_ack = size == TRANSPORT_ACK_SIZE &&
            get_u32(data) == TRANSPORT_REQUEST_MAGIC &&
            data[4] == TRANSPORT_REQUEST_VERSION && data[5] == TRANSPORT_ACK_TYPE;
        if (is_ack)
        {
            simulith_log("simulith_transport: Ignoring stale acknowledgement on %s\n",
                         port->name);
            zmq_msg_close(&message);
            if (monotonic_ms() <= deadline_ms) continue;
            return 0;
        }
        size_t space = sizeof(port->rx_buf) - port->rx_buf_len;
        if (size > space)
        {
            simulith_log("  RX[%s]: Buffer overflow, dropping %zu bytes\n",
                         port->name, size);
            zmq_msg_close(&message);
            return 0;
        }
        memcpy(port->rx_buf + port->rx_buf_len, data, size);
        port->rx_buf_len += size;
        zmq_msg_close(&message);
        simulith_log("  RX[%s]: %zu bytes buffered\n", port->name, size);
        return 1;
    }
}

int simulith_transport_receive_exact(transport_port_t *port, uint8_t *data,
                                     size_t len, int timeout_ms)
{
    if (!port || (!data && len != 0U) ||
        port->init != SIMULITH_TRANSPORT_INITIALIZED ||
        len > sizeof(port->rx_buf) || timeout_ms < 0)
        return SIMULITH_TRANSPORT_ERROR;
    if (len == 0U) return 0;

    int64_t deadline_ms = monotonic_ms() + timeout_ms;
    while (port->rx_buf_len < len)
    {
        int64_t remaining = deadline_ms - monotonic_ms();
        if (remaining < 0) return SIMULITH_TRANSPORT_ERROR;
        int result = transport_buffer_next_message(port, (int)remaining);
        if (result < 0) return SIMULITH_TRANSPORT_ERROR;
        if (result == 0 && port->rx_buf_len < len)
            return SIMULITH_TRANSPORT_ERROR;
    }
    return simulith_transport_receive(port, data, len);
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
    return transport_buffer_next_message(port, 0);
}

int simulith_transport_flush(transport_port_t *port)
{
    if (!port || port->init != SIMULITH_TRANSPORT_INITIALIZED) {
        simulith_log("simulith_transport_flush: Uninitialized transport port\n");
        return SIMULITH_TRANSPORT_ERROR;
    }
    /* Drain internal rx buffer */
    port->rx_buf_len = 0;
    /* Drain pending requests, responses, and acknowledgements. */
    for (;;) {
        zmq_msg_t msg;
        zmq_msg_init(&msg);
        int rc = zmq_msg_recv(&msg, port->zmq_sock, ZMQ_DONTWAIT);
        zmq_msg_close(&msg);
        if (rc < 0) break;
    }
    return SIMULITH_TRANSPORT_SUCCESS;
}

int simulith_transport_close(transport_port_t *port)
{
    if (!port || port->init != SIMULITH_TRANSPORT_INITIALIZED) {
        return SIMULITH_TRANSPORT_ERROR;
    }
    transport_cleanup_sockets(port);
    port->init = 0;
    simulith_log("Transport port %s closed\n", port->name);
    return SIMULITH_TRANSPORT_SUCCESS;
}
