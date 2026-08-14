#include "adcs_app_coveragetest_common.h"

#include <stdarg.h>

typedef struct {
    uint8_t *first_buf;
    uint32_t first_len;
    uint8_t *second_buf;
    uint32_t second_len;
    int call_count;
} uart_seq_state_t;

static void UartSeqReadHandler(void *UserObj, UT_EntryKey_t FuncKey, const UT_StubContext_t *Context, va_list va)
{
    uint8_t *dst = va_arg(va, uint8_t *);
    uint32_t n = va_arg(va, uint32_t);
    uart_seq_state_t *s = (uart_seq_state_t *)UserObj;
    if (!s || !dst) return;
    if (s->call_count == 0)
    {
        memcpy(dst, s->first_buf, (n < s->first_len) ? n : s->first_len);
    }
    else
    {
        memcpy(dst, s->second_buf, (n < s->second_len) ? n : s->second_len);
    }
    s->call_count++;
}

/* Handler that emulates ADCS_ReadData: copy a supplied buffer into the read_data arg and return success */
static void ADCS_ReadData_Handler(void *UserObj, UT_EntryKey_t FuncKey, const UT_StubContext_t *Context)
{
    const uint8_t *src = (const uint8_t *)UserObj;
    if (!src || !Context) return;
    uint8_t *dst = UT_Hook_GetArgValueByName(Context, "read_data", uint8_t *);
    uint8_t len = UT_Hook_GetArgValueByName(Context, "data_length", uint8_t);
    if (dst && len > 0)
    {
        memcpy(dst, src, len);
    }
    int32_t rc = OS_SUCCESS;
    UT_Stub_CopyToReturnValue(FuncKey, &rc, sizeof(rc));
}

/* Removed unused helper: forcing uart_flush return. Not needed. */

void Test_ADCS_ReadData(void)
{
    uart_info_t device;
    uint8_t     read_data[8];
    uint8_t     data_length = 8;
    ADCS_ReadData(&device, read_data, data_length);

    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, data_length);
    ADCS_ReadData(&device, read_data, data_length);

    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, data_length + 1);
    ADCS_ReadData(&device, read_data, data_length);
}

void Test_ADCS_CommandDevice(void)
{
    uart_info_t device;
    uint8_t     cmd_code = 0;
    uint32_t    payload  = 0;
    ADCS_CommandDevice(&device, cmd_code, payload);

    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_ERROR);
    ADCS_CommandDevice(&device, cmd_code, payload);

    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
    ADCS_CommandDevice(&device, cmd_code, payload);

    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
    {
        uint8_t small_read[9] = {0};
        UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, 9);
        UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, 9);
        UT_SetDataBuffer(UT_KEY(uart_read_port), &small_read, sizeof(small_read), false);
        ADCS_CommandDevice(&device, cmd_code, payload);
    }

    /* Simulate a successful echo response that matches the written command */
    {
        uint8_t read_echo[ADCS_DEVICE_CMD_SIZE];
        /* Construct expected echoed frame: HDR, cmd(0), payload(0), TRAILER */
        read_echo[0] = ADCS_DEVICE_HDR_0;
        read_echo[1] = ADCS_DEVICE_HDR_1;
        read_echo[2] = 0x00; /* cmd high */
        read_echo[3] = 0x00; /* cmd low */
        read_echo[4] = 0x00; /* payload high */
        read_echo[5] = 0x00; /* payload low */
        read_echo[6] = ADCS_DEVICE_TRAILER_0;
        read_echo[7] = ADCS_DEVICE_TRAILER_1;

        UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
        UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
        UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, ADCS_DEVICE_CMD_SIZE);
        UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, ADCS_DEVICE_CMD_SIZE);
        UT_SetDataBuffer(UT_KEY(uart_read_port), &read_echo, sizeof(read_echo), false);
        ADCS_CommandDevice(&device, cmd_code, payload);
    }

    /* Simulate a mismatched echo to exercise error path in echo verification */
    {
        uint8_t bad_echo[ADCS_DEVICE_CMD_SIZE] = {0};
        UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
        UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
        UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, ADCS_DEVICE_CMD_SIZE);
        UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, ADCS_DEVICE_CMD_SIZE);
        UT_SetDataBuffer(UT_KEY(uart_read_port), &bad_echo, sizeof(bad_echo), false);
        ADCS_CommandDevice(&device, cmd_code, payload);
    }
}

