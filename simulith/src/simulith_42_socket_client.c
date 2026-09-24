/*
 * Simulith 42 Socket Client
 * 
 * Client interface to communicate with NASA's 42 simulator via socket IPC.
 * This maintains license separation by communicating over network protocol
 * rather than linking 42 code directly.
 * 
 * Protocol: 42's native text-based IPC (see 42/Source/42ipc.c)
 * Format: Newline-delimited key-value pairs
 */

#include "simulith_42_socket_client.h"
#include "shire_ipc_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/un.h>

#define SOCKET_BUFFER_SIZE 16384
#define RECONNECT_ATTEMPTS 20
#define RECONNECT_DELAY_MS 1000

static unsigned int reconnect_attempts(void)
{
    const char *value = getenv("SIMULITH_42_RECONNECT_ATTEMPTS");
    int attempts = value ? atoi(value) : RECONNECT_ATTEMPTS;
    return attempts > 0 ? (unsigned int)attempts : 1U;
}

static unsigned int reconnect_delay_ms(void)
{
    const char *value = getenv("SIMULITH_42_RECONNECT_DELAY_MS");
    int delay = value ? atoi(value) : RECONNECT_DELAY_MS;
    return delay > 0 ? (unsigned int)delay : 0U;
}

static void wait_before_reconnect(void)
{
    unsigned int delay_ms = reconnect_delay_ms();
    struct timeval delay = {
        .tv_sec = (time_t)(delay_ms / 1000U),
        .tv_usec = (suseconds_t)(delay_ms % 1000U) * 1000
    };
    (void)select(0, NULL, NULL, NULL, &delay);
}

typedef struct {
    int socket_fd;
    int connected;
    char hostname[256];
    int port;
    char rx_buffer[SOCKET_BUFFER_SIZE];
    size_t rx_buffer_len;
    int binary_mode;
} fortytwo_socket_client_t;

static fortytwo_socket_client_t g_client = {
    .socket_fd = -1,
    .connected = 0,
    .hostname = "shire-42",
    .port = 5556,
    .rx_buffer_len = 0,
    .binary_mode = 1
};

static int socket_read_all(int socket_fd, void *buffer, size_t length)
{
    unsigned char *cursor = buffer;
    while (length > 0) {
        ssize_t received = recv(socket_fd, cursor, length, 0);
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) return -1;
        cursor += (size_t)received;
        length -= (size_t)received;
    }
    return 0;
}

