
/* Simulith Director
 * The main entry point for the Simulith simulation framework
 *
 * Configuration Management
 *   Read configuration files (likely JSON/YAML for structured data)
 *   Define which component simulators to load
 *   Configure simulation parameters (time step, duration, etc.)
 *   Handle 42 integration settings
 * Plugin/Component Loading
 *   Dynamic loading of component simulators (shared libraries or static linking)
 *   Initialize/cleanup component interfaces
 *   Manage component lifecycle within single process
 * Simulith Integration
 *   Connect to Simulith server for time synchronization
 *   Handle time step coordination
 *   Manage simulation state (start/stop/pause)
 * Data Management
 *   Shared memory or direct memory access between components
 *   Data flow coordination between simulators and 42
 *   State management and logging
*/

#include "simulith_director.h"
#include "simulith_42_socket_client.h"

director_config_t g_director_config;

static int g_udp_sock = -1;
static struct sockaddr_in g_udp_addr;
static int g_udp_publish_counter = 0;
static int g_backdoor_sock = -1;

static int ensure_backdoor_socket(void)
{
    if (g_backdoor_sock >= 0) return 0;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        perror("backdoor socket");
        return -1;
    }
    int reuse = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(BACKDOOR_PORT);
    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("backdoor bind");
        close(s);
        return -1;
    }
    // non-blocking
    int flags = fcntl(s, F_GETFL, 0);
    if (flags >= 0) fcntl(s, F_SETFL, flags | O_NONBLOCK);
    g_backdoor_sock = s;
    printf("Director backdoor listening on udp://0.0.0.0:%d\n", BACKDOOR_PORT);
    return 0;
}

static void process_backdoor_once(director_config_t* config)
{
    if (ensure_backdoor_socket() != 0) return;
    uint8_t buf[1500];
    struct sockaddr_in src;
    socklen_t slen = sizeof(src);
    ssize_t n = recvfrom(g_backdoor_sock, buf, sizeof(buf), 0, (struct sockaddr*)&src, &slen);
    if (n <= 0) return; // nothing to do

    static const uint8_t MAGIC[8] = { 'B','A','C','K','D','O','O','R' };
    if ((size_t)n < 8 + 1 + 2 + 2) return;
    if (memcmp(buf, MAGIC, 8) != 0) return;
    size_t off = 8;
    uint8_t tlen = buf[off++];
    if (tlen == 0 || tlen > 64) return;
    if (off + tlen + 2 + 2 > (size_t)n) return;
    char target[65];
    memcpy(target, &buf[off], tlen);
    target[tlen] = '\0';
    off += tlen;
    uint16_t cmd_id = (uint16_t)((buf[off] << 8) | buf[off+1]);
    off += 2;
    uint16_t plen = (uint16_t)((buf[off] << 8) | buf[off+1]);
    off += 2;
    if (off + plen > (size_t)n) return;
    const uint8_t* payload = &buf[off];

    // dispatch to component by name
    for (int i = 0; i < config->component_count; i++) {
        component_entry_t* ce = &config->components[i];
        if (!ce->active || !ce->interface) continue;
        if (!ce->interface->name) continue;
        if (strcmp(ce->interface->name, target) != 0) continue;
        if (ce->interface->backdoor) {
            ce->interface->backdoor(ce->state, cmd_id, payload, plen);
        }
        break;
    }
}

int parse_args(int argc, char *argv[], director_config_t *config) 
{
    // Set defaults
    strcpy(config->config_file, "spacecraft.conf");
    strcpy(config->components_dir, "./components");  // Default components directory
    strcpy(config->fortytwo_config, "./InOut");      // Default 42 configuration directory (Docker path)
    config->time_step_ms = 100;  // 100ms default
    config->duration_s = 0;      // Run indefinitely 
    config->verbose = 0;
    config->enable_42 = 1;       // Enable 42 by default now that we have the correct path
    config->fortytwo_initialized = 0;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--enable-42") == 0) {
            config->enable_42 = 1;
            printf("42 dynamics simulation enabled via command line\n");
        } else if (strcmp(argv[i], "--42-config") == 0 && i + 1 < argc) {
            strcpy(config->fortytwo_config, argv[++i]);
            printf("42 config directory set to: %s\n", config->fortytwo_config);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            config->verbose = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Simulith Director Options:\n");
            printf("  --enable-42        Enable 42 dynamics simulation\n");
            printf("  --42-config DIR    Set 42 configuration directory (default: ./InOut)\n");
            printf("  --verbose          Enable verbose output\n");
            printf("  --help             Show this help message\n");
            return -1;  // Exit after showing help
        }
    }
    
    return 0;
}

