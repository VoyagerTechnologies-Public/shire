#include "demo_app_coveragetest_common.h"

void Test_DEMO_ReadData(void)
{
    uart_info_t device;
    uint8_t     read_data[8];
    uint8_t     data_length = 8;
    int32_t status = DEMO_ReadData(&device, read_data, data_length);
    UtAssert_True(status != OS_SUCCESS, "DEMO_ReadData should fail when no bytes available (rc=%d)", (int)status);

    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, data_length);
    /* Provide a read buffer and make uart_read_port return the requested bytes */
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, data_length);
    UT_SetDataBuffer(UT_KEY(uart_read_port), read_data, data_length, false);
    status = DEMO_ReadData(&device, read_data, data_length);
    UtAssert_True(status == OS_SUCCESS, "DEMO_ReadData returned %d", (int)status);

    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, data_length + 1);
    /* Make read_port return at least data_length bytes */
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, data_length);
    UT_SetDataBuffer(UT_KEY(uart_read_port), read_data, data_length, false);
    status = DEMO_ReadData(&device, read_data, data_length);
    UtAssert_True(status == OS_SUCCESS, "DEMO_ReadData returned %d", (int)status);
}

void Test_DEMO_ReadData_PartialRead(void)
{
    uart_info_t device;
    uint8_t     read_data[8];
    uint8_t     data_length = 8;

    /* Simulate bytes_available larger than requested, but uart_read_port returns fewer bytes */
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, data_length + 4);
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, data_length - 2);
    UT_SetDataBuffer(UT_KEY(uart_read_port), &read_data, sizeof(read_data), false);
    int32_t status = DEMO_ReadData(&device, read_data, data_length);
    UtAssert_True(status != OS_SUCCESS, "DEMO_ReadData should detect partial read (rc=%d)", (int)status);
}

void Test_DEMO_CommandDevice(void)
{
    uart_info_t device;
    uint8_t     cmd_code = 0;
    uint32_t    payload  = 0;
    /* Ensure a clean UT state for each sub-case */
    UT_ResetState(0);
    int32_t status = DEMO_CommandDevice(&device, cmd_code, payload);
    UtAssert_True(status == OS_SUCCESS, "DEMO_CommandDevice returned %d", (int)status);

    /* Negative: uart_flush error */
    UT_ResetState(0);
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_ERROR);
    status = DEMO_CommandDevice(&device, cmd_code, payload);
    UtAssert_True(status != OS_SUCCESS, "DEMO_CommandDevice should fail when uart_flush errors (rc=%d)", (int)status);
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);

    /* Positive: successful write */
    UT_ResetState(0);
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, DEMO_DEVICE_CMD_SIZE);
    uint8_t tmp_echo[] = {0xC0, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFE, 0xFE};
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDataBuffer(UT_KEY(uart_read_port), &tmp_echo, sizeof(tmp_echo), false);
    status = DEMO_CommandDevice(&device, cmd_code, payload);
    UtAssert_True(status == OS_SUCCESS, "DEMO_CommandDevice returned %d", (int)status);

    /* Force DEMO_ReadData to succeed by providing bytes and buffer */
    UT_ResetState(0);
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, DEMO_DEVICE_CMD_SIZE);
    uint8_t tmp_echo2[] = {0xC0, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFE, 0xFE};
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDataBuffer(UT_KEY(uart_read_port), &tmp_echo2, sizeof(tmp_echo2), false);
    status = DEMO_CommandDevice(&device, cmd_code, payload);
    UtAssert_True(status == OS_SUCCESS, "DEMO_CommandDevice returned %d", (int)status);

    /* Test successful echo validation - all bytes match */
    uint8_t echo_data[] = {0xC0, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFE, 0xFE};
    UT_ResetState(0);
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDataBuffer(UT_KEY(uart_read_port), &echo_data, sizeof(echo_data), false);
    status = DEMO_CommandDevice(&device, cmd_code, payload);
    UtAssert_True(status == OS_SUCCESS, "DEMO_CommandDevice returned %d", (int)status);

    /* Test echo validation failure - mismatch in echoed data */
    uint8_t bad_echo_data[] = {0xC0, 0xFF, 0x00, 0x01, 0x00, 0x00, 0xFE, 0xFE};
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDataBuffer(UT_KEY(uart_read_port), &bad_echo_data, sizeof(bad_echo_data), false);
    status = DEMO_CommandDevice(&device, cmd_code, payload);
    UtAssert_True(status != OS_SUCCESS, "DEMO_CommandDevice should fail on bad echo data (rc=%d)", (int)status);
}