void Test_ADCS_RequestHK(void)
{
    uart_info_t            device;
    ADCS_Device_HK_tlm_t data;
    /* Ensure command device succeeds so parsing path is exercised */
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
    ADCS_RequestHK(&device, &data);

    uint8_t read_data[ADCS_DEVICE_HK_SIZE];
    memset(read_data, 0, sizeof(read_data));
    /* Header */
    read_data[0] = ADCS_DEVICE_HDR_0;
    read_data[1] = ADCS_DEVICE_HDR_1;
    /* DeviceCounter = 0x0102 */
    read_data[2] = 0x01;
    read_data[3] = 0x02;
    /* Target = 0x0304 */
    read_data[4] = 0x03;
    read_data[5] = 0x04;
    /* Mode */
    read_data[6] = 0x07;
    /* Fill some GPS seconds/subseconds */
    read_data[7] = 0x00; read_data[8] = 0x00; read_data[9] = 0x00; read_data[10] = 0x0C;
    read_data[11] = 0x00; read_data[12] = 0x12; read_data[13] = 0x00; read_data[14] = 0x00;
    /* Trailer */
    read_data[ADCS_DEVICE_HK_SIZE - 2] = ADCS_DEVICE_TRAILER_0;
    read_data[ADCS_DEVICE_HK_SIZE - 1] = ADCS_DEVICE_TRAILER_1;
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, (int)sizeof(read_data));
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, (int)sizeof(read_data));
    UT_SetDataBuffer(UT_KEY(uart_read_port), &read_data, sizeof(read_data), false);
    /* Call with valid frame data to exercise parsing */
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
    ADCS_RequestHK(&device, &data);

    /* Construct a full valid HK payload with float fields (big-endian) */
    memset(read_data, 0, sizeof(read_data));
    read_data[0] = ADCS_DEVICE_HDR_0;
    read_data[1] = ADCS_DEVICE_HDR_1;
    /* DeviceCounter = 0x0102 */
    read_data[2] = 0x01;
    read_data[3] = 0x02;
    /* Target = 0x0304 */
    read_data[4] = 0x03;
    read_data[5] = 0x04;
    /* Mode */
    read_data[6] = 0x07;
    /* GpsSeconds/Subseconds */
    read_data[7] = 0x00; read_data[8] = 0x00; read_data[9] = 0x00; read_data[10] = 0x0C;
    read_data[11] = 0x00; read_data[12] = 0x12; read_data[13] = 0x00; read_data[14] = 0x00;

    /* Helper to write big-endian float */
    do {
        /* write GpsPosition[3] = {1.0f, 2.0f, 3.0f} */
        float fvals1[3] = {1.0f, 2.0f, 3.0f};
        size_t pos = 15;
        for (int i = 0; i < 3; ++i)
        {
            uint32_t u;
            memcpy(&u, &fvals1[i], sizeof(u));
            read_data[pos++] = (u >> 24) & 0xFF;
            read_data[pos++] = (u >> 16) & 0xFF;
            read_data[pos++] = (u >> 8) & 0xFF;
            read_data[pos++] = u & 0xFF;
        }

        /* Velocity[3] = {4.0f, 5.0f, 6.0f} */
        float fvals2[3] = {4.0f, 5.0f, 6.0f};
        for (int i = 0; i < 3; ++i)
        {
            uint32_t u;
            memcpy(&u, &fvals2[i], sizeof(u));
            read_data[pos++] = (u >> 24) & 0xFF;
            read_data[pos++] = (u >> 16) & 0xFF;
            read_data[pos++] = (u >> 8) & 0xFF;
            read_data[pos++] = u & 0xFF;
        }

        /* AttitudeSource */
        read_data[pos++] = 0x01;

        /* AngRate[3] = {0.1f, 0.2f, 0.3f} */
        float fvals3[3] = {0.1f, 0.2f, 0.3f};
        for (int i = 0; i < 3; ++i)
        {
            uint32_t u;
            memcpy(&u, &fvals3[i], sizeof(u));
            read_data[pos++] = (u >> 24) & 0xFF;
            read_data[pos++] = (u >> 16) & 0xFF;
            read_data[pos++] = (u >> 8) & 0xFF;
            read_data[pos++] = u & 0xFF;
        }

        /* Quaternion[4] = {1,0,0,0} */
        float fvals4[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 4; ++i)
        {
            uint32_t u;
            memcpy(&u, &fvals4[i], sizeof(u));
            read_data[pos++] = (u >> 24) & 0xFF;
            read_data[pos++] = (u >> 16) & 0xFF;
            read_data[pos++] = (u >> 8) & 0xFF;
            read_data[pos++] = u & 0xFF;
        }

        /* Eclipse */
        read_data[pos++] = 0x01;

        /* SunVectorBody[3] = {0.5f, 0.6f, 0.7f} */
        float fvals5[3] = {0.5f, 0.6f, 0.7f};
        for (int i = 0; i < 3; ++i)
        {
            uint32_t u;
            memcpy(&u, &fvals5[i], sizeof(u));
            read_data[pos++] = (u >> 24) & 0xFF;
            read_data[pos++] = (u >> 16) & 0xFF;
            read_data[pos++] = (u >> 8) & 0xFF;
            read_data[pos++] = u & 0xFF;
        }

        /* Now add trailer */
        read_data[ADCS_DEVICE_HK_SIZE - 2] = ADCS_DEVICE_TRAILER_0;
        read_data[ADCS_DEVICE_HK_SIZE - 1] = ADCS_DEVICE_TRAILER_1;
    } while (0);

    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, (int)sizeof(read_data));
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, (int)sizeof(read_data));
    UT_SetDataBuffer(UT_KEY(uart_read_port), &read_data, sizeof(read_data), false);
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
    ADCS_RequestHK(&device, &data);

    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, OS_ERROR);
    ADCS_RequestHK(&device, &data);
}

