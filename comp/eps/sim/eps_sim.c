#include "eps_sim.h"
#include <math.h>

const component_interface_t *get_eps_sim_component_interface(void);
const component_interface_t *get_component_interface(void);

#define EPS_HK_UPDATE_PERIOD_NS 1000000000ULL
#define EPS_PRNG_INITIAL_STATE 0x45505331U

typedef enum { EPS_COMMAND_ERROR = -1, EPS_COMMAND_SUCCESS = 0,
               EPS_COMMAND_REJECTED = 1 } eps_command_result_t;

static uint32_t eps_prng_next(eps_sim_state_t *state)
{
    uint32_t v = state->prng_state;
    v ^= v << 13; v ^= v >> 17; v ^= v << 5;
    state->prng_state = v;
    return v;
}

static uint8_t encode(double value, double full_scale)
{
    double counts = fmax(0.0, fmin(255.0, floor(value * 255.0 / full_scale + 1e-10)));
    return (uint8_t)counts;
}

static double rail_power(const eps_sim_state_t *s, unsigned int idx)
{
    double w = 0;
    for (unsigned int j = 0; j < SIMULITH_POWER_LOADS; j++) {
        const simulith_power_load_config_t *l = &s->power.loads[j];
        if (l->component[0] && l->switch_id == idx && s->power.effective[idx])
            w += s->power.configured ? s->power.snapshots[j].watts : l->mode_w[0] * l->scale;
    }
    return w;
}

static void refresh_hk(eps_sim_state_t *s)
{
    s->hk.battery_voltage = encode(EPS_BATTERY_VOLTAGE_MIN +
        (s->battery_energy_wh / EPS_BATTERY_CAPACITY_WH) *
        (EPS_BATTERY_VOLTAGE_MAX - EPS_BATTERY_VOLTAGE_MIN), 32.0);
    s->hk.solar_voltage = encode(s->solar_power_w > 0 ? 4.5 : 0, 32.0);
    s->load_power_w = 0;
    for (unsigned int i = 0; i < EPS_NUM_SWITCHES; i++) {
        double w = rail_power(s, i);
        s->load_power_w += w;
        s->hk.switches[i].state = s->power.effective[i];
        s->hk.switches[i].voltage = encode(s->power.effective[i] ? s->power.voltage[i] : 0, 32.0);
        s->hk.switches[i].current = encode(w / s->power.voltage[i], 10.0);
    }
}

static int queue_switch(eps_sim_state_t *s, uint8_t idx, uint8_t requested)
{
    uint8_t effective = s->power.fault[idx] == 1 ? 0 :
                        s->power.fault[idx] == 2 ? 1 : requested;
    uint8_t scheduled = s->power.effective[idx];
    for (size_t i = 0; i < s->power.transition_count; i++)
        if (!s->power.transitions[i].reset && s->power.transitions[i].switch_id == idx) scheduled = s->power.transitions[i].on;
    if (effective != scheduled) {
        if (s->power.transition_count >= SIMULITH_POWER_TRANSITIONS) {
            s->power.overflow = 1;
            return -1;
        }
        s->power.transitions[s->power.transition_count++] =
            (simulith_power_transition_t){idx, effective, 0};
        /* A bound supply changes physically at COMMIT. Standalone simulator
         * callers have no director boundary and apply their local state here. */
        if (!s->power.configured) s->power.effective[idx] = effective;
    }
    s->power.requested[idx] = requested;
    refresh_hk(s);
    return 0;
}

