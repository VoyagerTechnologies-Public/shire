#ifndef _DEMO_APP_H_
#define _DEMO_APP_H_

/*
** Include Files
*/
#include "cfe.h"
#include "demo_device.h"
#include "demo_events.h"
#include "demo_perfids.h"
#include "demo_msg.h"
#include "demo_msgids.h"
#include "demo_version.h"
#include "hwlib.h"

/*
** Specified pipe depth - how many messages will be queued in the pipe
*/
#define DEMO_PIPE_DEPTH 32

/*
** Enabled and Disabled Definitions
*/
#define DEMO_DEVICE_DISABLED 0
#define DEMO_DEVICE_ENABLED  1

/*
** DEMO global data structure
** The cFE convention is to put all global app data in a single struct.
** This struct is defined in the `demo_app.h` file with one global instance
** in the `.c` file.
*/
typedef struct
{
    /*
    ** Housekeeping telemetry packet
    ** Each app defines its own packet which contains its OWN telemetry
    */
    DEMO_Hk_tlm_t HkTelemetryPkt; /* DEMO Housekeeping Telemetry Packet */

    /*
    ** Operational data  - not reported in housekeeping
    */
    CFE_MSG_Message_t *MsgPtr;    /* Pointer to msg received on software bus */
    CFE_SB_PipeId_t    CmdPipe;   /* Pipe Id for HK command pipe */
    uint32             RunStatus; /* App run status for controlling the application state */

    /*
     ** Device data
     */
    DEMO_Device_tlm_t DevicePkt; /* Device specific data packet */

    /*
    ** Device protocol
    */
    uart_info_t DemoUart; /* Hardware protocol definition */

} DEMO_AppData_t;

/*
** Exported Data
** Extern the global struct in the header for the Unit Test Framework (UTF).
*/
extern DEMO_AppData_t DEMO_AppData; /* DEMO App Data */

/*
**
** Local function prototypes.
**
** Note: Except for the entry point (DEMO_AppMain), these
**       functions are not called from any other source module.
*/
void  DEMO_AppMain(void);
int32 DEMO_AppInit(void);
void  DEMO_ProcessCommandPacket(void);
void  DEMO_ProcessGroundCommand(void);
void  DEMO_ProcessTelemetryRequest(void);
void  DEMO_ReportHousekeeping(void);
void  DEMO_ReportDeviceTelemetry(void);
void  DEMO_ResetCounters(void);
void  DEMO_Enable(void);
void  DEMO_Disable(void);
void  DEMO_Configure(void);
int32 DEMO_VerifyCmdLength(CFE_MSG_Message_t *msg, uint16 expected_length);

#endif /* _DEMO_APP_H_ */