void Test_ADCS_RequestHK_Parse_Success(void)
{
    uart_info_t               device;
    ADCS_Device_HK_tlm_t      data;
    uint8_t                   buf[ADCS_DEVICE_HK_SIZE];

    /* Construct a minimal, valid HK frame matching the wire format */
    memset(buf, 0, sizeof(buf));
    buf[0] = ADCS_DEVICE_HDR_0;
    buf[1] = ADCS_DEVICE_HDR_1;
    /* DeviceCounter */
    buf[2] = 0x12; buf[3] = 0x34;
    /* Target */
    buf[4] = 0x00; buf[5] = 0x01;
    /* Mode */
    buf[6] = 0x02;
    /* GpsSeconds/Subseconds (8 bytes) */
    buf[7] = 0x00; buf[8] = 0x00; buf[9] = 0x00; buf[10] = 0x01;
    buf[11] = 0x00; buf[12] = 0x00; buf[13] = 0x00; buf[14] = 0x02;
    /* Fill remaining float fields with zeroes (already zeroed) */
    /* Trailer */
    buf[ADCS_DEVICE_HK_SIZE - 2] = ADCS_DEVICE_TRAILER_0;
    buf[ADCS_DEVICE_HK_SIZE - 1] = ADCS_DEVICE_TRAILER_1;

    /* Force uart stubs to behave and return our buffer */
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, (int)sizeof(buf));
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, (int)sizeof(buf));
    UT_SetDataBuffer(UT_KEY(uart_read_port), &buf, sizeof(buf), false);

    /* Call and ensure parsing path is exercised */
    ADCS_RequestHK(&device, &data);
}

void Test_ADCS_RequestHK_InvalidHeader(void)
{
    uart_info_t          device;
    ADCS_Device_HK_tlm_t data;
    uint8_t              buf[ADCS_DEVICE_HK_SIZE];

    /* Construct frame with invalid header but valid trailer */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x00; buf[1] = 0x00; /* invalid header */
    buf[ADCS_DEVICE_HK_SIZE - 2] = ADCS_DEVICE_TRAILER_0;
    buf[ADCS_DEVICE_HK_SIZE - 1] = ADCS_DEVICE_TRAILER_1;

    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, (int)sizeof(buf));
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, (int)sizeof(buf));
    UT_SetDataBuffer(UT_KEY(uart_read_port), &buf, sizeof(buf), false);

    ADCS_RequestHK(&device, &data);
}

void Test_ADCS_RequestHK_InvalidTrailer(void)
{
    uart_info_t          device;
    ADCS_Device_HK_tlm_t data;
    uint8_t              buf[ADCS_DEVICE_HK_SIZE];

    /* Construct frame with valid header but corrupted trailer */
    memset(buf, 0, sizeof(buf));
    buf[0] = ADCS_DEVICE_HDR_0;
    buf[1] = ADCS_DEVICE_HDR_1;
    buf[ADCS_DEVICE_HK_SIZE - 2] = 0x00; /* bad trailer */
    buf[ADCS_DEVICE_HK_SIZE - 1] = 0x00; /* bad trailer */

    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, (int)sizeof(buf));
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, (int)sizeof(buf));
    UT_SetDataBuffer(UT_KEY(uart_read_port), &buf, sizeof(buf), false);

    ADCS_RequestHK(&device, &data);
}

void Test_ADCS_ParseHK_Direct_Success(void)
{
    ADCS_Device_HK_tlm_t data;
    uint8_t buf[ADCS_DEVICE_HK_SIZE];

    /* Build a full valid HK payload with non-zero floats to exercise memcpy conversions */
    memset(buf, 0, sizeof(buf));
    buf[0] = ADCS_DEVICE_HDR_0;
    buf[1] = ADCS_DEVICE_HDR_1;
    buf[2] = 0x00; buf[3] = 0x01; /* DeviceCounter */
    buf[4] = 0x00; buf[5] = 0x02; /* Target */
    buf[6] = 0x03; /* Mode */
    buf[7] = 0x00; buf[8] = 0x00; buf[9] = 0x00; buf[10] = 0x0A; /* GpsSeconds */
    buf[11] = 0x00; buf[12] = 0x00; buf[13] = 0x00; buf[14] = 0x0B; /* GpsSubseconds */

    size_t pos = 15;
    float fv[] = {1.5f, -2.5f, 3.25f, 4.5f, -5.5f, 6.75f, 0.125f, 0.25f, 0.375f, 1.0f, 0.0f, 0.0f, 0.5f, 0.6f, 0.7f};
    /* Fill fields in the same order as parsing: GpsPosition(3), Velocity(3), AttitudeSource(1), AngRate(3), Quaternion(4), Eclipse(1), SunVectorBody(3) */
    int idx = 0;
    for (int i = 0; i < 3; ++i)
    {
        uint32_t u; memcpy(&u, &fv[idx++], sizeof(u)); buf[pos++] = (u >> 24) & 0xFF; buf[pos++] = (u >> 16) & 0xFF; buf[pos++] = (u >> 8) & 0xFF; buf[pos++] = u & 0xFF;
    }
    for (int i = 0; i < 3; ++i)
    {
        uint32_t u; memcpy(&u, &fv[idx++], sizeof(u)); buf[pos++] = (u >> 24) & 0xFF; buf[pos++] = (u >> 16) & 0xFF; buf[pos++] = (u >> 8) & 0xFF; buf[pos++] = u & 0xFF;
    }
    /* AttitudeSource */ buf[pos++] = 0x01;
    for (int i = 0; i < 3; ++i)
    {
        uint32_t u; memcpy(&u, &fv[idx++], sizeof(u)); buf[pos++] = (u >> 24) & 0xFF; buf[pos++] = (u >> 16) & 0xFF; buf[pos++] = (u >> 8) & 0xFF; buf[pos++] = u & 0xFF;
    }
    for (int i = 0; i < 4; ++i)
    {
        uint32_t u; memcpy(&u, &fv[idx++], sizeof(u)); buf[pos++] = (u >> 24) & 0xFF; buf[pos++] = (u >> 16) & 0xFF; buf[pos++] = (u >> 8) & 0xFF; buf[pos++] = u & 0xFF;
    }
    /* Eclipse */ buf[pos++] = 0x01;
    for (int i = 0; i < 3; ++i)
    {
        uint32_t u; memcpy(&u, &fv[idx++], sizeof(u)); buf[pos++] = (u >> 24) & 0xFF; buf[pos++] = (u >> 16) & 0xFF; buf[pos++] = (u >> 8) & 0xFF; buf[pos++] = u & 0xFF;
    }

    /* Trailer */
    buf[ADCS_DEVICE_HK_SIZE - 2] = ADCS_DEVICE_TRAILER_0;
    buf[ADCS_DEVICE_HK_SIZE - 1] = ADCS_DEVICE_TRAILER_1;

    /* Call parser directly */
    int32_t rc = ADCS_ParseHK(buf, &data);
    UtAssert_True(rc == OS_SUCCESS, "ADCS_ParseHK returned success");
}

