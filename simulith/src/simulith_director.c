
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

#include <ctype.h>
#include <sys/eventfd.h>
#include <time.h>
#include "simulith_42_socket_client.h"
#include "simulith_control_trace.h"

director_config_t g_director_config;

static int g_udp_sock = -1;
static struct sockaddr_in g_udp_addr;
static int g_udp_publish_counter = 0;
static int g_backdoor_sock = -1;
static uint64_t g_prepare_count = 0;
static uint64_t g_commit_count = 0;
static uint64_t g_telemetry_count = 0;
static uint64_t g_telemetry_errors = 0;
static uint64_t g_fortytwo_errors = 0;
static uint64_t g_trace_final_time_ns = UINT64_MAX;
static uint64_t g_worker_ready_count = 0;
static uint64_t g_worker_parked_count = 0;
static uint64_t g_worker_admission_total_ns = 0;
static uint64_t g_worker_admission_max_ns = 0;
static uint64_t director_now_ns(void);
static void director_reset_timing(void);
static void director_write_timing(void);

static int component_interface_is_compatible(const component_interface_t *interface,
                                             const char *library)
{
    const char *source = library ? library : "component";
    if (!interface)
    {
        printf("Warning: %s returned a NULL component interface\n", source);
        return 0;
    }
    if (interface->api_version != SIMULITH_COMPONENT_API_VERSION ||
        interface->struct_size != sizeof(component_interface_t))
    {
        printf("Warning: %s has incompatible component API version/size "
               "(%u/%u, expected %u/%zu)\n", source,
               (unsigned)interface->api_version,
               (unsigned)interface->struct_size,
               (unsigned)SIMULITH_COMPONENT_API_VERSION,
               sizeof(component_interface_t));
        return 0;
    }
    if (!interface->name || interface->name[0] == '\0' ||
        !interface->create || !interface->destroy)
    {
        printf("Warning: %s component interface requires a name, create, and destroy\n",
               source);
        return 0;
    }
    if ((interface->service == NULL) != (interface->wait_for_service == NULL))
    {
        printf("Warning: %s component interface requires service and "
               "wait_for_service together\n", source);
        return 0;
    }
    return 1;
}

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
    if (!config)
        return 1;

    memset(config, 0, sizeof(*config));
    config->scenario_socket = -1;
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
        } else if (strcmp(argv[i], "--42-config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--42-config requires a directory\n");
                return 1;
            }
            const char *path = argv[++i];
            if (snprintf(config->fortytwo_config,
                         sizeof(config->fortytwo_config), "%s", path) >=
                (int)sizeof(config->fortytwo_config)) {
                fprintf(stderr, "--42-config path is too long\n");
                return 1;
            }
            printf("42 config directory set to: %s\n", config->fortytwo_config);
        } else if (strcmp(argv[i], "--scenario") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--scenario requires a JSON file\n");
                return 1;
            }
            if (snprintf(config->scenario_file, sizeof(config->scenario_file),
                         "%s", argv[++i]) >= (int)sizeof(config->scenario_file)) {
                fprintf(stderr, "--scenario path is too long\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--verbose") == 0) {
            config->verbose = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Simulith Director Options:\n");
            printf("  --enable-42        Enable 42 dynamics simulation\n");
            printf("  --42-config DIR    Set 42 configuration directory (default: ./InOut)\n");
            printf("  --scenario FILE    Inject versioned, sequence-numbered UDP commands\n");
            printf("  --verbose          Enable verbose output\n");
            printf("  --help             Show this help message\n");
            return -1;  // Exit after showing help
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }
    
    return 0;
}

/* One worker owns each component's device service callback. PREPARE/on_tick
 * and COMMIT/actuate remain sequential in the director so tiny callbacks do
 * not incur a thread wake and barrier round trip. */
static void signal_component_worker(int fd)
{
    uint64_t one = 1;
    if (fd < 0)
        return;
    while (write(fd, &one, sizeof(one)) < 0 && errno == EINTR)
    {
    }
}

static void drain_component_worker_signal(int fd)
{
    uint64_t value;
    if (fd < 0)
        return;
    while (read(fd, &value, sizeof(value)) < 0 && errno == EINTR)
    {
    }
}