static int socket_write_all(int socket_fd, const void *buffer, size_t length)
{
    const unsigned char *cursor = buffer;
    while (length > 0) {
        ssize_t sent = send(socket_fd, cursor, length, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return -1;
        cursor += (size_t)sent;
        length -= (size_t)sent;
    }
    return 0;
}

static int socket_read_text_frame(int socket_fd, char *buffer, size_t capacity)
{
    size_t used = 0;
    if (!buffer || capacity < 2)
        return -1;
    while (used < capacity - 1) {
        ssize_t received = recv(socket_fd, buffer + used, capacity - 1 - used, 0);
        if (received < 0 && errno == EINTR) continue;
        if (received <= 0) return -1;
        used += (size_t)received;
        buffer[used] = '\0';
        if (strstr(buffer, "[ENDMSG]\n") != NULL)
            return 0;
    }
    return -1;
}

static int receive_binary_state(simulith_42_context_t *context)
{
    shire_ipc_state_t state;
    if (socket_read_all(g_client.socket_fd, &state, sizeof(state)) != 0)
        return -1;
    if (state.header.magic != SHIRE_IPC_MAGIC ||
        state.header.version != SHIRE_IPC_VERSION ||
        state.header.type != SHIRE_IPC_STATE ||
        state.header.payload_size != sizeof(state) - sizeof(state.header))
        return -1;

    memset(context, 0, sizeof(*context));
    context->sim_time = state.sim_time;
    context->dyn_time = state.utc_civil_time;
    memcpy(context->qn, state.qn, sizeof(state.qn));
    memcpy(context->wn, state.wn, sizeof(state.wn));
    memcpy(context->pos_n, state.pos_n, sizeof(state.pos_n));
    memcpy(context->vel_n, state.vel_n, sizeof(state.vel_n));
    memcpy(context->sun_vector_body, state.sun_vector_body, sizeof(state.sun_vector_body));
    memcpy(context->mag_field_body, state.mag_field_body, sizeof(state.mag_field_body));
    memcpy(context->hvb, state.hvb, sizeof(state.hvb));
    context->mass = state.mass;
    memcpy(context->cm, state.cm, sizeof(state.cm));
    memcpy(context->inertia, state.inertia, sizeof(state.inertia));
    context->eclipse = state.eclipse;
    context->atmo_density = state.atmo_density;
    context->valid = 1;
    context->spacecraft_id = 0;
    context->exists = 1;
    strcpy(context->label, "SC[0]");
    return 0;
}

static int receive_binary_ack(void)
{
    shire_ipc_ack_t ack;
    if (socket_read_all(g_client.socket_fd, &ack, sizeof(ack)) != 0 ||
        ack.header.magic != SHIRE_IPC_MAGIC ||
        ack.header.version != SHIRE_IPC_VERSION ||
        ack.header.type != SHIRE_IPC_ACK ||
        ack.header.payload_size != sizeof(ack) - sizeof(ack.header) ||
        ack.status != 0)
        return -1;
    return 0;
}

/*
 * Connect to 42 IPC socket
 */
static int connect_to_42(void)
{
    struct sockaddr_in server_addr;
    struct hostent *host;
    int sockfd;
    unsigned int attempt;
    
    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "[42-client] Error creating socket: %s\n", strerror(errno));
        return -1;
    }
    
    // Set TCP_NODELAY for low latency
    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    
    // Resolve hostname
    host = gethostbyname(g_client.hostname);
    if (host == NULL) {
        fprintf(stderr, "[42-client] Error resolving hostname: %s\n", g_client.hostname);
        close(sockfd);
        return -1;
    }
    
    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, host->h_addr_list[0], (size_t)host->h_length);
    server_addr.sin_port = htons((uint16_t)g_client.port);
    
    // Connect with retries
    unsigned int attempts = reconnect_attempts();
    for (attempt = 0; attempt != attempts; attempt++) {
        if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == 0) {
            printf("[42-client] Connected to 42 at %s:%d\n", g_client.hostname, g_client.port);
            g_client.socket_fd = sockfd;
            g_client.connected = 1;
            return 0;
        }
        
        if (attempt + 1U != attempts) {
            //fprintf(stderr, "[42-client] Connection attempt %d failed, retrying...\n", attempt + 1);
            wait_before_reconnect();
        }
    }
    
    fprintf(stderr, "[42-client] Failed to connect to 42 after %u attempts\n", attempts);
    close(sockfd);
    return -1;
}