/* Worker thread: 
 * sleeps on tick_cond between ticks.  Using pthread_cond_wait frees the
 * CPU between ticks so the main thread's ZMQ spin-wait and the 42/FSW 
 * containers are not CPU-starved. */
static void* component_worker(void* arg)
{
    int idx = (int)(intptr_t)arg;
    uint64_t last_epoch = 0;

    for (;;) {
        pthread_mutex_lock(&g_director_config.tick_mutex);
        while (!g_director_config.threads_exit &&
               g_director_config.tick_epoch == last_epoch)
            pthread_cond_wait(&g_director_config.tick_cond, &g_director_config.tick_mutex);
        if (g_director_config.threads_exit) {
            pthread_mutex_unlock(&g_director_config.tick_mutex);
            return NULL;
        }
        last_epoch = g_director_config.tick_epoch;
        pthread_mutex_unlock(&g_director_config.tick_mutex);

        component_entry_t* e = &g_director_config.components[idx];
        if (e->active && e->interface && e->interface->tick && e->state)
            e->interface->tick(e->state, g_director_config.shared_tick_time_ns,
                               &g_director_config.shared_context_42);

        pthread_barrier_wait(&g_director_config.tick_barrier);
    }
    return NULL;
}

int load_components(director_config_t* config) 
{
    printf("Loading simulation components from: %s\n", config->components_dir);
    
    config->component_count = 0;
    config->lib_count = 0;
    
    DIR* dir = opendir(config->components_dir);
    if (!dir) {
        printf("Warning: Could not open components directory: %s (errno: %d)\n", 
               config->components_dir, errno);
        return 0;  // Not fatal - can run without components
    }
    
    printf("Successfully opened components directory\n");
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && config->component_count < MAX_COMPONENTS) {
        printf("Found directory entry: %s\n", entry->d_name);
        
        // Look for .so files
        if (strstr(entry->d_name, ".so") == NULL) {
            printf("  Skipping non-.so file: %s\n", entry->d_name);
            continue;
        }
        
        printf("  Found .so file: %s\n", entry->d_name);
        
        // Build full path
        char lib_path[512];
        snprintf(lib_path, sizeof(lib_path), "%s/%s", config->components_dir, entry->d_name);
        
        printf("Loading component library: %s\n", lib_path);
        
        // Load the shared library
        void* lib_handle = dlopen(lib_path, RTLD_LAZY);
        if (!lib_handle) {
            printf("Warning: Failed to load %s: %s\n", lib_path, dlerror());
            continue;
        }
        
        // Store library handle for cleanup
        if (config->lib_count < MAX_COMPONENT_LIBS) {
            config->lib_handles[config->lib_count++] = lib_handle;
        }
        
        // Use union to safely convert between object and function pointers
        union {
            void* obj;
            get_component_interface_fn func;
        } symbol_cast;

        symbol_cast.obj = dlsym(lib_handle, "get_component_interface");
        get_component_interface_fn get_interface = symbol_cast.func;
        
        if (!get_interface) {
            printf("Warning: Library %s does not export get_component_interface: %s\n", 
                   lib_path, dlerror());
            continue;
        }
        
        // Get the component interface
        const component_interface_t* interface = get_interface();
        if (!interface) {
            printf("Warning: Library %s returned NULL interface\n", lib_path);
            continue;
        }
        
        // Register the component
        config->components[config->component_count].interface = interface;
        config->components[config->component_count].state = NULL;
        config->components[config->component_count].lib_handle = lib_handle;
        config->components[config->component_count].active = 1;
        
        printf("Registered component: %s - %s\n", 
               interface->name, interface->description);
        config->component_count++;
    }
    
    closedir(dir);
    
    printf("Loaded %d components from shared libraries\n", config->component_count);
    return 0;
}

