#ifndef _EPS_SIM_H_
#define _EPS_SIM_H_

#include "eps_device.h"
#include "simulith.h"
#include "simulith_component.h"

/* Power calculation macro using configurable constants */
#define EPS_SWITCH_POWER_W(switch_idx) \
    ((switch_idx == 0 || switch_idx == 1) ? EPS_SWITCH_3V3_POWER_W : \
     (switch_idx == 2 || switch_idx == 3) ? EPS_SWITCH_5V_POWER_W : \
     (switch_idx == 4 || switch_idx == 5) ? EPS_SWITCH_12V_POWER_W : \
     (switch_idx == 6 || switch_idx == 7) ? EPS_SWITCH_24V_POWER_W : 0.0)

/*
** EPS simulation state structure
*/
typedef struct 
{
    EPS_Device_HK_tlm_t hk;         /* Housekeeping telemetry */
    uint32_t device_counter;        /* Device counter */
    transport_port_t i2c_device;    /* I2C device handle */
    double battery_energy_wh;       /* Battery energy in watt-hours */
} eps_sim_state_t;

/*
** Function prototypes
*/
int eps_sim_init(eps_sim_state_t* state);
void eps_sim_cleanup(eps_sim_state_t* state);

#endif /* _EPS_SIM_H_ */
