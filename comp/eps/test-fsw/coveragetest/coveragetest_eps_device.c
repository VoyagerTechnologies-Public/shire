#include "eps_app_coveragetest_common.h"

void Test_EPS_Calculate_CRC8(void)
{
    uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t crc = EPS_Calculate_CRC8(test_data, sizeof(test_data));
    UtAssert_True(crc != 0, "CRC should be calculated");
}

void Test_EPS_Verify_CRC8(void)
{
    /* Test with valid CRC */
    uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t crc = EPS_Calculate_CRC8(test_data, sizeof(test_data));
    bool result = EPS_Verify_CRC8(test_data, sizeof(test_data), crc);
    UtAssert_True(result == true, "CRC verification should pass with correct CRC");
    
    /* Test with invalid CRC */
    result = EPS_Verify_CRC8(test_data, sizeof(test_data), 0xFF);
    UtAssert_True(result == false, "CRC verification should fail with incorrect CRC");
}

void Test_EPS_InitDevice(void)
{
    i2c_bus_info_t device;
    int32_t status;
    
    /* Test NULL device */
    status = EPS_InitDevice(NULL);
    UtAssert_True(status == I2C_ERROR, "InitDevice should return error for NULL device");
    
    /* Test successful init */
    UT_SetDeferredRetcode(UT_KEY(i2c_master_init), 1, I2C_SUCCESS);
    status = EPS_InitDevice(&device);
    UtAssert_True(status == I2C_SUCCESS, "InitDevice should succeed");
    UtAssert_True(device.handle == EPS_CFG_I2C_BUS_ID, "Device handle should be set");
    UtAssert_True(device.addr == EPS_CFG_I2C_DEVICE_ADDR, "Device address should be set");
}

void Test_EPS_CommandDevice(void)
{
    i2c_bus_info_t device;
    uint8_t cmd_code = EPS_CMD_NOOP;
    uint8_t payload = 0;
    int32_t status;

    /* Test NULL device */
    status = EPS_CommandDevice(NULL, cmd_code, payload);
    UtAssert_True(status == I2C_ERROR, "CommandDevice should return error for NULL device");

    /* Test device not open */
    device.isOpen = I2C_CLOSED;
    status = EPS_CommandDevice(&device, cmd_code, payload);
    UtAssert_True(status == I2C_ERROR, "CommandDevice should return error for closed device");

    /* Test successful command */
    device.isOpen = I2C_OPEN;
    UT_SetDeferredRetcode(UT_KEY(i2c_write_transaction), 1, I2C_SUCCESS);
    status = EPS_CommandDevice(&device, cmd_code, payload);
    UtAssert_True(status == I2C_SUCCESS, "CommandDevice should succeed");
    
    /* Test write failure */
    UT_SetDeferredRetcode(UT_KEY(i2c_write_transaction), 1, I2C_ERROR);
    status = EPS_CommandDevice(&device, cmd_code, payload);
    UtAssert_True(status == I2C_ERROR, "CommandDevice should return error on write failure");
}