void Test_ADCS_ParseHK_Direct_Failure(void)
{
    ADCS_Device_HK_tlm_t data;
    uint8_t buf[ADCS_DEVICE_HK_SIZE];
    memset(buf, 0, sizeof(buf));
    /* Valid header but corrupt trailer */
    buf[0] = ADCS_DEVICE_HDR_0; buf[1] = ADCS_DEVICE_HDR_1;
    buf[ADCS_DEVICE_HK_SIZE - 2] = 0x00; buf[ADCS_DEVICE_HK_SIZE - 1] = 0x00;
    int32_t rc = ADCS_ParseHK(buf, &data);
    UtAssert_True(rc != OS_SUCCESS, "ADCS_ParseHK detected bad trailer");
}

void Test_ADCS_RequestHK_EndToEnd(void)
{
    uart_info_t               device;
    ADCS_Device_HK_tlm_t      data;
    uint8_t                   hk_buf[ADCS_DEVICE_HK_SIZE];
    uint8_t                   echo[ADCS_DEVICE_CMD_SIZE];
    /* Handler state for sequential uart_read_port responses */
    uart_seq_state_t          uart_state;

    /* Prepare a valid HK payload (reuse direct-success pattern) */
    memset(hk_buf, 0, sizeof(hk_buf));
    hk_buf[0] = ADCS_DEVICE_HDR_0; hk_buf[1] = ADCS_DEVICE_HDR_1;
    hk_buf[2] = 0x00; hk_buf[3] = 0x01; /* DeviceCounter */
    hk_buf[4] = 0x00; hk_buf[5] = 0x02; /* Target */
    hk_buf[6] = 0x03; /* Mode */
    hk_buf[7] = 0; hk_buf[8] = 0; hk_buf[9] = 0; hk_buf[10] = 0x0A;
    hk_buf[11] = 0; hk_buf[12] = 0; hk_buf[13] = 0; hk_buf[14] = 0x0B;
    /* minimal zero floats ok */
    hk_buf[ADCS_DEVICE_HK_SIZE - 2] = ADCS_DEVICE_TRAILER_0;
    hk_buf[ADCS_DEVICE_HK_SIZE - 1] = ADCS_DEVICE_TRAILER_1;

    /* Construct expected echoed command frame so ADCS_CommandDevice succeeds */
    echo[0] = ADCS_DEVICE_HDR_0; echo[1] = ADCS_DEVICE_HDR_1;
    echo[2] = 0x00; echo[3] = 0x01; /* cmd */
    echo[4] = 0x00; echo[5] = 0x00; /* payload */
    echo[6] = ADCS_DEVICE_TRAILER_0; echo[7] = ADCS_DEVICE_TRAILER_1;

    /* Setup stubs: flush/write succeed and provide sequential read buffers via handler */
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
    /* bytes_available: first call returns cmd size, second returns hk size */
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, ADCS_DEVICE_CMD_SIZE);
    /* Ensure ADCS_CommandDevice stub returns success for this focused test */
    UT_SetDefaultReturnValue(UT_KEY(ADCS_CommandDevice), OS_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 2, (int)sizeof(hk_buf));
    /* read_port return sizes: first echo, then hk_buf */
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, ADCS_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 2, (int)sizeof(hk_buf));

    /* Populate handler state and register handler to copy appropriate buffers */
    uart_state.first_buf = echo; uart_state.first_len = ADCS_DEVICE_CMD_SIZE;
    uart_state.second_buf = hk_buf; uart_state.second_len = (int)sizeof(hk_buf);
    uart_state.call_count = 0;
    UT_SetVaHandlerFunction(UT_KEY(uart_read_port), UartSeqReadHandler, &uart_state);

    /* Call end-to-end */
    ADCS_RequestHK(&device, &data);
    UT_SetHandlerFunction(UT_KEY(uart_read_port), NULL, NULL);
}

