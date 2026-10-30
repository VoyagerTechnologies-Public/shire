/* Compile the real model with scripted transport failures. No UDP/IPC or
 * flight retry is needed to distinguish a modeled rejection from an error. */
#include "unity.h"
#define EPS_BASE_LOAD_W 2.0
#include "../sim/eps_sim.c"

static int receive_result, send_result, completion_result, completion_status;
static EPS_Command_t request;
static EPS_Device_HK_tlm_t response;

/* The CRC implementation shares a translation unit with flight-side I2C
 * wrappers. Simulator tests must never enter those wrappers. */
int32_t i2c_master_init(i2c_bus_info_t *device)
{
    (void)device; TEST_FAIL_MESSAGE("flight I2C init called by simulator"); return I2C_ERROR;
}
int32_t i2c_read_transaction(i2c_bus_info_t *device, uint8_t address, void *data, uint8_t size, uint8_t timeout)
{
    (void)device; (void)address; (void)data; (void)size; (void)timeout;
    TEST_FAIL_MESSAGE("flight I2C read called by simulator"); return I2C_ERROR;
}
int32_t i2c_write_transaction(i2c_bus_info_t *device, uint8_t address, void *data, uint8_t size, uint8_t timeout)
{
    (void)device; (void)address; (void)data; (void)size; (void)timeout;
    TEST_FAIL_MESSAGE("flight I2C write called by simulator"); return I2C_ERROR;
}

int simulith_transport_init(transport_port_t *port)
{
    port->init = SIMULITH_TRANSPORT_INITIALIZED;
    return SIMULITH_TRANSPORT_SUCCESS;
}
int simulith_transport_close(transport_port_t *port)
{
    port->init = 0;
    return SIMULITH_TRANSPORT_SUCCESS;
}
int simulith_transport_receive_request(transport_port_t *port, uint8_t *data,
                                      size_t capacity, uint64_t *id)
{
    (void)port;
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(sizeof(request), capacity);
    memcpy(data, &request, sizeof(request));
    *id = 1;
    return receive_result;
}
int simulith_transport_send(transport_port_t *port, const uint8_t *data, size_t length)
{
    (void)port;
    TEST_ASSERT_EQUAL_size_t(sizeof(response), length);
    memcpy(&response, data, length);
    return send_result;
}
int simulith_transport_complete_request(transport_port_t *port, uint64_t id, int status)
{
    (void)port;
    TEST_ASSERT_EQUAL_UINT64(1, id);
    completion_status = status;
    return completion_result;
}
int simulith_transport_wait_for_request(transport_port_t *const ports[], size_t count, int fd)
{
    (void)ports; (void)fd;
    TEST_ASSERT_EQUAL_size_t(1, count);
    return SIMULITH_TRANSPORT_INTERRUPTED;
}
void setUp(void)
{
    receive_result = sizeof(request);
    send_result = sizeof(response);
    completion_result = SIMULITH_TRANSPORT_SUCCESS;
    completion_status = 123;
    request = (EPS_Command_t){EPS_CFG_I2C_DEVICE_ADDR, EPS_CMD_GET_HK, 0, 0};
    request.crc = EPS_Calculate_CRC8((uint8_t *)&request, sizeof(request)-1);
}
void tearDown(void) {}

static void test_transport_failures_with_crc_injection(void)
{
    eps_sim_state_t s;
    TEST_ASSERT_EQUAL_INT(0, eps_sim_init(&s));
    s.crc_remaining = 2;
    send_result = SIMULITH_TRANSPORT_ERROR;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, eps_component_service((component_state_t *)&s, 0, NULL));
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR, completion_status);
    TEST_ASSERT_EQUAL_UINT16(2, s.crc_remaining);
    TEST_ASSERT_EQUAL_UINT32(0, s.device_counter);
    send_result = sizeof(response);
    completion_result = SIMULITH_TRANSPORT_ERROR;
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, eps_component_service((component_state_t *)&s, 0, NULL));
    TEST_ASSERT_EQUAL_UINT16(1, s.crc_remaining); /* A response was emitted. */
    TEST_ASSERT_FALSE(EPS_Verify_CRC8((uint8_t *)&response, sizeof(response)-1, response.crc));
    completion_result = SIMULITH_TRANSPORT_SUCCESS;
    TEST_ASSERT_EQUAL_INT(COMPONENT_WORK, eps_component_service((component_state_t *)&s, 0, NULL));
    TEST_ASSERT_EQUAL_UINT16(0, s.crc_remaining);
}