static eps_command_result_t handle_eps_command(eps_sim_state_t *s,
                                               const uint8_t *data, size_t length)
{
    if (!s || !data) return EPS_COMMAND_ERROR;
    if (length != EPS_COMMAND_SIZE) return EPS_COMMAND_REJECTED;
    EPS_Command_t cmd;
    memcpy(&cmd, data, sizeof(cmd));
    if (cmd.i2c_addr != EPS_CFG_I2C_DEVICE_ADDR ||
        !EPS_Verify_CRC8(data, length - 1, cmd.crc)) return EPS_COMMAND_REJECTED;
    if (cmd.command > EPS_CMD_SWITCH_ON ||
        ((cmd.command == EPS_CMD_SWITCH_ON || cmd.command == EPS_CMD_SWITCH_OFF) &&
         cmd.payload >= EPS_NUM_SWITCHES)) {
        s->device_counter++;
        return EPS_COMMAND_REJECTED;
    }
    uint8_t selector = cmd.command == EPS_CMD_GET_HK ? 1 :
                       cmd.command == EPS_CMD_SWITCH_ON ? 2 :
                       cmd.command == EPS_CMD_SWITCH_OFF ? 3 : 4;
    if (s->fail_remaining && (!s->fail_selector || s->fail_selector == selector)) {
        s->fail_remaining--;
        return EPS_COMMAND_REJECTED;
    }
    switch (cmd.command) {
        case EPS_CMD_GET_HK: {
            refresh_hk(s);
            EPS_Device_HK_tlm_t response = s->hk;
            response.crc = EPS_Calculate_CRC8((const uint8_t *)&response, sizeof(response) - 1);
            if (s->crc_remaining) response.crc ^= 1U;
            if (simulith_transport_send(&s->i2c_device, (const uint8_t *)&response,
                                        sizeof(response)) != (int)sizeof(response))
                return EPS_COMMAND_ERROR;
            if (s->crc_remaining) s->crc_remaining--;
            s->hk.crc = EPS_Calculate_CRC8((const uint8_t *)&s->hk, sizeof(s->hk) - 1);
            break;
        }
        case EPS_CMD_SWITCH_OFF:
        case EPS_CMD_SWITCH_ON:
            if (queue_switch(s, cmd.payload, cmd.command == EPS_CMD_SWITCH_ON) != 0)
                return EPS_COMMAND_ERROR;
            break;
        default: break;
    }
    s->device_counter++;
    return EPS_COMMAND_SUCCESS;
}

static int eps_component_on_tick(component_state_t *state, uint64_t ns,
                                 const simulith_42_context_t *context)
{
    eps_sim_state_t *s = (eps_sim_state_t *)state;
    if (!s || ns < s->model_time_ns) return COMPONENT_ERROR;
    double hours = (double)(ns - s->model_time_ns) / 3.6e12;
    double generated = s->solar_power_w * hours;
    double consumed = s->load_power_w * hours;
    double energy = s->battery_energy_wh + generated - consumed;
    s->battery_energy_wh = fmax(0, fmin(EPS_BATTERY_CAPACITY_WH, energy));
    s->solar_energy_wh += generated;
    s->load_energy_wh += consumed;
    s->adjustment_wh += s->battery_energy_wh - energy;
    s->model_time_ns = ns;
    s->auto_solar_power_w = context && context->valid && !context->eclipse ?
        EPS_MAX_SOLAR_POWER_W * fmax(0, fmin(1, context->sun_vector_body[0])) : 0;
    s->solar_power_w = s->solar_override ? s->solar_override_w : s->auto_solar_power_w;
    refresh_hk(s);
    if (ns >= s->next_hk_update_ns) {
        s->hk.battery_temperature = encode(20.0 + (int)(eps_prng_next(s) % 3U) - 1, 250.0);
        s->hk.solar_temperature = encode(35.0 + (int)(eps_prng_next(s) % 3U) - 1, 250.0);
        uint64_t periods = (ns - s->next_hk_update_ns) / EPS_HK_UPDATE_PERIOD_NS + 1U;
        s->next_hk_update_ns = periods > (UINT64_MAX - s->next_hk_update_ns) / EPS_HK_UPDATE_PERIOD_NS ?
            UINT64_MAX : s->next_hk_update_ns + periods * EPS_HK_UPDATE_PERIOD_NS;
    }
    return COMPONENT_SUCCESS;
}

