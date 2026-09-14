#include "demo_sim.h"

/*
 * Reference component-simulator lifecycle
 * ---------------------------------------
 * CREATE:  demo_sim_component_create() allocates one private model instance
 *          and opens the simulator side of its UART connection.
 * PREPARE: demo_sim_component_on_tick() observes the immutable tick/42 state
 *          and advances autonomous device behavior exactly once.
 * EXECUTE: demo_sim_component_wait_for_service() sleeps until UART work exists
 *          (or COMMIT interrupts it), then demo_sim_component_service()
 *          processes one complete transaction without advancing time.
 * ACTUATE: demo_sim_component_actuate() publishes final actuator outputs. The
 *          demo has none, so its implementation documents the no-op contract.
 * DESTROY: demo_sim_component_destroy() closes owned resources and releases
 *          the state only after the director has quiesced every callback.
 *
 * Simulated time does not advance during EXECUTE. This device therefore models
 * a UART transaction as atomic at the simulation's 10 ms resolution: receive,
 * process, respond, and acknowledge all happen in the current tick. A component
 * whose real operation spans multiple ticks records that operation internally,
 * progresses it from on_tick(), and reports BUSY/COMPLETE through later
 * protocol requests. service() must not wait for a future tick because the
 * synchronized barrier cannot advance while FSW waits.
 */

/* Forward prototypes to satisfy -Wmissing-prototypes for REGISTER_COMPONENT export */
const component_interface_t* get_demo_sim_component_interface(void);
const component_interface_t* get_component_interface(void);

/* Internal command outcomes are deliberately separate from component callback
 * results. REJECTED means the simulator correctly handled invalid device input;
 * ERROR is reserved for a failure in the simulator or its transport. */
typedef enum
{
    DEMO_COMMAND_ERROR = -1,
    DEMO_COMMAND_SUCCESS = 0,
    DEMO_COMMAND_REJECTED = 1
} demo_command_result_t;

/**
 * Produce the next deterministic pseudo-random value for this model instance.
 *
 * A component-owned generator keeps repeatable scenarios independent of worker
 * scheduling and of random-number use by other component simulators.
 */
static uint32_t demo_sim_prng_next(demo_sim_state_t* state)
{
    uint32_t value = state->prng_state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    state->prng_state = value;
    return value;
}

/**
 * Serialize and send the demo device's housekeeping response.
 *
 * Protocol framing belongs to the modeled device rather than the director:
 * the director only schedules service work and does not interpret device data.
 */
static int send_housekeeping(demo_sim_state_t* state)
{
    if (!state) return SIMULITH_TRANSPORT_ERROR;
    uint8_t response[8];
    response[0] = DEMO_DEVICE_HDR_0;
    response[1] = DEMO_DEVICE_HDR_1;
    response[2] = (uint8_t)((state->hk.DeviceCounter >> 8) & 0xFF);
    response[3] = (uint8_t)(state->hk.DeviceCounter & 0xFF);
    response[4] = (uint8_t)((state->hk.DeviceConfig >> 8) & 0xFF);
    response[5] = (uint8_t)(state->hk.DeviceConfig & 0xFF);
    response[6] = DEMO_DEVICE_TRAILER_0;
    response[7] = DEMO_DEVICE_TRAILER_1;
    return simulith_transport_send(&state->uart_port, response,
                                   sizeof(response)) == (int)sizeof(response) ?
        SIMULITH_TRANSPORT_SUCCESS : SIMULITH_TRANSPORT_ERROR;
}

/**
 * Serialize and send the latest sampled demo data as one protocol frame.
 */
static int send_demo_data(demo_sim_state_t* state)
{
    if (!state) return SIMULITH_TRANSPORT_ERROR;
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
    return simulith_transport_send(&state->uart_port, response,
                                   sizeof(response)) == (int)sizeof(response) ?
        SIMULITH_TRANSPORT_SUCCESS : SIMULITH_TRANSPORT_ERROR;
}