static void test_switch_transition_queue_limit(void)
{
    eps_sim_state_t s;
    TEST_ASSERT_EQUAL_INT(0, eps_sim_init(&s));
    s.power.configured = 1;
    /* Real alternating requests fill the queue without overwriting an edge. */
    request.payload=7;
    for (unsigned int i = 0; i < SIMULITH_POWER_TRANSITIONS; i++) {
        request.command=i%2 ? EPS_CMD_SWITCH_OFF : EPS_CMD_SWITCH_ON;
        request.crc=EPS_Calculate_CRC8((uint8_t *)&request,sizeof(request)-1);
        TEST_ASSERT_EQUAL_INT(COMPONENT_WORK,eps_component_service((component_state_t *)&s,0,NULL));
    }
    TEST_ASSERT_EQUAL_size_t(SIMULITH_POWER_TRANSITIONS, s.power.transition_count);
    request.command = EPS_CMD_SWITCH_ON; request.payload = 7;
    request.crc = EPS_Calculate_CRC8((uint8_t *)&request, sizeof(request)-1);
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, eps_component_service((component_state_t *)&s, 0, NULL));
    TEST_ASSERT_TRUE(s.power.overflow);
    TEST_ASSERT_EQUAL_size_t(SIMULITH_POWER_TRANSITIONS, s.power.transition_count);
    TEST_ASSERT_FALSE(s.power.requested[7]);
    TEST_ASSERT_FALSE(s.power.effective[7]);
    TEST_ASSERT_EQUAL_UINT32(SIMULITH_POWER_TRANSITIONS, s.device_counter);
    TEST_ASSERT_EQUAL_UINT64(SIMULITH_POWER_TRANSITIONS, s.requests_successful);
    TEST_ASSERT_EQUAL_UINT64(1, s.requests_rejected);
    s.power.requested[0] = 1;
    s.power.effective[0] = 0;
    eps_component_backdoor((component_state_t *)&s, EPS_BD_CLEAR_FAULTS, NULL, 0);
    TEST_ASSERT_FALSE(s.backdoor_last_status);
    eps_component_backdoor((component_state_t *)&s, EPS_BD_RESET, NULL, 0);
    TEST_ASSERT_FALSE(s.backdoor_last_status);
}

static void test_private_api_bounds_and_supply_access(void)
{
    eps_sim_state_t s;
    TEST_ASSERT_EQUAL_INT(0, eps_sim_init(&s));
    TEST_ASSERT_NULL(eps_component_power_supply(NULL));
    TEST_ASSERT_EQUAL_PTR(&s.power, eps_component_power_supply((component_state_t *)&s));
    TEST_ASSERT_EQUAL_INT(EPS_COMMAND_ERROR, handle_eps_command(NULL, (uint8_t *)&request, sizeof(request)));
    TEST_ASSERT_EQUAL_INT(EPS_COMMAND_ERROR, handle_eps_command(&s, NULL, sizeof(request)));
    TEST_ASSERT_EQUAL_INT(COMPONENT_ERROR, eps_component_actuate(NULL, 0, NULL));
    eps_component_backdoor(NULL, EPS_BD_RESET, NULL, 0);
    uint8_t packet[EPS_CONSOLE_DOUBLES*8];
    TEST_ASSERT_EQUAL_size_t(0, eps_sim_console_state(NULL, 0, 0, packet, sizeof(packet)));
    TEST_ASSERT_EQUAL_size_t(0, eps_sim_console_state((component_state_t *)&s, 0, 0, NULL, sizeof(packet)));
    TEST_ASSERT_EQUAL_size_t(0, eps_sim_console_state((component_state_t *)&s, 0, 0, packet, sizeof(packet)-1));
}

