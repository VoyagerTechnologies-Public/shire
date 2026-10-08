#ifndef _EPS_SIM_H_
#define _EPS_SIM_H_
#include "eps_device.h"
#include "simulith.h"
#include "simulith_component.h"

#define EPS_CONSOLE_DOUBLES 110U

/* Optional debug console serialization, private to EPS. */
size_t eps_sim_console_state(component_state_t *state, uint64_t ns, double dyn_time,
                             uint8_t *packet, size_t capacity);

#define EPS_BD_SET_SOC 1U
#define EPS_BD_SET_SOLAR 2U
#define EPS_BD_SET_SWITCH 3U
#define EPS_BD_SET_LOAD_SCALE 4U
#define EPS_BD_SET_SWITCH_FAULT 5U
#define EPS_BD_CORRUPT_HK_CRC 6U
#define EPS_BD_FAIL_REQUESTS 7U
#define EPS_BD_CLEAR_FAULTS 8U
#define EPS_BD_RESET 9U

typedef struct {
    EPS_Device_HK_tlm_t hk;
    uint32_t device_counter;
    transport_port_t i2c_device;
    double battery_energy_wh;
    uint64_t next_hk_update_ns;
    uint32_t prng_state;
    simulith_power_supply_t power;
    uint64_t model_time_ns;
    double solar_power_w;
    double auto_solar_power_w;
    double load_power_w;
    double initial_energy_wh;
    double solar_energy_wh;
    double load_energy_wh;
    double adjustment_wh;
    uint8_t solar_override;
    double solar_override_w;
    uint64_t requests_successful;
    uint64_t requests_rejected;
    uint16_t crc_remaining;
    uint16_t fail_remaining;
    uint8_t fail_selector;
    uint64_t backdoor_accepted;
    uint64_t backdoor_rejected;
    uint16_t backdoor_last_id;
    uint8_t backdoor_last_status;
    uint64_t backdoor_generation;
    uint64_t backdoor_applied_ns;
    uint8_t debug_enabled;
} eps_sim_state_t;

int eps_sim_init(eps_sim_state_t *state);
void eps_sim_cleanup(eps_sim_state_t *state);
#endif
