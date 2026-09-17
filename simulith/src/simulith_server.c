#include "simulith.h"
#include "simulith_shared_barrier.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define MAX_CLIENTS 32
#define MAX_LATENCY_SAMPLES 10000

typedef struct
{
    char id[64];
    int  responded;
    uint32_t phase_mask;
    uint64_t completion_count[4];
    uint64_t measured_completion_count[4];
    uint64_t completion_latency_max_ns[4];
    uint64_t completion_latency_samples[4][MAX_LATENCY_SAMPLES];
    size_t completion_latency_sample_count[4];
} ClientState;

static void       *server_context             = NULL;
static void       *publisher                  = NULL;
static void       *responder                  = NULL;
static simulith_shared_barrier_t shared_barrier = {.fd = -1, .slot = -1};
static int          use_shared_barrier          = 0;
static uint64_t    current_time_ns            = 0;
static uint64_t    tick_interval_ns           = 0;
static int         expected_clients           = 0;
static ClientState client_states[MAX_CLIENTS] = {0};
static uint64_t     current_sequence           = 0;
static double       configured_speed           = 1.0;
static uint64_t     configured_duration_ns     = 0;
static uint64_t     configured_warmup_ns       = 0;
static char         configured_metrics_path[512] = {0};
static uint64_t     completed_ticks            = 0;
static uint64_t     measured_ticks_completed   = 0;
static uint64_t     protocol_errors            = 0;
static uint64_t     duplicate_completions      = 0;
static uint64_t     stale_completions          = 0;
static uint64_t     future_completions         = 0;
static uint64_t     tick_latency_total_ns      = 0;
static uint64_t     tick_latency_min_ns        = UINT64_MAX;
static uint64_t     tick_latency_max_ns        = 0;
static uint64_t     run_start_real_ns          = 0;
static uint64_t     measurement_start_real_ns  = 0;
static uint64_t     measurement_end_real_ns    = 0;
static uint64_t     active_tick_start_ns       = 0;
static uint64_t     active_phase_start_ns      = 0;
static simulith_phase_t current_phase           = SIMULITH_PHASE_PREPARE;
static uint64_t     completion_count           = 0;
static uint64_t     tick_latency_samples[MAX_LATENCY_SAMPLES];
static size_t       tick_latency_sample_count  = 0;
static double       g_attempted_speed          = 1.0;
static uint64_t     g_last_log_real_ns         = 0;
static uint64_t     g_last_log_sim_ns          = 0;
static uint64_t     watchdog_interval_ns       = 1000000000ULL;

/* Ground-command interface: backdoor listener + status-telemetry sender. */
static int                backdoor_sock         = -1;
static int                status_sock           = -1;
static struct sockaddr_in status_dest_addr;
static int                status_dest_valid      = 0;

/* Test/debug helper: request server shutdown from other threads. */
static volatile sig_atomic_t simulith_server_stop_requested = 0;

static uint64_t monotonic_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

static const char *phase_name(simulith_phase_t phase)
{
    switch (phase)
    {
        case SIMULITH_PHASE_PREPARE: return "prepare";
        case SIMULITH_PHASE_EXECUTE: return "execute";
        case SIMULITH_PHASE_COMMIT: return "commit";
        case SIMULITH_PHASE_STOP: return "stop";
        default: return "invalid";
    }
}

static void sleep_for_microseconds(long microseconds)
{
    struct timespec delay = {
        .tv_sec = (time_t)(microseconds / 1000000L),
        .tv_nsec = (microseconds % 1000000L) * 1000L
    };
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
    {
    }
}

static void close_server_resources(void)
{
    if (publisher)
        zmq_close(publisher);
    if (responder)
        zmq_close(responder);
    if (server_context)
        zmq_ctx_term(server_context);
    publisher      = NULL;
    responder      = NULL;
    simulith_shared_barrier_close(&shared_barrier);
    server_context = NULL;

    if (backdoor_sock >= 0)
        close(backdoor_sock);
    backdoor_sock = -1;
    if (status_sock >= 0)
        close(status_sock);
    status_sock = -1;
    status_dest_valid = 0;
}

static int is_client_id_taken(const char *id)
{
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (client_states[i].id[0] != '\0' && strcmp(client_states[i].id, id) == 0)
        {
            simulith_log("Client ID '%s' is already in use\n", id);
            return 1;
        }
    }
    return 0;
}

static int valid_client_id(const char *id)
{
    if (!id || id[0] == '\0')
        return 0;
    for (const unsigned char *cursor = (const unsigned char *)id; *cursor; ++cursor)
        if (!isalnum(*cursor) && *cursor != '-' && *cursor != '_' && *cursor != '.')
            return 0;
    return 1;
}