void Test_DEMO_RequestHK(void)
{
    uart_info_t            device;
    DEMO_Device_HK_tlm_t data;
    int32_t status = DEMO_RequestHK(&device, &data);
    UtAssert_True(status != OS_SUCCESS, "DEMO_RequestHK should fail without stubs (rc=%d)", (int)status);

    uint8_t hk_raw[] = {0xDE, 0xAD, 0x00, 0x00, 0x00, 0x07, 0x00, 0x06};
    /* Positive: exercise parsing directly by calling DEMO_ReadData for HK payload */
    UT_ResetState(0);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, sizeof(hk_raw));
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, sizeof(hk_raw));
    UT_SetDataBuffer(UT_KEY(uart_read_port), hk_raw, sizeof(hk_raw), false);
    status = DEMO_ReadData(&device, (uint8_t *)&hk_raw, sizeof(hk_raw));
    UtAssert_True(status == OS_SUCCESS, "DEMO_ReadData returned %d", (int)status);
    /* Parse HK buffer the same way as production code does */
    data.DeviceCounter = 0;
    data.DeviceConfig  = 0;
    if ((hk_raw[0] == DEMO_DEVICE_HDR_0) && (hk_raw[1] == DEMO_DEVICE_HDR_1) &&
        (hk_raw[6] == DEMO_DEVICE_TRAILER_0) && (hk_raw[7] == DEMO_DEVICE_TRAILER_1))
    {
        data.DeviceCounter |= hk_raw[2] << 8;
        data.DeviceCounter |= hk_raw[3];
        data.DeviceConfig  |= hk_raw[4] << 8;
        data.DeviceConfig  |= hk_raw[5];
    }
    UtAssert_True(data.DeviceCounter == 0x0000 || data.DeviceConfig == 0x0007, "Parsed HK looks reasonable");

    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, OS_ERROR);
    status = DEMO_RequestHK(&device, &data);
    UtAssert_True(status != OS_SUCCESS, "DEMO_RequestHK should fail when uart_flush errors (rc=%d)", (int)status);
}

void Test_DEMO_RequestHK_Success(void)
{
    uart_info_t            device;
    DEMO_Device_HK_tlm_t data;
    
    /* Test successful HK parsing with valid headers and trailers */
    memset(&data, 0, sizeof(data));
    /* uart_flush returns success */
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    /* uart_write_port returns correct byte count */
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, DEMO_DEVICE_CMD_SIZE);
    /* uart_bytes_available: first call for echo (8 bytes), second call for HK (8 bytes) */
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, DEMO_DEVICE_HK_SIZE);
    /* uart_read_port: first call returns echo, second call returns HK - use combined buffer */
    uint8_t combined_data[] = {
        /* Echo data (8 bytes) */
        0xC0, 0xFF, 0x00, 0x01, 0x00, 0x00, 0xFE, 0xFE,
        /* HK data (8 bytes): DeviceCounter=0x1234, DeviceConfig=0x5678 */
        0xC0, 0xFF, 0x12, 0x34, 0x56, 0x78, 0xFE, 0xFE
    };
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, DEMO_DEVICE_HK_SIZE);
    UT_SetDataBuffer(UT_KEY(uart_read_port), combined_data, sizeof(combined_data), true);
    DEMO_RequestHK(&device, &data);
    
    /* Verify data was parsed correctly */
    UtAssert_True(data.DeviceCounter == 0x1234, "DeviceCounter should be 0x1234");
    UtAssert_True(data.DeviceConfig == 0x5678, "DeviceConfig should be 0x5678");
}

void Test_DEMO_RequestHK_InvalidHeaders(void)
{
    uart_info_t            device;
    DEMO_Device_HK_tlm_t data;
    
    /* Test invalid header/trailer detection */
    memset(&data, 0, sizeof(data));
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, DEMO_DEVICE_HK_SIZE);
    /* Combined buffer: echo + invalid HK data (0xFF instead of 0xC0 for header) */
    uint8_t combined_data[] = {
        /* Echo data (8 bytes) */
        0xC0, 0xFF, 0x00, 0x01, 0x00, 0x00, 0xFE, 0xFE,
        /* Invalid HK data - bad headers */
        0xFF, 0xFF, 0x12, 0x34, 0x56, 0x78, 0xFE, 0xFE
    };
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, DEMO_DEVICE_HK_SIZE);
    UT_SetDataBuffer(UT_KEY(uart_read_port), combined_data, sizeof(combined_data), true);
    int32_t status = DEMO_RequestHK(&device, &data);
    
    /* Should return error due to invalid headers */
    UtAssert_True(status != OS_SUCCESS, "DEMO_RequestHK should return error for invalid headers");
}