static int eps_component_actuate(component_state_t *state, uint64_t ns,
                                  const simulith_42_context_t *context)
{
    if (!state) return COMPONENT_ERROR;
    eps_sim_state_t *s = (eps_sim_state_t *)state;
    refresh_hk(s);
    /* Log at COMMIT, after supply transitions and service workers quiesce.
     * Snapshots are optional troubleshooting output, never acceptance data. */
    if (s->debug_enabled && context && context->valid &&
        ns % UINT64_C(1000000000) == UINT64_C(1000000000) - INTERVAL_NS) {
        uint8_t packet[EPS_CONSOLE_DOUBLES * 8U];
        if (eps_sim_console_state(state, ns, context->dyn_time,
                                          packet, sizeof(packet)) != sizeof(packet))
            return COMPONENT_ERROR;
        flockfile(stdout);
        printf("EPS_SIM_STATE {\"time_ns\":%llu,\"values\":[", (unsigned long long)ns);
        for (unsigned int k = 0; k < EPS_CONSOLE_DOUBLES; k++) {
            uint64_t bits = 0;
            for (unsigned int b = 0; b < 8; b++) bits |= (uint64_t)packet[k * 8 + b] << (8 * b);
            double value; memcpy(&value, &bits, sizeof(value));
            printf("%s%.17g", k ? "," : "", value);
        }
        printf("]}\n");
        fflush(stdout);
        funlockfile(stdout);
    }
    return COMPONENT_SUCCESS;
}

static int eps_component_wait_for_service(component_state_t *state, int interrupt_fd)
{
    eps_sim_state_t *s = (eps_sim_state_t *)state;
    if (!s) return COMPONENT_ERROR;
    transport_port_t *ports[] = {&s->i2c_device};
    return simulith_transport_wait_for_request(ports, 1U, interrupt_fd);
}

static int eps_component_service(component_state_t *state, uint64_t ns,
                                  const simulith_42_context_t *context)
{
    (void)ns; (void)context;
    eps_sim_state_t *s = (eps_sim_state_t *)state;
    if (!s) return COMPONENT_ERROR;
    uint8_t bytes[256];
    uint64_t id;
    int n = simulith_transport_receive_request(&s->i2c_device, bytes, sizeof(bytes), &id);
    if (n < 0) return COMPONENT_ERROR;
    if (!n) return COMPONENT_IDLE;
    eps_command_result_t result = handle_eps_command(s, bytes, (size_t)n);
    if (result == EPS_COMMAND_SUCCESS) s->requests_successful++;
    else s->requests_rejected++;
    if (simulith_transport_complete_request(&s->i2c_device, id,
            result == EPS_COMMAND_SUCCESS ? SIMULITH_TRANSPORT_SUCCESS : SIMULITH_TRANSPORT_ERROR) !=
            SIMULITH_TRANSPORT_SUCCESS) return COMPONENT_ERROR;
    return result == EPS_COMMAND_ERROR ? COMPONENT_ERROR : COMPONENT_WORK;
}

static void restore_model(eps_sim_state_t *s, int cycle)
{
    double energy = EPS_BATTERY_CAPACITY_WH * EPS_BATTERY_INITIAL_SOC;
    s->adjustment_wh += energy - s->battery_energy_wh;
    s->battery_energy_wh = energy;
    s->solar_override = 0;
    s->solar_override_w = 0;
    s->solar_power_w = s->auto_solar_power_w;
    s->crc_remaining = s->fail_remaining = 0;
    s->fail_selector = 0;
    s->prng_state = EPS_PRNG_INITIAL_STATE;
    s->device_counter = 0;
    s->hk.battery_temperature = encode(20, 250);
    s->hk.solar_temperature = encode(35, 250);
    s->next_hk_update_ns = s->model_time_ns > UINT64_MAX - EPS_HK_UPDATE_PERIOD_NS ?
        UINT64_MAX : s->model_time_ns + EPS_HK_UPDATE_PERIOD_NS;
    const simulith_power_load_config_t defaults[] = EPS_POWER_LOAD_CONFIG;
    for (unsigned int i = 0; i < SIMULITH_POWER_LOADS; i++) s->power.loads[i].scale = defaults[i].scale;
    for (uint8_t i = 0; i < EPS_NUM_SWITCHES; i++) {
        s->power.fault[i] = 0;
        if (cycle) {
            if (s->power.transition_count >= SIMULITH_POWER_TRANSITIONS) { s->power.overflow = 1; return; }
            s->power.transitions[s->power.transition_count++] = (simulith_power_transition_t){i, 0, 1};
        }
        if (cycle) (void)queue_switch(s, i, 0);
        (void)queue_switch(s, i, s->power.startup[i]);
    }
    refresh_hk(s);
}

