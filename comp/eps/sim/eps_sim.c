#include "eps_sim.h"

/* Forward prototypes to satisfy -Wmissing-prototypes for REGISTER_COMPONENT export */
const component_interface_t* get_eps_sim_component_interface(void);
const component_interface_t* get_component_interface(void);

static double calculate_power_consumption(eps_sim_state_t* state)
{
    double total_power_w = 0.0;
    
    for (int i = 0; i < EPS_NUM_SWITCHES; i++)
    {
        if (state->hk.switches[i].state == EPS_SWITCH_ON)
        {
            total_power_w += EPS_SWITCH_POWER_W(i);
        }
    }
    
    return total_power_w;
}

static double calculate_solar_generation(const simulith_42_context_t* context_42)
{
    if (!context_42 || !context_42->valid || context_42->eclipse)
    {
        return 0.0;
    }
    
    // Solar array on +X side, so power depends on X-component of sun vector
    double sun_x = context_42->sun_vector_body[0];
    
    // Only generate power if sun is on +X side (sun_x > 0)
    if (sun_x > 0.0)
    {
        // Power proportional to sun_x (cosine of angle)
        // Max power when sun_x = 1.0 (directly facing sun)
        return EPS_MAX_SOLAR_POWER_W * sun_x;
    }
    
    return 0.0;
}

static void handle_eps_command(eps_sim_state_t* state, const uint8_t* data, size_t length)
{
    if (length < EPS_COMMAND_SIZE)
    {
        printf("EPS SIM: Command too short (%zu bytes, expected %zu)\n", length, EPS_COMMAND_SIZE);
        return;
    }

    EPS_Command_t* cmd = (EPS_Command_t*)data;
    
    /* Verify I2C address */
    if (cmd->i2c_addr != EPS_CFG_I2C_DEVICE_ADDR)
    {
        printf("EPS SIM: Wrong I2C address 0x%02X (expected 0x%02X)\n", cmd->i2c_addr, EPS_CFG_I2C_DEVICE_ADDR);
        return;
    }

    /* Verify CRC */
    if (!EPS_Verify_CRC8(data, EPS_COMMAND_SIZE - 1, cmd->crc))
    {
        printf("EPS SIM: CRC check failed\n");
        return;
    }

    #ifdef EPS_CFG_DEBUG
    printf("EPS SIM: Received command 0x%02X with payload 0x%02X\n", cmd->command, cmd->payload);
    #endif

    switch (cmd->command)
    {
        case EPS_CMD_NOOP:
            #ifdef EPS_CFG_DEBUG
            printf("EPS SIM: NOOP command\n");
            #endif
            break;

        case EPS_CMD_GET_HK:
            #ifdef EPS_CFG_DEBUG
            printf("EPS SIM: Housekeeping request\n");
            #endif
            /* Calculate CRC for housekeeping data */
            state->hk.crc = EPS_Calculate_CRC8((const uint8_t*)&state->hk, sizeof(state->hk) - 1);
            /* Send housekeeping data back via I2C */
            if (simulith_transport_send(&state->i2c_device, (const uint8_t*)&state->hk, sizeof(state->hk)) < 0)
            {
                printf("EPS SIM: Failed to send housekeeping data\n");
            }
            #ifdef EPS_CFG_DEBUG
            else
            {
                printf("EPS SIM: Sent housekeeping data (%zu bytes)\n", sizeof(state->hk));
            }
            #endif
            break;

        case EPS_CMD_SWITCH_OFF:
            if (cmd->payload < EPS_NUM_SWITCHES)
            {
                state->hk.switches[cmd->payload].state = EPS_SWITCH_OFF;
                #ifdef EPS_CFG_DEBUG
                printf("EPS SIM: Switch %d turned OFF\n", cmd->payload);
                #endif
            }
            #ifdef EPS_CFG_DEBUG
            else
            {
                printf("EPS SIM: Invalid switch number %d\n", cmd->payload);
            }
            #endif
            break;

        case EPS_CMD_SWITCH_ON:
            if (cmd->payload < EPS_NUM_SWITCHES)
            {
                state->hk.switches[cmd->payload].state = EPS_SWITCH_ON;
                #ifdef EPS_CFG_DEBUG
                printf("EPS SIM: Switch %d turned ON\n", cmd->payload);
                #endif
            }
            #ifdef EPS_CFG_DEBUG
            else
            {
                printf("EPS SIM: Invalid switch number %d\n", cmd->payload);
            }
            #endif
            break;

        default:
            printf("EPS SIM: Unknown command 0x%02X\n", cmd->command);
            break;
    }

    /* Update device counter for any command */
    state->device_counter++;
}

