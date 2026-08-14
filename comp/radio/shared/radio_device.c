#include "radio_device.h"

/*
** Generic initialize radio device (SPI + GPIO)
*/
int32_t RADIO_InitDevice(spi_info_t *spi_device, gpio_info_t *power_gpio, gpio_info_t *interrupt_gpio)
{
    int32_t status = OS_SUCCESS;
    
    /* Initialize SPI device */
    if (spi_device != NULL)
    {
        status = spi_init_dev(spi_device);
        if (status != SPI_SUCCESS)
        {
            OS_printf("RADIO_InitDevice: SPI initialization failed with error %d\n", status);
            return OS_ERROR;
        }
    }
    
    /* Initialize power GPIO */
    if (power_gpio != NULL)
    {
        status = gpio_init(power_gpio);
        if (status != GPIO_SUCCESS)
        {
            OS_printf("RADIO_InitDevice: Power GPIO initialization failed with error %d\n", status);
            return OS_ERROR;
        }
    }
    
    /* Initialize interrupt GPIO */
    if (interrupt_gpio != NULL)
    {
        status = gpio_init(interrupt_gpio);
        if (status != GPIO_SUCCESS)
        {
            OS_printf("RADIO_InitDevice: Interrupt GPIO initialization failed with error %d\n", status);
            return OS_ERROR;
        }
    }
    
    return OS_SUCCESS;
}

/*
** Power on radio device
*/
int32_t RADIO_PowerOn(gpio_info_t *power_gpio)
{
    if (power_gpio == NULL)
    {
        return OS_ERROR;
    }
    
    int32_t status = gpio_write(power_gpio, 1); /* Active high */
    if (status != GPIO_SUCCESS)
    {
        OS_printf("RADIO_PowerOn: Failed to set power GPIO high\n");
        return OS_ERROR;
    }
    
    /* Small delay for power stabilization */
    OS_TaskDelay(RADIO_CFG_MS_TIMEOUT / 100); /* Convert ms to 10ms units */
    
    return OS_SUCCESS;
}

/*
** Power off radio device
*/
int32_t RADIO_PowerOff(gpio_info_t *power_gpio)
{
    if (power_gpio == NULL)
    {
        return OS_ERROR;
    }
    
    int32_t status = gpio_write(power_gpio, 0); /* Active high, so 0 = off */
    if (status != GPIO_SUCCESS)
    {
        OS_printf("RADIO_PowerOff: Failed to set power GPIO low\n");
        return OS_ERROR;
    }
    
    return OS_SUCCESS;
}

/*
** Check interrupt status
*/
int32_t RADIO_CheckInterrupt(gpio_info_t *interrupt_gpio, uint8_t *interrupt_status)
{
    if (interrupt_gpio == NULL || interrupt_status == NULL)
    {
        return OS_ERROR;
    }
    
    int32_t status = gpio_read(interrupt_gpio, interrupt_status);
    if (status != GPIO_SUCCESS)
    {
        OS_printf("RADIO_CheckInterrupt: Failed to read interrupt GPIO\n");
        return OS_ERROR;
    }
    
    return OS_SUCCESS;
}

/*
** Generic command to device via SPI
*/
int32_t RADIO_CommandDevice(spi_info_t *device, uint8_t cmd, uint16_t payload_len, uint8_t *payload)
{
    int32_t status = OS_SUCCESS;
    uint8_t tx_buffer[2048]; /* Large enough for TM frame (1786) + header(1) + cmd(1) + len(2) + trailer(1) = 1791 */
    int32_t total_len;
    
    if (device == NULL)
    {
        return OS_ERROR;
    }
    
    /* Check payload length to prevent buffer overflow */
    if (payload_len > (sizeof(tx_buffer) - 5))
    {
        OS_printf("RADIO_CommandDevice: Payload too large (%d bytes), max=%d\n", payload_len, (int)(sizeof(tx_buffer) - 5));
        return OS_ERROR;
    }
    
    /* Build command packet */
    tx_buffer[0] = RADIO_DEVICE_HDR;          /* Header byte */
    tx_buffer[1] = cmd;                       /* Command */
    tx_buffer[2] = (uint8_t)((payload_len >> 8) & 0xFF); /* Payload length high byte */
    tx_buffer[3] = (uint8_t)(payload_len & 0xFF);        /* Payload length low byte */
    
    /* Copy payload if provided */
    if (payload_len > 0 && payload != NULL)
    {
        memcpy(&tx_buffer[4], payload, payload_len);
    }
    
    /* Add trailer */
    tx_buffer[4 + payload_len] = RADIO_DEVICE_TRAILER;  /* Trailer byte */

    /* Calculate total length */
    total_len = 5 + payload_len; /* header(1) + cmd(1) + len(2) + payload + trailer(1) */
    
    #ifdef RADIO_CFG_DEBUG
        OS_printf("RADIO_CommandDevice[%d] = ", total_len);
        for (int32_t i = 0; i < total_len; i++)
        {
            OS_printf("%02x", tx_buffer[i]);
        }
        OS_printf("\n");
    #endif
    
    /* Perform SPI transaction */
    status = spi_write(device, tx_buffer, (uint32_t)total_len);
    if (status != (int32_t)total_len)
    {
        OS_printf("RADIO_CommandDevice: SPI transaction failed with error %d\n", status);
        return OS_ERROR;
    }
    
    return OS_SUCCESS;
}