int eps_sim_init(eps_sim_state_t *s)
{
    if (!s) return -1;
    memset(s, 0, sizeof(*s));
#ifdef EPS_CFG_DEBUG
    s->debug_enabled = 1;
#endif
    const char *debug = getenv("SIMULITH_EPS_DEBUG");
    if (debug && debug[0]) {
        if (strcmp(debug, "0") && strcmp(debug, "1")) return -1;
        s->debug_enabled = (uint8_t)(debug[0] == '1');
    }
    const double volts[] = EPS_SWITCH_VOLTAGES;
    const uint8_t startup[] = EPS_SWITCH_STARTUP;
    const simulith_power_load_config_t loads[] = EPS_POWER_LOAD_CONFIG;
    memcpy(s->power.voltage, volts, sizeof(volts));
    memcpy(s->power.startup, startup, sizeof(startup));
    memcpy(s->power.loads, loads, sizeof(loads));
    s->initial_energy_wh = EPS_BATTERY_CAPACITY_WH * EPS_BATTERY_INITIAL_SOC;
    restore_model(s, 0);
    s->adjustment_wh = 0;
    s->power.transition_count = 0;
    return 0;
}

void eps_sim_cleanup(eps_sim_state_t *s) { (void)s; }

static uint32_t read_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static int scale_valid(const eps_sim_state_t *s, unsigned int load, double scale)
{
    if (!s->power.loads[load].component[0] || scale > 1000.0) return 0;
    for (unsigned int i = 0; i < EPS_NUM_SWITCHES; i++) {
        double peak = 0;
        for (unsigned int j = 0; j < SIMULITH_POWER_LOADS; j++) {
            const simulith_power_load_config_t *l = &s->power.loads[j];
            if (!l->component[0] || l->switch_id != i) continue;
            double w = l->boot_w;
            for (unsigned int m = 0; m < SIMULITH_POWER_MODES; m++) w = fmax(w, l->mode_w[m]);
            peak += w * (j == load ? scale : l->scale);
        }
        if (peak / s->power.voltage[i] > 10.0) return 0;
    }
    return 1;
}

static void eps_component_backdoor(component_state_t *state, uint16_t cmd,
                                    const uint8_t *p, uint16_t n)
{
    eps_sim_state_t *s = (eps_sim_state_t *)state;
    if (!s) return;
    int accepted = 0;
    if (n && !p) goto finish;
    switch (cmd) {
        case EPS_BD_SET_SOC:
            if (n == 4 && read_u32(p) <= 1000000U) {
                double energy = EPS_BATTERY_CAPACITY_WH * (read_u32(p) / 1e6);
                s->adjustment_wh += energy - s->battery_energy_wh;
                s->battery_energy_wh = energy; accepted = 1;
            }
            break;
        case EPS_BD_SET_SOLAR:
            if (n == 5 && p[0] <= 1 && read_u32(p + 1) / 1000.0 <= EPS_MAX_SOLAR_POWER_W) {
                s->solar_override = p[0]; s->solar_override_w = read_u32(p + 1) / 1000.0;
                s->solar_power_w = p[0] ? s->solar_override_w : s->auto_solar_power_w;
                accepted = 1;
            }
            break;
        case EPS_BD_SET_SWITCH:
            if (n == 2 && p[0] < EPS_NUM_SWITCHES && p[1] <= 1)
                accepted = queue_switch(s, p[0], p[1]) == 0;
            break;
        case EPS_BD_SET_LOAD_SCALE:
            if (n == 5 && p[0] < SIMULITH_POWER_LOADS &&
                scale_valid(s, p[0], read_u32(p + 1) / 1e6)) {
                s->power.loads[p[0]].scale = read_u32(p + 1) / 1e6; accepted = 1;
            }
            break;
        case EPS_BD_SET_SWITCH_FAULT:
            if (n == 2 && p[0] < EPS_NUM_SWITCHES && p[1] <= 2) {
                s->power.fault[p[0]] = p[1];
                accepted = queue_switch(s, p[0], s->power.requested[p[0]]) == 0;
            }
            break;
        case EPS_BD_CORRUPT_HK_CRC:
            if (n == 2) { s->crc_remaining = read_u16(p); accepted = 1; }
            break;
        case EPS_BD_FAIL_REQUESTS:
            if (n == 3 && p[0] <= 3) {
                s->fail_selector = p[0]; s->fail_remaining = read_u16(p + 1); accepted = 1;
            }
            break;
        case EPS_BD_CLEAR_FAULTS:
            if (!n) {
                s->crc_remaining = s->fail_remaining = 0;
                s->fail_selector = 0;
                for (uint8_t i = 0; i < EPS_NUM_SWITCHES; i++) {
                    s->power.fault[i] = 0;
                    if (queue_switch(s, i, s->power.requested[i]) != 0) goto finish;
                }
                accepted = 1;
            }
            break;
        case EPS_BD_RESET:
            if (!n) { restore_model(s, 1); accepted = !s->power.overflow; }
            break;
        default: break;
    }
finish:
    s->backdoor_last_id = cmd;
    s->backdoor_last_status = (uint8_t)accepted;
    s->backdoor_applied_ns = s->model_time_ns;
    if (accepted) { s->backdoor_accepted++; s->backdoor_generation++; refresh_hk(s); }
    else s->backdoor_rejected++;
    printf("EPS_BACKDOOR {\"id\":%u,\"accepted\":%d,\"generation\":%lu,\"time_ns\":%lu,\"payload_hex\":\"",
           cmd, accepted, (unsigned long)s->backdoor_generation, (unsigned long)s->model_time_ns);
    if (p) for (uint16_t i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\"}\n");
}

