#ifndef SIMULITH_DIRECTOR_H
#define SIMULITH_DIRECTOR_H

#include <arpa/inet.h>
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "simulith.h"
#include "simulith_42_context.h"
#include "simulith_42_commands.h"
#include "simulith_component.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configuration definitions
#define BACKDOOR_PORT 50060
#define MAX_COMPONENTS 32
#define MAX_COMPONENT_LIBS 32
#define UDP_PUBLISH_INTERVAL_TICKS 100 // 100 ticks = 1s
#define SIMULITH_42_TELEMETRY_SIZE 276
#define SIMULITH_SCENARIO_MAX_COMMANDS 32
#define SIMULITH_SCENARIO_MAX_PACKET_SIZE 2048

typedef struct
{
    uint64_t sequence;
    char host[128];
    uint16_t port;
    struct sockaddr_in destination;
    uint8_t packet[SIMULITH_SCENARIO_MAX_PACKET_SIZE];
    size_t packet_length;
    int injected;
} simulith_scenario_command_t;

// Component registry entry
typedef struct {
    const component_interface_t* interface;
    component_state_t* state;
    void* lib_handle;
    int active;
    int phase_status;
} component_entry_t;

// Director configuration structure
typedef struct 
{
    char config_file[256];
    char components_dir[256];
    int time_step_ms;
    int duration_s;
    int verbose;
    char scenario_file[256];
    simulith_scenario_command_t scenario_commands[SIMULITH_SCENARIO_MAX_COMMANDS];
    size_t scenario_command_count;
    uint64_t scenario_digest;
    uint64_t scenario_injected;
    uint64_t scenario_errors;
    int scenario_socket;
    int scenario_socket_initialized;
    
    // 42 integration
    int enable_42;
    char fortytwo_config[256];
    int fortytwo_initialized;
    
    // Component management
    component_entry_t components[MAX_COMPONENTS];
    int component_count;
    
    // Library handles for cleanup
    void* lib_handles[MAX_COMPONENT_LIBS];
    int lib_count;

    // Concurrent device-service workers; PREPARE and ACTUATE stay ordered.
    pthread_t         component_threads[MAX_COMPONENTS];
    int               component_thread_started[MAX_COMPONENTS];
    int               component_interrupt_fds[MAX_COMPONENTS];
    uint64_t          component_wait_epochs[MAX_COMPONENTS];
    pthread_mutex_t   tick_mutex;
    pthread_cond_t    tick_cond;
    uint64_t          execute_epoch;
    int               threads_exit;     /* set to 1 under tick_mutex to stop workers */
    int               execute_active;   /* service device I/O until COMMIT */
    size_t            active_service_callbacks;
    int               threads_spawned;  /* number of live worker threads */
    int               worker_sync_initialized;
    uint64_t          component_phase_errors;
    uint64_t          component_service_errors;
    uint64_t          shared_tick_sequence;
    uint64_t          shared_tick_time_ns;
    simulith_42_context_t shared_context_42;
} director_config_t;

// Function declarations
int director_configure_trace_duration(void);

/**
 * Parse command line arguments
 * @param argc Argument count
 * @param argv Argument values
 * @param config Configuration structure to populate
 * @return 0 on success, -1 on error
 */
int parse_args(int argc, char *argv[], director_config_t *config);

/**
 * Load components from shared libraries
 * @param config Director configuration
 * @return 0 on success, -1 on error
 */
int load_components(director_config_t* config);

/**
 * Initialize all loaded components
 * @param config Director configuration
 * @return 0 on success, -1 on error
 */
int initialize_components(director_config_t* config);

/**
 * Cleanup all components and close libraries
 * @param config Director configuration
 */
void cleanup_components(director_config_t* config);

/**
 * Initialize 42 dynamics simulation
 * @param config Director configuration
 * @return 0 on success, -1 on error
 */
int initialize_42(director_config_t* config);

/** Initialize the director's UDP telemetry publisher. */
int initialize_telemetry(void);

/** Load and validate the optional versioned scenario file. */
int initialize_scenario(director_config_t *config);

/** Inject commands scheduled for this COMMIT sequence exactly once. */
int director_inject_scenario_commands(director_config_t *config,
                                      uint64_t sequence);

/** Serialize the fixed-layout 42 truth telemetry packet. */
size_t simulith_serialize_42_telemetry(const simulith_42_context_t *context,
                                      uint8_t *packet, size_t packet_capacity);

/**
 * Execute one 42 simulation step
 * @return 0 on success, 1 if simulation is complete, -1 on error
 */
int step_42(void);

/**
 * Cleanup 42 simulation
 */
void cleanup_42(void);

/**
 * Tick callback function for Simulith time stepping
 * @param tick_time_ns Current simulation time in nanoseconds
 */
void on_tick(uint64_t tick_time_ns);
int director_prepare_tick(uint64_t sequence, uint64_t tick_time_ns);
int director_execute_tick(uint64_t sequence, uint64_t tick_time_ns);
int director_commit_tick(uint64_t sequence, uint64_t tick_time_ns);
void director_write_terminal_metrics(void);

// Global director configuration (for callback access)
extern director_config_t g_director_config;

#ifdef __cplusplus
}
#endif

#endif // SIMULITH_DIRECTOR_H
