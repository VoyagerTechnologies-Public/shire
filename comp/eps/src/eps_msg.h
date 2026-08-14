#ifndef _EPS_MSG_H_
#define _EPS_MSG_H_

#include "cfe.h"
#include "eps_device.h"

/*
** Ground Command Codes
*/
#define EPS_NOOP_CC           0
#define EPS_RESET_COUNTERS_CC 1
#define EPS_SWITCH_OFF_CC     2
#define EPS_SWITCH_ON_CC      3

/*
** Telemetry Request Command Codes
*/
#define EPS_REQ_HK_TLM   0

/*
** Generic "no arguments" command type definition
*/
typedef struct
{
    CFE_MSG_CommandHeader_t CmdHeader;

} EPS_NoArgs_cmd_t;

/*
** EPS switch control command
*/
typedef struct
{
    CFE_MSG_CommandHeader_t CmdHeader;
    uint8                   SwitchNumber;

} EPS_Switch_cmd_t;

/*
** EPS housekeeping type definition
*/
typedef struct
{
    CFE_MSG_TelemetryHeader_t TlmHeader;
    uint8                     CommandErrorCount;
    uint16                    CommandCount;
    uint8                     DeviceErrorCount;
    uint16                    DeviceCount;
    EPS_Device_HK_tlm_t       DeviceHK;

} __attribute__((packed)) EPS_Hk_tlm_t;
#define EPS_HK_TLM_LNGTH sizeof(EPS_Hk_tlm_t)

#endif /* _EPS_MSG_H_ */