int simulith_server_init(const char *pub_bind, const char *rep_bind, int client_count, uint64_t interval_ns)
{
    /* Clear any previous stop request so a fresh server run isn't short-circuited. */
    simulith_server_stop_requested = 0;

    // Validate parameters
    if (!pub_bind || !rep_bind)
    {
        simulith_log("Invalid server endpoint\n");
        return -1;
    }

    if (client_count <= 0 || client_count > MAX_CLIENTS)
    {
        simulith_log("Invalid client count: %d (must be between 1 and %d)\n", client_count, MAX_CLIENTS);
        return -1;
    }

    if (interval_ns == 0)
    {
        simulith_log("Invalid interval: must be greater than 0\n");
        return -1;
    }

    expected_clients = client_count;
    tick_interval_ns = interval_ns;
    current_time_ns = 0;
    current_sequence = 0;
    completed_ticks = 0;
    measured_ticks_completed = 0;
    completion_count = 0;
    tick_latency_sample_count = 0;
    protocol_errors = duplicate_completions = stale_completions = future_completions = 0;
    tick_latency_total_ns = tick_latency_max_ns = 0;
    tick_latency_min_ns = UINT64_MAX;
    run_start_real_ns = 0;
    measurement_start_real_ns = 0;
    measurement_end_real_ns = 0;
    g_last_log_real_ns = 0;
    g_last_log_sim_ns = 0;
    watchdog_interval_ns = 1000000000ULL;
    const char *watchdog_seconds = getenv("SIMULITH_WATCHDOG_SECONDS");
    if (watchdog_seconds && watchdog_seconds[0] != '\0')
    {
        char *end = NULL;
        double seconds = strtod(watchdog_seconds, &end);
        const double maximum_seconds = (double)UINT64_MAX / 1000000000.0;
        if (end && *end == '\0' && isfinite(seconds) && seconds > 0.0 &&
            seconds <= maximum_seconds)
            watchdog_interval_ns = (uint64_t)(seconds * 1000000000.0);
    }
    const char *sync_transport = getenv("SIMULITH_SYNC_TRANSPORT");
    use_shared_barrier = rep_bind && strncmp(rep_bind, "ipc://", 6) == 0 &&
        (!sync_transport || strcmp(sync_transport, "zmq") != 0);

    server_context = zmq_ctx_new();
    if (!server_context)
    {
        perror("zmq_ctx_new failed");
        return -1;
    }

    publisher = zmq_socket(server_context, ZMQ_PUB);
    if (!publisher || zmq_bind(publisher, pub_bind) != 0)
    {
        perror("Publisher socket setup failed");
        close_server_resources();
        return -1;
    }

    // Optimize ZMQ settings for performance
    int sndhwm = 1000;
    int linger = 0;
    zmq_setsockopt(publisher, ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
    zmq_setsockopt(publisher, ZMQ_LINGER, &linger, sizeof(linger));

    responder = zmq_socket(server_context, ZMQ_REP);
    if (!responder || zmq_bind(responder, rep_bind) != 0)
    {
        perror("Responder socket setup failed");
        close_server_resources();
        return -1;
    }

    /* Set a short receive timeout on the responder so the server loop can
     * periodically check for shutdown requests and avoid getting stuck in a
     * blocking recv during tests. */
    int recv_timeout_ms = 200;
    zmq_setsockopt(responder, ZMQ_RCVTIMEO, &recv_timeout_ms, sizeof(recv_timeout_ms));

    // Optimize responder settings
    int rcvhwm = 1000;
    zmq_setsockopt(responder, ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));
    zmq_setsockopt(responder, ZMQ_LINGER, &linger, sizeof(linger));

    if (use_shared_barrier && simulith_shared_barrier_create(&shared_barrier) != 0)
    {
        simulith_log("Unable to create shared synchronization barrier\n");
        close_server_resources();
        return -1;
    }

    // Initialize client states
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        client_states[i].id[0]     = '\0';
        client_states[i].responded = 0;
        client_states[i].phase_mask = 0;
        memset(client_states[i].completion_count, 0, sizeof(client_states[i].completion_count));
        memset(client_states[i].measured_completion_count, 0,
               sizeof(client_states[i].measured_completion_count));
        memset(client_states[i].completion_latency_max_ns, 0,
               sizeof(client_states[i].completion_latency_max_ns));
        memset(client_states[i].completion_latency_sample_count, 0,
               sizeof(client_states[i].completion_latency_sample_count));
    }

    simulith_log("Simulith server initialized. Clients expected: %d\n", expected_clients);
    return 0;
}

int simulith_server_configure(double speed, uint64_t duration_ns, const char *metrics_json_path)
{
    if (!isfinite(speed) || speed < 0.0)
        return -1;
    configured_speed = speed;
    configured_duration_ns = duration_ns;
    configured_metrics_path[0] = '\0';
    if (metrics_json_path && metrics_json_path[0] != '\0')
    {
        if (strlen(metrics_json_path) >= sizeof(configured_metrics_path))
            return -1;
        strcpy(configured_metrics_path, metrics_json_path);
    }
    return 0;
}

int simulith_server_configure_warmup(uint64_t warmup_ns)
{
    if (configured_duration_ns > 0 && warmup_ns >= configured_duration_ns)
        return -1;
    configured_warmup_ns = warmup_ns;
    return 0;
}

static void broadcast_phase(simulith_phase_t phase)
{
    simulith_tick_message_t tick = {
        .magic = SIMULITH_PROTOCOL_MAGIC,
        .version = SIMULITH_PROTOCOL_VERSION,
        .phase = phase,
        .sequence = current_sequence,
        .time_ns = current_time_ns
    };
    if (!use_shared_barrier)
        zmq_send(publisher, &tick, sizeof(tick), 0);
}

static void log_simulation_progress(void)
{
    static const uint64_t LOG_INTERVAL_NS = 10000000000ULL;

    if (current_time_ns - g_last_log_sim_ns >= LOG_INTERVAL_NS)
    {
        uint64_t now_real_ns = monotonic_ns();
        double sim_elapsed = (double)(current_time_ns - g_last_log_sim_ns) / 1e9;
        double real_elapsed = (g_last_log_real_ns > 0) ? ((double)(now_real_ns - g_last_log_real_ns) / 1e9) : 0.0;
        double actual_speed = (real_elapsed > 0.0) ? (sim_elapsed / real_elapsed) : 0.0;

        if (g_attempted_speed > 0.0)
            simulith_log("  Simulation time: %.3f seconds | Attempted speed: %.2fx | Actual: %.2fx\n",
                (double)current_time_ns / 1e9, g_attempted_speed, actual_speed);
        else
            simulith_log("  Simulation time: %.3f seconds | Attempted speed: max | Actual: %.2fx\n",
                (double)current_time_ns / 1e9, actual_speed);

        g_last_log_sim_ns = current_time_ns;
        g_last_log_real_ns = now_real_ns;
    }
}

