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

// Status codes
#define DEMO_SIM_SUCCESS 0
#define DEMO_SIM_ERROR  1

// Backdoor command IDs
#define DEMO_BD_SET_CONFIG  0x0001
#define DEMO_BD_RAND_HK     0x0002
#define DEMO_BD_RAND_DATA   0x0003

// Demo simulator state
typedef struct 
{
    // Communication handles
    uint8_t uart_port;
    uint32_t uart_handle;
    void* time_handle;
    // Simulator specifics
    double last_update_time;
    uint8_t rand_hk_enabled;
    uint8_t rand_data_enabled;
    // Device specifics
    DEMO_Device_HK_tlm_t hk;
    DEMO_Device_Data_tlm_t data;
} demo_sim_state_t;

// Function declarations
static void send_housekeeping(demo_sim_state_t* state);
static void send_demo_data(demo_sim_state_t* state);
static void handle_command(demo_sim_state_t* state, const uint8_t* data, size_t length);
static void demo_sim_on_tick(uint64_t tick_time_ns, const simulith_42_context_t* context_42);
int demo_sim_init(demo_sim_state_t* state);
void demo_sim_cleanup(demo_sim_state_t* state);

#endif /* DEMO_SIM_H */ 