/*
** Request housekeeping command
*/
int32_t RADIO_RequestHK(spi_info_t *device, RADIO_Device_HK_tlm_t *data)
{
    int32_t status = OS_SUCCESS;
    uint8_t rx_buffer[RADIO_DEVICE_HK_SIZE];
    uint8_t tx_buffer[5]; /* header(1) + cmd(1) + len(2) + payload + trailer(1) */
    
    if (device == NULL || data == NULL)
    {
        return OS_ERROR;
    }
    
    /* Send HK request command */
    status = RADIO_CommandDevice(device, RADIO_DEVICE_REQ_HK_CMD, 0, NULL);
    if (status != OS_SUCCESS)
    {
        OS_printf("RADIO_RequestHK: Command failed with error %d\n", status);
        return status;
    }
    
    /* Wait briefly for response */
    OS_TaskDelay(RADIO_CFG_MS_TIMEOUT / 100); /* Convert ms to 10ms units */
    
    /* Read HK response */
    memset(tx_buffer, 0, sizeof(tx_buffer));
    status = spi_read(device, rx_buffer, RADIO_DEVICE_HK_SIZE);
    if (status != RADIO_DEVICE_HK_SIZE)
    {
        OS_printf("RADIO_RequestHK: SPI read failed with error %d\n", status);
        return OS_ERROR;
    }
    
    #ifdef RADIO_CFG_DEBUG
        OS_printf("RADIO_RequestHK response = ");
        for (uint32_t i = 0; i < RADIO_DEVICE_HK_SIZE; i++)
        {
            OS_printf("%02x", rx_buffer[i]);
        }
        OS_printf("\n");
    #endif
    
    /* Verify response header and trailer */
    if ((rx_buffer[0] != RADIO_DEVICE_HDR) ||
        (rx_buffer[RADIO_DEVICE_HK_SIZE-1] != RADIO_DEVICE_TRAILER))
    {
        OS_printf("RADIO_RequestHK: Invalid response header/trailer\n");
        return OS_ERROR;
    }
    
    /* Parse housekeeping data */
    data->CommandCounter = (rx_buffer[1] << 8) | rx_buffer[2];
    data->Mode = rx_buffer[3];
    data->GroundLock = rx_buffer[4];
    data->RxSpeedSetting = rx_buffer[5];
    data->RxWavelengthSetting = rx_buffer[6];
    data->TxSpeedSetting = rx_buffer[7];
    data->TxWavelengthSetting = rx_buffer[8];
    data->BytesInRxBuffer = (rx_buffer[9] << 24) | (rx_buffer[10] << 16) | (rx_buffer[11] << 8) | rx_buffer[12];
    data->BytesReceived = (rx_buffer[13] << 24) | (rx_buffer[14] << 16) | (rx_buffer[15] << 8) | rx_buffer[16];
    data->BytesSent = (rx_buffer[17] << 24) | (rx_buffer[18] << 16) | (rx_buffer[19] << 8) | rx_buffer[20];
    
    #ifdef RADIO_CFG_DEBUG
        OS_printf("  CommandCounter    = %d\n", data->CommandCounter);
        OS_printf("  Mode              = %d\n", data->Mode);
        OS_printf("  GroundLock        = %d\n", data->GroundLock);
        OS_printf("  BytesInRxBuffer   = %d\n", data->BytesInRxBuffer);
        OS_printf("  BytesReceived     = %d\n", data->BytesReceived);
        OS_printf("  BytesSent         = %d\n", data->BytesSent);
    #endif
    
    return OS_SUCCESS;
}

/*
** Set configuration command
*/
int32_t RADIO_SetConfiguration(spi_info_t *device, RADIO_Device_Config_t *config)
{
    uint8_t payload[RADIO_CFG_PAYLOAD_SIZE];
    
    if (device == NULL || config == NULL)
    {
        return OS_ERROR;
    }
    
    /* Build configuration payload */
    payload[0] = config->Mode;
    payload[1] = config->RxSpeedSetting;
    payload[2] = config->RxWavelengthSetting;
    payload[3] = config->TxSpeedSetting;
    payload[4] = config->TxWavelengthSetting;
    
    /* Send configuration command */
    return RADIO_CommandDevice(device, RADIO_DEVICE_SET_CFG_CMD, RADIO_CFG_PAYLOAD_SIZE, payload);
}