#ifdef SIMULITH_TESTING
void simulith_server_broadcast_for_test(uint64_t time_ns)
{
    current_time_ns = time_ns;
    broadcast_phase(SIMULITH_PHASE_PREPARE);
    log_simulation_progress();
}
#endif

static int all_phase_participants_responded(void)
{
    for (int i = 0; i < expected_clients; ++i)
    {
        if ((client_states[i].phase_mask & SIMULITH_PHASE_BIT(current_phase)) != 0 &&
            !client_states[i].responded)
            return 0;
    }
    return 1;
}

static void reset_responses(void)
{
    for (int i = 0; i < expected_clients; ++i)
    {
        client_states[i].responded = 0;
    }
}

static void record_completion(int index)
{
    client_states[index].responded = 1;
    uint64_t latency_ns = monotonic_ns() - active_phase_start_ns;
    client_states[index].completion_count[current_phase]++;
    completion_count++;
    if (current_time_ns >= configured_warmup_ns)
    {
        client_states[index].measured_completion_count[current_phase]++;
        if (latency_ns > client_states[index].completion_latency_max_ns[current_phase])
            client_states[index].completion_latency_max_ns[current_phase] = latency_ns;
        if (client_states[index].completion_latency_sample_count[current_phase] < MAX_LATENCY_SAMPLES)
            client_states[index].completion_latency_samples[current_phase][
                client_states[index].completion_latency_sample_count[current_phase]++] = latency_ns;
    }
}

static uint32_t required_phase_mask(void)
{
    uint32_t mask = 0;
    for (int i = 0; i < expected_clients; ++i)
        if ((client_states[i].phase_mask & SIMULITH_PHASE_BIT(current_phase)) != 0)
            mask |= UINT32_C(1) << i;
    return mask;
}

static int record_shared_completions(uint32_t completed_mask)
{
    int recorded = 0;
    for (int i = 0; i < expected_clients; ++i)
        if ((completed_mask & (UINT32_C(1) << i)) != 0 &&
            (client_states[i].phase_mask & SIMULITH_PHASE_BIT(current_phase)) != 0 &&
            !client_states[i].responded)
        {
            record_completion(i);
            recorded++;
        }
    return recorded;
}

static const char *handle_completion(const char *message)
{
    unsigned long sequence = 0;
    unsigned phase = 0;
    char client_id[64] = {0};
    int consumed = 0;
    if (sscanf(message, "COMPLETE %lu %u %63s %n", &sequence, &phase,
               client_id, &consumed) != 3 || message[consumed] != '\0')
    {
        protocol_errors++;
        simulith_log("Rejected malformed completion: %s\n", message);
        return "ERR_PROTOCOL";
    }

    if ((uint64_t)sequence < current_sequence)
    {
        stale_completions++;
        simulith_log("Rejected stale completion from %s: got %lu, current %lu\n",
                     client_id, sequence, (unsigned long)current_sequence);
        return "ERR_STALE";
    }
    if ((uint64_t)sequence > current_sequence)
    {
        future_completions++;
        simulith_log("Rejected future completion from %s: got %lu, current %lu\n",
                     client_id, sequence, (unsigned long)current_sequence);
        return "ERR_FUTURE";
    }

    for (int i = 0; i < expected_clients; ++i)
    {
        if (client_states[i].id[0] != '\0' && strcmp(client_states[i].id, client_id) == 0)
        {
            if (phase != (unsigned)current_phase ||
                (client_states[i].phase_mask & SIMULITH_PHASE_BIT(current_phase)) == 0)
            {
                protocol_errors++;
                simulith_log("Rejected wrong phase from %s for tick %lu: got %u expected %u\n",
                             client_id, sequence, phase, (unsigned)current_phase);
                return "ERR_PHASE";
            }
            if (client_states[i].responded)
            {
                duplicate_completions++;
                simulith_log("Rejected duplicate completion from %s for tick %lu\n",
                             client_id, sequence);
                return "ERR_DUPLICATE";
            }
            record_completion(i);
            return "ACK";
        }
    }
    protocol_errors++;
    simulith_log("Completion received from unknown participant: %s\n", client_id);
    return "ERR_UNKNOWN";
}

/* Keep command interpretation independent of stdin so it is deterministic and
 * directly testable.  A non-zero return asks the caller to stop the server. */
