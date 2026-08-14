#include "demo_sim.h"

// Global state pointer for callback access
static demo_sim_state_t* g_state = NULL;

// Transport port struct for Simulith
static transport_port_t g_uart_port = {0};

/* Forward prototypes to satisfy -Wmissing-prototypes for REGISTER_COMPONENT export */
const component_interface_t* get_demo_sim_component_interface(void);
const component_interface_t* get_component_interface(void);

static void send_housekeeping(demo_sim_state_t* state)
{
    if (!state) return;
    uint8_t response[8];
    response[0] = DEMO_DEVICE_HDR_0;
    response[1] = DEMO_DEVICE_HDR_1;
    response[2] = (uint8_t)((state->hk.DeviceCounter >> 8) & 0xFF);
    response[3] = (uint8_t)(state->hk.DeviceCounter & 0xFF);
    response[4] = (uint8_t)((state->hk.DeviceConfig >> 8) & 0xFF);
    response[5] = (uint8_t)(state->hk.DeviceConfig & 0xFF);
    response[6] = DEMO_DEVICE_TRAILER_0;
    response[7] = DEMO_DEVICE_TRAILER_1;
    simulith_transport_send((transport_port_t*)&g_uart_port, response, sizeof(response));
}

static void send_demo_data(demo_sim_state_t* state)
{
    if (!state) return;
    uint8_t response[10];
    response[0] = DEMO_DEVICE_HDR_0;
    response[1] = DEMO_DEVICE_HDR_1;
    response[2] = (uint8_t)((state->data.Chan1 >> 8) & 0xFF);
    response[3] = (uint8_t)(state->data.Chan1 & 0xFF);
    response[4] = (uint8_t)((state->data.Chan2 >> 8) & 0xFF);
    response[5] = (uint8_t)(state->data.Chan2 & 0xFF);
    response[6] = (uint8_t)((state->data.Chan3 >> 8) & 0xFF);
    response[7] = (uint8_t)(state->data.Chan3 & 0xFF);
    response[8] = DEMO_DEVICE_TRAILER_0;
    response[9] = DEMO_DEVICE_TRAILER_1;
    simulith_transport_send((transport_port_t*)&g_uart_port, response, sizeof(response));
}

static void handle_command(demo_sim_state_t* state, const uint8_t* data, size_t length)
{
    if (!state || !data || length < DEMO_DEVICE_CMD_SIZE) 
    {  // Check for minimum command size
        printf("DEMO SIM: Invalid command parameters: state=%p, data=%p, length=%zu\n", 
               (void*)state, (const void*)data, length);
        return;
    }
    
    uint16_t header  = ((uint16_t) data[0] << 8) | data[1];
    uint16_t cmd_id  = ((uint16_t) data[2] << 8) | data[3];
    uint16_t payload = ((uint16_t) data[4] << 8) | data[5];
    uint16_t trailer = ((uint16_t) data[6] << 8) | data[7];

    // Validate header
    if (header != DEMO_DEVICE_HDR) 
    {
        printf("DEMO SIM: Invalid command header (0x%04X)\n", header);
        return;
    }

    // Validate trailer
    if (trailer != DEMO_DEVICE_TRAILER) 
    {
        printf("DEMO SIM: Invalid command trailer (0x%04X)\n", trailer);
        return;
    }

    // Echo command back
    #ifdef DEMO_CFG_DEBUG
    printf("DEMO SIM: handle_command: Echo command back to UART: ID=%d, Payload=0x%08X\n", cmd_id, payload);
    #endif
    simulith_transport_send((transport_port_t*)&g_uart_port, data, length);

    // Process command
    switch (cmd_id) 
    {
        case DEMO_DEVICE_NOOP_CMD:
            #ifdef DEMO_CFG_DEBUG
            printf("DEMO SIM: Processing NOOP command\n");
            #endif
            // Just echo the command back, which was already done
            break;

        case DEMO_DEVICE_REQ_HK_CMD:
            #ifdef DEMO_CFG_DEBUG
            printf("DEMO SIM: Processing GET_HK command\n");
            #endif
            send_housekeeping(state);
            break;

        case DEMO_DEVICE_REQ_DATA_CMD:
            #ifdef DEMO_CFG_DEBUG
            printf("DEMO SIM: Processing GET_DATA command\n");
            #endif
            send_demo_data(state);
            break;

        case DEMO_DEVICE_CFG_CMD:
            #ifdef DEMO_CFG_DEBUG
            printf("DEMO SIM: Processing SET_CONFIG command with payload 0x%08X\n", payload);
            #endif
            state->hk.DeviceConfig = payload;
            break;

        default:
            printf("DEMO SIM: Unknown command ID: %d\n", cmd_id);
            break;
    }

    // Increment command counter
    state->hk.DeviceCounter++;
}

