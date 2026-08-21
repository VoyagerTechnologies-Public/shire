#include "adcs_device.h"

/*
** Generic read data from device
*/
int32_t ADCS_ReadData(uart_info_t *device, uint8_t *read_data, uint8_t data_length)
{
    int32_t status             = OS_SUCCESS;
    int32_t bytes              = 0;
    int32_t bytes_available    = 0;
    /* Use a signed counter large enough for extended debug timeouts */
    int32_t ms_timeout_counter = 0;
    /* Allow longer timeouts when ADCS debug is enabled (helps simulated transports)
     * Default timeout is ADCS_CFG_MS_TIMEOUT ms; in debug builds multiply to give
     * the simulator more time to schedule component ticks and transport polling.
     */
    int32_t timeout_limit = ADCS_CFG_MS_TIMEOUT;

    /* Wait until all data received or timeout occurs */
    bytes_available = uart_bytes_available(device);
    while ((bytes_available < data_length) && (ms_timeout_counter < timeout_limit))
    {
        ms_timeout_counter++;
        OS_TaskDelay(1);
        bytes_available = uart_bytes_available(device);
    }

    if (ms_timeout_counter < timeout_limit)
    {
        /* Limit bytes available */
        if (bytes_available > data_length)
        {
            bytes_available = data_length;
        }

        /* Read data */
        bytes = uart_read_port(device, read_data, (uint32_t)bytes_available);
        if (bytes != bytes_available)
        {
            OS_printf("  ADCS_ReadData: Bytes read != to requested! read=%d expected=%d\n", bytes, bytes_available);
            status = OS_ERROR;
        } /* uart_read */
    }
    else
    {
        #ifdef ADCS_CFG_DEBUG
            OS_printf("  ADCS_ReadData: Timed out after %d ms waiting for %d bytes (debug timeout=%d)\n", ms_timeout_counter, data_length, timeout_limit);
        #endif
        status = OS_ERROR;
    } /* ms_timeout_counter */

    return status;
}

/*
** Generic command to device
** Note that confirming the echoed response is specific to this implementation
*/
int32_t ADCS_CommandDevice(uart_info_t *device, uint16_t cmd_code, uint16_t payload)
{
    int32_t status = OS_SUCCESS;
    int32_t bytes  = 0;
    uint8_t write_data[ADCS_DEVICE_CMD_SIZE];
    uint8_t read_data[ADCS_DEVICE_DATA_SIZE];

    /* Prepare command */
    write_data[0] = ADCS_DEVICE_HDR_0;
    write_data[1] = ADCS_DEVICE_HDR_1;
    write_data[2] = (uint8_t)(cmd_code >> 8);
    write_data[3] = (uint8_t)(cmd_code & 0xFF);
    write_data[4] = (uint8_t)(payload >> 8);
    write_data[5] = (uint8_t)(payload & 0xFF);
    write_data[6] = ADCS_DEVICE_TRAILER_0;
    write_data[7] = ADCS_DEVICE_TRAILER_1;

    /* Flush any prior data */
    status = uart_flush(device);
    if (status == UART_SUCCESS)
    {
        /* Write data */
        bytes = uart_write_port(device, write_data, ADCS_DEVICE_CMD_SIZE);
        #ifdef ADCS_CFG_DEBUG
            OS_printf("  ADCS_CommandDevice[%d] = ", bytes);
            for (uint32_t i = 0; i < ADCS_DEVICE_CMD_SIZE; i++)
            {
                OS_printf("%02x", write_data[i]);
            }
            OS_printf("\n");
        #endif
        if (bytes == ADCS_DEVICE_CMD_SIZE)
        {
            status = ADCS_ReadData(device, read_data, ADCS_DEVICE_CMD_SIZE);
            if (status == OS_SUCCESS)
            {
                /* Confirm echoed response */
                bytes = 0;
                while ((bytes < (int32_t)ADCS_DEVICE_CMD_SIZE) && (status == OS_SUCCESS))
                {
                    if (read_data[bytes] != write_data[bytes])
                    {
                        status = OS_ERROR;
                    }
                    bytes++;
                }
            } /* ADCS_ReadData */
            else
            {
                #ifdef ADCS_CFG_DEBUG
                    OS_printf("ADCS_CommandDevice - ADCS_ReadData returned %d \n", status);
                #endif
            }
        }
        else
        {
            #ifdef ADCS_CFG_DEBUG
                OS_printf("ADCS_CommandDevice - uart_write_port returned %d, expected %d \n", bytes, ADCS_DEVICE_CMD_SIZE);
            #endif
        } /* uart_write */
    } /* uart_flush*/
    else
    {
        OS_printf("ADCS_CommandDevice - uart_flush returned error %d \n", status);
    }
    return status;
}