static int is_gregorian_leap_year(long year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/*
 * Convert a UTC calendar time (year, day-of-year, time-of-day) from 42's
 * "TIME YYYY-DDD-HH:MM:SS.SSSSSSSSS" IPC line into seconds since the J2000
 * epoch (2000-01-01T12:00:00 UTC). This mirrors 42's own CivilTime
 * convention (see 42/Kit/Source/timekit.c's DateToTime) and is what
 * simulith_42_context_t's dyn_time field documents itself as. Anchoring to
 * the real calendar epoch configured in Inp_Sim.txt -- rather than just
 * time-of-day, which wraps every 86400s and carries no date at all -- is
 * required for any consumer that needs absolute time, e.g. ADCS's GPS time
 * sync into cFE TIME.
 */
static double civil_calendar_to_seconds_since_j2000(long year, long day_of_year,
                                                     int hour, int minute, double second)
{
    long days = 0;
    if (year >= 2000) {
        for (long y = 2000; y < year; y++) {
            days += is_gregorian_leap_year(y) ? 366 : 365;
        }
    } else {
        for (long y = year; y < 2000; y++) {
            days -= is_gregorian_leap_year(y) ? 366 : 365;
        }
    }
    days += (day_of_year - 1);
    /* J2000 is 2000-01-01T12:00:00 UTC (noon), a half day after the
       2000-01-01T00:00:00 reference the day count above is relative to. */
    return ((double)days - 0.5) * 86400.0 + hour * 3600.0 + minute * 60.0 + second;
}

/*
 * Parse spacecraft state from 42's IPC text format
 */
static int parse_42_state(const char *message, simulith_42_context_t *context)
{
    char line[512];
    const char *msg_ptr = message;
    int line_idx = 0;
    
    memset(context, 0, sizeof(simulith_42_context_t));
    context->valid = 0;
    
    // Parse line by line
    while (*msg_ptr != '\0') {
        // Extract one line
        line_idx = 0;
        while (*msg_ptr != '\n' && *msg_ptr != '\0' && line_idx < 511) {
            line[line_idx++] = *msg_ptr++;
        }
        line[line_idx] = '\0';
        if (*msg_ptr == '\n') msg_ptr++;
        
        // Parse known fields (based on 42's TxRxIPC.c output format)
        if (strncmp(line, "TIME ", 5) == 0) {
            // TIME format: YYYY-DDD-HH:MM:SS.SSSSSSSSS (UTC calendar time)
            long year, day_of_year;
            int hours, minutes;
            double seconds;
            sscanf(line + 5, "%ld-%ld-%d:%d:%lf", &year, &day_of_year, &hours, &minutes, &seconds);
            context->sim_time = hours * 3600.0 + minutes * 60.0 + seconds;
            context->dyn_time = civil_calendar_to_seconds_since_j2000(year, day_of_year,
                                                                      hours, minutes, seconds);
        }
        // 42 uses array format: "SC[0].qn = [q0 q1 q2 q3]"
        else if (strncmp(line, "SC[0].qn = [", 12) == 0) {
            sscanf(line, "SC[0].qn = [%lf %lf %lf %lf]",
                   &context->qn[0], &context->qn[1], &context->qn[2], &context->qn[3]);
        }
        else if (strncmp(line, "SC[0].wn = [", 12) == 0) {
            sscanf(line, "SC[0].wn = [%lf %lf %lf]",
                   &context->wn[0], &context->wn[1], &context->wn[2]);
        }
        else if (strncmp(line, "Orb[0].PosN = [", 15) == 0) {
            sscanf(line, "Orb[0].PosN = [%lf %lf %lf]",
                   &context->pos_n[0], &context->pos_n[1], &context->pos_n[2]);
        }
        else if (strncmp(line, "Orb[0].VelN = [", 15) == 0) {
            sscanf(line, "Orb[0].VelN = [%lf %lf %lf]",
                   &context->vel_n[0], &context->vel_n[1], &context->vel_n[2]);
        }
        else if (strncmp(line, "SC[0].svb = [", 13) == 0) {
            sscanf(line, "SC[0].svb = [%lf %lf %lf]",
                   &context->sun_vector_body[0], &context->sun_vector_body[1], &context->sun_vector_body[2]);
        }
        else if (strncmp(line, "SC[0].bvb = [", 13) == 0) {
            sscanf(line, "SC[0].bvb = [%lf %lf %lf]",
                   &context->mag_field_body[0], &context->mag_field_body[1], &context->mag_field_body[2]);
        }
        else if (strncmp(line, "SC[0].Hvb = [", 13) == 0) {
            sscanf(line, "SC[0].Hvb = [%lf %lf %lf]",
                   &context->hvb[0], &context->hvb[1], &context->hvb[2]);
        }
    }
    
    context->valid = 1;
    context->spacecraft_id = 0;
    context->exists = 1;
    snprintf(context->label, sizeof(context->label), "SC[0]");
    
    // Determine eclipse state from sun vector magnitude
    // 42 doesn't send explicit eclipse flag, so infer it from sun vector
    double svb_mag = sqrt(context->sun_vector_body[0]*context->sun_vector_body[0] + 
                          context->sun_vector_body[1]*context->sun_vector_body[1] + 
                          context->sun_vector_body[2]*context->sun_vector_body[2]);
    context->eclipse = (svb_mag < 0.01) ? 1 : 0;
    
    return 0;
}

#ifdef SIMULITH_TESTING
int simulith_42_parse_state_for_test(const char *message, simulith_42_context_t *context)
{
    if (!message || !context)
        return -1;
    return parse_42_state(message, context);
}
#endif

/*
 * Connect via Unix domain socket, used when both containers share a /tmp
 * volume (simulith_ipc).
 */
static int connect_to_42_unix(const char *socket_path)
{
    int sockfd;
    unsigned int attempt;
    struct sockaddr_un addr;
    size_t path_len = strlen(socket_path);

    if (path_len >= sizeof(addr.sun_path)) {
        fprintf(stderr, "[42-client] Unix socket path too long (%zu chars, max %zu): %s\n",
                path_len, sizeof(addr.sun_path) - 1, socket_path);
        return -1;
    }

    unsigned int attempts = reconnect_attempts();
    for (attempt = 0; attempt != attempts; attempt++) {
        sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sockfd < 0) {
            fprintf(stderr, "[42-client] Error creating Unix socket: %s\n", strerror(errno));
            return -1;
        }

        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        memcpy(addr.sun_path, socket_path, path_len + 1);

        if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            printf("[42-client] Connected to 42 via Unix socket %s\n", socket_path);
            g_client.socket_fd = sockfd;
            g_client.connected = 1;
            return 0;
        }

        close(sockfd);
        if (attempt + 1U != attempts)
            wait_before_reconnect();
    }

    fprintf(stderr, "[42-client] Failed to connect to 42 Unix socket %s after %u attempts\n",
            socket_path, attempts);
    return -1;
}