static simulith_power_supply_t *eps_component_power_supply(component_state_t *state)
{
    return state ? &((eps_sim_state_t *)state)->power : NULL;
}

size_t eps_sim_console_state(component_state_t *state, uint64_t ns,
                                           double dyn_time, uint8_t *packet, size_t capacity)
{
    eps_sim_state_t *s = (eps_sim_state_t *)state;
    if (!s || !packet || capacity < EPS_CONSOLE_DOUBLES * 8U) return 0;
    refresh_hk(s);
    double values[EPS_CONSOLE_DOUBLES];
    size_t i = 0;
    values[i++] = dyn_time; values[i++] = (double)ns / 1e9;
    values[i++] = s->battery_energy_wh;
    values[i++] = s->battery_energy_wh / EPS_BATTERY_CAPACITY_WH;
    values[i++] = s->solar_power_w; values[i++] = s->load_power_w;
    double expected = s->initial_energy_wh + s->solar_energy_wh - s->load_energy_wh + s->adjustment_wh;
    values[i++] = expected; values[i++] = s->battery_energy_wh - expected;
    values[i++] = (double)s->backdoor_accepted; values[i++] = (double)s->backdoor_rejected;
    values[i++] = s->backdoor_last_id; values[i++] = s->backdoor_last_status;
    values[i++] = (double)s->backdoor_generation; values[i++] = (double)s->backdoor_applied_ns / 1e9;
    values[i++] = s->solar_override; values[i++] = s->crc_remaining;
    values[i++] = s->fail_remaining; values[i++] = s->fail_selector;
    values[i++] = s->initial_energy_wh; values[i++] = s->solar_energy_wh;
    values[i++] = s->load_energy_wh; values[i++] = s->adjustment_wh;
    for (unsigned int j = 0; j < EPS_NUM_SWITCHES; j++) {
        values[i++] = s->power.requested[j]; values[i++] = s->power.effective[j];
        values[i++] = s->power.fault[j]; values[i++] = s->power.effective[j] ? s->power.voltage[j] : 0;
        values[i++] = rail_power(s, j) / s->power.voltage[j]; values[i++] = rail_power(s, j);
    }
    for (unsigned int j = 0; j < SIMULITH_POWER_LOADS; j++) {
        const simulith_power_snapshot_t *v = &s->power.snapshots[j];
        values[i++] = v->supplied; values[i++] = v->ready; values[i++] = v->mode;
        values[i++] = (double)v->cycles; values[i++] = (double)v->successful_requests;
        values[i++] = (double)v->rejected_requests; values[i++] = (double)v->last_response_ns / 1e9;
        values[i++] = v->watts; values[i++] = s->power.loads[j].scale;
        values[i++] = (double)v->rf_received; values[i++] = (double)v->rf_sent;
        values[i++] = s->power.loads[j].switch_id;
    }
    values[i++] = (double)(s->backdoor_applied_ns / INTERVAL_NS);
    values[i++] = s->solar_override_w;
    values[i++] = (double)s->requests_successful;
    values[i++] = (double)s->requests_rejected;
    if (i != EPS_CONSOLE_DOUBLES) return 0;
    for (size_t j = 0; j < i; j++) {
        uint64_t bits; memcpy(&bits, &values[j], 8);
        for (unsigned int k = 0; k < 8; k++) packet[j * 8 + k] = (uint8_t)(bits >> (k * 8));
    }
    return i * 8;
}