/**
 * Validate and execute one complete demo-device command.
 *
 * A well-received but invalid command returns DEMO_COMMAND_REJECTED so the
 * caller can complete the flight transaction with a device-level failure
 * without reporting a simulator failure to the director.
 */
static demo_command_result_t handle_command(demo_sim_state_t* state,
                                            const uint8_t* data,
                                            size_t length)
{
    if (!state || !data)
    {
        printf("DEMO SIM: Invalid command parameters: state=%p, data=%p, length=%zu\n", 
               (void*)state, (const void*)data, length);
        return DEMO_COMMAND_ERROR;
    }
    if (length != DEMO_DEVICE_CMD_SIZE)
    {
        printf("DEMO SIM: Invalid command length: %zu\n", length);
        return DEMO_COMMAND_REJECTED;
    }
    
    uint16_t header  = ((uint16_t) data[0] << 8) | data[1];
    uint16_t cmd_id  = ((uint16_t) data[2] << 8) | data[3];
    uint16_t payload = ((uint16_t) data[4] << 8) | data[5];
    uint16_t trailer = ((uint16_t) data[6] << 8) | data[7];

    // Validate header
    if (header != DEMO_DEVICE_HDR) 
    {
        printf("DEMO SIM: Invalid command header (0x%04X)\n", header);
        return DEMO_COMMAND_REJECTED;
    }

    // Validate trailer
    if (trailer != DEMO_DEVICE_TRAILER) 
    {
        printf("DEMO SIM: Invalid command trailer (0x%04X)\n", trailer);
        return DEMO_COMMAND_REJECTED;
    }

    // Echo command back
    #ifdef DEMO_CFG_DEBUG
    printf("DEMO SIM: handle_command: Echo command back to UART: ID=%d, Payload=0x%08X\n", cmd_id, payload);
    #endif
    if (simulith_transport_send(&state->uart_port, data, length) !=
        (int)length)
        return DEMO_COMMAND_ERROR;

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
            if (send_housekeeping(state) != SIMULITH_TRANSPORT_SUCCESS)
                return DEMO_COMMAND_ERROR;
            break;

        case DEMO_DEVICE_REQ_DATA_CMD:
            #ifdef DEMO_CFG_DEBUG
            printf("DEMO SIM: Processing GET_DATA command\n");
            #endif
            if (send_demo_data(state) != SIMULITH_TRANSPORT_SUCCESS)
                return DEMO_COMMAND_ERROR;
            break;

        case DEMO_DEVICE_CFG_CMD:
            #ifdef DEMO_CFG_DEBUG
            printf("DEMO SIM: Processing SET_CONFIG command with payload 0x%08X\n", payload);
            #endif
            state->hk.DeviceConfig = payload;
            break;

        default:
            printf("DEMO SIM: Unknown command ID: %d\n", cmd_id);
            state->hk.DeviceCounter++;
            return DEMO_COMMAND_REJECTED;
    }

    // Increment command counter
    state->hk.DeviceCounter++;
    return DEMO_COMMAND_SUCCESS;
}

/**
 * Observe one new synchronized simulation tick.
 *
 * This callback is for autonomous, time-dependent model behavior only. It is
 * called exactly once before FSW runs and receives an immutable 42 truth
 * snapshot. Components that only react to protocol requests should set
 * on_tick to NULL instead. Never receive or send device traffic here.
 */
