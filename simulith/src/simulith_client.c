#include "simulith.h"
#include "simulith_shared_barrier.h"
#include <ctype.h>
#include <signal.h>

static void    *client_context = NULL;
static void    *subscriber     = NULL;
static void    *requester      = NULL;
static char     client_id[64];
static uint64_t update_rate_ns = 0;
static uint32_t client_phase_mask = SIMULITH_PHASE_MASK_COMMIT;
static volatile sig_atomic_t client_stop_requested = 0;
static uint64_t last_received_sequence = UINT64_MAX;
static uint64_t last_received_time_ns = 0;
static simulith_phase_t last_received_phase = 0;
static int last_received_phase_completed = 0;
static int use_shared_barrier = 0;
static simulith_shared_barrier_t shared_barrier = {.fd = -1, .slot = -1};

static int valid_client_id(const char *id)
{
    if (!id || id[0] == '\0' || strlen(id) >= sizeof(client_id))
        return 0;
    for (const unsigned char *cursor = (const unsigned char *)id; *cursor; ++cursor)
        if (!isalnum(*cursor) && *cursor != '-' && *cursor != '_' && *cursor != '.')
            return 0;
    return 1;
}

static void close_client_resources(void)
{
    if (subscriber)
        zmq_close(subscriber);
    if (requester)
        zmq_close(requester);
    if (client_context)
        zmq_ctx_term(client_context);
    subscriber     = NULL;
    requester      = NULL;
    simulith_shared_barrier_close(&shared_barrier);
    client_context = NULL;
}

int simulith_client_init(const char *pub_addr, const char *rep_addr, const char *id, uint64_t rate_ns)
{
    client_stop_requested = 0;
    last_received_sequence = UINT64_MAX;
    last_received_time_ns = 0;
    last_received_phase = 0;
    last_received_phase_completed = 0;
    client_phase_mask = strcmp(id ? id : "", "shire-fsw") == 0 ?
        SIMULITH_PHASE_MASK_EXECUTE : SIMULITH_PHASE_MASK_COMMIT;
    const char *sync_transport = getenv("SIMULITH_SYNC_TRANSPORT");
    use_shared_barrier = rep_addr && strncmp(rep_addr, "ipc://", 6) == 0 &&
        (!sync_transport || strcmp(sync_transport, "zmq") != 0);
    // Validate parameters
    if (!pub_addr || !rep_addr || !id)
    {
        simulith_log("Invalid parameters: addresses and id cannot be NULL\n");
        return -1;
    }

    if (!valid_client_id(id))
    {
        simulith_log("Invalid client ID: use 1-63 letters, digits, '.', '_', or '-'\n");
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
        close_client_resources();
        return -1;
    }
    int linger = 0;
    zmq_setsockopt(subscriber, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0); // Subscribe to all messages
    /* A finite timeout makes shutdown observable without burning a core. */
    int tick_timeout_ms = 100;
    zmq_setsockopt(subscriber, ZMQ_RCVTIMEO, &tick_timeout_ms, sizeof(tick_timeout_ms));

    requester = zmq_socket(client_context, ZMQ_REQ);
    if (!requester || zmq_connect(requester, rep_addr) != 0)
    {
        perror("Requester socket setup failed");
        close_client_resources();
        return -1;
    }
    zmq_setsockopt(requester, ZMQ_LINGER, &linger, sizeof(linger));

    simulith_log("Simulith client [%s] initialized with update rate %lu ns\n", client_id, update_rate_ns);
    return 0;
}

int simulith_client_configure_phases(uint32_t phase_mask)
{
    const uint32_t valid_mask = SIMULITH_PHASE_MASK_PREPARE |
                                SIMULITH_PHASE_MASK_EXECUTE |
                                SIMULITH_PHASE_MASK_COMMIT;
    if (!subscriber || phase_mask == 0 || (phase_mask & ~valid_mask) != 0)
        return -1;
    client_phase_mask = phase_mask;
    return 0;
}