static void demo_sim_on_tick(uint64_t tick_time_ns, const simulith_42_context_t* context_42)
{
    int bytes;
    uint8_t data[256];

    if (!g_state) return;
    
    // Convert nanoseconds to seconds
    double current_time = (double)tick_time_ns / 1e9;
    
    // Update demo data at the specified rate
    if (current_time - g_state->last_update_time >= (1.0 / DEMO_SIM_UPDATE_RATE_HZ)) 
    {
        // If 42 context is available and random data is not enabled, populate channels with Sun Vector Body (SVB)
        if ((g_state->rand_data_enabled == 0) && context_42 && context_42->valid) {
            // Chan1: SVB X-component (scaled and offset for uint16)
            // Scale by 10000 and add 32768 offset to handle negative values
            g_state->data.Chan1 = (uint16_t)((context_42->sun_vector_body[0] * 10000.0) + 32768.0);
            
            // Chan2: SVB Y-component (scaled and offset for uint16)
            g_state->data.Chan2 = (uint16_t)((context_42->sun_vector_body[1] * 10000.0) + 32768.0);
            
            // Chan3: SVB Z-component (scaled and offset for uint16)
            g_state->data.Chan3 = (uint16_t)((context_42->sun_vector_body[2] * 10000.0) + 32768.0);
            
            // Optional: Print SVB data for debugging
            //if (g_state->hk.DeviceCounter % 1000 == 0) { // Print every 1000 cycles
            //    printf("42 SVB - Time: %.3f, Sun Vector Body: [%.6f, %.6f, %.6f], Channels: [%u, %u, %u]\n",
            //           context_42->sim_time, 
            //           context_42->sun_vector_body[0], context_42->sun_vector_body[1], context_42->sun_vector_body[2],
            //           g_state->data.Chan1, g_state->data.Chan2, g_state->data.Chan3);
            //}
        } else {
            // Random or default values
            // When random enabled, fill with pseudo-random 8-bit values
            // Using 8-bit values to make detection easier in testing
            if (g_state->rand_data_enabled == 1) {
                g_state->data.Chan1 = (uint16_t)(rand() & 0x00FF);
                g_state->data.Chan2 = (uint16_t)(rand() & 0x00FF);
                g_state->data.Chan3 = (uint16_t)(rand() & 0x00FF);
            } else {
                // Fallback: Use command counter if no 42 context available
                g_state->data.Chan1 = (uint16_t)(g_state->hk.DeviceCounter * 1);
                g_state->data.Chan2 = (uint16_t)(g_state->hk.DeviceCounter * 2);
                g_state->data.Chan3 = (uint16_t)(g_state->hk.DeviceCounter * 3);
            }
        }
        
        // Handle random HK if enabled
        if (g_state->rand_hk_enabled == 1) {
            g_state->hk.DeviceConfig = (uint16_t)(rand() & 0xFF00);
            g_state->hk.DeviceCounter = (uint16_t)(rand() & 0xFF00);
        }
        
        g_state->last_update_time = current_time;
    }

    // Process UART
    bytes = simulith_transport_available((transport_port_t*)&g_uart_port);
    if (bytes > 0)
    {
        // Read UART
        bytes = simulith_transport_receive((transport_port_t*)&g_uart_port, data, sizeof(data));

        #ifdef DEMO_CFG_DEBUG
        printf("DEMO SIM: Received %d bytes from UART\n", bytes);
        for(int i = 0; i < bytes; i++) 
        {
            printf("%02X ", data[i]);
        }
        printf("\n");
        #endif

        // Process the command
        handle_command(g_state, data, (size_t)bytes);
    }
}