static int process_cli_command(const char *command, int *paused, double *speed)
{
    char action[16] = {0};
    char argument[32] = {0};
    char extra[2] = {0};
    int fields = sscanf(command, " %15s %31s %1s", action, argument, extra);

    if (fields == 1 && strcmp(action, "p") == 0)
    {
        *paused = !*paused;
        printf(*paused ? "Simulation paused.\n" : "Simulation resumed.\n");
    }
    else if (fields == 1 && strcmp(action, "+") == 0)
    {
        if (*speed > 0.0)
        {
            *speed *= 2.0;
            if (*speed > 1024.0)
                *speed = 1024.0;
        }
        g_attempted_speed = *speed;
        if (*speed > 0.0)
            printf("Attempted simulation speed: %.2fx\n", *speed);
        else
            printf("Attempted simulation speed: max\n");
    }
    else if (fields == 1 && strcmp(action, "-") == 0)
    {
        if (*speed <= 0.0)
            *speed = 1024.0;
        else
            *speed /= 2.0;
        if (*speed < 0.015625)
            *speed = 0.015625;
        g_attempted_speed = *speed;
        printf("Attempted simulation speed: %.4fx\n", *speed);
    }
    else if (fields == 2 && strcmp(action, "speed") == 0)
    {
        double requested_speed = 0.0;
        if (strcmp(argument, "max") != 0)
        {
            char *end = NULL;
            errno = 0;
            requested_speed = strtod(argument, &end);
            if (errno != 0 || !end || *end != '\0' ||
                !isfinite(requested_speed) || requested_speed < 0.015625 ||
                requested_speed > 1024.0)
            {
                printf("Invalid speed. Use a factor from 0.015625 through 1024, or 'max'.\n");
                return 0;
            }
        }
        *speed = requested_speed;
        g_attempted_speed = *speed;
        if (*speed > 0.0)
            printf("Attempted simulation speed: %.2fx\n", *speed);
        else
            printf("Attempted simulation speed: max\n");
    }
    else if (fields == 1 && strcmp(action, "quit") == 0)
    {
        printf("Exiting simulation.\n");
        return 1;
    }
    else
    {
        printf("Unknown command. Use 'p', '+', '-', 'speed <factor|max>', or 'quit'.\n");
    }
    return 0;
}

#ifdef SIMULITH_TESTING
int simulith_server_process_cli_command_for_test(const char *command, int *paused, double *speed)
{
    return process_cli_command(command, paused, speed);
}
#endif

/* Ground-command backdoor: a UDP listener that accepts the same
 * MAGIC/target/cmd_id/payload framing as the director's backdoor
 * (simulith_director.c), so YAMCS can drive pause/play/speed the same way it
 * already drives other simulation-side backdoor commands. Pause/play are
 * explicit rather than a toggle so a duplicated or retried UDP datagram is a
 * safe no-op. */
static const uint8_t BACKDOOR_MAGIC[8] = { 'B', 'A', 'C', 'K', 'D', 'O', 'O', 'R' };
static const char *BACKDOOR_TARGET_NAME = "shire_server";

enum
{
    SERVER_BACKDOOR_CMD_PAUSE     = 0x0001,
    SERVER_BACKDOOR_CMD_PLAY      = 0x0002,
    SERVER_BACKDOOR_CMD_SET_SPEED = 0x0003
};

static int ensure_backdoor_socket(void)
{
    if (backdoor_sock >= 0)
        return 0;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
    {
        simulith_log("Unable to create server backdoor socket: %s\n", strerror(errno));
        return -1;
    }
    int reuse = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SERVER_BACKDOOR_PORT);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        simulith_log("Unable to bind server backdoor socket on port %d: %s\n",
                     SERVER_BACKDOOR_PORT, strerror(errno));
        close(s);
        return -1;
    }
    int flags = fcntl(s, F_GETFL, 0);
    if (flags >= 0)
        fcntl(s, F_SETFL, flags | O_NONBLOCK);
    backdoor_sock = s;
    simulith_log("Server backdoor listening on udp://0.0.0.0:%d\n", SERVER_BACKDOOR_PORT);
    return 0;
}

/* Best-effort: a missing status link should never prevent the server from
 * accepting commands, so failures here only disable the outbound telemetry. */
static void ensure_status_socket(void)
{
    if (status_sock >= 0)
        return;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
    {
        simulith_log("Unable to create server status socket: %s\n", strerror(errno));
        return;
    }

    memset(&status_dest_addr, 0, sizeof(status_dest_addr));
    status_dest_addr.sin_family = AF_INET;
    status_dest_addr.sin_port = htons(SERVER_STATUS_PORT);

    const char *gsw_hostname = getenv("SIMULITH_GSW_HOST");
    if (!gsw_hostname || gsw_hostname[0] == '\0')
        gsw_hostname = "shire-gsw";
    struct hostent *gsw_host = gethostbyname(gsw_hostname);
    if (gsw_host && gsw_host->h_addr_list[0])
        memcpy(&status_dest_addr.sin_addr, gsw_host->h_addr_list[0], sizeof(status_dest_addr.sin_addr));
    else
        status_dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    status_sock = s;
    status_dest_valid = 1;
}

#define SERVER_STATUS_PACKET_SIZE 17 /* paused(1) + speed(8) + sim_time_ns(8) */

/* Raw little-endian struct, no CCSDS header, mirroring how the director's
 * 42-truth telemetry is serialized (simulith_serialize_42_telemetry). */
static void send_status_update(int paused, double speed)
{
    if (status_sock < 0 || !status_dest_valid)
        return;
    uint8_t packet[SERVER_STATUS_PACKET_SIZE];
    uint8_t paused_flag = paused ? 1 : 0;
    memcpy(&packet[0], &paused_flag, sizeof(paused_flag));
    memcpy(&packet[1], &speed, sizeof(speed));
    memcpy(&packet[9], &current_time_ns, sizeof(current_time_ns));
    sendto(status_sock, packet, sizeof(packet), 0,
           (struct sockaddr *)&status_dest_addr, sizeof(status_dest_addr));
}