static void* component_worker(void* arg)
{
    int idx = (int)(intptr_t)arg;
    component_entry_t* entry = &g_director_config.components[idx];

    for (;;)
    {
        int ready = entry->interface->wait_for_service(
            entry->state, g_director_config.component_interrupt_fds[idx]);
        uint64_t ready_at_ns = director_now_ns();
        pthread_mutex_lock(&g_director_config.tick_mutex);
        if (g_director_config.threads_exit) {
            pthread_mutex_unlock(&g_director_config.tick_mutex);
            return NULL;
        }
        if (ready == COMPONENT_IDLE) {
            pthread_mutex_unlock(&g_director_config.tick_mutex);
            drain_component_worker_signal(
                g_director_config.component_interrupt_fds[idx]);
            continue;
        }
            if (ready == COMPONENT_ERROR)
            {
                entry->phase_status = COMPONENT_ERROR;
                g_director_config.component_service_errors++;
                pthread_cond_broadcast(&g_director_config.tick_cond);
                pthread_mutex_unlock(&g_director_config.tick_mutex);
                simulith_log("Component %s failed service wait on tick %lu\n",
                             entry->interface->name,
                             (unsigned long)g_director_config.shared_tick_sequence);
                return NULL;
            }
            /* A ready request may arrive between ticks. Keep its readiness
             * across that boundary, but only admit service during EXECUTE.
             * COMMIT closes admission under this mutex and waits for every
             * already admitted callback before ACTUATE. */
            if (!g_director_config.execute_active)
                __atomic_add_fetch(&g_worker_parked_count, 1, __ATOMIC_RELAXED);
            while (!g_director_config.threads_exit &&
                   !g_director_config.execute_active)
                pthread_cond_wait(&g_director_config.tick_cond,
                                  &g_director_config.tick_mutex);
            if (g_director_config.threads_exit) {
                pthread_mutex_unlock(&g_director_config.tick_mutex);
                return NULL;
            }
            g_director_config.active_service_callbacks++;
            uint64_t admission_ns = director_now_ns() - ready_at_ns;
            __atomic_add_fetch(&g_worker_ready_count, 1, __ATOMIC_RELAXED);
            __atomic_add_fetch(&g_worker_admission_total_ns, admission_ns,
                               __ATOMIC_RELAXED);
            uint64_t prior_max = __atomic_load_n(&g_worker_admission_max_ns,
                                                 __ATOMIC_RELAXED);
            while (admission_ns > prior_max &&
                   !__atomic_compare_exchange_n(&g_worker_admission_max_ns,
                                                &prior_max, admission_ns, 0,
                                                __ATOMIC_RELAXED,
                                                __ATOMIC_RELAXED)) {}
            pthread_mutex_unlock(&g_director_config.tick_mutex);

            int work = entry->interface->service(
                entry->state, g_director_config.shared_tick_time_ns,
                &g_director_config.shared_context_42);

            pthread_mutex_lock(&g_director_config.tick_mutex);
            g_director_config.active_service_callbacks--;
            if (work == COMPONENT_ERROR)
            {
                entry->phase_status = COMPONENT_ERROR;
                g_director_config.component_service_errors++;
                pthread_cond_broadcast(&g_director_config.tick_cond);
                pthread_mutex_unlock(&g_director_config.tick_mutex);
                simulith_log("Component %s failed EXECUTE on tick %lu\n",
                             entry->interface->name,
                             (unsigned long)g_director_config.shared_tick_sequence);
                return NULL;
            }
            pthread_cond_broadcast(&g_director_config.tick_cond);
            pthread_mutex_unlock(&g_director_config.tick_mutex);
    }
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
        
        // Look for shared-library filenames ending in .so.
        size_t name_length = strlen(entry->d_name);
        if (name_length < 3 || strcmp(entry->d_name + name_length - 3, ".so") != 0) {
            printf("  Skipping non-.so file: %s\n", entry->d_name);
            continue;
        }
        
        printf("  Found .so file: %s\n", entry->d_name);
        
        // Build full path
        char lib_path[512];
        if (snprintf(lib_path, sizeof(lib_path), "%s/%s", config->components_dir,
                     entry->d_name) >= (int)sizeof(lib_path)) {
            printf("Warning: Component library path is too long: %s\n", entry->d_name);
            continue;
        }
        
        printf("Loading component library: %s\n", lib_path);
        
        // Load the shared library
        void* lib_handle = dlopen(lib_path, RTLD_NOW);
        if (!lib_handle) {
            printf("Warning: Failed to load %s: %s\n", lib_path, dlerror());
            continue;
        }
        
        // Use union to safely convert between object and function pointers
        union {
            void* obj;
            get_component_interface_fn func;
        } symbol_cast;

        symbol_cast.obj = dlsym(lib_handle, SIMULITH_COMPONENT_ENTRY_POINT);
        get_component_interface_fn get_interface = symbol_cast.func;
        
        if (!get_interface) {
            printf("Warning: Library %s does not export get_component_interface: %s\n",
                   lib_path, dlerror());
            dlclose(lib_handle);
            continue;
        }
        
        // Get the component interface
        const component_interface_t* interface = get_interface();
        if (!component_interface_is_compatible(interface, lib_path)) {
            dlclose(lib_handle);
            continue;
        }

        int duplicate_name = 0;
        for (int i = 0; i < config->component_count; ++i)
        {
            if (strcmp(config->components[i].interface->name,
                       interface->name) == 0)
            {
                duplicate_name = 1;
                break;
            }
        }
        if (duplicate_name)
        {
            printf("Warning: Duplicate component name %s in %s\n",
                   interface->name, lib_path);
            dlclose(lib_handle);
            continue;
        }

        if (config->lib_count >= MAX_COMPONENT_LIBS) {
            printf("Warning: Component library capacity reached; skipping %s\n", lib_path);
            dlclose(lib_handle);
            continue;
        }

        config->lib_handles[config->lib_count++] = lib_handle;
        
        // Register the component
        config->components[config->component_count].interface = interface;
        config->components[config->component_count].state = NULL;
        config->components[config->component_count].lib_handle = lib_handle;
        config->components[config->component_count].active = 1;
        
        printf("Registered component: %s - %s\n",
               interface->name,
               interface->description ? interface->description : "");
        config->component_count++;
    }
    
    closedir(dir);
    
    printf("Loaded %d components from shared libraries\n", config->component_count);
    return 0;
}

