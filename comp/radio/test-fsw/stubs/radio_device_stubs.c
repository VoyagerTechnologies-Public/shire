#include "utgenstub.h"
#include "radio_device.h"

int32_t RADIO_InitDevice(spi_info_t *spi_device, gpio_info_t *power_gpio, gpio_info_t *interrupt_gpio)
{
    UT_GenStub_SetupReturnBuffer(RADIO_InitDevice, int32_t);
    UT_GenStub_AddParam(RADIO_InitDevice, spi_info_t *, spi_device);
    UT_GenStub_AddParam(RADIO_InitDevice, gpio_info_t *, power_gpio);
    UT_GenStub_AddParam(RADIO_InitDevice, gpio_info_t *, interrupt_gpio);
    UT_GenStub_Execute(RADIO_InitDevice, Basic, NULL);
    return UT_GenStub_GetReturnValue(RADIO_InitDevice, int32_t);
}

int32_t RADIO_ReceiveData(spi_info_t *device, uint8_t *data, uint16_t max_length, uint16_t *actual_length)
{
    UT_GenStub_SetupReturnBuffer(RADIO_ReceiveData, int32_t);

    UT_GenStub_AddParam(RADIO_ReceiveData, spi_info_t *, device);
    UT_GenStub_AddParam(RADIO_ReceiveData, uint8_t *, data);
    UT_GenStub_AddParam(RADIO_ReceiveData, uint16_t, max_length);
    UT_GenStub_AddParam(RADIO_ReceiveData, uint16_t *, actual_length);

    UT_GenStub_Execute(RADIO_ReceiveData, Basic, NULL);

    return UT_GenStub_GetReturnValue(RADIO_ReceiveData, int32_t);
}

int32_t RADIO_CommandDevice(spi_info_t *device, uint8_t cmd, uint16_t payload_len, uint8_t *payload)
{
    UT_GenStub_SetupReturnBuffer(RADIO_CommandDevice, int32_t);

    UT_GenStub_AddParam(RADIO_CommandDevice, spi_info_t *, device);
    UT_GenStub_AddParam(RADIO_CommandDevice, uint8_t, cmd);
    UT_GenStub_AddParam(RADIO_CommandDevice, uint16_t, payload_len);
    UT_GenStub_AddParam(RADIO_CommandDevice, uint8_t *, payload);

    UT_GenStub_Execute(RADIO_CommandDevice, Basic, NULL);

    return UT_GenStub_GetReturnValue(RADIO_CommandDevice, int32_t);
}

int32_t RADIO_RequestHK(spi_info_t *device, RADIO_Device_HK_tlm_t *data)
{
    UT_GenStub_SetupReturnBuffer(RADIO_RequestHK, int32_t);

    UT_GenStub_AddParam(RADIO_RequestHK, spi_info_t *, device);
    UT_GenStub_AddParam(RADIO_RequestHK, RADIO_Device_HK_tlm_t *, data);

    UT_GenStub_Execute(RADIO_RequestHK, Basic, NULL);

    return UT_GenStub_GetReturnValue(RADIO_RequestHK, int32_t);
}

int32_t RADIO_SetConfiguration(spi_info_t *device, RADIO_Device_Config_t *config)
{
    UT_GenStub_SetupReturnBuffer(RADIO_SetConfiguration, int32_t);
    UT_GenStub_AddParam(RADIO_SetConfiguration, spi_info_t *, device);
    UT_GenStub_AddParam(RADIO_SetConfiguration, RADIO_Device_Config_t *, config);
    UT_GenStub_Execute(RADIO_SetConfiguration, Basic, NULL);
    return UT_GenStub_GetReturnValue(RADIO_SetConfiguration, int32_t);
}

int32_t RADIO_SendData(spi_info_t *device, uint8_t *data, uint16_t data_length)
{
    UT_GenStub_SetupReturnBuffer(RADIO_SendData, int32_t);
    UT_GenStub_AddParam(RADIO_SendData, spi_info_t *, device);
    UT_GenStub_AddParam(RADIO_SendData, uint8_t *, data);
    UT_GenStub_AddParam(RADIO_SendData, uint16_t, data_length);
    UT_GenStub_Execute(RADIO_SendData, Basic, NULL);
    return UT_GenStub_GetReturnValue(RADIO_SendData, int32_t);
}

int32_t RADIO_CheckInterrupt(gpio_info_t *interrupt_gpio, uint8_t *interrupt_status)
{
    UT_GenStub_SetupReturnBuffer(RADIO_CheckInterrupt, int32_t);
    UT_GenStub_AddParam(RADIO_CheckInterrupt, gpio_info_t *, interrupt_gpio);
    UT_GenStub_AddParam(RADIO_CheckInterrupt, uint8_t *, interrupt_status);
    UT_GenStub_Execute(RADIO_CheckInterrupt, Basic, NULL);
    return UT_GenStub_GetReturnValue(RADIO_CheckInterrupt, int32_t);
}

int32_t RADIO_PowerOn(gpio_info_t *power_gpio)
{
    UT_GenStub_SetupReturnBuffer(RADIO_PowerOn, int32_t);
    UT_GenStub_AddParam(RADIO_PowerOn, gpio_info_t *, power_gpio);
    UT_GenStub_Execute(RADIO_PowerOn, Basic, NULL);
    return UT_GenStub_GetReturnValue(RADIO_PowerOn, int32_t);
}

int32_t RADIO_PowerOff(gpio_info_t *power_gpio)
{
    UT_GenStub_SetupReturnBuffer(RADIO_PowerOff, int32_t);
    UT_GenStub_AddParam(RADIO_PowerOff, gpio_info_t *, power_gpio);
    UT_GenStub_Execute(RADIO_PowerOff, Basic, NULL);
    return UT_GenStub_GetReturnValue(RADIO_PowerOff, int32_t);
}
// RADIO_RequestData was removed in favor of RADIO_ReceiveData/RADIO_RequestHK in production