int initialize_components(director_config_t* config)
{
    printf("Initializing components...\n");
    
    for (int i = 0; i < config->component_count; i++) {
        component_entry_t* entry = &config->components[i];
        if (entry->active && entry->interface && entry->interface->init) {
            printf("Initializing component: %s\n", entry->interface->name);
            
            int result = entry->interface->init(&entry->state);
            if (result != COMPONENT_SUCCESS) {
                printf("Failed to initialize component: %s\n", entry->interface->name);
                entry->active = 0;
                return -1;
            }
        }
    }
    
    printf("All components initialized successfully\n");

    /* Spawn one worker thread per component for parallel ticking */
    config->tick_epoch    = 0;
    config->threads_exit  = 0;
    config->threads_spawned = 0;

    pthread_mutex_init(&config->tick_mutex, NULL);
    pthread_cond_init(&config->tick_cond, NULL);

    /* Barrier count: one slot per worker + one for the main thread.
     * A count of 1 (no components) lets the main thread pass immediately. */
    int barrier_count = (config->component_count > 0) ? config->component_count + 1 : 1;
    pthread_barrier_init(&config->tick_barrier, NULL, (unsigned)barrier_count);

    for (int i = 0; i < config->component_count; i++) {
        if (!config->components[i].active) continue;
        if (pthread_create(&config->component_threads[i], NULL,
                           component_worker, (void*)(intptr_t)i) != 0) {
            fprintf(stderr, "Failed to spawn worker thread for component %d\n", i);
            return -1;
        }
        config->threads_spawned++;
    }

    printf("Spawned %d component worker thread(s)\n", config->threads_spawned);
    return 0;
}

int initialize_42(director_config_t* config)
{
    if (!config->enable_42) {
        printf("42 simulation disabled\n");
        return 0;
    }
    
    printf("Connecting to 42 via socket IPC...\n");

    /* Prefer a Unix domain socket path if configured */
    const char *unix_path = getenv("FORTYTWO_SOCKET_PATH");
    const char *hostname;
    int port;

    if (unix_path && unix_path[0] == '/') {
        hostname = unix_path;
        port = 0;
        printf("Using Unix domain socket: %s\n", hostname);
    } else {
        hostname = getenv("FORTYTWO_HOST");
        if (!hostname) hostname = "shire-42";
        const char *port_str = getenv("FORTYTWO_PORT");
        port = port_str ? atoi(port_str) : 5556;
    }

    // Initialize socket connection to 42
    if (simulith_42_init(hostname, port) != 0) {
        printf("Warning: Failed to connect to 42 at %s:%d\n", hostname, port);
        printf("Exiting...\n");
        config->enable_42 = 0;
        exit(1);
        return 0;
    }
    
    config->fortytwo_initialized = 1;
    printf("Connected to 42 successfully\n");
    return 0;
}

void cleanup_components(director_config_t* config)
{
    printf("Cleaning up components...\n");

    /* Stop worker threads before calling component cleanup so that no tick
     * runs concurrently with cleanup(). Signal workers via the cond so they
     * wake from pthread_cond_wait and see threads_exit == 1. */
    if (config->threads_spawned > 0) {
        pthread_mutex_lock(&config->tick_mutex);
        config->threads_exit = 1;
        config->tick_epoch++;
        pthread_cond_broadcast(&config->tick_cond);
        pthread_mutex_unlock(&config->tick_mutex);

        for (int i = 0; i < config->component_count; i++) {
            if (config->components[i].active)
                pthread_join(config->component_threads[i], NULL);
        }
        pthread_barrier_destroy(&config->tick_barrier);
        pthread_mutex_destroy(&config->tick_mutex);
        pthread_cond_destroy(&config->tick_cond);
        config->threads_spawned = 0;
    }

    for (int i = 0; i < config->component_count; i++) {
        component_entry_t* entry = &config->components[i];
        if (entry->active && entry->interface && entry->interface->cleanup) {
            printf("Cleaning up component: %s\n", entry->interface->name);
            entry->interface->cleanup(entry->state);
            entry->state = NULL;
        }
        entry->active = 0;
    }
    
    // Cleanup 42 socket connection
    if (config->enable_42 && config->fortytwo_initialized) {
        simulith_42_cleanup();
    }
    
    // Close shared library handles
    for (int i = 0; i < config->lib_count; i++) {
        if (config->lib_handles[i]) {
            dlclose(config->lib_handles[i]);
            config->lib_handles[i] = NULL;
        }
    }
    config->lib_count = 0;
}