int initialize_components(director_config_t* config)
{
    printf("Initializing components...\n");
    g_prepare_count = 0;
    g_commit_count = 0;
    g_telemetry_count = 0;
    g_telemetry_errors = 0;
    g_fortytwo_errors = 0;
    director_reset_timing();
    config->component_phase_errors = 0;
    config->component_service_errors = 0;
    for (int i = 0; i < MAX_COMPONENTS; ++i)
    {
        config->component_interrupt_fds[i] = -1;
        config->component_wait_epochs[i] = 0;
    }

    for (int i = 0; i < config->component_count; i++) {
        component_entry_t* entry = &config->components[i];
        if (entry->active) {
            if (!component_interface_is_compatible(entry->interface, "registered component"))
            {
                entry->active = 0;
                return -1;
            }
            printf("Initializing component: %s\n", entry->interface->name);

            int result = entry->interface->create(&entry->state);
            if (result != COMPONENT_SUCCESS || entry->state == NULL) {
                printf("Failed to initialize component: %s\n", entry->interface->name);
                if (entry->state != NULL && entry->interface->destroy != NULL)
                    entry->interface->destroy(entry->state);
                entry->state = NULL;
                entry->active = 0;
                return -1;
            }
        }
    }
    
    printf("All components initialized successfully\n");

    /* Spawn one worker per component for concurrent EXECUTE device service. */
    config->execute_epoch = 0;
    config->threads_exit  = 0;
    config->execute_active = 0;
    config->active_service_callbacks = 0;
    config->threads_spawned = 0;
    config->worker_sync_initialized = 0;
    memset(config->component_thread_started, 0,
           sizeof(config->component_thread_started));

    if (pthread_mutex_init(&config->tick_mutex, NULL) != 0)
        return -1;
    pthread_condattr_t condition_attributes;
    if (pthread_condattr_init(&condition_attributes) != 0) {
        pthread_mutex_destroy(&config->tick_mutex);
        return -1;
    }
    int condition_status = pthread_condattr_setclock(&condition_attributes,
                                                     CLOCK_MONOTONIC);
    if (condition_status == 0)
        condition_status = pthread_cond_init(&config->tick_cond,
                                             &condition_attributes);
    pthread_condattr_destroy(&condition_attributes);
    if (condition_status != 0) {
        pthread_mutex_destroy(&config->tick_mutex);
        return -1;
    }
    config->worker_sync_initialized = 1;

    for (int i = 0; i < config->component_count; i++) {
        if (!config->components[i].active ||
            config->components[i].interface->service == NULL)
            continue;
        config->component_interrupt_fds[i] = eventfd(
            0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (config->component_interrupt_fds[i] < 0) {
            fprintf(stderr, "Failed to create worker interrupt for component %d\n", i);
            goto worker_start_failure;
        }
        if (pthread_create(&config->component_threads[i], NULL,
                           component_worker, (void*)(intptr_t)i) != 0) {
            fprintf(stderr, "Failed to spawn worker thread for component %d\n", i);
            goto worker_start_failure;
        }
        config->component_thread_started[i] = 1;
        config->threads_spawned++;
    }

    printf("Spawned %d component worker thread(s)\n", config->threads_spawned);
    return 0;

worker_start_failure:
    pthread_mutex_lock(&config->tick_mutex);
    config->threads_exit = 1;
    pthread_cond_broadcast(&config->tick_cond);
    for (int worker = 0; worker < config->component_count; ++worker)
        signal_component_worker(config->component_interrupt_fds[worker]);
    pthread_mutex_unlock(&config->tick_mutex);
    for (int started = 0; started < config->component_count; ++started)
    {
        if (config->component_thread_started[started])
        {
            pthread_join(config->component_threads[started], NULL);
            config->component_thread_started[started] = 0;
        }
        if (config->component_interrupt_fds[started] >= 0)
            close(config->component_interrupt_fds[started]);
        config->component_interrupt_fds[started] = -1;
    }
    config->threads_spawned = 0;
    return -1;
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
        config->enable_42 = 0;
        return -1;
    }
    
    config->fortytwo_initialized = 1;
    printf("Connected to 42 successfully\n");
    return 0;
}
int initialize_telemetry(void)
{
    g_udp_publish_counter = 0;
    g_udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_udp_sock < 0)
    {
        perror("UDP socket creation failed");
        return -1;
    }

    memset(&g_udp_addr, 0, sizeof(g_udp_addr));
    g_udp_addr.sin_family = AF_INET;
    g_udp_addr.sin_port = htons(50042);

    const char *gsw_hostname = getenv("SIMULITH_GSW_HOST");
    if (!gsw_hostname || gsw_hostname[0] == '\0')
        gsw_hostname = "shire-gsw";
    struct hostent *gsw_host = gethostbyname(gsw_hostname);
    if (gsw_host && gsw_host->h_addrtype == AF_INET &&
        gsw_host->h_addr_list[0] != NULL)
    {
        memcpy(&g_udp_addr.sin_addr, gsw_host->h_addr_list[0], (size_t)gsw_host->h_length);
    }
    else
    {
        fprintf(stderr, "Director cannot resolve Yamcs host %s\n", gsw_hostname);
        close(g_udp_sock);
        g_udp_sock = -1;
        return -1;
    }
    return 0;
}

static const char *scenario_find_field(const char *begin, const char *end,
                                       const char *field)
{
    char key[80];
    int key_length = snprintf(key, sizeof(key), "\"%s\"", field);
    if (key_length <= 0 || key_length >= (int)sizeof(key))
        return NULL;
    size_t key_size = (size_t)key_length;
    const char *cursor = begin;
    while (cursor != end)
    {
        size_t remaining = (size_t)(end - cursor);
        if (remaining < key_size)
            return NULL;
        if (memcmp(cursor, key, key_size) != 0)
        {
            cursor++;
            continue;
        }
        cursor += key_size;
        while (cursor != end && isspace((unsigned char)*cursor)) cursor++;
        if (cursor == end || *cursor != ':') continue;
        cursor++;
        while (cursor != end && isspace((unsigned char)*cursor)) cursor++;
        return cursor != end ? cursor : NULL;
    }
    return NULL;
}

static int scenario_parse_u64(const char *begin, const char *end,
                              const char *field, uint64_t *value)
{
    const char *text = scenario_find_field(begin, end, field);
    if (!text || !isdigit((unsigned char)*text)) return -1;
    uint64_t parsed = 0;
    do
    {
        uint64_t digit = (uint64_t)(*text - '0');
        if (parsed > (UINT64_MAX - digit) / UINT64_C(10)) return -1;
        parsed = parsed * UINT64_C(10) + digit;
        text++;
    } while (text != end && isdigit((unsigned char)*text));
    *value = parsed;
    return 0;
}

static int scenario_parse_string(const char *begin, const char *end,
                                 const char *field, char *output,
                                 size_t output_size)
{
    const char *text = scenario_find_field(begin, end, field);
    if (!text || *text != '\"' || output_size == 0) return -1;
    text++;
    const char *tail = text;
    while (tail != end && *tail != '\"')
    {
        if (*tail == '\\' || (unsigned char)*tail < 0x20) return -1;
        tail++;
    }
    size_t length = (size_t)(tail - text);
    if (tail == end || length >= output_size) return -1;
    memcpy(output, text, length);
    output[length] = '\0';
    return 0;
}