static void test_solar_override_auto_integration_and_reset_deadline(void)
{
    eps_sim_state_t s;
    TEST_ASSERT_EQUAL_INT(0, eps_sim_init(&s));
    simulith_42_context_t truth={.valid=1}; truth.sun_vector_body[0]=0.5;
    TEST_ASSERT_EQUAL_INT(0, eps_component_on_tick((component_state_t *)&s,0,&truth));
    double load=s.load_power_w, start=s.battery_energy_wh;
    uint8_t solar[]={1,0,0,3,232}; /* 1 W in milliwatts */
    eps_component_backdoor((component_state_t *)&s,EPS_BD_SET_SOLAR,solar,5);
    TEST_ASSERT_TRUE(s.backdoor_last_status);
    truth.sun_vector_body[0]=1;
    TEST_ASSERT_EQUAL_INT(0, eps_component_on_tick((component_state_t *)&s,1000000000ULL,&truth));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9,1,s.solar_power_w);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9,start+(1-load)/3600,s.battery_energy_wh);
    solar[0]=0; solar[3]=solar[4]=0;
    eps_component_backdoor((component_state_t *)&s,EPS_BD_SET_SOLAR,solar,5);
    TEST_ASSERT_TRUE(s.backdoor_last_status);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9,EPS_MAX_SOLAR_POWER_W,s.solar_power_w);
    s.model_time_ns=UINT64_MAX-1;
    eps_component_backdoor((component_state_t *)&s,EPS_BD_RESET,NULL,0);
    TEST_ASSERT_TRUE(s.backdoor_last_status);
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX,s.next_hk_update_ns);
    setenv("SIMULITH_EPS_DEBUG","",1);
    TEST_ASSERT_EQUAL_INT(0,eps_sim_init(&s));
    unsetenv("SIMULITH_EPS_DEBUG");
    s.debug_enabled=1;
    TEST_ASSERT_EQUAL_INT(0,eps_component_actuate((component_state_t *)&s,0,NULL));
    truth.valid=0;
    TEST_ASSERT_EQUAL_INT(0,eps_component_actuate((component_state_t *)&s,0,&truth));
}

static void test_load_scale_bounds_and_shared_rail_limit(void)
{
    eps_sim_state_t s;
    TEST_ASSERT_EQUAL_INT(0,eps_sim_init(&s));
    memset(s.power.loads,0,sizeof(s.power.loads));
    uint8_t scale[]={0,0,15,66,64}; /* 1.0 */
    eps_component_backdoor((component_state_t *)&s,EPS_BD_SET_LOAD_SCALE,scale,5);
    TEST_ASSERT_FALSE(s.backdoor_last_status); /* No mapped consumer. */
    strcpy(s.power.loads[0].component,"demo_sim");
    s.power.loads[0].boot_w=20; s.power.loads[0].mode_w[0]=2; s.power.loads[0].scale=0.5;
    s.power.voltage[0]=1;
    scale[1]=0xff; scale[2]=scale[3]=scale[4]=0xff;
    eps_component_backdoor((component_state_t *)&s,EPS_BD_SET_LOAD_SCALE,scale,5);
    TEST_ASSERT_FALSE(s.backdoor_last_status); /* >1000. */
    scale[1]=0; scale[2]=15; scale[3]=66; scale[4]=64;
    eps_component_backdoor((component_state_t *)&s,EPS_BD_SET_LOAD_SCALE,scale,5);
    TEST_ASSERT_FALSE(s.backdoor_last_status); /* Boot draw exceeds 10 A. */
    TEST_ASSERT_DOUBLE_WITHIN(0,0.5,s.power.loads[0].scale);
    s.power.loads[0].boot_w=2;
    strcpy(s.power.loads[1].component,"adcs_sim");
    s.power.loads[1].scale=1; s.power.loads[1].boot_w=9;
    eps_component_backdoor((component_state_t *)&s,EPS_BD_SET_LOAD_SCALE,scale,5);
    TEST_ASSERT_FALSE(s.backdoor_last_status); /* Shared total, not just this load. */
    s.power.loads[1].switch_id=1;
    eps_component_backdoor((component_state_t *)&s,EPS_BD_SET_LOAD_SCALE,scale,5);
    TEST_ASSERT_TRUE(s.backdoor_last_status);
    s.power.effective[0]=1;
    refresh_hk(&s);
    TEST_ASSERT_DOUBLE_WITHIN(0,2 + EPS_BASE_LOAD_W,s.load_power_w);
    uint8_t solar[]={0,0xff,0xff,0xff,0xff};
    eps_component_backdoor((component_state_t *)&s,EPS_BD_SET_SOLAR,solar,5);
    TEST_ASSERT_FALSE(s.backdoor_last_status);
    uint8_t fault[]={8,0};
    eps_component_backdoor((component_state_t *)&s,EPS_BD_SET_SWITCH_FAULT,fault,2);
    TEST_ASSERT_FALSE(s.backdoor_last_status);
}