static int demo_sim_component_on_tick(component_state_t* component_state,
                                      uint64_t tick_time_ns,
                                      const simulith_42_context_t* context_42)
{
    demo_sim_state_t* state = (demo_sim_state_t*)component_state;
    if (!state) return COMPONENT_ERROR;
    
    // Update demo data at the specified rate.
    if (tick_time_ns >= state->next_update_time_ns)
    {
        // If 42 context is available and random data is not enabled, populate channels with Sun Vector Body (SVB)
        if ((state->rand_data_enabled == 0) && context_42 && context_42->valid) {
            // Chan1: SVB X-component (scaled and offset for uint16)
            // Scale by 10000 and add 32768 offset to handle negative values
            state->data.Chan1 = (uint16_t)((context_42->sun_vector_body[0] * 10000.0) + 32768.0);
            
            // Chan2: SVB Y-component (scaled and offset for uint16)
            state->data.Chan2 = (uint16_t)((context_42->sun_vector_body[1] * 10000.0) + 32768.0);
            
            // Chan3: SVB Z-component (scaled and offset for uint16)
            state->data.Chan3 = (uint16_t)((context_42->sun_vector_body[2] * 10000.0) + 32768.0);
            
        } else {
            // Random or default values
            // When random enabled, fill with pseudo-random 8-bit values
            // Using 8-bit values to make detection easier in testing
            if (state->rand_data_enabled == 1) {
                state->data.Chan1 = (uint16_t)(demo_sim_prng_next(state) & 0x00FFU);
                state->data.Chan2 = (uint16_t)(demo_sim_prng_next(state) & 0x00FFU);
                state->data.Chan3 = (uint16_t)(demo_sim_prng_next(state) & 0x00FFU);
            } else {
                // Fallback: Use command counter if no 42 context available
                state->data.Chan1 = (uint16_t)(state->hk.DeviceCounter * 1);
                state->data.Chan2 = (uint16_t)(state->hk.DeviceCounter * 2);
                state->data.Chan3 = (uint16_t)(state->hk.DeviceCounter * 3);
            }
        }
        
        // Handle random HK if enabled
        if (state->rand_hk_enabled == 1) {
            state->hk.DeviceConfig = (uint16_t)(demo_sim_prng_next(state) & 0xFF00U);
            state->hk.DeviceCounter = (uint16_t)(demo_sim_prng_next(state) & 0xFF00U);
        }
        
        /* Advance the modeled sampling deadline, not "last time called". This
         * keeps the requested cadence exact when its period is not an integer
         * multiple of the global tick and avoids repeating work if the same
         * tick is inspected more than once in a unit test. */
        uint64_t elapsed_periods =
            ((tick_time_ns - state->next_update_time_ns) /
             DEMO_SIM_UPDATE_PERIOD_NS) + 1U;
        if (elapsed_periods >
            (UINT64_MAX - state->next_update_time_ns) /
                DEMO_SIM_UPDATE_PERIOD_NS)
            state->next_update_time_ns = UINT64_MAX;
        else
            state->next_update_time_ns +=
                elapsed_periods * DEMO_SIM_UPDATE_PERIOD_NS;
    }

    return COMPONENT_SUCCESS;
}

/**
 * Wait efficiently for flight-side device traffic during EXECUTE.
 *
 * This callback is the component's readiness declaration. It blocks on the
 * owned transport and the Director-provided interrupt descriptor, consuming
 * neither request bytes nor modeled state. Keeping readiness separate from
 * service() prevents idle polling while preserving a simple rule for component
 * authors: wait here, perform the entire ready transaction in service(). The
 * Director signals interrupt_fd at COMMIT and shutdown, so this function must
 * always include it in the wait and return COMPONENT_IDLE when it fires.
 */
static int demo_sim_component_wait_for_service(component_state_t* component_state,
                                               int interrupt_fd)
{
    demo_sim_state_t* state = (demo_sim_state_t*)component_state;
    if (!state) return COMPONENT_ERROR;
    transport_port_t* ports[] = {&state->uart_port};
    return simulith_transport_wait_for_request(ports, 1U, interrupt_fd);
}

/**
 * Service at most one currently ready UART request without blocking.
 *
 * The director may invoke this function multiple times during EXECUTE. A call
 * owns the complete protocol exchange for the request it receives: validate,
 * update modeled state, emit every immediate response frame, then acknowledge
 * completion. Long physical operations are started here, progressed by
 * on_tick(), and observed by later protocol requests rather than by waiting.
 * Request-driven sensors may read tick_time_ns/context_42 lazily here; this demo
 * instead returns data from its autonomous 10 Hz on_tick() sampling model.
 */