int simulith_client_handshake(void)
{
    // Format READY message with client ID
    char ready_msg[96];
    snprintf(ready_msg, sizeof(ready_msg), "READY %s %u %s", client_id,
             (unsigned)client_phase_mask, use_shared_barrier ? "shared" : "zmq");
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

    if ((size_t)size >= sizeof(buffer))
    {
        simulith_log("Oversized reply to READY\n");
        return -1;
    }
    buffer[size] = '\0';

    // Check for duplicate ID rejection
    if (strcmp(buffer, "DUP_ID") == 0)
    {
        simulith_log("Handshake failed - duplicate client ID: %s\n", client_id);
        return -1;
    }

    int slot = -1;
    int consumed = 0;
    int ack_has_slot = sscanf(buffer, "ACK %d %n", &slot, &consumed) == 1 &&
        buffer[consumed] == '\0';
    if (strcmp(buffer, ack_msg) != 0 && !ack_has_slot)
    {
        simulith_log("Unexpected reply to READY: %s\n", buffer);
        return -1;
    }

    if (use_shared_barrier)
    {
        if (!ack_has_slot ||
            simulith_shared_barrier_connect(&shared_barrier, slot) != 0)
        {
            simulith_log("Unable to connect shared synchronization barrier\n");
            return -1;
        }
    }

    // Reset timeout to infinite for normal operation
    timeout = -1;
    zmq_setsockopt(requester, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));

    simulith_log("Handshake complete with server.\n");
    return 0;
}

void simulith_client_run_loop(simulith_tick_callback on_tick)
{
    while (!client_stop_requested)
    {
        uint64_t time_ns;
        uint64_t sequence;
        simulith_phase_t phase;
        if (simulith_client_receive_phase(&time_ns, &sequence, &phase) != 0)
            continue;

        if (client_stop_requested)
            break;

        if (phase != SIMULITH_PHASE_COMMIT)
            continue;
        if (on_tick)
            on_tick(time_ns);
        if (simulith_client_complete_tick(sequence, phase) != 0)
            simulith_log("Client %s failed to complete tick %lu\n", client_id,
                         (unsigned long)sequence);
    }
}

void simulith_client_run_phased_loop(simulith_phase_callback on_prepare,
                                     simulith_phase_callback on_execute,
                                     simulith_phase_callback on_commit)
{
    while (!client_stop_requested)
    {
        uint64_t time_ns;
        uint64_t sequence;
        simulith_phase_t phase;
        if (simulith_client_receive_phase(&time_ns, &sequence, &phase) != 0)
            continue;
        simulith_phase_callback callback = NULL;
        if (phase == SIMULITH_PHASE_PREPARE)
            callback = on_prepare;
        else if (phase == SIMULITH_PHASE_EXECUTE)
            callback = on_execute;
        else if (phase == SIMULITH_PHASE_COMMIT)
            callback = on_commit;
        else
            continue;

        if (callback && callback(sequence, time_ns) != 0)
        {
            simulith_log("Client %s failed phase %u for tick %lu; completion withheld\n",
                         client_id, (unsigned)phase,
                         (unsigned long)sequence);
            return;
        }
        if (simulith_client_complete_tick(sequence, phase) != 0)
        {
            simulith_log("Client %s failed phase %u for tick %lu\n", client_id,
                         (unsigned)phase, (unsigned long)sequence);
            return;
        }
    }
}

void simulith_client_request_stop(void)
{
    client_stop_requested = 1;
    if (use_shared_barrier)
        simulith_shared_barrier_interrupt(&shared_barrier);
}

int simulith_client_wait_for_tick(uint64_t* tick_time_ns)
{
    if (!tick_time_ns || !subscriber || !requester)
    {
        return -1;
    }

    uint64_t sequence;
    if (simulith_client_receive_tick(tick_time_ns, &sequence) != 0)
        return -1;

    return simulith_client_complete_tick(sequence, SIMULITH_PHASE_EXECUTE);
}

int simulith_client_receive_tick(uint64_t *tick_time_ns, uint64_t *sequence)
{
    simulith_phase_t phase;
    do
    {
        if (simulith_client_receive_phase(tick_time_ns, sequence, &phase) != 0)
            return -1;
    }
    while (phase != SIMULITH_PHASE_EXECUTE);
    return 0;
}

