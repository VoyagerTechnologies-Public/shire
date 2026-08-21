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
#include <sys/un.h>

#define SOCKET_BUFFER_SIZE 16384
#define RECONNECT_ATTEMPTS 20
#define RECONNECT_DELAY_MS 1000

typedef struct {
    int socket_fd;
    int connected;
    char hostname[256];
    int port;
    char rx_buffer[SOCKET_BUFFER_SIZE];
    size_t rx_buffer_len;
} fortytwo_socket_client_t;

static fortytwo_socket_client_t g_client = {
    .socket_fd = -1,
    .connected = 0,
    .hostname = "shire-42",
    .port = 5556,
    .rx_buffer_len = 0
};

/*
 * Connect to 42 IPC socket
 */
static int connect_to_42(void)
{
    struct sockaddr_in server_addr;
    struct hostent *host;
    int sockfd;
    int attempt;
    
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
    memcpy(&server_addr.sin_addr.s_addr, host->h_addr, (size_t)host->h_length);
    server_addr.sin_port = htons((uint16_t)g_client.port);
    
    // Connect with retries
    for (attempt = 0; attempt < RECONNECT_ATTEMPTS; attempt++) {
        if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == 0) {
            printf("[42-client] Connected to 42 at %s:%d\n", g_client.hostname, g_client.port);
            g_client.socket_fd = sockfd;
            g_client.connected = 1;
            return 0;
        }
        
        if (attempt < RECONNECT_ATTEMPTS - 1) {
            //fprintf(stderr, "[42-client] Connection attempt %d failed, retrying...\n", attempt + 1);
            usleep(RECONNECT_DELAY_MS * 1000);
        }
    }
    
    fprintf(stderr, "[42-client] Failed to connect to 42 after %d attempts\n", RECONNECT_ATTEMPTS);
    close(sockfd);
    return -1;
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
            // TIME format: YYYY-DDD-HH:MM:SS.SSSSSSSSS
            // Extract total SimTime by converting HH:MM:SS to seconds
            int hours, minutes;
            double seconds;
            sscanf(line + 5, "%*d-%*d-%d:%d:%lf", &hours, &minutes, &seconds);
            context->sim_time = hours * 3600.0 + minutes * 60.0 + seconds;
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
    
    // Set dyn_time to sim_time (they're the same in 42)
    context->dyn_time = context->sim_time;
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

/*
 * Connect via Unix domain socket, used when both containers share a /tmp
 * volume (simulith_ipc).
 */
static int connect_to_42_unix(const char *socket_path)
{
    int sockfd;
    int attempt;
    struct sockaddr_un addr;
    size_t path_len = strlen(socket_path);

    if (path_len >= sizeof(addr.sun_path)) {
        fprintf(stderr, "[42-client] Unix socket path too long (%zu chars, max %zu): %s\n",
                path_len, sizeof(addr.sun_path) - 1, socket_path);
        return -1;
    }

    for (attempt = 0; attempt < RECONNECT_ATTEMPTS; attempt++) {
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
        if (attempt < RECONNECT_ATTEMPTS - 1)
            usleep(RECONNECT_DELAY_MS * 1000);
    }

    fprintf(stderr, "[42-client] Failed to connect to 42 Unix socket %s after %d attempts\n",
            socket_path, RECONNECT_ATTEMPTS);
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
    if (hostname) {
        strncpy(g_client.hostname, hostname, sizeof(g_client.hostname) - 1);
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
    char ack[4];
    ssize_t bytes_received;
    
    if (!g_client.connected) {
        fprintf(stderr, "[42-client] Not connected to 42\n");
        exit(1);
        return -1;
    }
    
    // Read state from 42 (blocking - waits for 42 to send next state)
    // This creates natural time synchronization with 42
    bytes_received = recv(g_client.socket_fd, g_client.rx_buffer, 
                         SOCKET_BUFFER_SIZE - 1, 0);  // Blocking read
    
    if (bytes_received < 0) {
        fprintf(stderr, "[42-client] Error receiving from 42: %s\n", strerror(errno));
        g_client.connected = 0;
        return -1;
    }
    
    if (bytes_received == 0) {
        fprintf(stderr, "[42-client] Connection closed by 42\n");
        g_client.connected = 0;
        exit(1);
        return -1;
    }
    
    g_client.rx_buffer[bytes_received] = '\0';
    
    // Send acknowledgment (42 expects "Ack" response)
    strcpy(ack, "Ack");
    send(g_client.socket_fd, ack, 4, 0);
    
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
    char msg[2048];  /* Larger buffer for batched commands */
    size_t msg_len = 0;
    
    if (!g_client.connected) {
        fprintf(stderr, "[42-client] Not connected to 42\n");
        return -1;
    }
    
    if (!commands || count <= 0) {
        return -1;
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
                /* Skip unsupported command types */
                break;
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
        ssize_t sent = send(g_client.socket_fd, msg, msg_len, 0);
        if (sent != (ssize_t)msg_len) {
            fprintf(stderr, "[42-client] Failed to send batched commands\n");
            return -1;
        }
        
        /* Read acknowledgment from 42 (TXRX mode expects Ack response) */
        ssize_t ack_received = recv(g_client.socket_fd, ack, 4, 0);
        if (ack_received < 0) {
            fprintf(stderr, "[42-client] Failed to receive Ack from 42\n");
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
    
    // Just send ENDMSG marker
    size_t msg_len = (size_t)snprintf(msg, sizeof(msg), "[ENDMSG]\n");
    
    ssize_t sent = send(g_client.socket_fd, msg, msg_len, 0);
    if (sent != (ssize_t)msg_len) {
        fprintf(stderr, "[42-client] Failed to send empty commands\n");
        return -1;
    }
    
    // Read acknowledgment from 42
    ssize_t ack_received = recv(g_client.socket_fd, ack, 4, 0);
    if (ack_received < 0) {
        fprintf(stderr, "[42-client] Failed to receive Ack from 42\n");
        return -1;
    }
    
    return 0;
}