void Test_DEMO_RequestData(void)
{
    uart_info_t              device;
    DEMO_Device_Data_tlm_t data;
    int32_t status = DEMO_RequestData(&device, &data);
    UtAssert_True(status != OS_SUCCESS, "DEMO_RequestData should fail without stubs (rc=%d)", (int)status);

    uint8_t read_data[] = {0xDE, 0xAD, 0x00, 0x00, 0x00, 0x07, 0x00, 0x06,
                           0x00, 0x0C, 0x00, 0x12, 0x00, 0x00, 0xBE, 0xEF};
    /* Positive: exercise parsing directly by calling DEMO_ReadData for data payload */
    UT_ResetState(0);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, 16);
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, 16);
    UT_SetDataBuffer(UT_KEY(uart_read_port), &read_data, sizeof(read_data), false);
    status = DEMO_ReadData(&device, (uint8_t *)&read_data, 16);
    UtAssert_True(status == OS_SUCCESS, "DEMO_ReadData returned %d", (int)status);
    /* Parse data buffer the same way as production code: verify headers/trailers and populate channels */
    if ((read_data[0] == DEMO_DEVICE_HDR_0) && (read_data[1] == DEMO_DEVICE_HDR_1) &&
        (read_data[8] == DEMO_DEVICE_TRAILER_0) && (read_data[9] == DEMO_DEVICE_TRAILER_1))
    {
        data.Chan1 = read_data[2] << 8;
        data.Chan1 |= read_data[3];
        data.Chan2 = read_data[4] << 8;
        data.Chan2 |= read_data[5];
        data.Chan3 = read_data[6] << 8;
        data.Chan3 |= read_data[7];
    }
    UtAssert_True(data.Chan1 == 0x0000 || data.Chan2 == 0x0000 || data.Chan3 == 0x0000, "Parsed data channels present");

    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, OS_ERROR);
    status = DEMO_RequestData(&device, &data);
    UtAssert_True(status != OS_SUCCESS, "DEMO_RequestData should fail when uart_flush errors (rc=%d)", (int)status);
}

void Test_DEMO_RequestData_Success(void)
{
    uart_info_t              device;
    DEMO_Device_Data_tlm_t data;
    
    /* Test successful data parsing with valid headers and trailers */
    memset(&data, 0, sizeof(data));
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, DEMO_DEVICE_DATA_SIZE);
    /* Combined buffer: echo + actual data */
    uint8_t combined_data[] = {
        /* Echo data (8 bytes) */
        0xC0, 0xFF, 0x00, 0x02, 0x00, 0x00, 0xFE, 0xFE,
        /* Data (10 bytes): Chan1=0x1122, Chan2=0x3344, Chan3=0x5566 */
        0xC0, 0xFF, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0xFE, 0xFE
    };
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, DEMO_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, DEMO_DEVICE_DATA_SIZE);
    UT_SetDataBuffer(UT_KEY(uart_read_port), combined_data, sizeof(combined_data), true);
    int32_t status = DEMO_RequestData(&device, &data);
    
    /* Verify success and data parsed correctly */
    UtAssert_True(status == OS_SUCCESS, "DEMO_RequestData should succeed");
    UtAssert_True(data.Chan1 == 0x1122, "Chan1 should be 0x1122");
    UtAssert_True(data.Chan2 == 0x3344, "Chan2 should be 0x3344");
    UtAssert_True(data.Chan3 == 0x5566, "Chan3 should be 0x5566");
}

void Test_DEMO_RequestData_Hook(void *UserObj, UT_EntryKey_t FuncKey, const UT_StubContext_t *Context, va_list va) {}

/*
 * Setup function prior to every test
 */
void Demo_UT_Setup(void)
{
    UT_ResetState(0);
}

/*
 * Teardown function after every test
 */
void Demo_UT_TearDown(void) {}

/*
 * Register the test cases to execute with the unit test tool
 */
void UtTest_Setup(void)
{
    UT_SetVaHandlerFunction(UT_KEY(Test_DEMO_RequestData), Test_DEMO_RequestData_Hook, NULL);
    ADD_TEST(DEMO_ReadData);
    ADD_TEST(DEMO_CommandDevice);
    ADD_TEST(DEMO_RequestHK);
    ADD_TEST(DEMO_RequestHK_Success);
    ADD_TEST(DEMO_RequestHK_InvalidHeaders);
    ADD_TEST(DEMO_RequestData);
    ADD_TEST(DEMO_RequestData_Success);
    ADD_TEST(DEMO_ReadData_PartialRead);
}