static int eps_component_create(component_state_t** state)
{    
    if (!state)
        return COMPONENT_ERROR;
    *state = NULL;
    /* Allocate component state */
    eps_sim_state_t* eps_state = (eps_sim_state_t*)malloc(sizeof(eps_sim_state_t));
    if (!eps_state)
    {
        printf("EPS SIM: Failed to allocate component state\n");
        return COMPONENT_ERROR;
    }
    
    /* Initialize simulation state */
    if (eps_sim_init(eps_state) != 0)
    {
        printf("EPS SIM: Failed to initialize simulation state\n");
        free(eps_state);
        return COMPONENT_ERROR;
    }
    
    /* Initialize I2C device */
    memset(&eps_state->i2c_device, 0, sizeof(eps_state->i2c_device));
    /* Set up ZMQ address for this device */
    snprintf(eps_state->i2c_device.name, sizeof(eps_state->i2c_device.name), 
        "eps_sim_bus%d_addr0x%02X", EPS_CFG_I2C_BUS_ID, EPS_CFG_I2C_DEVICE_ADDR);
    snprintf(eps_state->i2c_device.address, sizeof(eps_state->i2c_device.address), 
        "ipc:///tmp/simulith_pub:%d", SIMULITH_I2C_BASE_PORT + EPS_CFG_I2C_BUS_ID * 100 + EPS_CFG_I2C_DEVICE_ADDR);
    eps_state->i2c_device.is_server = 1;  // Always server/bind for the simulator

    if (simulith_transport_init(&eps_state->i2c_device) != 0)
    {
        printf("EPS SIM: Failed to initialize I2C device\n");
        eps_sim_cleanup(eps_state);
        free(eps_state);
        return COMPONENT_ERROR;
    }
    
    *state = (component_state_t*)eps_state;
    printf("EPS SIM: Initialized successfully as %s\n", eps_state->i2c_device.name);
    return COMPONENT_SUCCESS;
}

/*
** Component cleanup for simulith framework
*/
static void eps_component_destroy(component_state_t* state)
{
    eps_sim_state_t* eps_state = (eps_sim_state_t*)state;
    if (eps_state)
    {
        simulith_transport_close(&eps_state->i2c_device);
        eps_sim_cleanup(eps_state);
        free(eps_state);
    }
    printf("EPS SIM: Component cleaned up\n");
}

/*
** Component interface definition
*/
static const component_interface_t eps_component_interface = {
    .api_version = SIMULITH_COMPONENT_API_VERSION,
    .struct_size = sizeof(component_interface_t),
    .name = "eps_sim",
    .description = "EPS component simulation with I2C interface",
    .create = eps_component_create,
    .on_tick = eps_component_on_tick,
    .wait_for_service = eps_component_wait_for_service,
    .service = eps_component_service,
    .actuate = eps_component_actuate,
    .destroy = eps_component_destroy,
    .backdoor = eps_component_backdoor,
    .power_supply = eps_component_power_supply
};

/*
** Component registration function required by simulith director
*/
const component_interface_t* get_component_interface(void)
{
    return &eps_component_interface;
}