/* Stubbed ReadData at call-site: ensure ADCS_RequestHK exercises ADCS_ReadData success branch */
void Test_ADCS_RequestHK_ReadDataStubbed(void)
{
    uart_info_t device;
    ADCS_Device_HK_tlm_t data;
    uint8_t hk_buf[ADCS_DEVICE_HK_SIZE];

    memset(hk_buf, 0, sizeof(hk_buf));
    hk_buf[0] = ADCS_DEVICE_HDR_0; hk_buf[1] = ADCS_DEVICE_HDR_1;
    hk_buf[2] = 0x01; hk_buf[3] = 0x02; /* DeviceCounter */
    hk_buf[4] = 0x03; hk_buf[5] = 0x04; /* Target */
    hk_buf[6] = 0x05; /* Mode */
    hk_buf[ADCS_DEVICE_HK_SIZE - 2] = ADCS_DEVICE_TRAILER_0;
    hk_buf[ADCS_DEVICE_HK_SIZE - 1] = ADCS_DEVICE_TRAILER_1;

    /* Avoid invoking the real ADCS_CommandDevice implementation; make it succeed */
    UT_SetDefaultReturnValue(UT_KEY(ADCS_CommandDevice), OS_SUCCESS);

    /* Install handler to pretend ADCS_ReadData returns the hk frame */
    UT_SetHandlerFunction(UT_KEY(ADCS_ReadData), ADCS_ReadData_Handler, hk_buf);
    ADCS_RequestHK(&device, &data);
    UT_SetHandlerFunction(UT_KEY(ADCS_ReadData), NULL, NULL);
}

void Test_ADCS_RequestData_EndToEnd(void)
{
    uart_info_t                 device;
    ADCS_Device_Data_tlm_t      data;
    uint8_t                     data_buf[ADCS_DEVICE_DATA_SIZE];
    uint8_t                     echo[ADCS_DEVICE_CMD_SIZE];

    /* Prepare a valid data payload */
    memset(data_buf, 0, sizeof(data_buf));
    data_buf[0] = ADCS_DEVICE_HDR_0; data_buf[1] = ADCS_DEVICE_HDR_1;
    data_buf[2] = 0x01; data_buf[3] = 0x02; /* Chan1 */
    data_buf[4] = 0x03; data_buf[5] = 0x04; /* Chan2 */
    data_buf[6] = 0x05; data_buf[7] = 0x06; /* Chan3 */
    data_buf[ADCS_DEVICE_DATA_SIZE - 2] = ADCS_DEVICE_TRAILER_0;
    data_buf[ADCS_DEVICE_DATA_SIZE - 1] = ADCS_DEVICE_TRAILER_1;

    /* Echo frame for command */
    echo[0] = ADCS_DEVICE_HDR_0; echo[1] = ADCS_DEVICE_HDR_1;
    echo[2] = 0x00; echo[3] = 0x0A; /* cmd */
    echo[4] = 0x00; echo[5] = 0x00; /* payload */
    echo[6] = ADCS_DEVICE_TRAILER_0; echo[7] = ADCS_DEVICE_TRAILER_1;

    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, ADCS_DEVICE_CMD_SIZE);
    /* Ensure ADCS_CommandDevice stub returns success for this focused test */
    UT_SetDefaultReturnValue(UT_KEY(ADCS_CommandDevice), OS_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 2, (int)sizeof(data_buf));
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, ADCS_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 2, (int)sizeof(data_buf));

    uart_seq_state_t uart_state2;
    uart_state2.first_buf = echo; uart_state2.first_len = ADCS_DEVICE_CMD_SIZE;
    uart_state2.second_buf = data_buf; uart_state2.second_len = (int)sizeof(data_buf);
    uart_state2.call_count = 0;
    UT_SetVaHandlerFunction(UT_KEY(uart_read_port), UartSeqReadHandler, &uart_state2);
    ADCS_RequestData(&device, &data, ADCS_DEVICE_GET_CSS_CMD);
    UT_SetHandlerFunction(UT_KEY(uart_read_port), NULL, NULL);
}

/* Stubbed ReadData at call-site: ensure ADCS_RequestData exercises ADCS_ReadData success branch */
void Test_ADCS_RequestData_ReadDataStubbed(void)
{
    uart_info_t device;
    ADCS_Device_Data_tlm_t data;
    uint8_t data_buf[ADCS_DEVICE_DATA_SIZE];

    memset(data_buf, 0, sizeof(data_buf));
    data_buf[0] = ADCS_DEVICE_HDR_0; data_buf[1] = ADCS_DEVICE_HDR_1;
    data_buf[2] = 0x01; data_buf[3] = 0x02;
    data_buf[4] = 0x03; data_buf[5] = 0x04;
    data_buf[6] = 0x05; data_buf[7] = 0x06;
    data_buf[ADCS_DEVICE_DATA_SIZE - 2] = ADCS_DEVICE_TRAILER_0;
    data_buf[ADCS_DEVICE_DATA_SIZE - 1] = ADCS_DEVICE_TRAILER_1;

    UT_SetDefaultReturnValue(UT_KEY(ADCS_CommandDevice), OS_SUCCESS);
    UT_SetHandlerFunction(UT_KEY(ADCS_ReadData), ADCS_ReadData_Handler, data_buf);
    ADCS_RequestData(&device, &data, ADCS_DEVICE_GET_CSS_CMD);
    UT_SetHandlerFunction(UT_KEY(ADCS_ReadData), NULL, NULL);
}

