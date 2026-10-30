/* Exercise the CLI boundary without contacting a transport endpoint. */
#define main eps_cli_application_main
#include "../cli/eps_cli.c"
#undef main
#include "unity.h"

static unsigned int switch_calls;
static uint8_t last_switch;
static bool last_state;

int32_t EPS_InitDevice(i2c_bus_info_t *device) { (void)device; return OS_SUCCESS; }
int32_t EPS_CommandDevice(i2c_bus_info_t *device, uint8_t cmd, uint8_t payload)
{ (void)device; (void)cmd; (void)payload; return OS_SUCCESS; }
int32_t EPS_RequestHK(i2c_bus_info_t *device, EPS_Device_HK_tlm_t *data)
{ (void)device; (void)data; return OS_SUCCESS; }
int32_t EPS_SetSwitch(i2c_bus_info_t *device, uint8_t number, bool state)
{ (void)device; switch_calls++; last_switch=number; last_state=state; return OS_SUCCESS; }
int32_t i2c_master_close(i2c_bus_info_t *device) { (void)device; return OS_SUCCESS; }

void setUp(void) { switch_calls=0; }
void tearDown(void) {}

static void test_invalid_integer_never_reaches_device(void)
{
    const char *bad[]={"", "-1", "8", "256", "258", "1x", "1.0", "99999999999999999999999999999999999999999999"};
    char tokens[MAX_INPUT_TOKENS][MAX_INPUT_TOKEN_SIZE]={{0}};
    for (size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        strcpy(tokens[0],bad[i]);
        EPS_process_command(CMD_SWITCH_ON,1,tokens);
        EPS_process_command(CMD_SWITCH_OFF,1,tokens);
    }
    TEST_ASSERT_EQUAL_UINT(0,switch_calls);
}
static void test_unterminated_argument_never_reaches_device(void)
{
    char tokens[MAX_INPUT_TOKENS][MAX_INPUT_TOKEN_SIZE]={{0}};
    memset(tokens[0],'1',sizeof(tokens[0]));
    EPS_process_command(CMD_SWITCH_ON,1,tokens);
    TEST_ASSERT_EQUAL_UINT(0,switch_calls);
}
static void test_every_valid_switch_and_state(void)
{
    char tokens[MAX_INPUT_TOKENS][MAX_INPUT_TOKEN_SIZE]={{0}};
    for (uint8_t i=0;i<8;i++) {
        snprintf(tokens[0],sizeof(tokens[0]),"%u",i);
        EPS_process_command(CMD_SWITCH_ON,1,tokens);
        TEST_ASSERT_EQUAL_UINT8(i,last_switch); TEST_ASSERT_TRUE(last_state);
        EPS_process_command(CMD_SWITCH_OFF,1,tokens);
        TEST_ASSERT_EQUAL_UINT8(i,last_switch); TEST_ASSERT_FALSE(last_state);
    }
    TEST_ASSERT_EQUAL_UINT(16,switch_calls);
}
static void test_wrong_argument_counts_and_long_command(void)
{
    char tokens[MAX_INPUT_TOKENS][MAX_INPUT_TOKEN_SIZE]={{"1"}};
    EPS_process_command(CMD_SWITCH_ON,0,tokens);
    EPS_process_command(CMD_SWITCH_ON,2,tokens);
    char command[100];memset(command,'a',sizeof(command));command[99]='\0';
    TEST_ASSERT_EQUAL_INT(CMD_UNKNOWN,EPS_get_command(command));
    TEST_ASSERT_EQUAL_UINT(0,switch_calls);
}
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_invalid_integer_never_reaches_device);
    RUN_TEST(test_unterminated_argument_never_reaches_device);
    RUN_TEST(test_every_valid_switch_and_state);
    RUN_TEST(test_wrong_argument_counts_and_long_command);
    return UNITY_END();
}