/*
** Request housekeeping command
*/
int32_t ADCS_RequestHK(uart_info_t *device, ADCS_Device_HK_tlm_t *data)
{
    int32_t status = OS_SUCCESS;
    uint8_t read_data[ADCS_DEVICE_HK_SIZE];

    /* Command device to send HK */
    status = ADCS_CommandDevice(device, ADCS_DEVICE_REQ_HK_CMD, 0);
    if (status == OS_SUCCESS)
    {
        /* Read HK data */
        status = ADCS_ReadData(device, read_data, sizeof(read_data));
        if (status == OS_SUCCESS)
        {
#ifdef ADCS_CFG_DEBUG
            /* Print the exact number of bytes expected and the raw hex we received */
            OS_printf("ADCS_RequestHK: expected_size=%zu\n", sizeof(read_data));
            OS_printf("ADCS_RequestHK: raw recv: ");
            for (uint32_t i = 0; i < sizeof(read_data); i++)
            {
                OS_printf("%02X ", read_data[i]);
            }
            OS_printf("\n");
#endif
            #ifdef ADCS_CFG_DEBUG
                OS_printf("  ADCS_RequestHK = ");
                for (uint32_t i = 0; i < sizeof(read_data); i++)
                {
                    OS_printf("%02x", read_data[i]);
                }
                OS_printf("\n");
            #endif

            /* Verify header/trailer and parse */
            if (ADCS_HandleRequestHK(read_data, data) != OS_SUCCESS)
            {
                OS_printf("  ADCS_RequestHK: ADCS_ReadData reported error %d \n", status);
                status = OS_ERROR;
            }
        } /* ADCS_ReadData */
    }
    else
    {
        OS_printf("  ADCS_RequestHK: ADCS_CommandDevice reported error %d \n", status);
    }
    return status;
}

/*
** Request data command
*/
int32_t ADCS_RequestData(uart_info_t *device, ADCS_Device_Data_tlm_t *data, uint16_t data_cmd)
{
    int32_t status = OS_SUCCESS;
    uint8_t read_data[ADCS_DEVICE_DATA_SIZE];

    /* Command device to request a specific data frame */
    status = ADCS_CommandDevice(device, data_cmd, 0);
    if (status == OS_SUCCESS)
    {
        /* Read HK data */
        status = ADCS_ReadData(device, read_data, sizeof(read_data));
        if (status == OS_SUCCESS)
        {
            #ifdef ADCS_CFG_DEBUG
                OS_printf("  ADCS_RequestData = ");
                for (uint32_t i = 0; i < sizeof(read_data); i++)
                {
                    OS_printf("%02x", read_data[i]);
                }
                OS_printf("\n");
            #endif

            /* Verify data header and trailer and parse into structure */
            if (ADCS_HandleRequestData(read_data, data) != OS_SUCCESS)
            {
                status = OS_ERROR;
            }
        }
        else
        {
            OS_printf("  ADCS_RequestData: Invalid data read! \n");
            status = OS_ERROR;
        } /* ADCS_ReadData */
    }
    else
    {
        OS_printf("  ADCS_RequestData: ADCS_CommandDevice reported error %d \n", status);
    }
    return status;
}

/* Pretty-print housekeeping structure to stdout */
void ADCS_PrintHK(const ADCS_Device_HK_tlm_t *hk)
{
    if (!hk) return;
    printf("ADCS Housekeeping:\n");
    printf("  DeviceCounter : %u\n", hk->DeviceCounter);
    printf("  Mode          : %u\n", hk->Mode);
    printf("  GpsSeconds    : %u\n", hk->GpsSeconds);
    printf("  GpsSubseconds : %u\n", hk->GpsSubseconds);
    printf("  GpsPosition   : %.6f, %.6f, %.6f\n", hk->GpsPosition[0], hk->GpsPosition[1], hk->GpsPosition[2]);
    printf("  Velocity      : %.6f, %.6f, %.6f\n", hk->Velocity[0], hk->Velocity[1], hk->Velocity[2]);
    printf("  AttitudeSrc   : %u\n", hk->AttitudeSource);
    printf("  AngRate       : %.6f, %.6f, %.6f\n", hk->AngRate[0], hk->AngRate[1], hk->AngRate[2]);
    printf("  Quaternion    : %.6f, %.6f, %.6f, %.6f\n", hk->Quaternion[0], hk->Quaternion[1], hk->Quaternion[2], hk->Quaternion[3]);
    printf("  Eclipse       : %u\n", hk->Eclipse);
    printf("  SunVectorBody : %.6f, %.6f, %.6f\n", hk->SunVectorBody[0], hk->SunVectorBody[1], hk->SunVectorBody[2]);
}