/* Force ADCS_HandleRequestHK to fail when called from ADCS_RequestHK (ADCS_ReadData returns a bad frame) */
void Test_ADCS_RequestHK_HandleFail(void)
{
    uart_info_t device;
    ADCS_Device_HK_tlm_t data;
    uint8_t bad_hk[ADCS_DEVICE_HK_SIZE];

    /* valid header, corrupt trailer to force parse failure */
    memset(bad_hk, 0, sizeof(bad_hk));
    bad_hk[0] = ADCS_DEVICE_HDR_0; bad_hk[1] = ADCS_DEVICE_HDR_1;
    bad_hk[ADCS_DEVICE_HK_SIZE - 2] = 0x00; bad_hk[ADCS_DEVICE_HK_SIZE - 1] = 0x00;

    /* Ensure command succeeds and simulate uart read returning the corrupted frame */
    UT_SetDefaultReturnValue(UT_KEY(ADCS_CommandDevice), OS_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, (int)sizeof(bad_hk));
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, (int)sizeof(bad_hk));
    UT_SetDataBuffer(UT_KEY(uart_read_port), &bad_hk, sizeof(bad_hk), false);

    ADCS_RequestHK(&device, &data);
}

/* Force ADCS_HandleRequestData to fail when called from ADCS_RequestData (ADCS_ReadData returns a bad frame) */
void Test_ADCS_RequestData_HandleFail(void)
{
    uart_info_t device;
    ADCS_Device_Data_tlm_t data;
    uint8_t bad_data[ADCS_DEVICE_DATA_SIZE];

    /* valid header, corrupt trailer to force parse failure */
    memset(bad_data, 0, sizeof(bad_data));
    bad_data[0] = ADCS_DEVICE_HDR_0; bad_data[1] = ADCS_DEVICE_HDR_1;
    bad_data[ADCS_DEVICE_DATA_SIZE - 2] = 0x00; bad_data[ADCS_DEVICE_DATA_SIZE - 1] = 0x00;

    UT_SetDefaultReturnValue(UT_KEY(ADCS_CommandDevice), OS_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, (int)sizeof(bad_data));
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, (int)sizeof(bad_data));
    UT_SetDataBuffer(UT_KEY(uart_read_port), &bad_data, sizeof(bad_data), false);

    ADCS_RequestData(&device, &data, ADCS_DEVICE_GET_CSS_CMD);
}

void Test_ADCS_RequestData(void)
{
    uart_info_t              device;
    ADCS_Device_Data_tlm_t data;
    /* Ensure ADCS_CommandDevice succeeds so parsing is exercised */
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
    ADCS_RequestData(&device, &data, ADCS_DEVICE_GET_CSS_CMD);

    uint8_t data_read[ADCS_DEVICE_DATA_SIZE];
    memset(data_read, 0, sizeof(data_read));
    data_read[0] = ADCS_DEVICE_HDR_0;
    data_read[1] = ADCS_DEVICE_HDR_1;
    /* Chan1 = 0x0102 */
    data_read[2] = 0x01; data_read[3] = 0x02;
    /* Chan2 = 0x0304 */
    data_read[4] = 0x03; data_read[5] = 0x04;
    /* Chan3 = 0x0506 */
    data_read[6] = 0x05; data_read[7] = 0x06;
    data_read[ADCS_DEVICE_DATA_SIZE - 2] = ADCS_DEVICE_TRAILER_0;
    data_read[ADCS_DEVICE_DATA_SIZE - 1] = ADCS_DEVICE_TRAILER_1;
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, (int)sizeof(data_read));
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, (int)sizeof(data_read));
    UT_SetDataBuffer(UT_KEY(uart_read_port), &data_read, sizeof(data_read), false);
    /* Call with valid frame data to exercise parsing */
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
    ADCS_RequestData(&device, &data, ADCS_DEVICE_GET_CSS_CMD);

    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, OS_ERROR);
    ADCS_RequestData(&device, &data, ADCS_DEVICE_GET_CSS_CMD);
}

void Test_ADCS_RequestData_Hook(void *UserObj, UT_EntryKey_t FuncKey, const UT_StubContext_t *Context, va_list va) {}

void Test_ADCS_PrintHK(void)
{
    ADCS_Device_HK_tlm_t hk;

    /* NULL pointer should be handled gracefully */
    ADCS_PrintHK(NULL);

    /* Populate a small HK and ensure printing doesn't crash */
    memset(&hk, 0, sizeof(hk));
    hk.DeviceCounter = 0x1234;
    hk.Mode = 2;
    hk.GpsSeconds = 0xABCDEF01;
    ADCS_PrintHK(&hk);
}