static void populate_42_context(simulith_42_context_t* context)
{
    // Initialize context
    memset(context, 0, sizeof(simulith_42_context_t));
    
    // Check if 42 is enabled and initialized
    if (!g_director_config.enable_42 || !g_director_config.fortytwo_initialized) {
        context->valid = 0;
        return;
    }
    
    // Request latest state from 42 via socket
    if (simulith_42_request_state(context) != 0) {
        context->valid = 0;
        return;
    }
    
    // Context is now populated by socket client
    // No need to access 42 globals directly
}

// Process commands and apply them to 42
static void process_42_commands(void)
{
    simulith_42_command_t commands[16];  /* Buffer for batching commands */
    int cmd_count = 0;
    
    if (!g_director_config.enable_42 || !g_director_config.fortytwo_initialized) {
        return;
    }
    
    /* Collect all commands from queue into batch buffer */
    while (cmd_count < 16 && dequeue_command(&commands[cmd_count]) == 0) {
        cmd_count++;
    }
    
    /* Send all commands in a single message to 42 */
    if (cmd_count > 0) {
        if (simulith_42_send_command_batch(commands, cmd_count) != 0) {
            if (g_director_config.verbose) {
                fprintf(stderr, "[director] Failed to send command batch to 42\n");
            }
        }
    } else {
        /* In TXRX mode, we must ALWAYS send something to 42, even if there are no commands */
        /* If no commands were collected, send an empty message with just [ENDMSG] */
        simulith_42_send_empty_commands();
    }
}

/* Per-tick phase timing — set to 1 to enable, 0 to disable (default off). */
#define DIRECTOR_TIMING_ENABLED 0
#define TICK_TIMING_INTERVAL    500

#if DIRECTOR_TIMING_ENABLED
static uint64_t ns_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
#endif