static int scenario_hex_nibble(char value)
{
    switch (value)
    {
        case '0': return 0;
        case '1': return 1;
        case '2': return 2;
        case '3': return 3;
        case '4': return 4;
        case '5': return 5;
        case '6': return 6;
        case '7': return 7;
        case '8': return 8;
        case '9': return 9;
        case 'a': case 'A': return 10;
        case 'b': case 'B': return 11;
        case 'c': case 'C': return 12;
        case 'd': case 'D': return 13;
        case 'e': case 'E': return 14;
        case 'f': case 'F': return 15;
        default: return -1;
    }
}

static int scenario_decode_hex(const char *hex, uint8_t *packet,
                               size_t capacity, size_t *packet_length)
{
    size_t length = strlen(hex);
    if (length == 0 || (length & 1U) != 0 || length / 2U > capacity) return -1;
    for (size_t index = 0; index < length / 2U; ++index)
    {
        int high = scenario_hex_nibble(hex[index * 2U]);
        int low = scenario_hex_nibble(hex[index * 2U + 1U]);
        if (high < 0 || low < 0) return -1;
        packet[index] = (uint8_t)((high << 4) | low);
    }
    *packet_length = length / 2U;
    return 0;
}

static int scenario_resolve(simulith_scenario_command_t *command)
{
    char port[16];
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf(port, sizeof(port), "%u", (unsigned)command->port);
    int status = 0;
    /* Compose starts FSW after Director; its service name can appear in DNS
     * just after the scenario is parsed. Resolve once before any tick begins. */
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        status = getaddrinfo(command->host, port, &hints, &addresses);
        if (status == 0 && addresses) break;
        if (addresses) { freeaddrinfo(addresses); addresses = NULL; }
        if (status != EAI_AGAIN && status != EAI_NONAME) break;
        if (attempt < 19)
        {
            struct timespec delay = {.tv_nsec = 100000000L};
            nanosleep(&delay, NULL);
        }
    }
    if (status != 0 || !addresses)
    {
        fprintf(stderr, "Scenario destination %s:%s could not be resolved: %s\n",
                command->host, port, gai_strerror(status));
        return -1;
    }
    memcpy(&command->destination, addresses->ai_addr,
           sizeof(command->destination));
    freeaddrinfo(addresses);
    return 0;
}

int initialize_scenario(director_config_t *config)
{
    if (!config) return -1;
    config->scenario_command_count = 0;
    config->scenario_digest = UINT64_C(1469598103934665603);
    config->scenario_injected = 0;
    config->scenario_errors = 0;
    if (config->scenario_file[0] == '\0') return 0;

    FILE *file = fopen(config->scenario_file, "rb");
    if (!file) return -1;
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return -1;
    }
    long measured_size = ftell(file);
    if (measured_size < 1 || measured_size > 65536 ||
        fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return -1;
    }
    size_t file_size = (size_t)measured_size;
    char *json = malloc(file_size + 1U);
    if (!json)
    {
        fclose(file);
        return -1;
    }
    size_t read_size = fread(json, 1, file_size, file);
    fclose(file);
    if (read_size != file_size)
    {
        free(json);
        return -1;
    }
    json[read_size] = '\0';
    for (size_t index = 0; index < read_size; ++index)
    {
        config->scenario_digest ^= (uint8_t)json[index];
        config->scenario_digest *= UINT64_C(1099511628211);
    }

    uint64_t version = 0;
    if (scenario_parse_u64(json, json + read_size, "schema_version", &version) != 0 ||
        version != 1)
    {
        free(json);
        return -1;
    }
    const char *commands_key = scenario_find_field(json, json + read_size, "commands");
    if (!commands_key || *commands_key != '[')
    {
        free(json);
        return -1;
    }

    const char *json_end = json + read_size;
    const char *cursor = commands_key + 1;
    while (cursor != json_end)
    {
        while (cursor != json_end &&
               (isspace((unsigned char)*cursor) || *cursor == ',')) cursor++;
        if (cursor == json_end || *cursor == ']') break;
        if (*cursor != '{' ||
            config->scenario_command_count >= SIMULITH_SCENARIO_MAX_COMMANDS)
        {
            free(json);
            return -1;
        }
        const char *object_end = memchr(cursor, '}',
                                        (size_t)(json_end - cursor));
        if (!object_end)
        {
            free(json);
            return -1;
        }
        simulith_scenario_command_t *command =
            &config->scenario_commands[config->scenario_command_count];
        memset(command, 0, sizeof(*command));
        uint64_t port = 0;
        char packet_hex[SIMULITH_SCENARIO_MAX_PACKET_SIZE * 2U + 1U];
        if (scenario_parse_u64(cursor, object_end, "sequence", &command->sequence) != 0 ||
            scenario_parse_u64(cursor, object_end, "port", &port) != 0 ||
            port == 0 || port > UINT16_MAX ||
            scenario_parse_string(cursor, object_end, "host", command->host,
                                  sizeof(command->host)) != 0 ||
            scenario_parse_string(cursor, object_end, "packet_hex", packet_hex,
                                  sizeof(packet_hex)) != 0 ||
            scenario_decode_hex(packet_hex, command->packet, sizeof(command->packet),
                                &command->packet_length) != 0)
        {
            free(json);
            return -1;
        }
        command->port = (uint16_t)port;
        if (scenario_resolve(command) != 0)
        {
            free(json);
            return -1;
        }
        if (config->scenario_command_count > 0 &&
            command->sequence <= config->scenario_commands[
                config->scenario_command_count - 1U].sequence)
        {
            free(json);
            return -1;
        }
        config->scenario_command_count++;
        cursor = object_end + 1;
    }
    free(json);
    if (config->scenario_command_count == 0) return -1;
    config->scenario_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (config->scenario_socket < 0) return -1;
    config->scenario_socket_initialized = 1;
    printf("Loaded %zu scenario commands (digest=%016lx)\n",
           config->scenario_command_count,
           (unsigned long)config->scenario_digest);
    return 0;
}

