#include "simulith.h"
#include <sched.h>
#include <signal.h>

#define MAX_CLIENTS 32

typedef struct
{
    char id[64];
    int  responded;
} ClientState;

static void       *server_context             = NULL;
static void       *publisher                  = NULL;
static void       *responder                  = NULL;
static uint64_t    current_time_ns            = 0;
static uint64_t    tick_interval_ns           = 0;
static int         expected_clients           = 0;
static ClientState client_states[MAX_CLIENTS] = {0};

/* Test/debug helper: request server shutdown from other threads. */
static volatile sig_atomic_t simulith_server_stop_requested = 0;

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

int simulith_server_init(const char *pub_bind, const char *rep_bind, int client_count, uint64_t interval_ns)
{
    /* Clear any previous stop request so a fresh server run isn't short-circuited. */
    simulith_server_stop_requested = 0;

    // Validate parameters
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

    // Initialize client states
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        client_states[i].id[0]     = '\0';
        client_states[i].responded = 0;
    }

    simulith_log("Simulith server initialized. Clients expected: %d\n", expected_clients);
    return 0;
}

// Global for speed tracking
static double g_attempted_speed = 1.0;
static uint64_t g_last_log_real_ns = 0;

static void broadcast_time(void)
{
    static uint64_t last_log_time = 0;
    static const uint64_t LOG_INTERVAL_NS = 10000000000; // Log every 10 seconds

    zmq_send(publisher, &current_time_ns, sizeof(current_time_ns), 0);

    // Only log time broadcasts every LOG_INTERVAL_NS
    if (current_time_ns - last_log_time >= LOG_INTERVAL_NS) 
    {
        // Calculate actual speed (sim seconds per real second)
        struct timespec now_ts;
        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        uint64_t now_real_ns = (uint64_t)now_ts.tv_sec * 1000000000ULL + (uint64_t)now_ts.tv_nsec;
        double sim_elapsed = (double)(current_time_ns - last_log_time) / 1e9;
        double real_elapsed = (g_last_log_real_ns > 0) ? ((double)(now_real_ns - g_last_log_real_ns) / 1e9) : 0.0;
        double actual_speed = (real_elapsed > 0.0) ? (sim_elapsed / real_elapsed) : 0.0;

        simulith_log("  Simulation time: %.3f seconds | Attempted speed: %.2fx | Actual: %.2fx\n",
            (double)current_time_ns / 1e9, g_attempted_speed, actual_speed);

        last_log_time = current_time_ns;
        g_last_log_real_ns = now_real_ns;
    }
}

static int all_clients_responded(void)
{
    int count = 0;
    for (int i = 0; i < expected_clients; ++i)
    {
        if (client_states[i].responded)
            count++;
    }
    return count == expected_clients;
}

static void reset_responses(void)
{
    for (int i = 0; i < expected_clients; ++i)
    {
        client_states[i].responded = 0;
    }
}

static void handle_ack(const char *client_id)
{
    for (int i = 0; i < expected_clients; ++i)
    {
        if (client_states[i].id[0] != '\0' && strcmp(client_states[i].id, client_id) == 0)
        {
            client_states[i].responded = 1;
            return;
        }
    }
    simulith_log("ACK received from unknown client: %s\n", client_id);
}

