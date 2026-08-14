#include "utgenstub.h"
#include "adcs_device.h"

int32_t ADCS_ReadData(uart_info_t *device, uint8_t *read_data, uint8_t data_length)
{
    UT_GenStub_SetupReturnBuffer(ADCS_ReadData, int32_t);

    UT_GenStub_AddParam(ADCS_ReadData, uart_info_t *, device);
    UT_GenStub_AddParam(ADCS_ReadData, uint8_t *, read_data);
    UT_GenStub_AddParam(ADCS_ReadData, uint8_t, data_length);

    UT_GenStub_Execute(ADCS_ReadData, Basic, NULL);

    return UT_GenStub_GetReturnValue(ADCS_ReadData, int32_t);
}

int32_t ADCS_CommandDevice(uart_info_t *device, uint16_t cmd, uint16_t payload)
{
    UT_GenStub_SetupReturnBuffer(ADCS_CommandDevice, int32_t);

    UT_GenStub_AddParam(ADCS_CommandDevice, uart_info_t *, device);
    UT_GenStub_AddParam(ADCS_CommandDevice, uint8_t, cmd);
    UT_GenStub_AddParam(ADCS_CommandDevice, uint32_t, payload);

    UT_GenStub_Execute(ADCS_CommandDevice, Basic, NULL);

    return UT_GenStub_GetReturnValue(ADCS_CommandDevice, int32_t);
}

int32_t ADCS_RequestHK(uart_info_t *device, ADCS_Device_HK_tlm_t *data)
{
    UT_GenStub_SetupReturnBuffer(ADCS_RequestHK, int32_t);

    UT_GenStub_AddParam(ADCS_RequestHK, uart_info_t *, device);
    UT_GenStub_AddParam(ADCS_RequestHK, ADCS_Device_HK_tlm_t *, data);

    UT_GenStub_Execute(ADCS_RequestHK, Basic, NULL);

    return UT_GenStub_GetReturnValue(ADCS_RequestHK, int32_t);
}

int32_t ADCS_RequestData(uart_info_t *device, ADCS_Device_Data_tlm_t *data, uint16_t data_cmd)
{
    UT_GenStub_SetupReturnBuffer(ADCS_RequestData, int32_t);

    UT_GenStub_AddParam(ADCS_RequestData, uart_info_t *, device);
    UT_GenStub_AddParam(ADCS_RequestData, ADCS_Device_Data_tlm_t *, data);
    UT_GenStub_AddParam(ADCS_RequestData, uint16_t, data_cmd);

    UT_GenStub_Execute(ADCS_RequestData, Basic, NULL);

    return UT_GenStub_GetReturnValue(ADCS_RequestData, int32_t);
}