static void demo_sim_backdoor(component_state_t* cstate, uint16_t cmd_id, const uint8_t* payload, uint16_t payload_len)
{
    demo_sim_state_t* state = (demo_sim_state_t*)cstate;
    if (!state) return;
    
    switch (cmd_id) 
    {
        case DEMO_BD_SET_CONFIG:    
            if (payload_len >= 2) 
            {
                uint16_t value = (uint16_t)((payload[0] << 8) | payload[1]);
                state->hk.DeviceConfig = value;
                printf("DEMO SIM BACKDOOR: Setting DeviceConfig from backdoor command to 0x%04X\n", 
                   (payload_len >= 2) ? ((payload[0] << 8) | payload[1]) : 0);
            }
            break;
        
        case DEMO_BD_RAND_HK:
            state->rand_hk_enabled = (payload_len >= 1) ? (payload[0] != 0) : true;
            printf("DEMO SIM BACKDOOR: Setting random HK to %s\n", state->rand_hk_enabled ? "ENABLED" : "DISABLED");
            break;
        
        case DEMO_BD_RAND_DATA:
            state->rand_data_enabled = (payload_len >= 1) ? (payload[0] != 0) : true;
            printf("DEMO SIM BACKDOOR: Setting random DATA to %s\n", state->rand_data_enabled ? "ENABLED" : "DISABLED");
            break;
        
        default:
            break;
    }
}

int demo_sim_init(demo_sim_state_t* state)
{
    if (!state) return DEMO_SIM_ERROR;

    // Initialize state
    memset(state, 0, sizeof(demo_sim_state_t));
    
    //// Seed PRNG
    //static int seeded = 0;
    //if (!seeded) { srand((unsigned int)time(NULL)); seeded = 1; }

    // Set global state pointer
    g_state = state;

    // Initialize UART port struct for Simulith (server/bind)
    memset(&g_uart_port, 0, sizeof(g_uart_port));
    snprintf(g_uart_port.name, sizeof(g_uart_port.name), "demo_sim_uart%d", DEMO_CFG_HANDLE);
    snprintf(g_uart_port.address, sizeof(g_uart_port.address), "ipc:///tmp/simulith_pub:%d", SIMULITH_UART_BASE_PORT + DEMO_CFG_HANDLE);
    g_uart_port.is_server = 1; // Always server/bind for the simulator

    int uart_result = simulith_transport_init((transport_port_t*)&g_uart_port);
    if (uart_result < 0) 
    {
        printf("DEMO SIM: Failed to initialize Simulith UART server\n");
        return DEMO_SIM_ERROR;
    }

    // Initialize default values
    state->hk.DeviceCounter = 0;
    state->hk.DeviceConfig = 0;
    state->data.Chan1 = 0;
    state->data.Chan2 = 0;
    state->data.Chan3 = 0;
    state->last_update_time = 0.0;
    state->rand_hk_enabled = 0;
    state->rand_data_enabled = 0;

    printf("DEMO SIM: Initialized successfully as %s\n", g_uart_port.name);
    return DEMO_SIM_SUCCESS;
}

void demo_sim_cleanup(demo_sim_state_t* state)
{
    if (!state) return;

    g_state = NULL;  // Clear global state pointer
    simulith_transport_close((transport_port_t*)&g_uart_port);
}

// Component interface implementation
static int demo_sim_component_init(component_state_t** state)
{
    demo_sim_state_t* demo_state = malloc(sizeof(demo_sim_state_t));
    if (!demo_state) {
        return COMPONENT_ERROR;
    }
    
    int result = demo_sim_init(demo_state);
    if (result != DEMO_SIM_SUCCESS) {
        free(demo_state);
        return COMPONENT_ERROR;
    }
    
    *state = (component_state_t*)demo_state;
    return COMPONENT_SUCCESS;
}

static void demo_sim_component_tick(component_state_t* state, uint64_t tick_time_ns, const simulith_42_context_t* context_42)
{
    if (!state) return;
    
    demo_sim_state_t* demo_state = (demo_sim_state_t*)state;
    
    // Set global state for the tick callback
    demo_sim_state_t* old_state = g_state;
    g_state = demo_state;
    
    // Call the original tick function with 42 context
    demo_sim_on_tick(tick_time_ns, context_42);
    
    // Restore previous state
    g_state = old_state;
}

static void demo_sim_component_cleanup(component_state_t* state)
{
    if (!state) return;
    
    demo_sim_state_t* demo_state = (demo_sim_state_t*)state;
    demo_sim_cleanup(demo_state);
    free(demo_state);
}

static const component_interface_t demo_sim_interface = {
    .name = "demo_sim",
    .description = "Demo component simulation with UART interface",
    .init = demo_sim_component_init,
    .tick = demo_sim_component_tick,
    .cleanup = demo_sim_component_cleanup,
    .backdoor = demo_sim_backdoor
};

// Component registration function - exported for dynamic loading
REGISTER_COMPONENT(demo_sim)
{
    return &demo_sim_interface;
}

// Export the registration function with a standard name for dynamic loading
__attribute__((visibility("default")))
const component_interface_t* get_component_interface(void)
{
    return &demo_sim_interface;
}