int director_inject_scenario_commands(director_config_t *config,
                                      uint64_t sequence)
{
    if (!config || config->scenario_file[0] == '\0') return 0;
    for (size_t index = 0; index < config->scenario_command_count; ++index)
    {
        simulith_scenario_command_t *command = &config->scenario_commands[index];
        if (command->sequence != sequence || command->injected) continue;
        ssize_t sent = sendto(config->scenario_socket, command->packet,
                              command->packet_length, 0,
                              (struct sockaddr *)&command->destination,
                              sizeof(command->destination));
        command->injected = 1;
        if (sent != (ssize_t)command->packet_length)
        {
            config->scenario_errors++;
            return -1;
        }
        config->scenario_injected++;
        printf("SIMULITH_SCENARIO injected sequence=%lu bytes=%zu host=%s port=%u\n",
               (unsigned long)sequence, command->packet_length, command->host,
               (unsigned)command->port);
    }
    return 0;
}

size_t simulith_serialize_42_telemetry(const simulith_42_context_t *context,
                                      uint8_t *packet, size_t packet_capacity)
{
    if (!context || !packet || packet_capacity < SIMULITH_42_TELEMETRY_SIZE)
        return 0;

    memset(packet, 0, SIMULITH_42_TELEMETRY_SIZE);
    size_t offset = 0;
    memcpy(packet + offset, &context->dyn_time, sizeof(double));
    offset += sizeof(double);
    for (int i = 0; i < 3; i++) {
        memcpy(packet + offset, &context->pos_n[i], sizeof(double));
        offset += sizeof(double);
    }
    for (int i = 0; i < 3; i++) {
        memcpy(packet + offset, &context->sun_vector_body[i], sizeof(double));
        offset += sizeof(double);
    }
    for (int i = 0; i < 3; i++) {
        memcpy(packet + offset, &context->mag_field_body[i], sizeof(double));
        offset += sizeof(double);
    }
    for (int i = 0; i < 3; i++) {
        memcpy(packet + offset, &context->hvb[i], sizeof(double));
        offset += sizeof(double);
    }
    for (int i = 0; i < 3; i++) {
        memcpy(packet + offset, &context->wn[i], sizeof(double));
        offset += sizeof(double);
    }
    for (int i = 0; i < 4; i++) {
        memcpy(packet + offset, &context->qn[i], sizeof(double));
        offset += sizeof(double);
    }
    memcpy(packet + offset, &context->mass, sizeof(double));
    offset += sizeof(double);
    for (int i = 0; i < 3; i++) {
        memcpy(packet + offset, &context->cm[i], sizeof(double));
        offset += sizeof(double);
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            memcpy(packet + offset, &context->inertia[i][j], sizeof(double));
            offset += sizeof(double);
        }
    }
    memcpy(packet + offset, &context->eclipse, sizeof(int));
    offset += sizeof(int);
    memcpy(packet + offset, &context->atmo_density, sizeof(double));
    offset += sizeof(double);
    return offset;
}