/*
 * Initialize connection to 42.
 *
 * If hostname starts with '/' it is treated as a Unix domain socket path
 * (e.g. "/tmp/42_ipc.sock"); port is ignored in that case.
 * Otherwise the existing TCP path is used.
 */
int simulith_42_init(const char *hostname, int port)
{
    const char *mode = getenv("FORTYTWO_IPC_MODE");
    g_client.binary_mode = !mode || strcmp(mode, "text") != 0;
    if (hostname) {
        strncpy(g_client.hostname, hostname, sizeof(g_client.hostname) - 1);
        g_client.hostname[sizeof(g_client.hostname) - 1] = '\0';
    }
    if (g_client.hostname[0] != '/' && (port <= 0 || port > UINT16_MAX)) {
        fprintf(stderr, "[42-client] Invalid TCP port: %d\n", port);
        return -1;
    }
    if (port > 0) {
        g_client.port = port;
    }

    if (g_client.hostname[0] == '/') {
        printf("[42-client] Initializing Unix socket connection to 42 at %s\n",
               g_client.hostname);
        return connect_to_42_unix(g_client.hostname);
    }

    printf("[42-client] Initializing TCP connection to 42 at %s:%d\n",
           g_client.hostname, g_client.port);
    return connect_to_42();
}

/*
 * Request state update from 42
 * 
 * This creates lock-step time synchronization between director and 42:
 * 1. Director calls this function (blocking read)
 * 2. 42 sends state and waits for Ack (if AllowBlocking=TRUE in Inp_IPC.txt)
 * 3. Director receives state and sends Ack
 * 4. In TXRX mode, 42 then waits for commands from director
 * 5. Director must send commands (or empty message) and receive Ack
 * 
 * This naturally synchronizes 42's time with Simulith's tick rate.
 */
int simulith_42_request_state(simulith_42_context_t *context)
{
    static const char ack[4] = "Ack";
    
    if (!context) {
        fprintf(stderr, "[42-client] State destination is NULL\n");
        return -1;
    }
    if (!g_client.connected) {
        fprintf(stderr, "[42-client] Not connected to 42\n");
        return -1;
    }

    if (g_client.binary_mode) {
        if (receive_binary_state(context) != 0) {
            fprintf(stderr, "[42-client] Invalid or incomplete binary state frame\n");
            g_client.connected = 0;
            return -1;
        }
        return 0;
    }
    
    // Read state from 42 (blocking - waits for 42 to send next state)
    // This creates natural time synchronization with 42
    if (socket_read_text_frame(g_client.socket_fd, g_client.rx_buffer,
                               sizeof(g_client.rx_buffer)) != 0) {
        fprintf(stderr, "[42-client] Invalid, oversized, or incomplete text state frame\n");
        g_client.connected = 0;
        return -1;
    }

    // Send acknowledgment (42 expects "Ack" response)
    if (socket_write_all(g_client.socket_fd, ack, sizeof(ack)) != 0) {
        g_client.connected = 0;
        return -1;
    }
    
    // Parse the received state
    int parse_result = parse_42_state(g_client.rx_buffer, context);
    
    return parse_result;
}