static void process_backdoor_command(const uint8_t *frame, size_t frame_len,
                                      int *paused, double *speed)
{
    if (frame_len < 8 + 1 + 2 + 2 || memcmp(frame, BACKDOOR_MAGIC, 8) != 0)
        return;
    size_t off = 8;
    uint8_t target_len = frame[off++];
    if (target_len == 0 || target_len > 64 || off + (size_t)target_len + 2 + 2 > frame_len)
        return;
    char target[65];
    memcpy(target, &frame[off], target_len);
    target[target_len] = '\0';
    off += target_len;
    if (strcmp(target, BACKDOOR_TARGET_NAME) != 0)
        return;

    uint16_t cmd_id = (uint16_t)((frame[off] << 8) | frame[off + 1]);
    off += 2;
    uint16_t payload_len = (uint16_t)((frame[off] << 8) | frame[off + 1]);
    off += 2;
    if (off + payload_len > frame_len)
        return;
    const uint8_t *payload = &frame[off];

    switch (cmd_id)
    {
        case SERVER_BACKDOOR_CMD_PAUSE:
            *paused = 1;
            printf("Simulation paused (ground command).\n");
            break;
        case SERVER_BACKDOOR_CMD_PLAY:
            *paused = 0;
            printf("Simulation resumed (ground command).\n");
            break;
        case SERVER_BACKDOOR_CMD_SET_SPEED:
        {
            if (payload_len != 8)
                return;
            /* Big-endian on the wire, matching BACKDOOR_CONFIG_ArgType's
             * existing convention for backdoor command arguments. */
            uint64_t bits = 0;
            for (int i = 0; i < 8; ++i)
                bits = (bits << 8) | (uint64_t)payload[i];
            char command[64];
            if (bits == 0)
            {
                /* +0.0 on the wire is the "max" sentinel; compared as an
                 * integer bit pattern rather than a converted double to
                 * avoid a floating-point equality comparison. */
                snprintf(command, sizeof(command), "speed max");
            }
            else
            {
                double requested_speed;
                memcpy(&requested_speed, &bits, sizeof(requested_speed));
                snprintf(command, sizeof(command), "speed %.17g", requested_speed);
            }
            process_cli_command(command, paused, speed);
            break;
        }
        default:
            return;
    }

    send_status_update(*paused, *speed);
}

#ifdef SIMULITH_TESTING
void simulith_server_process_backdoor_command_for_test(const uint8_t *frame, size_t frame_len,
                                                        int *paused, double *speed)
{
    process_backdoor_command(frame, frame_len, paused, speed);
}
#endif

/* Poll stdin and the ground-command backdoor socket for pending input and
 * apply any command found. Returns 1 if the caller should stop the server
 * (interactive 'quit'), 0 otherwise. */
static int poll_and_dispatch_commands(int *paused, double *speed)
{
    fd_set readfds;
    struct timeval tv = { 0, 0 };
    FD_ZERO(&readfds);
    FD_SET(0, &readfds);
    int max_fd = 0;
    if (backdoor_sock >= 0)
    {
        FD_SET(backdoor_sock, &readfds);
        max_fd = backdoor_sock;
    }

    int ready = select(max_fd + 1, &readfds, NULL, NULL, &tv);
    if (ready <= 0)
        return 0;

    int should_stop = 0;
    if (FD_ISSET(0, &readfds))
    {
        char cli_buf[32];
        if (fgets(cli_buf, sizeof(cli_buf), stdin))
        {
            should_stop = process_cli_command(cli_buf, paused, speed);
            if (!should_stop)
                send_status_update(*paused, *speed);
        }
    }
    if (backdoor_sock >= 0 && FD_ISSET(backdoor_sock, &readfds))
    {
        uint8_t frame[256];
        ssize_t n = recvfrom(backdoor_sock, frame, sizeof(frame), 0, NULL, NULL);
        if (n > 0)
            process_backdoor_command(frame, (size_t)n, paused, speed);
    }
    return should_stop;
}