static void test_nonmatching_request_failure_does_not_consume_fault(void)
{
    eps_sim_state_t s;
    TEST_ASSERT_EQUAL_INT(0,eps_sim_init(&s));
    s.fail_selector=2; s.fail_remaining=1;
    TEST_ASSERT_EQUAL_INT(COMPONENT_WORK,eps_component_service((component_state_t *)&s,0,NULL));
    TEST_ASSERT_EQUAL_UINT16(1,s.fail_remaining);
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_SUCCESS,completion_status);
    request.command=EPS_CMD_SWITCH_ON;
    request.crc=EPS_Calculate_CRC8((uint8_t *)&request,sizeof(request)-1);
    TEST_ASSERT_EQUAL_INT(COMPONENT_WORK,eps_component_service((component_state_t *)&s,0,NULL));
    TEST_ASSERT_EQUAL_UINT16(0,s.fail_remaining);
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,completion_status);
    s.fail_selector=0; s.fail_remaining=1;
    request.command=EPS_CMD_NOOP;
    request.crc=EPS_Calculate_CRC8((uint8_t *)&request,sizeof(request)-1);
    TEST_ASSERT_EQUAL_INT(COMPONENT_WORK,eps_component_service((component_state_t *)&s,0,NULL));
    TEST_ASSERT_EQUAL_UINT16(0,s.fail_remaining);
    TEST_ASSERT_EQUAL_INT(SIMULITH_TRANSPORT_ERROR,completion_status);
}

static void test_eclipse_base_load_with_all_switches_off(void)
{
    eps_sim_state_t s;
    TEST_ASSERT_EQUAL_INT(0, eps_sim_init(&s));
    memset(s.power.effective, 0, sizeof(s.power.effective));
    refresh_hk(&s);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 2.0, s.load_power_w);
    for (unsigned int i = 0; i < EPS_NUM_SWITCHES; i++)
        TEST_ASSERT_EQUAL_UINT8(0, s.hk.switches[i].current);
    simulith_42_context_t ctx = {0};
    ctx.valid = 1; ctx.eclipse = 1; ctx.sun_vector_body[0] = 1;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, eps_component_on_tick((component_state_t *)&s, 0, &ctx));
    /* Start midway through a reporting bin rather than on its lower edge. */
    s.battery_energy_wh += EPS_BATTERY_CAPACITY_WH / (2 * 255.0);
    refresh_hk(&s);
    double initial = s.battery_energy_wh;
    uint8_t voltage = s.hk.battery_voltage;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, eps_component_on_tick((component_state_t *)&s, UINT64_C(1000000000), &ctx));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, initial - 2.0 / 3600, s.battery_energy_wh);
    TEST_ASSERT_EQUAL_UINT8(voltage, s.hk.battery_voltage); /* Drain below one telemetry count. */
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, eps_component_on_tick((component_state_t *)&s, UINT64_C(3600000000000), &ctx));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, initial - 2.0, s.battery_energy_wh);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 2.0, s.load_energy_wh);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, s.solar_energy_wh);
    TEST_ASSERT_LESS_THAN_UINT8(voltage, s.hk.battery_voltage);
}

static void test_passive_load_bank_switching_and_eclipse_energy(void)
{
    eps_sim_state_t s;
    TEST_ASSERT_EQUAL_INT(0, eps_sim_init(&s));
    memset(s.power.effective, 0, sizeof(s.power.effective));
    refresh_hk(&s);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 2.0, s.load_power_w);
    request.command = EPS_CMD_SWITCH_ON; request.payload = 5;
    request.crc = EPS_Calculate_CRC8((uint8_t *)&request, sizeof(request)-1);
    TEST_ASSERT_EQUAL_INT(EPS_COMMAND_SUCCESS, handle_eps_command(&s, (uint8_t *)&request, sizeof(request)));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 22.0, s.load_power_w);
    TEST_ASSERT_EQUAL_UINT8(encode(20.0 / 12.0, 10.0), s.hk.switches[5].current);
    simulith_42_context_t ctx = {0}; ctx.valid = 1; ctx.eclipse = 1;
    eps_component_on_tick((component_state_t *)&s, 0, &ctx);
    double initial = s.battery_energy_wh;
    eps_component_on_tick((component_state_t *)&s, UINT64_C(3600000000000), &ctx);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, initial - 22.0, s.battery_energy_wh);
    request.command = EPS_CMD_SWITCH_OFF;
    request.crc = EPS_Calculate_CRC8((uint8_t *)&request, sizeof(request)-1);
    TEST_ASSERT_EQUAL_INT(EPS_COMMAND_SUCCESS, handle_eps_command(&s, (uint8_t *)&request, sizeof(request)));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 2.0, s.load_power_w);
    TEST_ASSERT_EQUAL_UINT8(0, s.hk.switches[5].current);
}

