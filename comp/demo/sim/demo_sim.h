#ifndef DEMO_SIM_H
#define DEMO_SIM_H

#include <math.h>
#include "demo_device.h"
#include "simulith.h"
#include "simulith_component.h"
#include "simulith_42_context.h"
#include "simulith_42_commands.h"

// Configuration parameters
#define DEMO_SIM_UPDATE_RATE_HZ 10
#define DEMO_SIM_UPDATE_PERIOD_NS (1000000000ULL / DEMO_SIM_UPDATE_RATE_HZ)
#define DEMO_SIM_PRNG_SEED 0x44454D4FU

// Status codes
#define DEMO_SIM_SUCCESS 0
#define DEMO_SIM_ERROR  1

// Backdoor command IDs
#define DEMO_BD_SET_CONFIG  0x0001
#define DEMO_BD_RAND_HK     0x0002
#define DEMO_BD_RAND_DATA   0x0003

/*
 * Complete mutable state for one demo simulator instance.
 *
 * New component simulators should keep transport ownership and modeled state
 * here rather than in file-scope globals. create() allocates this object, the
 * director passes it to every component_interface_t callback, and destroy()
 * releases it after the callbacks have been quiesced.
 */
typedef struct 
{
    // Simulator-side endpoint for the flight UART device API.
    transport_port_t uart_port;

    // Simulator behavior and update timing.
    uint64_t next_update_time_ns;
    uint32_t prng_state;
    uint8_t rand_hk_enabled;
    uint8_t rand_data_enabled;

    // Modeled device state returned to flight software.
    DEMO_Device_HK_tlm_t hk;
    DEMO_Device_Data_tlm_t data;
} demo_sim_state_t;

#endif /* DEMO_SIM_H */