static int demo_sim_component_service(component_state_t* component_state,
                                      uint64_t tick_time_ns,
                                      const simulith_42_context_t* context_42)
{
    demo_sim_state_t* state = (demo_sim_state_t*)component_state;
    uint8_t data[256];
    int bytes;
    uint64_t transaction_id;

    (void)tick_time_ns;
    (void)context_42;
    if (!state) return COMPONENT_ERROR;
    /* receive_request() does not wait. The transaction ID ties the final
     * completion acknowledgement to this exact request and current phase. */
    bytes = simulith_transport_receive_request(&state->uart_port, data,
                                               sizeof(data), &transaction_id);
    if (bytes > 0)
    {
        #ifdef DEMO_CFG_DEBUG
        printf("DEMO SIM: Received %d bytes from UART\n", bytes);
        for(int i = 0; i < bytes; i++) 
        {
            printf("%02X ", data[i]);
        }
        printf("\n");
        #endif

        demo_command_result_t command_status =
            handle_command(state, data, (size_t)bytes);
        /* Completion is deliberately sent only after command validation,
         * modeled state changes, and every response byte has been sent. A
         * rejected command receives a failed protocol completion but still
         * counts as successfully serviced component work. */
        int completion_status = command_status == DEMO_COMMAND_SUCCESS ?
            SIMULITH_TRANSPORT_SUCCESS : SIMULITH_TRANSPORT_ERROR;
        if (simulith_transport_complete_request(&state->uart_port,
                                                transaction_id,
                                                completion_status) !=
            SIMULITH_TRANSPORT_SUCCESS)
            return COMPONENT_ERROR;
        return command_status == DEMO_COMMAND_ERROR ?
            COMPONENT_ERROR : COMPONENT_WORK;
    }
    if (bytes < 0) return COMPONENT_ERROR;
    return COMPONENT_IDLE;
}

/**
 * Apply a simulation-only control that is intentionally absent from the flight
 * device protocol. Backdoors are useful for deterministic fault injection and
 * test setup, but must never replace a real flight transaction.
 */
static void demo_sim_backdoor(component_state_t* cstate, uint16_t cmd_id,
                              const uint8_t* payload, uint16_t payload_len)
{
    demo_sim_state_t* state = (demo_sim_state_t*)cstate;
    if (!state) return;
    
    switch (cmd_id) 
    {
        case DEMO_BD_SET_CONFIG:    
            if (payload && payload_len >= 2)
            {
                uint16_t value = (uint16_t)((payload[0] << 8) | payload[1]);
                state->hk.DeviceConfig = value;
                printf("DEMO SIM BACKDOOR: Setting DeviceConfig from backdoor command to 0x%04X\n", 
                   (payload_len >= 2) ? ((payload[0] << 8) | payload[1]) : 0);
            }
            break;
        
        case DEMO_BD_RAND_HK:
            if (payload_len == 0)
                state->rand_hk_enabled = true;
            else if (payload)
                state->rand_hk_enabled = payload[0] != 0;
            printf("DEMO SIM BACKDOOR: Setting random HK to %s\n", state->rand_hk_enabled ? "ENABLED" : "DISABLED");
            break;
        
        case DEMO_BD_RAND_DATA:
            if (payload_len == 0)
                state->rand_data_enabled = true;
            else if (payload)
                state->rand_data_enabled = payload[0] != 0;
            printf("DEMO SIM BACKDOOR: Setting random DATA to %s\n", state->rand_data_enabled ? "ENABLED" : "DISABLED");
            break;
        
        default:
            break;
    }
}

/**
 * Initialize the already-allocated demo state and open its UART endpoint.
 *
 * Keeping this rollback-friendly work separate from allocation makes create()
 * straightforward: either it returns one fully usable instance or no instance.
 */
