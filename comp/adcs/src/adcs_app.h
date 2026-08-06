#ifndef _ADCS_APP_H_
#define _ADCS_APP_H_

/*
** Include Files
*/
#include "cfe.h"
#include "adcs_device.h"
#include "adcs_events.h"
#include "adcs_perfids.h"
#include "adcs_msg.h"
#include "adcs_msgids.h"
#include "adcs_version.h"
#include "hwlib.h"

/*
** Specified pipe depth - how many messages will be queued in the pipe
*/
#define ADCS_PIPE_DEPTH 32

/*
** Enabled and Disabled Definitions
*/
#define ADCS_DEVICE_DISABLED 0
#define ADCS_DEVICE_ENABLED  1

/*
** ADCS global data structure
** The cFE convention is to put all global app data in a single struct.
** This struct is defined in the `adcs_app.h` file with one global instance
** in the `.c` file.
*/
typedef struct
{
    /*
    ** Housekeeping telemetry packet
    ** Each app defines its own packet which contains its OWN telemetry
    */
    ADCS_Hk_tlm_t HkTelemetryPkt; /* ADCS Housekeeping Telemetry Packet */

    /*
    ** Operational data  - not reported in housekeeping
    */
    CFE_MSG_Message_t *MsgPtr;    /* Pointer to msg received on software bus */
    CFE_SB_PipeId_t    CmdPipe;   /* Pipe Id for HK command pipe */
    uint32             RunStatus; /* App run status for controlling the application state */

    /*
     ** Device data
     */
    ADCS_Device_tlm_t DevicePkt; /* Device specific data packet */

    /*
    ** Device protocol
    */
    uart_info_t AdcsUart; /* Hardware protocol definition */

} ADCS_AppData_t;

/*
** Exported Data
** Extern the global struct in the header for the Unit Test Framework (UTF).
*/
extern ADCS_AppData_t ADCS_AppData; /* ADCS App Data */

/*
**
** Local function prototypes.
**
** Note: Except for the entry point (ADCS_AppMain), these
**       functions are not called from any other source module.
*/
void  ADCS_AppMain(void);
int32 ADCS_AppInit(void);
void  ADCS_ProcessCommandPacket(void);
void  ADCS_ProcessGroundCommand(void);
void  ADCS_ProcessTelemetryRequest(void);
void  ADCS_ReportHousekeeping(void);
void  ADCS_ReportDeviceTelemetry(void);
void  ADCS_ResetCounters(void);
void  ADCS_Enable(void);
void  ADCS_Disable(void);
int32 ADCS_VerifyCmdLength(CFE_MSG_Message_t *msg, uint16 expected_length);

#endif /* _ADCS_APP_H_ */
