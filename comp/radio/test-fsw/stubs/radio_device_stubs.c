#include "utgenstub.h"
#include "radio_device.h"

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
// RADIO_RequestData was removed in favor of RADIO_ReceiveData/RADIO_RequestHK in production