/*
** Send data command
*/
int32_t RADIO_SendData(spi_info_t *device, uint8_t *data, uint16_t data_length)
{
    if (device == NULL || data == NULL)
    {
        return OS_ERROR;
    }

    #ifdef RADIO_CFG_DEBUG
        OS_printf("RADIO_SendData: Sending %d bytes: ", data_length);
        for (uint16_t i = 0; i < data_length; i++)
        {
            OS_printf("%02x", data[i]);
        }
        OS_printf("\n");
    #endif
    
    /* Send data command with data as payload */
    return RADIO_CommandDevice(device, RADIO_DEVICE_SEND_CMD, data_length, data);
}

/*
** Receive data command
*/
int32_t RADIO_ReceiveData(spi_info_t *device, uint8_t *data, uint16_t max_length, uint16_t *actual_length)
{
    int32_t status = OS_SUCCESS;
    uint8_t payload[2];
    uint8_t rx_buffer[2048]; /* Large enough for TM frame + headers */
    uint16_t response_len;
    
    if (device == NULL || data == NULL || actual_length == NULL)
    {
        return OS_ERROR;
    }
    
    /* Build receive request payload (number of bytes to receive, uint16 big-endian) */
    payload[0] = (uint8_t)((max_length >> 8) & 0xFF);
    payload[1] = (uint8_t)(max_length & 0xFF);
    
    /* Send receive command (payload_len=2) */
    status = RADIO_CommandDevice(device, RADIO_DEVICE_RECEIVE_CMD, 2, payload);
    if (status != OS_SUCCESS)
    {
        return status;
    }

    /* Wait briefly for response (same pattern as RADIO_RequestHK) */
    OS_TaskDelay(RADIO_CFG_MS_TIMEOUT / 100);

    #ifdef RADIO_CFG_DEBUG
    OS_printf("RADIO_ReceiveData: Requesting SPI read of %u bytes\n", max_length);
    #endif
    /* Read entire response in a single call. The simulator may pad with zeros up to max_length. */
    status = spi_read(device, rx_buffer, (uint32_t)(max_length + 4)); /* header(1)+len(2)+payload+trailer(1) */
    if (status <= 0)
    {
        /* Retry once after the full timeout; at >1x sim speed the larger response
           buffer takes proportionally longer to arrive through the transport. */
        OS_TaskDelay(RADIO_CFG_MS_TIMEOUT);
        status = spi_read(device, rx_buffer, (uint32_t)(max_length + 4));
        if (status <= 0)
        {
            OS_printf("RADIO_ReceiveData: SPI read failed with error %d\n", status);
            return OS_ERROR;
        }
    }

    int bytes_read = status;

    /* Need at least header(1) + len(2) + trailer(1) => 4 bytes */
    if (bytes_read < 4)
    {
        OS_printf("RADIO_ReceiveData: Incomplete response (bytes_read=%d)\n", bytes_read);
        return OS_ERROR;
    }

    /* Verify data header */
    if (rx_buffer[0] != RADIO_DEVICE_HDR)
    {
        OS_printf("RADIO_ReceiveData: Invalid response header 0x%02X\n", rx_buffer[0]);
        return OS_ERROR;
    }

    /* Get payload length from response (second and third byte, big-endian) */
    response_len = ((uint16_t)rx_buffer[1] << 8) | (uint16_t)rx_buffer[2];

    /* Ensure payload length is within requested maximum */
    if (response_len > max_length)
    {
        OS_printf("RADIO_ReceiveData: Response too large (%d > %d)\n", response_len, max_length);
        return OS_ERROR;
    }

    /* Compute index of trailer within the received buffer */
    int trailer_index = 3 + response_len; /* header(0), len(1,2), payload[3..], trailer at 3+len */

    /* Ensure we actually read the trailer byte (allow padding after trailer) */
    if (bytes_read <= trailer_index)
    {
        OS_printf("RADIO_ReceiveData: Response truncated (need %d bytes, got %d)\n", trailer_index + 1, bytes_read);
        return OS_ERROR;
    }

    /* Verify trailer byte */
    if (rx_buffer[trailer_index] != RADIO_DEVICE_TRAILER)
    {
        OS_printf("RADIO_ReceiveData: Invalid response trailer 0x%02X at index %d\n", rx_buffer[trailer_index], trailer_index);
        return OS_ERROR;
    }

    /* Copy payload to caller buffer */
    if (response_len > 0)
    {
        memcpy(data, &rx_buffer[3], response_len);
    }

    *actual_length = response_len; /* caller expects payload length only */

    return OS_SUCCESS;
}
