#ifndef _DEMO_MSG_H_
#define _DEMO_MSG_H_

#include "cfe.h"
#include "demo_device.h"

/*
** Ground Command Codes
*/
#define DEMO_NOOP_CC           0
#define DEMO_RESET_COUNTERS_CC 1
#define DEMO_ENABLE_CC         2
#define DEMO_DISABLE_CC        3
#define DEMO_CONFIG_CC         4

/*
** Telemetry Request Command Codes
*/
#define DEMO_REQ_HK_TLM   0
#define DEMO_REQ_DATA_TLM 1

/*
** Generic "no arguments" command type definition
*/
typedef struct
{
    CFE_MSG_CommandHeader_t CmdHeader;

} DEMO_NoArgs_cmd_t;

/*
** DEMO write configuration command
*/
typedef struct
{
    CFE_MSG_CommandHeader_t CmdHeader;
    uint16                  DeviceCfg;

} DEMO_Config_cmd_t;

/*
** DEMO device telemetry definition
*/
typedef struct
{
    CFE_MSG_TelemetryHeader_t TlmHeader;
    DEMO_Device_Data_tlm_t    Demo;

} __attribute__((packed)) DEMO_Device_tlm_t;
#define DEMO_DEVICE_TLM_LNGTH sizeof(DEMO_Device_tlm_t)

/*
** DEMO housekeeping type definition
*/
typedef struct
{
    CFE_MSG_TelemetryHeader_t TlmHeader;
    uint8                     CommandErrorCount;
    uint16                    CommandCount;
    uint8                     DeviceErrorCount;
    uint16                    DeviceCount;

    /*
    ** Edit and add specific telemetry values to this struct
    */
    uint8                     DeviceEnabled;
    DEMO_Device_HK_tlm_t      DeviceHK;

} __attribute__((packed)) DEMO_Hk_tlm_t;
#define DEMO_HK_TLM_LNGTH sizeof(DEMO_Hk_tlm_t)

#endif /* _DEMO_MSG_H_ */
