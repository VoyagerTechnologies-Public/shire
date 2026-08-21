#ifndef _RADIO_MSG_H_
#define _RADIO_MSG_H_

#include "cfe.h"
#include "radio_device.h"

/*
** Ground Command Codes
*/
#define RADIO_NOOP_CC           0
#define RADIO_RESET_COUNTERS_CC 1
#define RADIO_ENABLE_CC         2
#define RADIO_DISABLE_CC        3
#define RADIO_CONFIG_CC         4
#define RADIO_SERVICE_CC        5

/*
** Telemetry Request Command Codes
*/
#define RADIO_REQ_HK_TLM   0
#define RADIO_REQ_DATA_TLM 1

/*
** Generic "no arguments" command type definition
*/
typedef struct
{
    CFE_MSG_CommandHeader_t CmdHeader;

} RADIO_NoArgs_cmd_t;

/*
** RADIO write configuration command
*/
typedef struct
{
    CFE_MSG_CommandHeader_t CmdHeader;
    RADIO_Device_Config_t   DeviceCfg;

} RADIO_Config_cmd_t;

/*
** RADIO housekeeping type definition
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
    RADIO_Device_HK_tlm_t      DeviceHK;

} __attribute__((packed)) RADIO_Hk_tlm_t;
#define RADIO_HK_TLM_LNGTH sizeof(RADIO_Hk_tlm_t)

#endif /* _RADIO_MSG_H_ */