/*
 * Cleanup and close connection
 */
void simulith_42_cleanup(void)
{
    if (g_client.socket_fd >= 0) {
        close(g_client.socket_fd);
        g_client.socket_fd = -1;
    }
    g_client.connected = 0;
    printf("[42-client] Disconnected from 42\n");
}

/*
 * Check if connected to 42
 */
int simulith_42_is_connected(void)
{
    return g_client.connected;
}

/*
 * Send batch of commands to 42 in a single message
 * More efficient than multiple individual sends
 */
int simulith_42_send_command_batch(const simulith_42_command_t *commands, int count)
{
    char msg[32768];
    size_t msg_len = 0;
    
    if (!g_client.connected) {
        fprintf(stderr, "[42-client] Not connected to 42\n");
        return -1;
    }
    
    if (!commands || count <= 0 || count > SIMULITH_42_CMD_QUEUE_SIZE) {
        return -1;
    }

    if (g_client.binary_mode) {
        shire_ipc_commands_t batch;
        memset(&batch, 0, sizeof(batch));
        batch.header.magic = SHIRE_IPC_MAGIC;
        batch.header.version = SHIRE_IPC_VERSION;
        batch.header.type = SHIRE_IPC_COMMANDS;
        batch.header.payload_size = SHIRE_IPC_COMMANDS_PREFIX_SIZE;
        for (int source = 0; source < count && batch.count < SHIRE_IPC_MAX_COMMANDS; ++source) {
            const simulith_42_command_t *command = &commands[source];
            if (!command->valid) continue;
            shire_ipc_command_t *wire = &batch.commands[batch.count];
            wire->spacecraft_id = command->spacecraft_id;
            switch (command->type) {
                case SIMULITH_42_CMD_WHEEL_TORQUE:
                    wire->type = SHIRE_IPC_CMD_WHEEL;
                    wire->enable_mask = (uint32_t)command->cmd.wheel.enable_mask;
                    memcpy(wire->values, command->cmd.wheel.torque,
                           sizeof(command->cmd.wheel.torque));
                    break;
                case SIMULITH_42_CMD_MTB_TORQUE:
                    wire->type = SHIRE_IPC_CMD_MTB;
                    wire->enable_mask = (uint32_t)command->cmd.mtb.enable_mask;
                    memcpy(wire->values, command->cmd.mtb.dipole,
                           sizeof(command->cmd.mtb.dipole));
                    break;
                case SIMULITH_42_CMD_THRUSTER:
                    wire->type = SHIRE_IPC_CMD_THRUSTER;
                    wire->enable_mask = (uint32_t)command->cmd.thruster.enable_mask;
                    memcpy(wire->values, command->cmd.thruster.thrust,
                           sizeof(command->cmd.thruster.thrust));
                    memcpy(&wire->values[3], command->cmd.thruster.torque,
                           sizeof(command->cmd.thruster.torque));
                    break;
                case SIMULITH_42_CMD_NONE:
                case SIMULITH_42_CMD_SET_MODE:
                case SIMULITH_42_CMD_COUNT:
                default:
                    fprintf(stderr, "[42-client] Unsupported binary command type %d\n",
                            command->type);
                    return -1;
            }
            batch.count++;
        }
        batch.header.payload_size = (uint32_t)
            SHIRE_IPC_COMMANDS_PAYLOAD_SIZE(batch.count);
        if (socket_write_all(g_client.socket_fd, &batch,
                             SHIRE_IPC_COMMANDS_FRAME_SIZE(batch.count)) != 0) {
            g_client.connected = 0;
            return -1;
        }
        if (receive_binary_ack() != 0) {
            g_client.connected = 0;
            return -1;
        }
        return 0;
    }
    
    /* Build single message with all commands */
    for (int cmd_idx = 0; cmd_idx < count; cmd_idx++) {
        const simulith_42_command_t *cmd = &commands[cmd_idx];
        
        if (!cmd->valid) {
            continue;
        }
        
        /* Serialize command based on type */
        switch (cmd->type) {
            case SIMULITH_42_CMD_WHEEL_TORQUE:
                for (int i = 0; i < 4; i++) {
                    if (cmd->cmd.wheel.enable_mask & (1 << i)) {
                        int len = snprintf(msg + msg_len, sizeof(msg) - msg_len,
                                          "SC[%d].Whl[%d].Tcmd = %18.12le\n",
                                          cmd->spacecraft_id, i, cmd->cmd.wheel.torque[i]);
                        if (len > 0 && ((size_t)len + msg_len) < sizeof(msg)) {
                            msg_len += (size_t)len;
                        }
                    }
                }
                break;
                
            case SIMULITH_42_CMD_MTB_TORQUE:
                for (int i = 0; i < 3; i++) {
                    if (cmd->cmd.mtb.enable_mask & (1 << i)) {
                        int len = snprintf(msg + msg_len, sizeof(msg) - msg_len,
                                          "SC[%d].MTB[%d].Mcmd = %18.12le\n",
                                          cmd->spacecraft_id, i, cmd->cmd.mtb.dipole[i]);
                        if (len > 0 && ((size_t)len + msg_len) < sizeof(msg)) {
                            msg_len += (size_t)len;
                        }
                    }
                }
                break;
            
            case SIMULITH_42_CMD_NONE:
            case SIMULITH_42_CMD_THRUSTER:
            case SIMULITH_42_CMD_SET_MODE:
            case SIMULITH_42_CMD_COUNT:
            default:
                fprintf(stderr, "[42-client] Unsupported text command type %d\n",
                        cmd->type);
                return -1;
        }
    }
    
    /* Add ENDMSG marker */
    int len = snprintf(msg + msg_len, sizeof(msg) - msg_len, "[ENDMSG]\n");
    if (len > 0) {
        msg_len += (size_t)len;
    }
    
    /* Send the batched message to 42 */
    if (msg_len > 0) {
        char ack[4];
        if (socket_write_all(g_client.socket_fd, msg, msg_len) != 0) {
            fprintf(stderr, "[42-client] Failed to send batched commands\n");
            g_client.connected = 0;
            return -1;
        }
        
        /* Read acknowledgment from 42 (TXRX mode expects Ack response) */
        if (socket_read_all(g_client.socket_fd, ack, sizeof(ack)) != 0 ||
            memcmp(ack, "Ack", sizeof(ack)) != 0) {
            fprintf(stderr, "[42-client] Failed to receive Ack from 42\n");
            g_client.connected = 0;
            return -1;
        }
    }
    
    return 0;
}