void on_tick(uint64_t tick_time_ns)
{
#if DIRECTOR_TIMING_ENABLED
    static uint64_t tick_count = 0;
    static uint64_t acc_42_fetch_ns   = 0;
    static uint64_t acc_sim_ticks_ns  = 0;
    static uint64_t acc_42_cmd_ns     = 0;
    static uint64_t acc_total_ns      = 0;
    static uint64_t acc_inter_tick_ns = 0;
    static uint64_t last_tick_end_ns  = 0;

    uint64_t t0 = ns_now();
    if (last_tick_end_ns > 0)
        acc_inter_tick_ns += t0 - last_tick_end_ns;
#endif

    /* Phase 1: Fetch 42 state. 42 has been stepping since the end of the
     * previous tick's process_42_commands() call, so its step is already done
     * and this returns quickly. */
    simulith_42_context_t context_42;
    populate_42_context(&context_42);
    g_director_config.shared_context_42 = context_42;

#if DIRECTOR_TIMING_ENABLED
    uint64_t t1 = ns_now();
#endif

    /* Phase 2: Tick all component sims in parallel using the fresh 42 context.
     * Workers sleep in pthread_cond_wait between ticks; this frees their CPU
     * cores for 42 and FSW so the main thread's ZMQ spin-wait is not starved. */
    pthread_mutex_lock(&g_director_config.tick_mutex);
    g_director_config.shared_tick_time_ns = tick_time_ns;
    g_director_config.tick_epoch++;
    pthread_cond_broadcast(&g_director_config.tick_cond);
    pthread_mutex_unlock(&g_director_config.tick_mutex);

    if (g_director_config.threads_spawned > 0) {
        pthread_barrier_wait(&g_director_config.tick_barrier);
    }

#if DIRECTOR_TIMING_ENABLED
    uint64_t t2 = ns_now();
#endif

    /* Phase 3: Send accumulated commands to 42.  This releases 42 to run its
     * next dynamics step, which will overlap with the next tick's sims phase. */
    process_42_commands();

#if DIRECTOR_TIMING_ENABLED
    uint64_t t3 = ns_now();

    acc_42_fetch_ns  += t1 - t0;
    acc_sim_ticks_ns += t2 - t1;
    acc_42_cmd_ns    += t3 - t2;
    acc_total_ns     += t3 - t0;
    tick_count++;
    last_tick_end_ns  = t3;

    if (tick_count % TICK_TIMING_INTERVAL == 0) {
        double scale = 1.0 / (double)TICK_TIMING_INTERVAL;
        double avg_inter = (double)acc_inter_tick_ns * scale / 1000.0;
        double avg_total = (double)acc_total_ns      * scale / 1000.0;
        printf("[director timing] avg over %d ticks:"
               "  inter=%.0f µs  42_fetch=%.0f µs  sims=%.0f µs  42_cmd=%.0f µs"
               "  on_tick=%.0f µs  period=%.0f µs\n",
               TICK_TIMING_INTERVAL,
               avg_inter,
               (double)acc_42_fetch_ns  * scale / 1000.0,
               (double)acc_sim_ticks_ns * scale / 1000.0,
               (double)acc_42_cmd_ns    * scale / 1000.0,
               avg_total,
               avg_inter + avg_total);
        acc_42_fetch_ns = acc_sim_ticks_ns = acc_42_cmd_ns = acc_total_ns = acc_inter_tick_ns = 0;
    }
#endif

    // Service backdoor packets
    process_backdoor_once(&g_director_config);

    // Publish telemetry
    g_udp_publish_counter = (g_udp_publish_counter + 1) % UDP_PUBLISH_INTERVAL_TICKS;
    if (g_udp_sock >= 0 && context_42.valid && g_udp_publish_counter == 0)
    {
        // Packet structure matches XTCE SIM_42_TRUTH_DATA:
        // DYN_TIME, POSITION_N_1/2/3, SVB_1/2/3, BVB_1/2/3, HVB_1/2/3, WN_1/2/3, QN_1/2/3/4, MASS, CM_1/2/3, INERTIA_11/12/13/21/22/23/31/32/33, ECLIPSE, ATMO_DENSITY
        unsigned char packet[276]; // Exact size: 34 doubles (8 bytes each) + 1 int (4 bytes) = 276 bytes
        memset(packet, 0, sizeof(packet));
        size_t offset = 0;
        // 1. DYN_TIME
        double d_dyn_time = context_42.dyn_time;
        memcpy(packet+offset, &d_dyn_time, sizeof(double)); offset += sizeof(double);
        // 2-4. POSITION_N_1/2/3
        for (int i = 0; i < 3; i++) { double d = context_42.pos_n[i]; memcpy(packet+offset, &d, sizeof(double)); offset += sizeof(double); }
        // 5-7. SVB_1/2/3
        for (int i = 0; i < 3; i++) { double d = context_42.sun_vector_body[i]; memcpy(packet+offset, &d, sizeof(double)); offset += sizeof(double); }
        // 8-10. BVB_1/2/3
        for (int i = 0; i < 3; i++) { double d = context_42.mag_field_body[i]; memcpy(packet+offset, &d, sizeof(double)); offset += sizeof(double); }
        // 11-13. HVB_1/2/3
        for (int i = 0; i < 3; i++) { double d = context_42.hvb[i]; memcpy(packet+offset, &d, sizeof(double)); offset += sizeof(double); }
        // 14-16. WN_1/2/3
        for (int i = 0; i < 3; i++) { double d = context_42.wn[i]; memcpy(packet+offset, &d, sizeof(double)); offset += sizeof(double); }
        // 17-20. QN_1/2/3/4
        for (int i = 0; i < 4; i++) { double d = context_42.qn[i]; memcpy(packet+offset, &d, sizeof(double)); offset += sizeof(double); }
        // 21. MASS
        double d_mass = context_42.mass;
        memcpy(packet+offset, &d_mass, sizeof(double)); offset += sizeof(double);
        // 22-24. CM_1/2/3
        for (int i = 0; i < 3; i++) { double d = context_42.cm[i]; memcpy(packet+offset, &d, sizeof(double)); offset += sizeof(double); }
        // 25-33. INERTIA_11/12/13/21/22/23/31/32/33
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { double d = context_42.inertia[i][j]; memcpy(packet+offset, &d, sizeof(double)); offset += sizeof(double); }
        // 34. ECLIPSE (int)
        int ecl = context_42.eclipse;
        memcpy(packet+offset, &ecl, sizeof(int)); offset += sizeof(int);
        // 35. ATMO_DENSITY
        double d_atmo_density = context_42.atmo_density;
        memcpy(packet+offset, &d_atmo_density, sizeof(double)); offset += sizeof(double);
        // Send exactly 276 bytes
        sendto(g_udp_sock, packet, 276, 0, (struct sockaddr*)&g_udp_addr, sizeof(g_udp_addr));
    }
}