void simulith_server_run(void)
{
    simulith_log("Waiting for clients to be ready...\n");

    // Wait for all clients to send "READY"
    int ready_clients = 0;
    int session_shared_transport = -1;
    while (ready_clients < expected_clients)
    {
        if (simulith_server_stop_requested) {
            simulith_log("Server shutdown requested while waiting for READY\n");
            return;
        }

        char buffer[96] = {0};
        int  size       = zmq_recv(responder, buffer, sizeof(buffer) - 1, 0);
        if (size > 0)
        {
            if ((size_t)size >= sizeof(buffer))
            {
                simulith_log("Rejected oversized handshake\n");
                zmq_send(responder, "ERR", 3, 0);
                continue;
            }
            buffer[size] = '\0';

            char client_id[64] = {0};
            unsigned phase_mask = 0;
            char transport[16] = {0};
            const char *id_start = strncmp(buffer, "READY ", 6) == 0 ? buffer + 6 : "";
            size_t id_length = strcspn(id_start, " \t\r\n");
            int consumed = 0;
            int ready_fields = sscanf(buffer, "READY %63s %u %15s %n", client_id,
                                      &phase_mask, transport, &consumed);
            if (ready_fields == 3 && buffer[consumed] != '\0')
                ready_fields = 0;
            else if (ready_fields == 2)
            {
                consumed = 0;
                if (sscanf(buffer, "READY %63s %u %n", client_id, &phase_mask,
                           &consumed) != 2 || buffer[consumed] != '\0')
                    ready_fields = 0;
            }
            else if (ready_fields == 1)
            {
                consumed = 0;
                if (sscanf(buffer, "READY %63s %n", client_id, &consumed) != 1 ||
                    buffer[consumed] != '\0')
                    ready_fields = 0;
            }
            if (ready_fields < 1 || id_length == 0 || id_length >= sizeof(client_id))
            {
                simulith_log("Invalid handshake message: %s\n", buffer);
                zmq_send(responder, "ERR", 3, 0);
                continue;
            }
            if (!valid_client_id(client_id))
            {
                simulith_log("Invalid client ID: %s\n", client_id);
                zmq_send(responder, "ERR", 3, 0);
                continue;
            }
            if (ready_fields == 1)
                phase_mask = strcmp(client_id, "shire-fsw") == 0 ?
                    SIMULITH_PHASE_MASK_EXECUTE : SIMULITH_PHASE_MASK_COMMIT;
            const unsigned valid_mask = SIMULITH_PHASE_MASK_PREPARE |
                                        SIMULITH_PHASE_MASK_EXECUTE |
                                        SIMULITH_PHASE_MASK_COMMIT;
            if (phase_mask == 0 || (phase_mask & ~valid_mask) != 0)
            {
                simulith_log("Invalid phase mask from %s: %u\n", client_id, phase_mask);
                zmq_send(responder, "ERR", 3, 0);
                continue;
            }

            int requested_shared = ready_fields >= 3 && strcmp(transport, "shared") == 0;
            if (ready_fields >= 3 && strcmp(transport, "shared") != 0 &&
                strcmp(transport, "zmq") != 0)
            {
                simulith_log("Client %s requested unknown synchronization transport\n", client_id);
                zmq_send(responder, "ERR TRANSPORT", 13, 0);
                continue;
            }
            if (requested_shared && !use_shared_barrier)
            {
                simulith_log("Client %s requested unavailable shared transport\n", client_id);
                zmq_send(responder, "ERR TRANSPORT", 13, 0);
                continue;
            }
            if (session_shared_transport >= 0 && requested_shared != session_shared_transport)
            {
                simulith_log("Client %s requested a mixed synchronization transport\n", client_id);
                zmq_send(responder, "ERR TRANSPORT", 13, 0);
                continue;
            }

            // Check for duplicate client ID
            if (is_client_id_taken(client_id))
            {
                simulith_log("Rejecting duplicate client ID: %s\n", client_id);
                zmq_send(responder, "DUP_ID", 6, 0);
                continue;
            }

            // Find empty slot and store client ID
            int slot = -1;
            for (int i = 0; i < MAX_CLIENTS; ++i)
            {
                if (client_states[i].id[0] == '\0')
                {
                    slot = i;
                    break;
                }
            }

            if (slot == -1)
            {
                simulith_log("No available slots for new client\n");
                zmq_send(responder, "ERR", 3, 0);
                continue;
            }

            // Register client
            snprintf(client_states[slot].id, sizeof(client_states[slot].id), "%s", client_id);
            client_states[slot].responded                              = 0;
            client_states[slot].phase_mask = phase_mask;
            session_shared_transport = requested_shared;
            ready_clients++;

            char ready_reply[32];
            int ready_reply_length;
            if (ready_fields >= 2)
                ready_reply_length = snprintf(ready_reply, sizeof(ready_reply), "ACK %d", slot);
            else
                ready_reply_length = snprintf(ready_reply, sizeof(ready_reply), "ACK");
            zmq_send(responder, ready_reply, (size_t)ready_reply_length, 0);
            simulith_log("Registered client %s (%d/%d)\n", client_id, ready_clients, expected_clients);
        }
        else
        {
            /* recv timed out or failed; check for shutdown and continue waiting */
            if (errno == EAGAIN)
            {
                /* timeout - loop again so we can detect shutdown requests */
                continue;
            }
            else
            {
                simulith_log("Error receiving handshake: %s\n", strerror(errno));
                continue;
            }
        }
    }

    use_shared_barrier = session_shared_transport == 1;

    simulith_log("All clients ready. Starting time broadcast.\n");

    // Reset client responded flags for tick ACKs
    reset_responses();

    // CLI state
    int paused = 0;
    int running = 1;
    double speed = configured_speed; // zero means unbounded
    g_attempted_speed = speed;

    ensure_backdoor_socket();
    ensure_status_socket();

    printf("Simulith CLI started. Type 'p' (pause/play), '+' (faster), '-' (slower), or 'speed <factor|max>'.\n");
    run_start_real_ns = monotonic_ns();
    g_last_log_real_ns = run_start_real_ns;
    g_last_log_sim_ns = current_time_ns;
    measurement_start_real_ns = configured_warmup_ns == 0 ? run_start_real_ns : 0;
    uint64_t next_tick_deadline_ns = run_start_real_ns;
    double pacing_speed = speed;

    while (running && !simulith_server_stop_requested)
    {
        // Check for CLI input and ground-command backdoor input (non-blocking)
        if (poll_and_dispatch_commands(&paused, &speed))
        {
            running = 0;
            break;
        }

        if (!paused)
        {
            uint64_t tick_start_ns = monotonic_ns();
            active_tick_start_ns = tick_start_ns;
            log_simulation_progress();
            if (current_time_ns == configured_warmup_ns && speed > 0.0)
                next_tick_deadline_ns = tick_start_ns;

            for (current_phase = SIMULITH_PHASE_PREPARE;
                 current_phase <= SIMULITH_PHASE_COMMIT && running &&
                 !simulith_server_stop_requested;
                 current_phase = (simulith_phase_t)(current_phase + 1))
            {
                reset_responses();
                active_phase_start_ns = monotonic_ns();
                uint64_t last_watchdog_ns = active_phase_start_ns;
                if (use_shared_barrier)
                {
                    if (simulith_shared_barrier_publish(&shared_barrier, current_sequence,
                                                        current_time_ns, current_phase,
                                                        required_phase_mask()) != 0)
                    {
                        simulith_log("Unable to publish tick %lu phase %s to shared barrier\n",
                                     (unsigned long)current_sequence, phase_name(current_phase));
                        running = 0;
                        break;
                    }
                }
                else
                    broadcast_phase(current_phase);

                while (!all_phase_participants_responded() && running &&
                       !simulith_server_stop_requested)
                {
                    char buffer[160] = {0};
                    int made_progress = 0;

                    if (use_shared_barrier)
                    {
                        uint32_t completed = simulith_shared_barrier_wait(
                            &shared_barrier, required_phase_mask(), 10);
                        made_progress = record_shared_completions(completed) > 0;
                    }

                    if (!use_shared_barrier)
                    {
                        int size = zmq_recv(responder, buffer, sizeof(buffer) - 1, ZMQ_DONTWAIT);
                        if (size > 0)
                        {
                            if ((size_t)size >= sizeof(buffer))
                            {
                                protocol_errors++;
                                zmq_send(responder, "ERR_PROTOCOL", 12, 0);
                                continue;
                            }
                            buffer[size] = '\0';
                            const char *reply = handle_completion(buffer);
                            zmq_send(responder, reply, strlen(reply), 0);
                            made_progress = 1;
                        }
                        else
                        {
                            zmq_pollitem_t item = {.socket = responder, .events = ZMQ_POLLIN};
                            int poll_status = zmq_poll(&item, 1, 200);
                            if (poll_status > 0 && (item.revents & ZMQ_POLLIN) != 0)
                                size = zmq_recv(responder, buffer, sizeof(buffer) - 1, 0);
                            if (size > 0)
                            {
                                if ((size_t)size >= sizeof(buffer))
                                {
                                    protocol_errors++;
                                    zmq_send(responder, "ERR_PROTOCOL", 12, 0);
                                    continue;
                                }
                                buffer[size] = '\0';
                                const char *reply = handle_completion(buffer);
                                zmq_send(responder, reply, strlen(reply), 0);
                                made_progress = 1;
                            }
                        }
                    }

                    if (!made_progress)
                    {
                        uint64_t now_ns = monotonic_ns();
                        if (now_ns - last_watchdog_ns >= watchdog_interval_ns)
                        {
                            simulith_log("Tick %lu phase %s stalled; waiting for",
                                         (unsigned long)current_sequence,
                                         phase_name(current_phase));
                            for (int i = 0; i < expected_clients; ++i)
                            {
                                if ((client_states[i].phase_mask & SIMULITH_PHASE_BIT(current_phase)) != 0 &&
                                    !client_states[i].responded)
                                    simulith_log(" %s/%s", client_states[i].id,
                                                 phase_name(current_phase));
                            }
                            simulith_log("\n");
                            last_watchdog_ns = now_ns;
                        }
                    }

                    if (poll_and_dispatch_commands(&paused, &speed))
                        running = 0;
                }
            }

            if (current_phase <= SIMULITH_PHASE_COMMIT)
                break;

            uint64_t completion_ns = monotonic_ns();
            uint64_t tick_latency_ns = completion_ns - tick_start_ns;
            completed_ticks++;
            if (current_time_ns >= configured_warmup_ns)
            {
                measured_ticks_completed++;
                if (measurement_start_real_ns == 0)
                    measurement_start_real_ns = tick_start_ns;
                tick_latency_total_ns += tick_latency_ns;
                if (tick_latency_ns < tick_latency_min_ns) tick_latency_min_ns = tick_latency_ns;
                if (tick_latency_ns > tick_latency_max_ns) tick_latency_max_ns = tick_latency_ns;
                if (tick_latency_sample_count < MAX_LATENCY_SAMPLES)
                    tick_latency_samples[tick_latency_sample_count++] = tick_latency_ns;
            }

            // Sleep to simulate real time (adjusted by speed), accounting for processing time
            if (speed > 0.0) 
            {
                uint64_t target_ns = (uint64_t)((double)tick_interval_ns / speed);
                if (speed < pacing_speed || speed > pacing_speed)
                {
                    next_tick_deadline_ns = completion_ns;
                    pacing_speed = speed;
                }
                next_tick_deadline_ns += target_ns;
                if (completion_ns < next_tick_deadline_ns)
                {
                    struct timespec deadline = {
                        .tv_sec = (time_t)(next_tick_deadline_ns / 1000000000ULL),
                        .tv_nsec = (long)(next_tick_deadline_ns % 1000000000ULL)
                    };
                    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL) != 0 &&
                           errno == EINTR)
                    {
                    }
                }
            }
            if (current_time_ns >= configured_warmup_ns)
                measurement_end_real_ns = monotonic_ns();
            current_time_ns += tick_interval_ns;
            current_sequence++;
            if (configured_duration_ns > 0 && current_time_ns >= configured_duration_ns)
                running = 0;
        } else 
        {
            // If paused, sleep briefly to avoid busy loop
            sleep_for_microseconds(100000);
            next_tick_deadline_ns = monotonic_ns();
        }
    }

    /* Release every connected client on normal duration, interactive quit,
     * watchdog-driven shutdown, or a phase error. */
    if (ready_clients == expected_clients && publisher)
    {
        if (use_shared_barrier)
            simulith_shared_barrier_stop(&shared_barrier);
        else
            broadcast_phase(SIMULITH_PHASE_STOP);
        sleep_for_microseconds(10000);
    }
}