static void test_sunlit_charge_and_load_bank_discharge(void)
{
    eps_sim_state_t s;
    TEST_ASSERT_EQUAL_INT(0, eps_sim_init(&s));
    simulith_42_context_t ctx = {0}; ctx.valid = 1; ctx.sun_vector_body[0] = 1;
    eps_component_on_tick((component_state_t *)&s, 0, &ctx);
    double initial = s.battery_energy_wh;
    double watts = s.load_power_w;
    TEST_ASSERT_GREATER_THAN_DOUBLE(watts, EPS_MAX_SOLAR_POWER_W);
    eps_component_on_tick((component_state_t *)&s, UINT64_C(360000000000), &ctx);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, initial + (EPS_MAX_SOLAR_POWER_W - watts) / 10, s.battery_energy_wh);
    queue_switch(&s, 5, 1);
    initial = s.battery_energy_wh;
    TEST_ASSERT_GREATER_THAN_DOUBLE(EPS_MAX_SOLAR_POWER_W, s.load_power_w);
    eps_component_on_tick((component_state_t *)&s, UINT64_C(720000000000), &ctx);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, initial - (watts + 20 - EPS_MAX_SOLAR_POWER_W) / 10, s.battery_energy_wh);
}

static void test_battery_reporting_full_byte_span_and_wire_layout(void)
{
    eps_sim_state_t s;
    TEST_ASSERT_EQUAL_INT(0, eps_sim_init(&s));
    TEST_ASSERT_EQUAL_size_t(29, sizeof(EPS_Device_HK_tlm_t));
    TEST_ASSERT_EQUAL_size_t(4, sizeof(EPS_Command_t));
    for (unsigned int count = 0; count <= 255; count++) {
        s.battery_energy_wh = EPS_BATTERY_CAPACITY_WH * count / 255.0;
        refresh_hk(&s);
        TEST_ASSERT_EQUAL_UINT8(count, s.hk.battery_voltage);
    }
    s.battery_energy_wh = EPS_BATTERY_CAPACITY_WH * 0.5;
    refresh_hk(&s);
    TEST_ASSERT_EQUAL_UINT8(127, s.hk.battery_voltage);
    s.battery_energy_wh += EPS_BATTERY_CAPACITY_WH / 255;
    refresh_hk(&s);
    TEST_ASSERT_EQUAL_UINT8(128, s.hk.battery_voltage);
    s.battery_energy_wh = -1; refresh_hk(&s);
    TEST_ASSERT_EQUAL_UINT8(0, s.hk.battery_voltage);
    s.battery_energy_wh = EPS_BATTERY_CAPACITY_WH + 1; refresh_hk(&s);
    TEST_ASSERT_EQUAL_UINT8(255, s.hk.battery_voltage);
}

static void test_solar_cosine_losses_and_eclipse(void)
{
    eps_sim_state_t s;
    TEST_ASSERT_EQUAL_INT(0, eps_sim_init(&s));
    simulith_42_context_t ctx = {0}; ctx.valid = 1;
    const double directions[][3] = {
        {1, 0, 0}, {0.8660254037844386, 0.5, 0},
        {0.5, 0.8660254037844386, 0}, {0, 1, 0}, {-1, 0, 0}
    };
    const double fractions[] = {1, 0.8660254037844386, 0.5, 0, 0};
    for (unsigned int i = 0; i < 5; i++) {
        memcpy(ctx.sun_vector_body, directions[i], sizeof(ctx.sun_vector_body));
        TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, eps_component_on_tick((component_state_t *)&s, i * UINT64_C(1000000000), &ctx));
        TEST_ASSERT_DOUBLE_WITHIN(1e-9, EPS_MAX_SOLAR_POWER_W * fractions[i], s.solar_power_w);
    }
    ctx.eclipse = 1; ctx.sun_vector_body[0] = 1;
    TEST_ASSERT_EQUAL_INT(COMPONENT_SUCCESS, eps_component_on_tick((component_state_t *)&s, UINT64_C(5000000000), &ctx));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0, s.solar_power_w);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_solar_cosine_losses_and_eclipse);
    RUN_TEST(test_battery_reporting_full_byte_span_and_wire_layout);
    RUN_TEST(test_eclipse_base_load_with_all_switches_off);
    RUN_TEST(test_passive_load_bank_switching_and_eclipse_energy);
    RUN_TEST(test_sunlit_charge_and_load_bank_discharge);
    RUN_TEST(test_transport_failures_with_crc_injection);
    RUN_TEST(test_switch_transition_queue_limit);
    RUN_TEST(test_private_api_bounds_and_supply_access);
    RUN_TEST(test_solar_override_auto_integration_and_reset_deadline);
    RUN_TEST(test_load_scale_bounds_and_shared_rail_limit);
    RUN_TEST(test_nonmatching_request_failure_does_not_consume_fault);
    return UNITY_END();
}