int main(int argc, char *argv[]) 
{
    printf("Simulith Director starting...\n");
    
    int parse_result = parse_args(argc, argv, &g_director_config);
    if (parse_result < 0) {
        return 0;  // Help was shown or parsing failed
    } else if (parse_result > 0) {
        fprintf(stderr, "Failed to parse arguments\n");
        return 1;
    }

    if (load_components(&g_director_config) != 0) 
    {
        fprintf(stderr, "Failed to load components\n");
        return 1;
    }

    if (initialize_components(&g_director_config) != 0)
    {
        fprintf(stderr, "Failed to initialize components\n");
        cleanup_components(&g_director_config);
        return 1;
    }

    if (initialize_42(&g_director_config) != 0)
    {
        fprintf(stderr, "Warning: 42 simulation initialization had issues, continuing without it\n");
        g_director_config.enable_42 = 0;
        g_director_config.fortytwo_initialized = 0;
    }

    // UDP Telemetry Socket Init
    g_udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_udp_sock < 0) 
    {
        perror("UDP socket creation failed");
    } 
    else 
    {
        memset(&g_udp_addr, 0, sizeof(g_udp_addr));
        g_udp_addr.sin_family = AF_INET;
        g_udp_addr.sin_port = htons(50042); // Default port for 42 telemetry

        // Resolve shire-gsw hostname
        const char* gsw_hostname = "shire-gsw";
        struct hostent* gsw_host = gethostbyname(gsw_hostname);
        if (gsw_host && gsw_host->h_addrtype == AF_INET) {
            memcpy(&g_udp_addr.sin_addr, gsw_host->h_addr_list[0], (size_t)gsw_host->h_length);
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &g_udp_addr.sin_addr, ip_str, sizeof(ip_str));
            printf("UDP telemetry publisher initialized for YAMCS at %s:50042\n", ip_str);
        } else {
            printf("Warning: Could not resolve hostname '%s', defaulting to 127.0.0.1\n", gsw_hostname);
            g_udp_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        }
    }

    // Wait a second for the Simulith server to start up
    sleep(1);

    if (simulith_client_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, "shire-director", INTERVAL_NS) != 0) 
    {
        printf("Failed to initialize Simulith client\n");
        cleanup_components(&g_director_config);
        return 1;
    }

    // Handshake with Simulith server
    if (simulith_client_handshake() != 0) 
    {
        printf("Failed to handshake with Simulith server\n");
        simulith_client_shutdown();
        cleanup_components(&g_director_config);
        return 1;
    }

    simulith_client_run_loop(on_tick);
    
    printf("Simulith director shutting down...\n");
    
    // Cleanup
    simulith_client_shutdown();
    cleanup_components(&g_director_config);
    
    return 0;
}
