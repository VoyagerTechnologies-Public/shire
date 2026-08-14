#ifndef _ADCS_MSG_H_
#define _ADCS_MSG_H_

#include "cfe.h"
#include "adcs_device.h"

/*
** Ground Command Codes
*/
#define ADCS_NOOP_CC           0
#define ADCS_RESET_COUNTERS_CC 1
#define ADCS_ENABLE_CC         2
#define ADCS_DISABLE_CC        3
#define ADCS_CONFIG_CC         4
#define ADCS_SET_MODE_CC       5
#define ADCS_SET_TARGET_CC     6

/*
** Telemetry Request Command Codes
*/
#define ADCS_REQ_HK_TLM   0
#define ADCS_REQ_CSS_TLM  1
#define ADCS_REQ_FSS_TLM  2
#define ADCS_REQ_GPS_TLM  3
#define ADCS_REQ_IMU_TLM  4
#define ADCS_REQ_MAG_TLM  5
#define ADCS_REQ_MTB_TLM  6
#define ADCS_REQ_RW_TLM   7
#define ADCS_REQ_ST_TLM   8

/*
** Generic "no arguments" command type definition
*/
typedef struct
{
    CFE_MSG_CommandHeader_t CmdHeader;

} ADCS_NoArgs_cmd_t;

/* Set Mode command (payload: uint16 mode) */
typedef struct
{
    CFE_MSG_CommandHeader_t CmdHeader;
    uint16                  Mode;

} ADCS_SetMode_cmd_t;

/* Set Target command (payload: uint16 target id/value) */
typedef struct
{
    CFE_MSG_CommandHeader_t CmdHeader;
    uint16                  Target;

} ADCS_SetTarget_cmd_t;

/* Get CSS / sensor data command has no additional args */

/*
** ADCS device telemetry definition
*/
typedef struct
{
    CFE_MSG_TelemetryHeader_t TlmHeader;
    ADCS_Device_Data_tlm_t    Adcs;

} __attribute__((packed)) ADCS_Device_tlm_t;
#define ADCS_DEVICE_TLM_LNGTH sizeof(ADCS_Device_tlm_t)

/*
** ADCS housekeeping type definition
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
    ADCS_Device_HK_tlm_t      DeviceHK;

} __attribute__((packed)) ADCS_Hk_tlm_t;
#define ADCS_HK_TLM_LNGTH sizeof(ADCS_Hk_tlm_t)

#endif /* _ADCS_MSG_H_ */
