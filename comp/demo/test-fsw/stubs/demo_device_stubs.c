#include "utgenstub.h"
#include "demo_device.h"

int32_t DEMO_ReadData(uart_info_t *device, uint8_t *read_data, uint8_t data_length)
{
    UT_GenStub_SetupReturnBuffer(DEMO_ReadData, int32_t);

    UT_GenStub_AddParam(DEMO_ReadData, uart_info_t *, device);
    UT_GenStub_AddParam(DEMO_ReadData, uint8_t *, read_data);
    UT_GenStub_AddParam(DEMO_ReadData, uint8_t, data_length);

    UT_GenStub_Execute(DEMO_ReadData, Basic, NULL);

    return UT_GenStub_GetReturnValue(DEMO_ReadData, int32_t);
}

int32_t DEMO_CommandDevice(uart_info_t *device, uint16_t cmd, uint16_t payload)
{
    UT_GenStub_SetupReturnBuffer(DEMO_CommandDevice, int32_t);

    UT_GenStub_AddParam(DEMO_CommandDevice, uart_info_t *, device);
    UT_GenStub_AddParam(DEMO_CommandDevice, uint8_t, cmd);
    UT_GenStub_AddParam(DEMO_CommandDevice, uint32_t, payload);

    UT_GenStub_Execute(DEMO_CommandDevice, Basic, NULL);

    return UT_GenStub_GetReturnValue(DEMO_CommandDevice, int32_t);
}

int32_t DEMO_RequestHK(uart_info_t *device, DEMO_Device_HK_tlm_t *data)
{
    UT_GenStub_SetupReturnBuffer(DEMO_RequestHK, int32_t);

    UT_GenStub_AddParam(DEMO_RequestHK, uart_info_t *, device);
    UT_GenStub_AddParam(DEMO_RequestHK, DEMO_Device_HK_tlm_t *, data);

    UT_GenStub_Execute(DEMO_RequestHK, Basic, NULL);

    return UT_GenStub_GetReturnValue(DEMO_RequestHK, int32_t);
}

int32_t DEMO_RequestData(uart_info_t *device, DEMO_Device_Data_tlm_t *data)
{
    UT_GenStub_SetupReturnBuffer(DEMO_RequestData, int32_t);

    UT_GenStub_AddParam(DEMO_RequestData, uart_info_t *, device);
    UT_GenStub_AddParam(DEMO_RequestData, DEMO_Device_Data_tlm_t *, data);

    UT_GenStub_Execute(DEMO_RequestData, Basic, NULL);

    return UT_GenStub_GetReturnValue(DEMO_RequestData, int32_t);
}