static int demo_sim_initialize_state(demo_sim_state_t* state)
{
    if (!state) return DEMO_SIM_ERROR;

    // Initialize state
    memset(state, 0, sizeof(demo_sim_state_t));
    
    // Initialize the simulator-owned UART endpoint (server/bind).
    snprintf(state->uart_port.name, sizeof(state->uart_port.name),
             "demo_sim_uart%d", DEMO_CFG_HANDLE);
    snprintf(state->uart_port.address, sizeof(state->uart_port.address),
             "ipc:///tmp/simulith_pub:%d", SIMULITH_UART_BASE_PORT + DEMO_CFG_HANDLE);
    state->uart_port.is_server = 1;

    int uart_result = simulith_transport_init(&state->uart_port);
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
    state->next_update_time_ns = DEMO_SIM_UPDATE_PERIOD_NS;
    state->prng_state = DEMO_SIM_PRNG_SEED;
    state->rand_hk_enabled = 0;
    state->rand_data_enabled = 0;

    printf("DEMO SIM: Initialized successfully as %s\n", state->uart_port.name);
    return DEMO_SIM_SUCCESS;
}

/**
 * Create one independently owned demo simulator instance.
 *
 * The director calls create() once after loading the component library. New
 * components should allocate all mutable model state and own all transport
 * handles here; file-scope mutable state prevents safe multi-instance use.
 */
static int demo_sim_component_create(component_state_t** state)
{
    if (!state)
        return COMPONENT_ERROR;

    *state = NULL;
    demo_sim_state_t* demo_state = malloc(sizeof(demo_sim_state_t));
    if (!demo_state) {
        return COMPONENT_ERROR;
    }
    
    int result = demo_sim_initialize_state(demo_state);
    if (result != DEMO_SIM_SUCCESS) {
        free(demo_state);
        return COMPONENT_ERROR;
    }
    
    *state = (component_state_t*)demo_state;
    return COMPONENT_SUCCESS;
}

/**
 * Publish the final actuator outputs produced by this tick's FSW activity.
 *
 * The demo device has no actuators, so this is intentionally a documented
 * no-op. An actuator component should enqueue one coherent output batch here;
 * the director commits all component batches to 42 only after every actuate()
 * callback has returned.
 */
static int demo_sim_component_actuate(component_state_t* component_state,
                                       uint64_t tick_time_ns,
                                       const simulith_42_context_t* context_42)
{
    if (!component_state)
        return COMPONENT_ERROR;
    (void)tick_time_ns;
    (void)context_42;
    return COMPONENT_SUCCESS;
}

/**
 * Destroy one demo simulator instance after all callbacks are quiescent.
 *
 * Every resource acquired by create() is released here. destroy(NULL) is a
 * harmless no-op, which keeps failure rollback and director shutdown simple.
 */
static void demo_sim_component_destroy(component_state_t* state)
{
    if (!state) return;
    
    demo_sim_state_t* demo_state = (demo_sim_state_t*)state;
    simulith_transport_close(&demo_state->uart_port);
    free(demo_state);
}

/**
 * Describe the demo simulator to the director.
 *
 * This table is the primary mold for new components. Keep the lifecycle in
 * phase order so ownership and tick causality are visible at a glance.
 */
static const component_interface_t demo_sim_interface = {
    .api_version = SIMULITH_COMPONENT_API_VERSION,
    .struct_size = sizeof(component_interface_t),
    .name = "demo_sim",
    .description = "Demo component simulation with UART interface",
    .create = demo_sim_component_create,
    .on_tick = demo_sim_component_on_tick,
    .wait_for_service = demo_sim_component_wait_for_service,
    .service = demo_sim_component_service,
    .actuate = demo_sim_component_actuate,
    .destroy = demo_sim_component_destroy,
    .backdoor = demo_sim_backdoor
};

/** Return the demo-named registration alias used by component-specific tools. */
REGISTER_COMPONENT(demo_sim)
{
    return &demo_sim_interface;
}

/** Return the standard registration symbol resolved by the director loader. */
__attribute__((visibility("default")))
const component_interface_t* get_component_interface(void)
{
    return &demo_sim_interface;
}