/* Parse an HK wire-format buffer (big-endian) into the in-memory structure. */
int32_t ADCS_ParseHK(const uint8_t *read_data, ADCS_Device_HK_tlm_t *data)
{
    /* Verify header/trailer */
    if (!read_data || !data) return OS_ERROR;
    if (!((read_data[0] == ADCS_DEVICE_HDR_0) && (read_data[1] == ADCS_DEVICE_HDR_1) &&
          (read_data[ADCS_DEVICE_HK_SIZE - 2] == ADCS_DEVICE_TRAILER_0) &&
          (read_data[ADCS_DEVICE_HK_SIZE - 1] == ADCS_DEVICE_TRAILER_1)))
    {
        return OS_ERROR;
    }

    /* Parse big-endian wire format per README */
    const uint8_t *ptr = &read_data[2]; /* skip header (2 bytes) */

    data->DeviceCounter = ((uint16_t)ptr[0] << 8) | ptr[1];
    ptr += 2;

    /* Parse Target (present in simulated device frame) */
    data->Target = ((uint16_t)ptr[0] << 8) | ptr[1];
    ptr += 2;

    data->Mode = ptr[0];
    ptr += 1;

    data->GpsSeconds = ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) | ((uint32_t)ptr[2] << 8) | ptr[3];
    ptr += 4;

    data->GpsSubseconds = ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) | ((uint32_t)ptr[2] << 8) | ptr[3];
    ptr += 4;

    /* Helper to convert big-endian 4 bytes to float */
    for (int i = 0; i < 3; i++)
    {
        uint32_t u = ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) | ((uint32_t)ptr[2] << 8) | ptr[3];
        float f;
        memcpy(&f, &u, sizeof(f));
        data->GpsPosition[i] = f;
        ptr += 4;
    }

    for (int i = 0; i < 3; i++)
    {
        uint32_t u = ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) | ((uint32_t)ptr[2] << 8) | ptr[3];
        float f;
        memcpy(&f, &u, sizeof(f));
        data->Velocity[i] = f;
        ptr += 4;
    }

    data->AttitudeSource = ptr[0];
    ptr += 1;

    for (int i = 0; i < 3; i++)
    {
        uint32_t u = ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) | ((uint32_t)ptr[2] << 8) | ptr[3];
        float f;
        memcpy(&f, &u, sizeof(f));
        data->AngRate[i] = f;
        ptr += 4;
    }

    for (int i = 0; i < 4; i++)
    {
        uint32_t u = ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) | ((uint32_t)ptr[2] << 8) | ptr[3];
        float f;
        memcpy(&f, &u, sizeof(f));
        data->Quaternion[i] = f;
        ptr += 4;
    }

    data->Eclipse = ptr[0];
    ptr += 1;

    for (int i = 0; i < 3; i++)
    {
        uint32_t u = ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) | ((uint32_t)ptr[2] << 8) | ptr[3];
        float f;
        memcpy(&f, &u, sizeof(f));
        data->SunVectorBody[i] = f;
        ptr += 4;
    }

    return OS_SUCCESS;
}

/* Helper to process a raw HK frame (header/trailer verified already) */
int32_t ADCS_HandleRequestHK(const uint8_t *read_data, ADCS_Device_HK_tlm_t *data)
{
    if (!read_data || !data) return OS_ERROR;

#ifdef ADCS_CFG_DEBUG
    /* Print the exact number of bytes expected and the raw hex we received */
    OS_printf("ADCS_HandleRequestHK: raw recv: ");
    for (uint32_t i = 0; i < ADCS_DEVICE_HK_SIZE; i++)
    {
        OS_printf("%02X ", read_data[i]);
    }
    OS_printf("\n");
#endif

    if (ADCS_ParseHK(read_data, data) != OS_SUCCESS)
    {
        return OS_ERROR;
    }
    return OS_SUCCESS;
}

/* Helper to process a raw data frame into ADCS_Device_Data_tlm_t */
int32_t ADCS_HandleRequestData(const uint8_t *read_data, ADCS_Device_Data_tlm_t *data)
{
    if (!read_data || !data) return OS_ERROR;

    if ((read_data[0] == ADCS_DEVICE_HDR_0) && (read_data[1] == ADCS_DEVICE_HDR_1) &&
        (read_data[ADCS_DEVICE_DATA_SIZE - 2] == ADCS_DEVICE_TRAILER_0) &&
        (read_data[ADCS_DEVICE_DATA_SIZE - 1] == ADCS_DEVICE_TRAILER_1))
    {
        data->Chan1 = ((uint16_t)read_data[2] << 8) | read_data[3];
        data->Chan2 = ((uint16_t)read_data[4] << 8) | read_data[5];
        data->Chan3 = ((uint16_t)read_data[6] << 8) | read_data[7];
        return OS_SUCCESS;
    }
    return OS_ERROR;
}
