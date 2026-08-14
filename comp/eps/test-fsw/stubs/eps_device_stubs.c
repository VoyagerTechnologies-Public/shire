#include "utgenstub.h"
#include "eps_device.h"

/* Stubs updated to match the EPS device I2C API used by the component */

int32_t EPS_InitDevice(i2c_bus_info_t *device)
{
    UT_GenStub_SetupReturnBuffer(EPS_InitDevice, int32_t);

    UT_GenStub_AddParam(EPS_InitDevice, i2c_bus_info_t *, device);

    UT_GenStub_Execute(EPS_InitDevice, Basic, NULL);

    return UT_GenStub_GetReturnValue(EPS_InitDevice, int32_t);
}

int32_t EPS_CommandDevice(i2c_bus_info_t *device, uint8_t cmd, uint8_t payload)
{
    UT_GenStub_SetupReturnBuffer(EPS_CommandDevice, int32_t);

    UT_GenStub_AddParam(EPS_CommandDevice, i2c_bus_info_t *, device);
    UT_GenStub_AddParam(EPS_CommandDevice, uint8_t, cmd);
    UT_GenStub_AddParam(EPS_CommandDevice, uint8_t, payload);

    UT_GenStub_Execute(EPS_CommandDevice, Basic, NULL);

    return UT_GenStub_GetReturnValue(EPS_CommandDevice, int32_t);
}

int32_t EPS_RequestHK(i2c_bus_info_t *device, EPS_Device_HK_tlm_t *data)
{
    UT_GenStub_SetupReturnBuffer(EPS_RequestHK, int32_t);

    UT_GenStub_AddParam(EPS_RequestHK, i2c_bus_info_t *, device);
    UT_GenStub_AddParam(EPS_RequestHK, EPS_Device_HK_tlm_t *, data);

    UT_GenStub_Execute(EPS_RequestHK, Basic, NULL);

    return UT_GenStub_GetReturnValue(EPS_RequestHK, int32_t);
}

int32_t EPS_SetSwitch(i2c_bus_info_t *device, uint8_t switch_num, bool state)
{
    UT_GenStub_SetupReturnBuffer(EPS_SetSwitch, int32_t);

    UT_GenStub_AddParam(EPS_SetSwitch, i2c_bus_info_t *, device);
    UT_GenStub_AddParam(EPS_SetSwitch, uint8_t, switch_num);
    UT_GenStub_AddParam(EPS_SetSwitch, bool, state);

    UT_GenStub_Execute(EPS_SetSwitch, Basic, NULL);

    return UT_GenStub_GetReturnValue(EPS_SetSwitch, int32_t);
}