void simulith_server_run(void)
{
    simulith_log("Waiting for clients to be ready...\n");

    // Wait for all clients to send "READY"
    int ready_clients = 0;
    while (ready_clients < expected_clients)
    {
        if (simulith_server_stop_requested) {
            simulith_log("Server shutdown requested while waiting for READY\n");
            return;
        }

        char buffer[64] = {0};
        int  size       = zmq_recv(responder, buffer, sizeof(buffer) - 1, 0);
        if (size > 0)
        {
            buffer[size] = '\0';

            // Parse READY message
            char *space = strchr(buffer, ' ');
            if (!space || strncmp(buffer, "READY", 5) != 0)
            {
                simulith_log("Invalid handshake message: %s\n", buffer);
                zmq_send(responder, "ERR", 3, 0);
                continue;
            }

            // Extract client ID (skip "READY " prefix)
            char *client_id = space + 1;
            if (strlen(client_id) == 0)
            {
                simulith_log("Empty client ID in handshake\n");
                zmq_send(responder, "ERR", 3, 0);
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
            strncpy(client_states[slot].id, client_id, sizeof(client_states[slot].id) - 1);
            client_states[slot].id[sizeof(client_states[slot].id) - 1] = '\0';
            client_states[slot].responded                              = 0;
            ready_clients++;

            zmq_send(responder, "ACK", 3, 0);
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

    simulith_log("All clients ready. Starting time broadcast.\n");

    // Reset client responded flags for tick ACKs
    reset_responses();

    // CLI state
    int paused = 0;
    int running = 1;
    double speed = 1.0; // 1.0 = real time
    g_attempted_speed = speed;
    fd_set readfds;
    struct timeval tv;
    char cli_buf[32];

    printf("Simulith CLI started. Type 'p' (pause/play), '+' (faster), or '-' (slower).\n");

    while (running)
    {
        // Check for CLI input (non-blocking)
        FD_ZERO(&readfds);
        FD_SET(0, &readfds); // stdin
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        int cli_ready = select(1, &readfds, NULL, NULL, &tv);
        if (cli_ready > 0 && FD_ISSET(0, &readfds)) 
        {
            if (fgets(cli_buf, sizeof(cli_buf), stdin)) 
            {
                if (strncmp(cli_buf, "p", 1) == 0) 
                {
                    paused = !paused;
                    printf(paused ? "Simulation paused.\n" : "Simulation resumed.\n");
                } else if (strncmp(cli_buf, "+", 1) == 0) 
                {
                    speed *= 2.0;
                    if (speed > 1024.0) speed = 1024.0;
                    g_attempted_speed = speed;
                    printf("Attempted simulation speed: %.2fx\n", speed);
                } else if (strncmp(cli_buf, "-", 1) == 0) 
                {
                    speed /= 2.0;
                    if (speed < 0.015625) speed = 0.015625;
                    g_attempted_speed = speed;
                    printf("Attempted simulation speed: %.4fx\n", speed);
                } else if (strncmp(cli_buf, "quit", 4) == 0) 
                {
                    running = 0;
                    printf("Exiting simulation.\n");
                    break;
                } else 
                {
                    printf("Unknown command. Use 'p', '+', or '-'.\n");
                }
            }
        }

        if (!paused) 
        {
            struct timespec start_ts, end_ts;
            clock_gettime(CLOCK_MONOTONIC, &start_ts);

            broadcast_time();
            reset_responses();

            while (!all_clients_responded() && running) 
            {
                char buffer[64] = {0};
                int  size       = zmq_recv(responder, buffer, sizeof(buffer) - 1, ZMQ_DONTWAIT);
                if (size > 0) 
                {
                    buffer[size] = '\0';
                    handle_ack(buffer);
                    zmq_send(responder, "ACK", 3, 0);
                }
                else if (errno == EAGAIN)
                {
                    // No message available, yield CPU more aggressively for high speed
                    if (speed >= 256.0) {
                        // At extreme speeds, pure busy wait with minimal overhead
                        continue;
                    } else if (speed >= 128.0) {
                        // At very high speeds, don't yield at all - busy wait
                        continue;
                    } else if (speed >= 64.0) {
                        // At high speeds, minimal yield
                        sched_yield();
                    } else if (speed >= 16.0) {
                        // At medium speeds, minimal yield
                        sched_yield();
                    } else {
                        // At lower speeds, small sleep is fine
                        usleep(1);
                    }
                }
                
                // Check for CLI input during wait (much less frequently at high speeds)
                static int cli_check_counter = 0;
                int cli_check_interval = (speed >= 256.0) ? 50000 : (speed >= 128.0) ? 20000 : (speed >= 64.0) ? 10000 : (speed >= 16.0) ? 1000 : 100;
                if (++cli_check_counter % cli_check_interval == 0)
                {
                    FD_ZERO(&readfds);
                    FD_SET(0, &readfds);
                    tv.tv_sec = 0;
                    tv.tv_usec = 0;
                    cli_ready = select(1, &readfds, NULL, NULL, &tv);
                    if (cli_ready > 0 && FD_ISSET(0, &readfds)) 
                    {
                        if (fgets(cli_buf, sizeof(cli_buf), stdin)) 
                        {
                            if (strncmp(cli_buf, "p", 1) == 0) 
                            {
                                paused = !paused;
                                printf(paused ? "Simulation paused.\n" : "Simulation resumed.\n");
                            } else if (strncmp(cli_buf, "+", 1) == 0) 
                            {
                                speed *= 2.0;
                                if (speed > 1024.0) speed = 1024.0;
                                g_attempted_speed = speed;
                                printf("Attempted simulation speed: %.2fx\n", speed);
                            } else if (strncmp(cli_buf, "-", 1) == 0) 
                            {
                                speed /= 2.0;
                                if (speed < 0.015625) speed = 0.015625;
                                g_attempted_speed = speed;
                                printf("Attempted simulation speed: %.4fx\n", speed);
                            } else 
                            {
                                printf("Unknown command. Use 'p', '+', or '-'.\n");
                            }
                        }
                    }
                }
            }

            // Sleep to simulate real time (adjusted by speed), accounting for processing time
            if (speed > 0.0) 
            {
                clock_gettime(CLOCK_MONOTONIC, &end_ts);
                /* Use signed 64-bit for intermediate differences to avoid sign-conversion warnings */
                int64_t sec_diff = (int64_t)end_ts.tv_sec - (int64_t)start_ts.tv_sec;
                int64_t nsec_diff = (int64_t)end_ts.tv_nsec - (int64_t)start_ts.tv_nsec;
                uint64_t elapsed_ns = (uint64_t)(sec_diff * 1000000000LL + nsec_diff);
                uint64_t target_ns = (uint64_t)((double)tick_interval_ns / speed);
                
                if (elapsed_ns < target_ns) 
                {
                    uint64_t sleep_ns = target_ns - elapsed_ns;
                    
                    // At very high speeds, skip sleep entirely for maximum performance
                    if (speed >= 256.0) {
                        // No sleep - run as fast as possible with maximum performance
                    } else if (speed >= 128.0) {
                        // No sleep - run as fast as possible
                    } else if (speed >= 64.0) {
                        // Very short busy wait for precise timing
                        struct timespec start_wait, now_wait;
                        clock_gettime(CLOCK_MONOTONIC, &start_wait);
                        do {
                            clock_gettime(CLOCK_MONOTONIC, &now_wait);
                        } while (((int64_t)now_wait.tv_sec - (int64_t)start_wait.tv_sec) * 1000000000LL +
                                ((int64_t)now_wait.tv_nsec - (int64_t)start_wait.tv_nsec) < (int64_t)sleep_ns);
                    } else {
                        // Normal nanosleep for lower speeds
                        struct timespec ts;
                        ts.tv_sec = (time_t)(sleep_ns / 1000000000ULL);
                        ts.tv_nsec = (long)(sleep_ns % 1000000000ULL);
                        nanosleep(&ts, NULL);
                    }
                }
            }
            current_time_ns += tick_interval_ns;
        } else 
        {
            // If paused, sleep briefly to avoid busy loop
            usleep(100000);
        }
    }
}

void simulith_server_shutdown(void)
{
    /* Request the server loop to stop, then proceed to close sockets. */
    simulith_server_stop_requested = 1;

    if (publisher)
        zmq_close(publisher);
    if (responder)
        zmq_close(responder);
    if (server_context)
        zmq_ctx_term(server_context);
    publisher      = NULL;
    responder      = NULL;
    server_context = NULL;
    simulith_log("Simulith server shut down\n");
}