static int compare_uint64(const void *left, const void *right)
{
    const uint64_t a = *(const uint64_t *)left;
    const uint64_t b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static double percentile_us(uint64_t *values, size_t count, double percentile)
{
    if (count == 0)
        return 0.0;
    qsort(values, count, sizeof(values[0]), compare_uint64);
    size_t index = (size_t)(percentile * (double)(count - 1));
    return (double)values[index] / 1000.0;
}

static void write_metrics(FILE *stream)
{
    uint64_t now_ns = monotonic_ns();
    uint64_t total_elapsed_ns = run_start_real_ns > 0 ? now_ns - run_start_real_ns : 0;
    uint64_t elapsed_ns = measurement_start_real_ns > 0 && measurement_end_real_ns >= measurement_start_real_ns ?
        measurement_end_real_ns - measurement_start_real_ns : 0;
    uint64_t measured_ticks = measured_ticks_completed;
    uint64_t measured_simulated_ns = measured_ticks * tick_interval_ns;
    double achieved = elapsed_ns > 0 ? (double)measured_simulated_ns / (double)elapsed_ns : 0.0;
    double mean_latency_us = measured_ticks > 0 ?
        (double)tick_latency_total_ns / (double)measured_ticks / 1000.0 : 0.0;
    double min_latency_us = measured_ticks > 0 ? (double)tick_latency_min_ns / 1000.0 : 0.0;
    char requested_speed[32];
    if (configured_speed <= 0.0)
        strcpy(requested_speed, "\"max\"");
    else
        snprintf(requested_speed, sizeof(requested_speed), "%.9f", configured_speed);
    fprintf(stream,
            "{\"schema_version\":1,\"ticks\":%lu,\"simulated_ns\":%lu,"
            "\"warmup_ns\":%lu,\"measured_ticks\":%lu,\"measured_simulated_ns\":%lu,"
            "\"startup_and_run_wall_ns\":%lu,\"wall_ns\":%lu,"
            "\"requested_speed\":%s,\"achieved_speed\":%.9f,"
            "\"tick_latency_us\":{\"min\":%.3f,\"mean\":%.3f,\"p50\":%.3f,"
            "\"p95\":%.3f,\"p99\":%.3f,\"max\":%.3f},"
            "\"completions\":%lu,\"protocol_errors\":%lu,"
            "\"duplicate_completions\":%lu,\"stale_completions\":%lu,"
            "\"future_completions\":%lu,\"participants\":[",
            (unsigned long)completed_ticks, (unsigned long)current_time_ns,
            (unsigned long)configured_warmup_ns, (unsigned long)measured_ticks,
            (unsigned long)measured_simulated_ns, (unsigned long)total_elapsed_ns,
            (unsigned long)elapsed_ns, requested_speed,
            achieved, min_latency_us, mean_latency_us,
            percentile_us(tick_latency_samples, tick_latency_sample_count, 0.50),
            percentile_us(tick_latency_samples, tick_latency_sample_count, 0.95),
            percentile_us(tick_latency_samples, tick_latency_sample_count, 0.99),
            (double)tick_latency_max_ns / 1000.0,
            (unsigned long)completion_count,
            (unsigned long)protocol_errors, (unsigned long)duplicate_completions,
            (unsigned long)stale_completions, (unsigned long)future_completions);
    int participant_index = 0;
    for (int i = 0; i < expected_clients; ++i)
    {
        for (simulith_phase_t phase = SIMULITH_PHASE_PREPARE;
             phase <= SIMULITH_PHASE_COMMIT;
             phase = (simulith_phase_t)(phase + 1))
        {
            if ((client_states[i].phase_mask & SIMULITH_PHASE_BIT(phase)) == 0)
                continue;
            if (participant_index++ > 0) fputc(',', stream);
            fprintf(stream,
                    "{\"id\":\"%s\",\"phase\":\"%s\",\"count\":%lu,\"measured_count\":%lu,"
                    "\"latency_us\":{\"p50\":%.3f,\"p95\":%.3f,\"p99\":%.3f,\"max\":%.3f}}",
                    client_states[i].id, phase_name(phase),
                    (unsigned long)client_states[i].completion_count[phase],
                    (unsigned long)client_states[i].measured_completion_count[phase],
                    percentile_us(client_states[i].completion_latency_samples[phase],
                                  client_states[i].completion_latency_sample_count[phase], 0.50),
                    percentile_us(client_states[i].completion_latency_samples[phase],
                                  client_states[i].completion_latency_sample_count[phase], 0.95),
                    percentile_us(client_states[i].completion_latency_samples[phase],
                                  client_states[i].completion_latency_sample_count[phase], 0.99),
                    (double)client_states[i].completion_latency_max_ns[phase] / 1000.0);
        }
    }
    fputs("]}\n", stream);
}

void simulith_server_request_stop(void)
{
    simulith_server_stop_requested = 1;
    if (use_shared_barrier)
        simulith_shared_barrier_stop(&shared_barrier);
}

void simulith_server_shutdown(void)
{
    /* Close resources after the owner loop has returned. */
    simulith_server_request_stop();

    simulith_log("SIMULITH_METRICS ");
    write_metrics(stdout);
    if (configured_metrics_path[0] != '\0')
    {
        FILE *metrics = fopen(configured_metrics_path, "w");
        if (metrics)
        {
            write_metrics(metrics);
            fclose(metrics);
        }
        else
        {
            simulith_log("Unable to write metrics file %s: %s\n",
                         configured_metrics_path, strerror(errno));
        }
    }
    close_server_resources();
    simulith_log("Simulith server shut down\n");
}