int simulith_client_receive_phase(uint64_t *tick_time_ns, uint64_t *sequence,
                                  simulith_phase_t *phase)
{
    if (!tick_time_ns || !sequence || !phase || !subscriber)
        return -1;

    simulith_tick_message_t tick;
    if (use_shared_barrier)
    {
        uint32_t shared_phase;
        int status = simulith_shared_barrier_receive(
            &shared_barrier, sequence, tick_time_ns, &shared_phase);
        if (status != 0)
        {
            if (status == 1)
            {
                client_stop_requested = 1;
                *phase = SIMULITH_PHASE_STOP;
            }
            return status;
        }
        tick.magic = SIMULITH_PROTOCOL_MAGIC;
        tick.version = SIMULITH_PROTOCOL_VERSION;
        tick.phase = (uint16_t)shared_phase;
        tick.sequence = *sequence;
        tick.time_ns = *tick_time_ns;
    }
    else
    {
        for (;;)
        {
            int recv_bytes = zmq_recv(subscriber, &tick, sizeof(tick), 0);
            if (recv_bytes == (int)sizeof(tick))
                break;
            if (recv_bytes < 0 && errno == EAGAIN && !client_stop_requested)
                continue;
            return -1;
        }
    }

    if (tick.magic != SIMULITH_PROTOCOL_MAGIC ||
        tick.version != SIMULITH_PROTOCOL_VERSION ||
        tick.phase < SIMULITH_PHASE_PREPARE || tick.phase > SIMULITH_PHASE_STOP)
    {
        simulith_log("Client %s rejected invalid tick frame\n", client_id);
        return -1;
    }
    if (tick.phase == SIMULITH_PHASE_STOP)
    {
        client_stop_requested = 1;
        *tick_time_ns = tick.time_ns;
        *sequence = tick.sequence;
        *phase = SIMULITH_PHASE_STOP;
        return 1;
    }
    if (last_received_sequence != UINT64_MAX &&
        (tick.sequence < last_received_sequence ||
         (tick.sequence == last_received_sequence && tick.phase <= (uint16_t)last_received_phase)))
    {
        simulith_log("Client %s rejected stale/duplicate tick %lu\n", client_id,
                     (unsigned long)tick.sequence);
        return -1;
    }
    if (last_received_sequence != UINT64_MAX &&
        ((tick.sequence == last_received_sequence &&
          tick.time_ns != last_received_time_ns) ||
         (tick.sequence > last_received_sequence &&
          (tick.sequence != last_received_sequence + 1 ||
           tick.time_ns <= last_received_time_ns ||
           tick.time_ns - last_received_time_ns != update_rate_ns))))
    {
        simulith_log("Client %s rejected non-monotonic time %lu for tick %lu\n",
                     client_id, (unsigned long)tick.time_ns,
                     (unsigned long)tick.sequence);
        return -1;
    }

    last_received_sequence = tick.sequence;
    last_received_time_ns = tick.time_ns;
    last_received_phase = (simulith_phase_t)tick.phase;
    last_received_phase_completed = 0;
    *tick_time_ns = tick.time_ns;
    *sequence = tick.sequence;
    *phase = last_received_phase;
    return 0;
}

int simulith_client_complete_tick(uint64_t sequence, simulith_phase_t phase)
{
    if (phase < SIMULITH_PHASE_PREPARE || phase > SIMULITH_PHASE_COMMIT ||
        (!requester && !use_shared_barrier) || sequence != last_received_sequence || phase != last_received_phase ||
        last_received_phase_completed ||
        (client_phase_mask & SIMULITH_PHASE_BIT(phase)) == 0)
        return -1;

    if (use_shared_barrier)
    {
        int status = simulith_shared_barrier_complete(&shared_barrier, sequence,
                                                      (uint32_t)phase);
        if (status == 0)
            last_received_phase_completed = 1;
        return status;
    }

    char completion[128];
    int length = snprintf(completion, sizeof(completion), "COMPLETE %lu %u %s",
                          (unsigned long)sequence, (unsigned)phase, client_id);
    if (length <= 0 || (size_t)length >= sizeof(completion) ||
        zmq_send(requester, completion, (size_t)length, 0) == -1)
        return -1;

    char reply[32] = {0};
    int reply_size = zmq_recv(requester, reply, sizeof(reply) - 1, 0);
    if (reply_size < 0)
        return -1;
    reply[reply_size] = '\0';
    if (strcmp(reply, "ACK") != 0)
        return -1;
    last_received_phase_completed = 1;
    return 0;
}

void simulith_client_shutdown(void)
{
    close_client_resources();
    simulith_log("Simulith client [%s] shut down\n", client_id);
}
