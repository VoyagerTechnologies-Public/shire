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

// Component registry entry
typedef struct {
    const component_interface_t* interface;
    component_state_t* state;
    void* lib_handle;
    int active;
} component_entry_t;

// Director configuration structure
typedef struct 
{
    char config_file[256];
    char components_dir[256];
    int time_step_ms;
    int duration_s;
    int verbose;
    
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

    // Parallel component tick thread pool
    pthread_t         component_threads[MAX_COMPONENTS];
    pthread_barrier_t tick_barrier;
    pthread_mutex_t   tick_mutex;       /* guards tick_epoch, threads_exit, and cond wait */
    pthread_cond_t    tick_cond;        /* workers sleep here between ticks */
    uint64_t          tick_epoch;       /* incremented under tick_mutex each tick */
    int               threads_exit;     /* set to 1 under tick_mutex to stop workers */
    int               threads_spawned;  /* number of live worker threads */
    uint64_t          shared_tick_time_ns;
    simulith_42_context_t shared_context_42;
} director_config_t;

// Function declarations

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

// Global director configuration (for callback access)
extern director_config_t g_director_config;

#ifdef __cplusplus
}
#endif

#endif // SIMULITH_DIRECTOR_H