void cleanup_components(director_config_t* config)
{
    printf("Cleaning up components...\n");

    /* Stop worker threads before destroying component state so that no
     * callback runs concurrently with destroy(). Signal workers via the cond so they
     * wake from pthread_cond_wait and see threads_exit == 1. */
    if (config->worker_sync_initialized) {
        pthread_mutex_lock(&config->tick_mutex);
        config->threads_exit = 1;
        pthread_cond_broadcast(&config->tick_cond);
        for (int i = 0; i < config->component_count; ++i)
            signal_component_worker(config->component_interrupt_fds[i]);
        pthread_mutex_unlock(&config->tick_mutex);

        for (int i = 0; i < config->component_count; i++) {
            if (config->component_thread_started[i]) {
                pthread_join(config->component_threads[i], NULL);
                config->component_thread_started[i] = 0;
            }
            if (config->component_interrupt_fds[i] >= 0) {
                close(config->component_interrupt_fds[i]);
                config->component_interrupt_fds[i] = -1;
            }
        }
        pthread_cond_destroy(&config->tick_cond);
        pthread_mutex_destroy(&config->tick_mutex);
        config->threads_spawned = 0;
        config->worker_sync_initialized = 0;
    }

    for (int i = 0; i < config->component_count; i++) {
        component_entry_t* entry = &config->components[i];
        if (entry->active && entry->interface && entry->interface->destroy &&
            entry->state) {
            printf("Cleaning up component: %s\n", entry->interface->name);
            entry->interface->destroy(entry->state);
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

    if (g_udp_sock >= 0)
    {
        close(g_udp_sock);
        g_udp_sock = -1;
    }
    if (g_backdoor_sock >= 0)
    {
        close(g_backdoor_sock);
        g_backdoor_sock = -1;
    }
    if (config->scenario_socket_initialized)
    {
        close(config->scenario_socket);
        config->scenario_socket = -1;
        config->scenario_socket_initialized = 0;
    }
    g_udp_publish_counter = 0;
}
static int populate_42_context(simulith_42_context_t* context)
{
    // Initialize context
    memset(context, 0, sizeof(simulith_42_context_t));
    
    // Diagnostic/component-only runs may intentionally omit 42.
    if (!g_director_config.enable_42) {
        context->valid = 0;
        return 0;
    }
    if (!g_director_config.fortytwo_initialized) {
        context->valid = 0;
        g_fortytwo_errors++;
        return -1;
    }
    
    // Request latest state from 42 via socket
    if (simulith_42_request_state(context) != 0) {
        context->valid = 0;
        g_fortytwo_errors++;
        return -1;
    }
    
    return 0;
}

// Process commands and apply them to 42
static int process_42_commands(simulith_42_command_t *commands, uint32_t *count)
{
    int cmd_count = 0;
    
    if (!g_director_config.enable_42)
        return 0;
    if (!g_director_config.fortytwo_initialized)
        return -1;
    
    /* Collect all commands from queue into batch buffer */
    while (cmd_count < SIMULITH_42_CMD_QUEUE_SIZE &&
           dequeue_command(&commands[cmd_count]) == 0) {
        cmd_count++;
    }
    
    /* Send all commands in a single message to 42 */
    if (cmd_count > 0) {
        if (simulith_42_send_command_batch(commands, cmd_count) != 0) {
            g_fortytwo_errors++;
            if (g_director_config.verbose) {
                fprintf(stderr, "[director] Failed to send command batch to 42\n");
            }
            return -1;
        }
    } else {
        /* In TXRX mode, we must ALWAYS send something to 42, even if there are no commands */
        /* If no commands were collected, send an empty message with just [ENDMSG] */
        if (simulith_42_send_empty_commands() != 0) {
            g_fortytwo_errors++;
            return -1;
        }
    }
    *count = (uint32_t)cmd_count;
    return 0;
}

/* These spans partition the Director's blocking work in PREPARE and COMMIT.
 * Keep measurements in memory: per-tick logging changes scheduler behavior. */
#define DIRECTOR_TIMING_SAMPLES 10000
typedef struct
{
    const char *name;
    uint64_t count;
    uint64_t total_ns;
    uint64_t max_ns;
    uint64_t samples[DIRECTOR_TIMING_SAMPLES];
    size_t sample_count;
} director_timing_t;

enum
{
    TIMING_42_STATE,
    TIMING_ON_TICK,
    TIMING_QUIESCE,
    TIMING_ACTUATE,
    TIMING_42_COMMANDS,
    TIMING_COUNT
};

static director_timing_t g_timing[TIMING_COUNT] = {
    {.name = "42_state"},
    {.name = "on_tick"},
    {.name = "quiesce"},
    {.name = "actuate"},
    {.name = "42_commands"},
};
static uint64_t g_timing_warmup_ns = 0;
static int g_timing_active = 1;

static uint64_t director_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void director_record_timing(int metric, uint64_t started_ns)
{
    if (!g_timing_active) return;
    uint64_t elapsed = director_now_ns() - started_ns;
    director_timing_t *item = &g_timing[metric];
    item->count++;
    item->total_ns += elapsed;
    if (elapsed > item->max_ns) item->max_ns = elapsed;
    if (item->sample_count < DIRECTOR_TIMING_SAMPLES)
        item->samples[item->sample_count++] = elapsed;
}

static int director_compare_ns(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left;
    uint64_t b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static double director_percentile_us(const director_timing_t *item, double fraction)
{
    if (item->sample_count == 0) return 0.0;
    size_t index = (size_t)(fraction * (double)(item->sample_count - 1));
    return (double)item->samples[index] / 1000.0;
}

static void director_reset_timing(void)
{
    const char *warmup = getenv("SIMULITH_WARMUP");
    g_timing_warmup_ns = 0;
    if (warmup)
    {
        char *end = NULL;
        double seconds = strtod(warmup, &end);
        if (end != warmup && *end == '\0' && isfinite(seconds) && seconds >= 0.0 &&
            seconds <= (double)UINT64_MAX / 1000000000.0)
            g_timing_warmup_ns = (uint64_t)(seconds * 1000000000.0);
    }
    g_timing_active = g_timing_warmup_ns == 0;
    for (int i = 0; i < TIMING_COUNT; ++i)
    {
        g_timing[i].count = 0;
        g_timing[i].total_ns = 0;
        g_timing[i].max_ns = 0;
        g_timing[i].sample_count = 0;
    }
}

static void director_write_timing(void)
{
    printf("SIMULITH_DIRECTOR_TIMING {\"segments\":[");
    for (int i = 0; i < TIMING_COUNT; ++i)
    {
        director_timing_t *item = &g_timing[i];
        qsort(item->samples, item->sample_count, sizeof(item->samples[0]),
              director_compare_ns);
        printf("%s{\"name\":\"%s\",\"count\":%lu,\"sample_count\":%zu,"
               "\"mean_us\":%.3f,\"p50_us\":%.3f,\"p95_us\":%.3f,"
               "\"max_us\":%.3f}",
               i == 0 ? "" : ",", item->name, (unsigned long)item->count,
               item->sample_count,
               item->count ? (double)item->total_ns / (double)item->count / 1000.0 : 0.0,
               director_percentile_us(item, 0.50),
               director_percentile_us(item, 0.95),
               (double)item->max_ns / 1000.0);
    }
    printf("]}\n");
}

static int validate_tick_identity(uint64_t sequence, uint64_t tick_time_ns,
                                  const char *phase)
{
    if (g_director_config.shared_tick_sequence != sequence ||
        g_director_config.shared_tick_time_ns != tick_time_ns)
    {
        simulith_log("Director rejected %s for tick %lu at %lu ns; "
                     "active tick is %lu at %lu ns\n", phase,
                     (unsigned long)sequence, (unsigned long)tick_time_ns,
                     (unsigned long)g_director_config.shared_tick_sequence,
                     (unsigned long)g_director_config.shared_tick_time_ns);
        return -1;
    }
    return 0;
}

static int component_phase_succeeded(const char *phase)
{
    int status = COMPONENT_SUCCESS;
    for (int i = 0; i < g_director_config.component_count; ++i)
    {
        component_entry_t *entry = &g_director_config.components[i];
        if (!entry->active || entry->phase_status == COMPONENT_SUCCESS)
            continue;
        g_director_config.component_phase_errors++;
        simulith_log("Director withholding %s completion for tick %lu: "
                     "component %s failed\n", phase,
                     (unsigned long)g_director_config.shared_tick_sequence,
                     entry->interface->name);
        status = COMPONENT_ERROR;
    }
    return status;
}

int director_prepare_tick(uint64_t sequence, uint64_t tick_time_ns)
{
    g_prepare_count++;
    g_timing_active = tick_time_ns >= g_timing_warmup_ns;
    /* Fetch 42 state. 42 has been stepping since the previous tick's command
     * commit, so its step is already done and this returns quickly. */
    simulith_42_context_t context_42;
    uint64_t started_ns = director_now_ns();
    int context_status = populate_42_context(&context_42);
    director_record_timing(TIMING_42_STATE, started_ns);
    g_director_config.shared_context_42 = context_42;
    if (context_status != 0)
        return COMPONENT_ERROR;

    pthread_mutex_lock(&g_director_config.tick_mutex);
    g_director_config.shared_tick_sequence = sequence;
    g_director_config.shared_tick_time_ns = tick_time_ns;
    for (int i = 0; i < g_director_config.component_count; ++i)
        g_director_config.components[i].phase_status = COMPONENT_SUCCESS;
    pthread_mutex_unlock(&g_director_config.tick_mutex);

    /* PREPARE callbacks are deliberately ordered and run in the director.
     * They are small and do not perform device transactions. */
    started_ns = director_now_ns();
    for (int i = 0; i < g_director_config.component_count; ++i) {
        component_entry_t *entry = &g_director_config.components[i];
        if (entry->active && entry->interface && entry->interface->on_tick && entry->state)
        {
            if (entry->interface->on_tick(
                    entry->state, tick_time_ns,
                    &g_director_config.shared_context_42) != COMPONENT_SUCCESS)
            {
                entry->phase_status = COMPONENT_ERROR;
                simulith_log("Component %s failed PREPARE on tick %lu\n",
                             entry->interface->name,
                             (unsigned long)sequence);
            }
        }
    }
    director_record_timing(TIMING_ON_TICK, started_ns);

    return component_phase_succeeded("PREPARE");
}

int director_execute_tick(uint64_t sequence, uint64_t tick_time_ns)
{
    if (validate_tick_identity(sequence, tick_time_ns, "EXECUTE") != 0)
        return COMPONENT_ERROR;

    pthread_mutex_lock(&g_director_config.tick_mutex);
    g_director_config.execute_active = 1;
    g_director_config.execute_epoch++;
    pthread_cond_broadcast(&g_director_config.tick_cond);
    pthread_mutex_unlock(&g_director_config.tick_mutex);
    return COMPONENT_SUCCESS;
}

int director_commit_tick(uint64_t sequence, uint64_t tick_time_ns)
{
    if (validate_tick_identity(sequence, tick_time_ns, "COMMIT") != 0)
        return COMPONENT_ERROR;

    g_commit_count++;
    uint64_t started_ns = director_now_ns();
    pthread_mutex_lock(&g_director_config.tick_mutex);
    g_director_config.execute_active = 0;
    while (g_director_config.active_service_callbacks != 0U)
        pthread_cond_wait(&g_director_config.tick_cond,
                          &g_director_config.tick_mutex);
    pthread_mutex_unlock(&g_director_config.tick_mutex);
    director_record_timing(TIMING_QUIESCE, started_ns);

    if (component_phase_succeeded("COMMIT") != COMPONENT_SUCCESS)
        return COMPONENT_ERROR;

    /* ACTUATE observes a quiescent service layer and emits one ordered command
     * batch after every component has consumed the latest FSW output. */
    started_ns = director_now_ns();
    for (int i = 0; i < g_director_config.component_count; ++i)
    {
        component_entry_t *entry = &g_director_config.components[i];
        if (entry->active && entry->interface && entry->interface->actuate &&
            entry->state &&
            entry->interface->actuate(entry->state, tick_time_ns,
                                      &g_director_config.shared_context_42) !=
                COMPONENT_SUCCESS)
        {
            entry->phase_status = COMPONENT_ERROR;
            simulith_log("Component %s failed COMMIT on tick %lu\n",
                         entry->interface->name, (unsigned long)sequence);
        }
    }
    director_record_timing(TIMING_ACTUATE, started_ns);
    if (component_phase_succeeded("COMMIT") != COMPONENT_SUCCESS)
        return COMPONENT_ERROR;

    /* Device handlers enqueue actuator changes during FSW execute. Commit the
     * complete batch only after SCH reports that slot finished. */
    started_ns = director_now_ns();
    simulith_42_command_t commands[SIMULITH_42_CMD_QUEUE_SIZE];
    uint32_t command_count = 0;
    int command_status = process_42_commands(commands, &command_count);
    director_record_timing(TIMING_42_COMMANDS, started_ns);
    if (command_status != 0)
        return COMPONENT_ERROR;

    // Service backdoor packets
    process_backdoor_once(&g_director_config);

    // Publish telemetry
    g_udp_publish_counter = (g_udp_publish_counter + 1) % UDP_PUBLISH_INTERVAL_TICKS;
    const simulith_42_context_t *context_42 = &g_director_config.shared_context_42;
    if (g_udp_sock >= 0 && context_42->valid && g_udp_publish_counter == 0)
    {
        // Packet structure matches XTCE SIM_42_TRUTH_DATA:
        // DYN_TIME, POSITION_N_1/2/3, SVB_1/2/3, BVB_1/2/3, HVB_1/2/3, WN_1/2/3, QN_1/2/3/4, MASS, CM_1/2/3, INERTIA_11/12/13/21/22/23/31/32/33, ECLIPSE, ATMO_DENSITY
        unsigned char packet[SIMULITH_42_TELEMETRY_SIZE];
        size_t packet_size = simulith_serialize_42_telemetry(
            context_42, packet, sizeof(packet));
        g_telemetry_count++;
        ssize_t sent = sendto(g_udp_sock, packet, packet_size, 0,
                              (struct sockaddr*)&g_udp_addr, sizeof(g_udp_addr));
        if (sent != (ssize_t)packet_size)
            g_telemetry_errors++;
    }
    if (director_inject_scenario_commands(&g_director_config, sequence) != 0)
        return COMPONENT_ERROR;
    if (simulith_control_trace_tick(sequence, tick_time_ns,
                                   &g_director_config.shared_context_42,
                                   commands, command_count) != 0)
        return COMPONENT_ERROR;
    if (tick_time_ns + INTERVAL_NS >= g_trace_final_time_ns &&
        simulith_control_trace_finish() != 0)
        return COMPONENT_ERROR;
    return COMPONENT_SUCCESS;
}

int director_configure_trace_duration(void)
{
    const char *duration = getenv("SIMULITH_DURATION");
    if (!duration || !*duration)
        return 0;
    char *end = NULL;
    double seconds = strtod(duration, &end);
    if (end == duration || *end != '\0' || !isfinite(seconds) ||
        seconds <= 0.0 || seconds > (double)UINT64_MAX / 1000000000.0)
        return -1;
    g_trace_final_time_ns = (uint64_t)(seconds * 1000000000.0);
    return 0;
}

void director_write_terminal_metrics(void)
{
    /* The final commit releases 42 to calculate the terminal state. Fetch it
     * once here after STOP so fidelity compares the state after every tick. */
    simulith_42_context_t terminal;
    (void)populate_42_context(&terminal);
    simulith_42_cmd_queue_stats_t queue_stats;
    simulith_42_get_command_queue_stats(&queue_stats);
    uint64_t worker_ready = __atomic_load_n(&g_worker_ready_count,
                                            __ATOMIC_RELAXED);
    uint64_t worker_parked = __atomic_load_n(&g_worker_parked_count,
                                             __ATOMIC_RELAXED);
    uint64_t worker_total_ns = __atomic_load_n(&g_worker_admission_total_ns,
                                                __ATOMIC_RELAXED);
    uint64_t worker_max_ns = __atomic_load_n(&g_worker_admission_max_ns,
                                              __ATOMIC_RELAXED);

    const unsigned char *bytes = (const unsigned char *)&terminal;
    uint64_t digest = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < sizeof(terminal); ++i)
    {
        digest ^= bytes[i];
        digest *= UINT64_C(1099511628211);
    }

    printf("SIMULITH_DIRECTOR_TERMINAL {\"prepare_count\":%lu,\"commit_count\":%lu,"
           "\"telemetry_count\":%lu,\"queue\":{\"enqueued\":%lu,\"dequeued\":%lu,"
           "\"overflows\":%lu,\"high_watermark\":%lu,"
           "\"by_type\":[%lu,%lu,%lu,%lu,%lu],"
           "\"nonzero_actuator_commands\":%lu},"
           "\"component_service_errors\":%lu,\"telemetry_errors\":%lu,"
           "\"component_phase_errors\":%lu,"
           "\"fortytwo_errors\":%lu,"
           "\"worker_admission\":{\"ready\":%lu,\"parked\":%lu,"
           "\"mean_us\":%.3f,\"max_us\":%.3f},"
           "\"scenario\":{\"digest\":\"%016lx\",\"expected\":%zu,"
           "\"injected\":%lu,\"errors\":%lu},"
           "\"digest\":\"%016lx\","
           "\"state\":{\"valid\":%d,\"sim_time\":%.17g,\"dyn_time\":%.17g,"
           "\"qn\":[%.17g,%.17g,%.17g,%.17g],\"wn\":[%.17g,%.17g,%.17g],"
           "\"pos_n\":[%.17g,%.17g,%.17g],\"vel_n\":[%.17g,%.17g,%.17g]}}\n",
           (unsigned long)g_prepare_count, (unsigned long)g_commit_count,
           (unsigned long)g_telemetry_count,
           (unsigned long)queue_stats.enqueued, (unsigned long)queue_stats.dequeued,
           (unsigned long)queue_stats.overflows, (unsigned long)queue_stats.high_watermark,
           (unsigned long)queue_stats.by_type[SIMULITH_42_CMD_NONE],
           (unsigned long)queue_stats.by_type[SIMULITH_42_CMD_MTB_TORQUE],
           (unsigned long)queue_stats.by_type[SIMULITH_42_CMD_WHEEL_TORQUE],
           (unsigned long)queue_stats.by_type[SIMULITH_42_CMD_THRUSTER],
           (unsigned long)queue_stats.by_type[SIMULITH_42_CMD_SET_MODE],
           (unsigned long)queue_stats.nonzero_actuator_commands,
           (unsigned long)g_director_config.component_service_errors,
           (unsigned long)g_telemetry_errors,
           (unsigned long)g_director_config.component_phase_errors,
           (unsigned long)g_fortytwo_errors,
           (unsigned long)worker_ready,
           (unsigned long)worker_parked,
           worker_ready ?
               (double)worker_total_ns /
               (double)worker_ready / 1000.0 : 0.0,
           (double)worker_max_ns / 1000.0,
           (unsigned long)g_director_config.scenario_digest,
           g_director_config.scenario_command_count,
           (unsigned long)g_director_config.scenario_injected,
           (unsigned long)g_director_config.scenario_errors,
           (unsigned long)digest, terminal.valid, terminal.sim_time, terminal.dyn_time,
           terminal.qn[0], terminal.qn[1], terminal.qn[2], terminal.qn[3],
           terminal.wn[0], terminal.wn[1], terminal.wn[2],
           terminal.pos_n[0], terminal.pos_n[1], terminal.pos_n[2],
           terminal.vel_n[0], terminal.vel_n[1], terminal.vel_n[2]);
    fflush(stdout);
    director_write_timing();
    fflush(stdout);
}

void on_tick(uint64_t tick_time_ns)
{
    /* Compatibility entry point used by component/director unit tests. */
    if (director_prepare_tick(0, tick_time_ns) == COMPONENT_SUCCESS &&
        director_execute_tick(0, tick_time_ns) == COMPONENT_SUCCESS)
        (void)director_commit_tick(0, tick_time_ns);
}