void Test_EPS_RequestHK(void)
{
    i2c_bus_info_t device;
    EPS_Device_HK_tlm_t data;
    int32_t status;

    /* Test NULL device */
    status = EPS_RequestHK(NULL, &data);
    UtAssert_True(status == I2C_ERROR, "RequestHK should return error for NULL device");
    
    /* Test NULL data */
    status = EPS_RequestHK(&device, NULL);
    UtAssert_True(status == I2C_ERROR, "RequestHK should return error for NULL data");

    /* Test device not open */
    device.isOpen = I2C_CLOSED;
    status = EPS_RequestHK(&device, &data);
    UtAssert_True(status == I2C_ERROR, "RequestHK should return error for closed device");

    /* Test command failure */
    device.isOpen = I2C_OPEN;
    UT_SetDeferredRetcode(UT_KEY(i2c_write_transaction), 1, I2C_ERROR);
    status = EPS_RequestHK(&device, &data);
    UtAssert_True(status == I2C_ERROR, "RequestHK should return error on command failure");
    
    /* Test read failure */
    device.isOpen = I2C_OPEN;
    UT_SetDeferredRetcode(UT_KEY(i2c_write_transaction), 1, I2C_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(i2c_read_transaction), 1, I2C_ERROR);
    status = EPS_RequestHK(&device, &data);
    UtAssert_True(status == I2C_ERROR, "RequestHK should return error on read failure");
    
    /* Test CRC failure */
    device.isOpen = I2C_OPEN;
    memset(&data, 0, sizeof(data));
    data.crc = 0xFF; /* Invalid CRC */
    UT_SetDeferredRetcode(UT_KEY(i2c_write_transaction), 1, I2C_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(i2c_read_transaction), 1, I2C_SUCCESS);
    status = EPS_RequestHK(&device, &data);
    UtAssert_True(status == I2C_ERROR, "RequestHK should return error on CRC failure");
    
    /* Test successful HK request with valid CRC */
    device.isOpen = I2C_OPEN;
    memset(&data, 0, sizeof(data));
    data.battery_voltage = 200;
    data.battery_temperature = 50;
    data.solar_voltage = 180;
    data.solar_temperature = 45;
    /* Calculate correct CRC for this data */
    data.crc = EPS_Calculate_CRC8((const uint8_t*)&data, sizeof(data) - 1);
    UT_SetDeferredRetcode(UT_KEY(i2c_write_transaction), 1, I2C_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(i2c_read_transaction), 1, I2C_SUCCESS);
    status = EPS_RequestHK(&device, &data);
    UtAssert_True(status == I2C_SUCCESS, "RequestHK should succeed with valid CRC");
}

void Test_EPS_SetSwitch(void)
{
    i2c_bus_info_t device;
    int32_t status;
    
    /* Test NULL device */
    status = EPS_SetSwitch(NULL, 0, true);
    UtAssert_True(status == I2C_ERROR, "SetSwitch should return error for NULL device");
    
    /* Test device not open */
    device.isOpen = I2C_CLOSED;
    status = EPS_SetSwitch(&device, 0, true);
    UtAssert_True(status == I2C_ERROR, "SetSwitch should return error for closed device");
    
    /* Test invalid switch number */
    device.isOpen = I2C_OPEN;
    status = EPS_SetSwitch(&device, EPS_NUM_SWITCHES, true);
    UtAssert_True(status == I2C_ERROR, "SetSwitch should return error for invalid switch number");
    
    /* Test switch ON success */
    device.isOpen = I2C_OPEN;
    UT_SetDeferredRetcode(UT_KEY(i2c_write_transaction), 1, I2C_SUCCESS);
    status = EPS_SetSwitch(&device, 0, true);
    UtAssert_True(status == I2C_SUCCESS, "SetSwitch should succeed for ON command");
    
    /* Test switch OFF success */
    device.isOpen = I2C_OPEN;
    UT_SetDeferredRetcode(UT_KEY(i2c_write_transaction), 1, I2C_SUCCESS);
    status = EPS_SetSwitch(&device, 3, false);
    UtAssert_True(status == I2C_SUCCESS, "SetSwitch should succeed for OFF command");
}

/*
 * Setup function prior to every test
 */
void Eps_UT_Setup(void)
{
    UT_ResetState(0);
}

/*
 * Teardown function after every test
 */
void Eps_UT_TearDown(void) {}

/*
 * Register the test cases to execute with the unit test tool
 */
void UtTest_Setup(void)
{
    ADD_TEST(EPS_Calculate_CRC8);
    ADD_TEST(EPS_Verify_CRC8);
    ADD_TEST(EPS_InitDevice);
    ADD_TEST(EPS_CommandDevice);
    ADD_TEST(EPS_RequestHK);
    ADD_TEST(EPS_SetSwitch);
}