/*
** Tick callback for simulation updates
*/
static void eps_component_tick(component_state_t* state, uint64_t tick_time_ns, const simulith_42_context_t* context_42)
{
    eps_sim_state_t* eps_state = (eps_sim_state_t*)state;
    if (!eps_state)
    {
        return;
    }

    static uint64_t last_hk_update = 0;
    const uint64_t hk_update_interval = 1000000000ULL; /* 1 second in nanoseconds */
    
    /* Update housekeeping data every second */
    if (tick_time_ns - last_hk_update >= hk_update_interval)
    {
        /* Calculate power consumption and generation */
        double power_consumption_w = calculate_power_consumption(eps_state);
        double solar_generation_w = calculate_solar_generation(context_42);
        double net_power_w = solar_generation_w - power_consumption_w;
        
        /* Update battery energy (convert watts to watt-hours over time interval) */
        double time_hours = (double)hk_update_interval / 3600000000000.0; /* nanoseconds to hours */
        eps_state->battery_energy_wh += net_power_w * time_hours;
        
        /* Clamp battery energy to valid range */
        if (eps_state->battery_energy_wh < 0.0)
            eps_state->battery_energy_wh = 0.0;
        if (eps_state->battery_energy_wh > EPS_BATTERY_CAPACITY_WH)
            eps_state->battery_energy_wh = EPS_BATTERY_CAPACITY_WH;
        
        /* Update battery voltage based on state of charge */
        double soc = eps_state->battery_energy_wh / EPS_BATTERY_CAPACITY_WH; /* State of charge 0-1 */
        /* Simple model: voltage decreases linearly from max to min as SOC goes from 1 to 0 */
        double battery_voltage_v = EPS_BATTERY_VOLTAGE_MIN + (soc * (EPS_BATTERY_VOLTAGE_MAX - EPS_BATTERY_VOLTAGE_MIN));
        eps_state->hk.battery_voltage = (uint8_t)(battery_voltage_v / (32.0 / 255.0));
        
        /* Update solar voltage based on generation */
        double solar_voltage_v = (solar_generation_w > 0.0) ? 4.5 : 0.0; /* Simplified */
        eps_state->hk.solar_voltage = (uint8_t)(solar_voltage_v / (32.0 / 255.0));
        
        /* Update battery/solar temperature with random slight variation (+1, 0, or -1) */
        int t_delta = (rand() % 3) - 1; // -1, 0, or +1
        eps_state->hk.battery_temperature = (uint8_t)(20 + t_delta);
        t_delta = (rand() % 3) - 1;
        eps_state->hk.solar_temperature = (uint8_t)(35 + t_delta);
        
        /* Update switch voltages and currents based on state */
        for (int i = 0; i < EPS_NUM_SWITCHES; i++)
        {
            if (eps_state->hk.switches[i].state == EPS_SWITCH_ON)
            {
                /* Set voltage according to switch index, convert to counts (32V/255 per count) */
                float voltage = 0.0f;
                if (i == 0 || i == 1)
                    voltage = 3.3f;
                else if (i == 2 || i == 3)
                    voltage = 5.0f;
                else if (i == 4 || i == 5)
                    voltage = 12.0f;
                else if (i == 6 || i == 7)
                    voltage = 24.0f;
                uint8_t voltage_count = (uint8_t)(voltage / (32.0f / 255.0f));
                eps_state->hk.switches[i].voltage = voltage_count;
                /* Current stays low as nothing is connected, convert to counts (10A/255 per count) */
                float current = 0.05f; /* 0.05A, example low value */
                uint8_t current_count = (uint8_t)(current / (10.0f / 255.0f));
                eps_state->hk.switches[i].current = current_count;
            }
            else
            {
                eps_state->hk.switches[i].voltage = 0;
                eps_state->hk.switches[i].current = 0;
            }
        }

        #ifdef EPS_CFG_DEBUG
        printf("EPS SIM: HK updated - Battery %.2f Wh (%.1f%%), %.1fV; Solar %.2fW, %.1fV; Consumption %.3fW\n",
               eps_state->battery_energy_wh, soc * 100.0,
               battery_voltage_v, solar_generation_w, solar_voltage_v, power_consumption_w);
        #endif

        last_hk_update = tick_time_ns;
    }
        
    int available = simulith_transport_available(&eps_state->i2c_device);
    if (available > 0)
    {
        uint8_t cmd_buffer[256];
        int bytes_read = simulith_transport_receive(&eps_state->i2c_device, cmd_buffer, sizeof(cmd_buffer));
        if (bytes_read > 0)
        {
            handle_eps_command(eps_state, cmd_buffer, (size_t)bytes_read);
        }
    }
}

/*
** Initialize EPS simulation
*/
int eps_sim_init(eps_sim_state_t* state)
{
    if (!state)
    {
        printf("EPS SIM: NULL state pointer\n");
        return -1;
    }

    /* Initialize housekeeping data */
    memset(&state->hk, 0, sizeof(state->hk));
    state->device_counter = 0;
    state->battery_energy_wh = EPS_BATTERY_CAPACITY_WH * EPS_BATTERY_INITIAL_SOC; /* Start at configured SOC */
    
    /* Set initial values based on configuration */
    double initial_voltage = EPS_BATTERY_VOLTAGE_MIN + (EPS_BATTERY_INITIAL_SOC * (EPS_BATTERY_VOLTAGE_MAX - EPS_BATTERY_VOLTAGE_MIN));
    state->hk.battery_voltage = (uint8_t)(initial_voltage / (32.0 / 255.0)); /* Convert to telemetry counts */
    state->hk.battery_temperature = 20;
    state->hk.solar_voltage = 180;
    state->hk.solar_temperature = 35;
    
    /* Initialize all switches to OFF */
    for (int i = 0; i < EPS_NUM_SWITCHES; i++)
    {
        state->hk.switches[i].state = EPS_SWITCH_OFF;
        state->hk.switches[i].voltage = 0;
        state->hk.switches[i].current = 0;
    }

    printf("EPS SIM: Initialized\n");
    return 0;
}

/*
** Cleanup EPS simulation
*/
void eps_sim_cleanup(eps_sim_state_t* state)
{
    if (state)
    {
        memset(state, 0, sizeof(*state));
    }
    printf("EPS SIM: Cleaned up\n");
}

/*
** Component initialization for simulith framework
*/
static int eps_component_init(component_state_t** state)
{    
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
static void eps_component_cleanup(component_state_t* state)
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
    .name = "eps_sim",
    .description = "EPS component simulation with I2C interface",
    .init = eps_component_init,
    .tick = eps_component_tick,
    .cleanup = eps_component_cleanup
};

/*
** Component registration function required by simulith director
*/
const component_interface_t* get_component_interface(void)
{
    return &eps_component_interface;
}