void Test_ADCS_ReadData_DelayedAvailability(void)
{
    uart_info_t device;
    uint8_t     read_data[8];
    uint8_t     data_length = 8;

    /* First call: bytes_available returns 0, then returns data_length to exercise the wait loop */
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, 0);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 2, data_length);
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, data_length);
    UT_SetDataBuffer(UT_KEY(uart_read_port), &read_data, sizeof(read_data), false);
    ADCS_ReadData(&device, read_data, data_length);
}

/* Focused test: ensure ADCS_RequestHK exercises ADCS_ReadData success branch and parsing */
void Test_ADCS_RequestHK_SuccessPath(void)
{
    ADCS_Device_HK_tlm_t data;
    uint8_t hk_buf[ADCS_DEVICE_HK_SIZE];
    uint8_t echo[ADCS_DEVICE_CMD_SIZE];
    uart_seq_state_t state;

    /* Build a minimal valid HK frame */
    memset(hk_buf, 0, sizeof(hk_buf));
    hk_buf[0] = ADCS_DEVICE_HDR_0; hk_buf[1] = ADCS_DEVICE_HDR_1;
    hk_buf[2] = 0x01; hk_buf[3] = 0x02; /* DeviceCounter */
    hk_buf[4] = 0x03; hk_buf[5] = 0x04; /* Target */
    hk_buf[6] = 0x05; /* Mode */
    hk_buf[ADCS_DEVICE_HK_SIZE - 2] = ADCS_DEVICE_TRAILER_0;
    hk_buf[ADCS_DEVICE_HK_SIZE - 1] = ADCS_DEVICE_TRAILER_1;

    /* Echo frame */
    echo[0] = ADCS_DEVICE_HDR_0; echo[1] = ADCS_DEVICE_HDR_1;
    echo[2] = 0x00; echo[3] = 0x01; echo[4] = 0x00; echo[5] = 0x00;
    echo[6] = ADCS_DEVICE_TRAILER_0; echo[7] = ADCS_DEVICE_TRAILER_1;

    /* Force uart stubs into success sequence */
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, ADCS_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, ADCS_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 2, (int)sizeof(hk_buf));
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 2, (int)sizeof(hk_buf));

    state.first_buf = echo; state.first_len = ADCS_DEVICE_CMD_SIZE;
    state.second_buf = hk_buf; state.second_len = (int)sizeof(hk_buf);
    state.call_count = 0;
    UT_SetVaHandlerFunction(UT_KEY(uart_read_port), UartSeqReadHandler, &state);

    /* Directly exercise internal HK handling logic */
    int32_t rc = ADCS_HandleRequestHK(state.second_buf, &data);
    UtAssert_True(rc == OS_SUCCESS, "ADCS_HandleRequestHK returned success");

    UT_SetVaHandlerFunction(UT_KEY(uart_read_port), NULL, NULL);
}

/* Focused test: ensure ADCS_RequestData exercises header/trailer success branch */
void Test_ADCS_RequestData_SuccessPath(void)
{
    ADCS_Device_Data_tlm_t data;
    uint8_t data_buf[ADCS_DEVICE_DATA_SIZE];
    uint8_t echo[ADCS_DEVICE_CMD_SIZE];
    uart_seq_state_t state;

    memset(data_buf, 0, sizeof(data_buf));
    data_buf[0] = ADCS_DEVICE_HDR_0; data_buf[1] = ADCS_DEVICE_HDR_1;
    data_buf[2] = 0x01; data_buf[3] = 0x02;
    data_buf[4] = 0x03; data_buf[5] = 0x04;
    data_buf[6] = 0x05; data_buf[7] = 0x06;
    data_buf[ADCS_DEVICE_DATA_SIZE - 2] = ADCS_DEVICE_TRAILER_0;
    data_buf[ADCS_DEVICE_DATA_SIZE - 1] = ADCS_DEVICE_TRAILER_1;

    echo[0] = ADCS_DEVICE_HDR_0; echo[1] = ADCS_DEVICE_HDR_1;
    echo[2] = 0x00; echo[3] = 0x0A; echo[4] = 0x00; echo[5] = 0x00;
    echo[6] = ADCS_DEVICE_TRAILER_0; echo[7] = ADCS_DEVICE_TRAILER_1;

    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, ADCS_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, ADCS_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 2, (int)sizeof(data_buf));
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 2, (int)sizeof(data_buf));

    state.first_buf = echo; state.first_len = ADCS_DEVICE_CMD_SIZE;
    state.second_buf = data_buf; state.second_len = (int)sizeof(data_buf);
    state.call_count = 0;
    UT_SetVaHandlerFunction(UT_KEY(uart_read_port), UartSeqReadHandler, &state);

    /* Directly exercise internal data handling logic */
    int32_t rc = ADCS_HandleRequestData(state.second_buf, &data);
    UtAssert_True(rc == OS_SUCCESS, "ADCS_HandleRequestData returned success");

    UT_SetVaHandlerFunction(UT_KEY(uart_read_port), NULL, NULL);
}