/*
 * Send empty command message to 42
 * Required in TXRX mode when there are no commands to send
 */
int simulith_42_send_empty_commands(void)
{
    char msg[32];
    char ack[4];
    
    if (!g_client.connected) {
        return -1;
    }

    if (g_client.binary_mode) {
        shire_ipc_commands_t batch;
        memset(&batch, 0, sizeof(batch));
        batch.header.magic = SHIRE_IPC_MAGIC;
        batch.header.version = SHIRE_IPC_VERSION;
        batch.header.type = SHIRE_IPC_COMMANDS;
        batch.header.payload_size = SHIRE_IPC_COMMANDS_PREFIX_SIZE;
        if (socket_write_all(g_client.socket_fd, &batch,
                             SHIRE_IPC_COMMANDS_FRAME_SIZE(0)) != 0) {
            g_client.connected = 0;
            return -1;
        }
        if (receive_binary_ack() != 0) {
            g_client.connected = 0;
            return -1;
        }
        return 0;
    }
    
    // Just send ENDMSG marker
    size_t msg_len = (size_t)snprintf(msg, sizeof(msg), "[ENDMSG]\n");
    
    if (socket_write_all(g_client.socket_fd, msg, msg_len) != 0) {
        fprintf(stderr, "[42-client] Failed to send empty commands\n");
        g_client.connected = 0;
        return -1;
    }
    
    // Read acknowledgment from 42
    if (socket_read_all(g_client.socket_fd, ack, sizeof(ack)) != 0 ||
        memcmp(ack, "Ack", sizeof(ack)) != 0) {
        fprintf(stderr, "[42-client] Failed to receive Ack from 42\n");
        g_client.connected = 0;
        return -1;
    }
    
    return 0;
}
