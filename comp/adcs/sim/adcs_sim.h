#ifndef ADCS_SIM_H
#define ADCS_SIM_H

#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "adcs_device.h"
#include "simulith.h"
#include "simulith_component.h"
#include "simulith_42_context.h"
#include "simulith_42_commands.h"

// Configuration parameters
#define ADCS_SIM_UPDATE_RATE_HZ 10

// Status codes
#define ADCS_SIM_SUCCESS 0
#define ADCS_SIM_ERROR  1

// ADCS Controller Constants
#define ADCS_CONTROLLER_UPDATE_RATE_HZ 5.0   // Controller update rate
#define ADCS_SUN_POINT_KP 0.5               // Aggressive proportional gain
#define ADCS_SUN_POINT_KD 0.1               // Aggressive derivative gain
#define ADCS_WHEEL_MAX_TORQUE 0.005         // Max wheel torque updated to match SC_NOS3.txt
#define ADCS_MTB_MAX_DIPOLE 1.42            // Max MTB dipole from SC_NOS3.txt
#define ADCS_DETUMBLE_GAIN_BASE 0.01        // Moderate base detumble gain
#define ADCS_DETUMBLE_GAIN_HIGH 0.02        // Moderate high rate detumble gain
#define ADCS_WHEEL_DESAT_THRESHOLD 0.032    // Wheel momentum threshold for desaturation (80% of 0.04)
#define ADCS_RATE_THRESHOLD 0.10            // Rate threshold - switch to hybrid (rad/s)
#define ADCS_HIGH_RATE_THRESHOLD 0.5        // High rate threshold (rad/s)
#define ADCS_ERROR_THRESHOLD 0.15           // Error threshold for fine pointing (rad)

// Adcs simulator state
typedef struct 
{
    // Communication handles
    uint8_t uart_port;
    uint32_t uart_handle;
    void* time_handle;
    // Simulator specifics
    double last_update_time;
    // Device specifics
    ADCS_Device_HK_tlm_t hk;
    ADCS_Device_Data_tlm_t data;
    // ADCS Controller state
    double last_control_time;
    double prev_attitude_error[3];
    int current_mode;
    int controller_active;
} adcs_sim_state_t;

// Public API
int adcs_sim_init(adcs_sim_state_t* state);
void adcs_sim_cleanup(adcs_sim_state_t* state);

#endif /* ADCS_SIM_H */ 