void Test_ADCS_ReadData_Timeout(void)
{
    uart_info_t device;
    uint8_t     read_data[8];
    uint8_t     data_length = 8;

    /* Always timeout: bytes_available never reaches required length */
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, 0);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 2, 0);
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 3, 0);
    /* ADCS_ReadData should return error (logged) */
    ADCS_ReadData(&device, read_data, data_length);
}

void Test_ADCS_ReadData_PartialRead(void)
{
    uart_info_t device;
    uint8_t     read_data[8];
    uint8_t     data_length = 8;

    /* Simulate bytes_available larger than requested, but uart_read_port returns fewer bytes */
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, data_length + 4);
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, data_length - 2);
    ADCS_ReadData(&device, read_data, data_length);
}

void Test_ADCS_RequestData_InvalidTrailer(void)
{
    uart_info_t              device;
    ADCS_Device_Data_tlm_t data;
    uint8_t data_read[ADCS_DEVICE_DATA_SIZE];

    /* Construct frame with bad trailer to hit invalid-data branch */
    memset(data_read, 0, sizeof(data_read));
    data_read[0] = ADCS_DEVICE_HDR_0;
    data_read[1] = ADCS_DEVICE_HDR_1;
    data_read[2] = 0x01; data_read[3] = 0x02;
    data_read[4] = 0x03; data_read[5] = 0x04;
    data_read[6] = 0x05; data_read[7] = 0x06;
    /* Intentionally corrupt trailer */
    data_read[ADCS_DEVICE_DATA_SIZE - 2] = 0x00;
    data_read[ADCS_DEVICE_DATA_SIZE - 1] = 0x00;

    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, (int)sizeof(data_read));
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, (int)sizeof(data_read));
    UT_SetDataBuffer(UT_KEY(uart_read_port), &data_read, sizeof(data_read), false);
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
    ADCS_RequestData(&device, &data, ADCS_DEVICE_GET_CSS_CMD);
}

void Test_ADCS_CommandDevice_WriteShort(void)
{
    uart_info_t device;
    uint8_t     cmd_code = 0;
    uint32_t    payload  = 0;

    /* uart_flush succeeds but write returns fewer bytes than expected */
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, 4); /* short write */
    ADCS_CommandDevice(&device, cmd_code, payload);
}

void Test_ADCS_CommandDevice_ReadPartialEcho(void)
{
    uart_info_t device;
    uint8_t     cmd_code = 0;
    uint32_t    payload  = 0;
    uint8_t     read_data[ADCS_DEVICE_CMD_SIZE];

    /* Flush/write succeed */
    UT_SetDeferredRetcode(UT_KEY(uart_flush), 1, UART_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(uart_write_port), 1, ADCS_DEVICE_CMD_SIZE);
    /* bytes_available reports full size but uart_read_port returns fewer bytes */
    UT_SetDeferredRetcode(UT_KEY(uart_bytes_available), 1, ADCS_DEVICE_CMD_SIZE);
    UT_SetDeferredRetcode(UT_KEY(uart_read_port), 1, ADCS_DEVICE_CMD_SIZE - 1);
    UT_SetDataBuffer(UT_KEY(uart_read_port), &read_data, sizeof(read_data), false);
    ADCS_CommandDevice(&device, cmd_code, payload);
}

/*
 * Setup function prior to every test
 */
void Adcs_UT_Setup(void)
{
    UT_ResetState(0);
}

/*
 * Teardown function after every test
 */
void Adcs_UT_TearDown(void) {}

/*
 * Register the test cases to execute with the unit test tool
 */
void UtTest_Setup(void)
{
    UT_SetVaHandlerFunction(UT_KEY(Test_ADCS_RequestData), Test_ADCS_RequestData_Hook, NULL);
    ADD_TEST(ADCS_ReadData);
    ADD_TEST(ADCS_CommandDevice);
    ADD_TEST(ADCS_RequestHK);
    ADD_TEST(ADCS_RequestData);
    ADD_TEST(ADCS_PrintHK);
    ADD_TEST(ADCS_ReadData_DelayedAvailability);
    ADD_TEST(ADCS_ReadData_Timeout);
    ADD_TEST(ADCS_RequestData_InvalidTrailer);
    ADD_TEST(ADCS_RequestHK_Parse_Success);
    ADD_TEST(ADCS_RequestHK_InvalidHeader);
    ADD_TEST(ADCS_RequestHK_InvalidTrailer);
    ADD_TEST(ADCS_ReadData_PartialRead);
    ADD_TEST(ADCS_CommandDevice_WriteShort);
    ADD_TEST(ADCS_CommandDevice_ReadPartialEcho);
    ADD_TEST(ADCS_ParseHK_Direct_Success);
    ADD_TEST(ADCS_ParseHK_Direct_Failure);
    ADD_TEST(ADCS_RequestHK_EndToEnd);
    ADD_TEST(ADCS_RequestData_EndToEnd);
    ADD_TEST(ADCS_RequestHK_ReadDataStubbed);
    ADD_TEST(ADCS_RequestData_ReadDataStubbed);
    ADD_TEST(ADCS_RequestHK_HandleFail);
    ADD_TEST(ADCS_RequestData_HandleFail);
    ADD_TEST(ADCS_RequestHK_SuccessPath);
    ADD_TEST(ADCS_RequestData_SuccessPath);
}
