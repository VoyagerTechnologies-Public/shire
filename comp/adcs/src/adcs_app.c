#include "adcs_app.h"
#include <string.h>

ADCS_AppData_t ADCS_AppData;

/*
** Application entry point and main process loop
*/
void ADCS_AppMain(void)
{
    int32 status = OS_SUCCESS;

    /*
    ** Create the first performance log entry
    */
    CFE_ES_PerfLogEntry(ADCS_PERF_ID);

    /*
    ** Perform application initialization
    */
    status = ADCS_AppInit();
    if (status != CFE_SUCCESS)
    {
        ADCS_AppData.RunStatus = CFE_ES_RunStatus_APP_ERROR;
    }

    /*
    ** Main loop
    */
    while (CFE_ES_RunLoop(&ADCS_AppData.RunStatus) == true)
    {
        /*
        ** Performance log exit stamp
        */
        CFE_ES_PerfLogExit(ADCS_PERF_ID);

        /*
        ** Pend on the arrival of the next software bus message
        ** Note that this is the standard, but timeouts are available
        */
        status = CFE_SB_ReceiveBuffer((CFE_SB_Buffer_t **)&ADCS_AppData.MsgPtr, ADCS_AppData.CmdPipe, CFE_SB_PEND_FOREVER);

        /*
        ** Begin performance metrics on anything after this line.
        */
        CFE_ES_PerfLogEntry(ADCS_PERF_ID);

        /*
        ** If the CFE_SB_ReceiveBuffer was successful, then continue to process the command packet
        ** If not, then exit the application in error.
        ** Note that a SB read error should not always result in an app quitting.
        */
        if (status == CFE_SUCCESS)
        {
            ADCS_ProcessCommandPacket();
        }
        else
        {
            CFE_EVS_SendEvent(ADCS_PIPE_ERR_EID, CFE_EVS_EventType_ERROR, "ADCS: SB Pipe Read Error = %d",
                              (int)status);
            ADCS_AppData.RunStatus = CFE_ES_RunStatus_APP_ERROR;
        }
    }

    /*
    ** Disable component, which cleans up the interface, upon exit
    */
    ADCS_Disable();

    /*
    ** Performance log exit stamp
    */
    CFE_ES_PerfLogExit(ADCS_PERF_ID);

    /*
    ** Exit the application
    */
    CFE_ES_ExitApp(ADCS_AppData.RunStatus);
}

/*
** Initialize application
*/
int32 ADCS_AppInit(void)
{
    int32 status = OS_SUCCESS;

    ADCS_AppData.RunStatus = CFE_ES_RunStatus_APP_RUN;

    /*
    ** Register the events
    */
    status = CFE_EVS_Register(NULL, 0, CFE_EVS_EventFilter_BINARY); /* as default, no filters are used */
    if (status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("ADCS: Error registering for event services: 0x%08X\n", (unsigned int)status);
        return status;
    }

    /*
    ** Create the Software Bus command pipe
    */
    status = CFE_SB_CreatePipe(&ADCS_AppData.CmdPipe, ADCS_PIPE_DEPTH, "ADCS_CMD_PIPE");
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(ADCS_PIPE_ERR_EID, CFE_EVS_EventType_ERROR, "Error Creating SB Pipe,RC=0x%08X",
                          (unsigned int)status);
        return status;
    }

    /*
    ** Subscribe to ground commands
    */
    status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(ADCS_CMD_MID), ADCS_AppData.CmdPipe);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(ADCS_SUB_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Subscribing to HK Gnd Cmds, MID=0x%04X, RC=0x%08X", ADCS_CMD_MID,
                          (unsigned int)status);
        return status;
    }

    /*
    ** Subscribe to housekeeping (hk) message requests
    */
    status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(ADCS_REQ_HK_MID), ADCS_AppData.CmdPipe);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(ADCS_SUB_REQ_HK_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Subscribing to HK Request, MID=0x%04X, RC=0x%08X", ADCS_REQ_HK_MID,
                          (unsigned int)status);
        return status;
    }

    /*
    ** Initialize the published HK message - this HK message will contain the
    ** telemetry that has been defined in the ADCS_Hk_tlm_t for this app.
    */
    CFE_MSG_Init(CFE_MSG_PTR(ADCS_AppData.HkTelemetryPkt.TlmHeader), CFE_SB_ValueToMsgId(ADCS_HK_TLM_MID),
                 ADCS_HK_TLM_LNGTH);

    /*
    ** Initialize the device packet message
    ** This packet is specific to your application
    */
    /* Device telemetry uses the CSS telemetry MID for device frames */
    CFE_MSG_Init(CFE_MSG_PTR(ADCS_AppData.DevicePkt.TlmHeader), CFE_SB_ValueToMsgId(ADCS_CSS_TLM_MID),
                 ADCS_DEVICE_TLM_LNGTH);

    /*
    ** Reset all counters during application initialization
    */
    ADCS_ResetCounters();

    /*
    ** Initialize application data
    ** Note that counters are excluded as they were reset in the previous code block
    */
    ADCS_AppData.HkTelemetryPkt.DeviceErrorCount = 0;
    ADCS_AppData.HkTelemetryPkt.DeviceCount      = 0;
    ADCS_AppData.HkTelemetryPkt.DeviceEnabled    = ADCS_DEVICE_DISABLED;

    /*
    ** Send an information event that the app has initialized.
    ** This is useful for debugging the loading of individual applications.
    */
    status = CFE_EVS_SendEvent(ADCS_STARTUP_INF_EID, CFE_EVS_EventType_INFORMATION,
                               "ADCS App Initialized. Version %d.%d.%d.%d", ADCS_MAJOR_VERSION,
                               ADCS_MINOR_VERSION, ADCS_REVISION, ADCS_MISSION_REV);
    if (status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("ADCS: Error sending initialization event: 0x%08X\n", (unsigned int)status);
    }
    return status;
}

/*
** Process packets received on the ADCS command pipe
*/
void ADCS_ProcessCommandPacket(void)
{
    CFE_SB_MsgId_t MsgId = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_GetMsgId(ADCS_AppData.MsgPtr, &MsgId);
    switch (CFE_SB_MsgIdToValue(MsgId))
    {
        /*
        ** Ground Commands with command codes fall under the ADCS_CMD_MID (Message ID)
        */
        case ADCS_CMD_MID:
            ADCS_ProcessGroundCommand();
            break;

        /*
        ** Housekeeping requests with command codes fall under the ADCS_REQ_HK_MID (Message ID)
        */
        case ADCS_REQ_HK_MID:
            ADCS_ProcessTelemetryRequest();
            break;

        /*
        ** All other invalid messages that this app doesn't recognize,
        ** increment the command error counter and log as an error event.
        */
        default:
            /* Increment the command error counter upon receipt of an invalid command packet */
            ADCS_AppData.HkTelemetryPkt.CommandErrorCount++;

            /* Send event failure to the console*/
            CFE_EVS_SendEvent(ADCS_PROCESS_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                              "ADCS: Invalid command packet, MID = 0x%x", CFE_SB_MsgIdToValue(MsgId));
            break;
    }
    return;
}

/*
** Process ground commands
*/
void ADCS_ProcessGroundCommand(void)
{
    CFE_SB_MsgId_t    MsgId       = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t CommandCode = 0;

    /*
    ** MsgId is only needed if the command code is not recognized. See default case
    */
    CFE_MSG_GetMsgId(ADCS_AppData.MsgPtr, &MsgId);

    /*
    ** Ground Commands have a command code (_CC) associated with them
    ** Pull this command code from the message and then process
    */
    CFE_MSG_GetFcnCode(ADCS_AppData.MsgPtr, &CommandCode);
    switch (CommandCode)
    {
        /*
        ** NOOP Command
        */
        case ADCS_NOOP_CC:
            /*
            ** Verify the command length immediately after CC identification
            */
            if (ADCS_VerifyCmdLength(ADCS_AppData.MsgPtr, sizeof(ADCS_NoArgs_cmd_t)) == OS_SUCCESS)
            {
#ifdef ADCS_CFG_DEBUG
                OS_printf("ADCS: ADCS_NOOP_CC received \n");
#endif

                /* Do any necessary checks, none for a NOOP */

                /* Increment command success or error counter, NOOP can only be successful */
                ADCS_AppData.HkTelemetryPkt.CommandCount++;

                /* Do the action, none for a NOOP */

                /* Increment device success or error counter, none for NOOP as application only */

                /* Send event success or failure to the console, NOOP can only be successful */
                CFE_EVS_SendEvent(ADCS_CMD_NOOP_INF_EID, CFE_EVS_EventType_INFORMATION,
                                  "ADCS: NOOP command received");
            }
            break;

        /*
        ** Reset Counters Command
        */
        case ADCS_RESET_COUNTERS_CC:
            if (ADCS_VerifyCmdLength(ADCS_AppData.MsgPtr, sizeof(ADCS_NoArgs_cmd_t)) == OS_SUCCESS)
            {
#ifdef ADCS_CFG_DEBUG
                OS_printf("ADCS: ADCS_RESET_COUNTERS_CC received \n");
#endif
                ADCS_ResetCounters();
            }
            break;

        /*
        ** Enable Command
        */
        case ADCS_ENABLE_CC:
            if (ADCS_VerifyCmdLength(ADCS_AppData.MsgPtr, sizeof(ADCS_NoArgs_cmd_t)) == OS_SUCCESS)
            {
#ifdef ADCS_CFG_DEBUG
                OS_printf("ADCS: ADCS_ENABLE_CC received \n");
#endif
                ADCS_Enable();
            }
            break;

        /*
        ** Disable Command
        */
        case ADCS_DISABLE_CC:
            if (ADCS_VerifyCmdLength(ADCS_AppData.MsgPtr, sizeof(ADCS_NoArgs_cmd_t)) == OS_SUCCESS)
            {
#ifdef ADCS_CFG_DEBUG
                OS_printf("ADCS: ADCS_DISABLE_CC received \n");
#endif
                ADCS_Disable();
            }
            break;

        case ADCS_SET_MODE_CC:
            if (ADCS_VerifyCmdLength(ADCS_AppData.MsgPtr, sizeof(ADCS_SetMode_cmd_t)) == OS_SUCCESS)
            {
                /* Check that device is enabled */
                if (ADCS_AppData.HkTelemetryPkt.DeviceEnabled == ADCS_DEVICE_ENABLED)
                {
                    ADCS_SetMode_cmd_t cmd_buf;
                    memcpy(&cmd_buf, ADCS_AppData.MsgPtr, sizeof(cmd_buf));
                    ADCS_SetMode_cmd_t *cmd = &cmd_buf;
                    uint16 mode = cmd->Mode;
                    int32 status = ADCS_CommandDevice(&ADCS_AppData.AdcsUart, ADCS_DEVICE_SET_MODE_CMD, mode);
                    if (status == OS_SUCCESS)
                    {
                        ADCS_AppData.HkTelemetryPkt.CommandCount++;
                        CFE_EVS_SendEvent(ADCS_SET_MODE_INF_EID, CFE_EVS_EventType_INFORMATION,
                                        "ADCS: Set mode command forwarded to device (mode=%u)", mode);
                    }
                    else
                    {
                        ADCS_AppData.HkTelemetryPkt.CommandErrorCount++;
                        CFE_EVS_SendEvent(ADCS_SET_MODE_ERR_EID, CFE_EVS_EventType_ERROR,
                                        "ADCS: Set mode device command failed: %d", status);
                    }
                }
                else
                {
                    /* Increment command error count */
                    ADCS_AppData.HkTelemetryPkt.CommandErrorCount++;

                    /* Send command event failure to the console */
                    CFE_EVS_SendEvent(ADCS_CMD_DISABLED_ERR_EID, CFE_EVS_EventType_ERROR,
                                      "ADCS: Set mode command failed, device not enabled");
                }
            }
            break;

        case ADCS_SET_TARGET_CC:
            if (ADCS_VerifyCmdLength(ADCS_AppData.MsgPtr, sizeof(ADCS_SetTarget_cmd_t)) == OS_SUCCESS)
            {
                /* Check that device is enabled */
                if (ADCS_AppData.HkTelemetryPkt.DeviceEnabled == ADCS_DEVICE_ENABLED)
                {
                    ADCS_SetTarget_cmd_t tcmd_buf;
                    memcpy(&tcmd_buf, ADCS_AppData.MsgPtr, sizeof(tcmd_buf));
                    ADCS_SetTarget_cmd_t *tcmd = &tcmd_buf;
                    uint16 target = tcmd->Target;
                    int32 status = ADCS_CommandDevice(&ADCS_AppData.AdcsUart, ADCS_DEVICE_SET_TARGET_CMD, target);
                    if (status == OS_SUCCESS)
                    {
                        ADCS_AppData.HkTelemetryPkt.CommandCount++;
                        CFE_EVS_SendEvent(ADCS_SET_TARGET_INF_EID, CFE_EVS_EventType_INFORMATION,
                                        "ADCS: Set target command forwarded to device (target=%u)", target);
                    }
                    else
                    {
                        ADCS_AppData.HkTelemetryPkt.CommandErrorCount++;
                        CFE_EVS_SendEvent(ADCS_SET_TARGET_ERR_EID, CFE_EVS_EventType_ERROR,
                                        "ADCS: Set target device command failed: %d", status);
                    }
                }
                else
                {
                    /* Increment command error count */
                    ADCS_AppData.HkTelemetryPkt.CommandErrorCount++;

                    /* Send command event failure to the console */
                    CFE_EVS_SendEvent(ADCS_CMD_DISABLED_ERR_EID, CFE_EVS_EventType_ERROR,
                                      "ADCS: Set mode command failed, device not enabled");
                }
            }
            break;

        /*
        ** Invalid Command Codes
        */
        default:
            /* Increment the command error counter upon receipt of an invalid command */
            ADCS_AppData.HkTelemetryPkt.CommandErrorCount++;

            /* Send invalid command code failure to the console */
            CFE_EVS_SendEvent(ADCS_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                              "ADCS: Invalid command code for packet, MID = 0x%x, cmdCode = 0x%x",
                              CFE_SB_MsgIdToValue(MsgId), CommandCode);
            break;
    }
    return;
}

/*
** Process Telemetry Request - Triggered in response to a telemetry request
*/
void ADCS_ProcessTelemetryRequest(void)
{
    CFE_SB_MsgId_t    MsgId       = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t CommandCode = 0;

    /* MsgId is only needed if the command code is not recognized. See default case */
    CFE_MSG_GetMsgId(ADCS_AppData.MsgPtr, &MsgId);

    /* Pull this command code from the message and then process */
    CFE_MSG_GetFcnCode(ADCS_AppData.MsgPtr, &CommandCode);
    switch (CommandCode)
    {
        case ADCS_REQ_HK_TLM:
            ADCS_ReportHousekeeping();
            break;

        case ADCS_REQ_CSS_TLM:
            ADCS_ReportDeviceTelemetry();
            break;

        case ADCS_REQ_FSS_TLM:
            ADCS_ReportDeviceTelemetry();
            break;
        
        case ADCS_REQ_GPS_TLM:
            ADCS_ReportDeviceTelemetry();
            break;

        case ADCS_REQ_IMU_TLM:
            ADCS_ReportDeviceTelemetry();
            break;

        case ADCS_REQ_MAG_TLM:
            ADCS_ReportDeviceTelemetry();
            break;

        case ADCS_REQ_MTB_TLM:
            ADCS_ReportDeviceTelemetry();
            break;

        case ADCS_REQ_RW_TLM:
            ADCS_ReportDeviceTelemetry();
            break;

        case ADCS_REQ_ST_TLM:
            ADCS_ReportDeviceTelemetry();
            break;

        /*
        ** Invalid Command Codes
        */
        default:
            /* Increment the error counter upon receipt of an invalid command */
            ADCS_AppData.HkTelemetryPkt.CommandErrorCount++;

            /* Send invalid command code failure to the console */
            CFE_EVS_SendEvent(ADCS_DEVICE_TLM_ERR_EID, CFE_EVS_EventType_ERROR,
                              "ADCS: Invalid command code for packet, MID = 0x%x, cmdCode = 0x%x",
                              CFE_SB_MsgIdToValue(MsgId), CommandCode);
            break;
    }
    return;
}

/*
** Report Application Housekeeping
*/
void ADCS_ReportHousekeeping(void)
{
    int32 status = OS_SUCCESS;

    /* Check that device is enabled */
    if (ADCS_AppData.HkTelemetryPkt.DeviceEnabled == ADCS_DEVICE_ENABLED)
    {
        status = ADCS_RequestHK(&ADCS_AppData.AdcsUart,
                                  (ADCS_Device_HK_tlm_t *)&ADCS_AppData.HkTelemetryPkt.DeviceHK);
        if (status == OS_SUCCESS)
        {
            ADCS_AppData.HkTelemetryPkt.DeviceCount++;
        }
        else
        {
            ADCS_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(ADCS_REQ_HK_ERR_EID, CFE_EVS_EventType_ERROR,
                              "ADCS: Request device HK reported error %d", status);
        }
    }
    else
    {
        /* Clear DeviceHK when no live data is available */
        memset(&ADCS_AppData.HkTelemetryPkt.DeviceHK, 0, sizeof(ADCS_Device_HK_tlm_t));
    }
    /* Intentionally do not report errors if disabled */

    /* Time stamp and publish housekeeping telemetry */
    CFE_SB_TimeStampMsg((CFE_MSG_Message_t *)&ADCS_AppData.HkTelemetryPkt);
    CFE_SB_TransmitMsg((CFE_MSG_Message_t *)&ADCS_AppData.HkTelemetryPkt, true);
    return;
}

/*
** Collect and report device telemetry
*/
void ADCS_ReportDeviceTelemetry(void)
{
    int32 status = OS_SUCCESS;

    /* Check that device is enabled */
    if (ADCS_AppData.HkTelemetryPkt.DeviceEnabled == ADCS_DEVICE_ENABLED)
    {
    status = ADCS_RequestData(&ADCS_AppData.AdcsUart,
                    (ADCS_Device_Data_tlm_t *)&ADCS_AppData.DevicePkt.Adcs,
                    ADCS_DEVICE_GET_CSS_CMD);
        if (status == OS_SUCCESS)
        {
            /* Update packet count */
            ADCS_AppData.HkTelemetryPkt.DeviceCount++;

            /* Time stamp and publish data telemetry */
            CFE_SB_TimeStampMsg((CFE_MSG_Message_t *)&ADCS_AppData.DevicePkt);
            CFE_SB_TransmitMsg((CFE_MSG_Message_t *)&ADCS_AppData.DevicePkt, true);
        }
        else
        {
            ADCS_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(ADCS_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                              "ADCS: Request device data reported error %d", status);
        }
    }
    /* Intentionally do not report errors if device disabled */
    return;
}

/*
** Reset all global counter variables
*/
void ADCS_ResetCounters(void)
{
    /* Do any necessary checks, none for reset counters */

    /* Increment command success or error counter, omitted as action is to reset */

    /* Do the action, clear all global counter variables */
    ADCS_AppData.HkTelemetryPkt.CommandErrorCount = 0;
    ADCS_AppData.HkTelemetryPkt.CommandCount      = 0;
    ADCS_AppData.HkTelemetryPkt.DeviceErrorCount  = 0;
    ADCS_AppData.HkTelemetryPkt.DeviceCount       = 0;

    /* Increment device success or error counter, none as application only */

    /* Send event success to the console */
    CFE_EVS_SendEvent(ADCS_CMD_RESET_INF_EID, CFE_EVS_EventType_INFORMATION,
                      "ADCS: RESET counters command received");
    return;
}

/*
** Enable component
*/
void ADCS_Enable(void)
{
    int32 status = OS_SUCCESS;

    /* Do any necessary checks, confirm that device is currently disabled */
    if (ADCS_AppData.HkTelemetryPkt.DeviceEnabled == ADCS_DEVICE_DISABLED)
    {
        /* Increment command success counter */
        ADCS_AppData.HkTelemetryPkt.CommandCount++;

        /*
        ** Do the action, initialize hardware interface and set enabled
        */
        ADCS_AppData.AdcsUart.deviceString  = ADCS_CFG_STRING;
        ADCS_AppData.AdcsUart.handle        = ADCS_CFG_HANDLE;
        ADCS_AppData.AdcsUart.isOpen        = PORT_CLOSED;
        ADCS_AppData.AdcsUart.baud          = ADCS_CFG_BAUDRATE_HZ;
        ADCS_AppData.AdcsUart.access_option = uart_access_flag_RDWR;

        status = uart_init_port(&ADCS_AppData.AdcsUart);
        if (status == OS_SUCCESS)
        {
            ADCS_AppData.HkTelemetryPkt.DeviceEnabled = ADCS_DEVICE_ENABLED;

            /* Increment device success counter */
            ADCS_AppData.HkTelemetryPkt.DeviceCount++;

            /* Send device event success to the console */
            CFE_EVS_SendEvent(ADCS_ENABLE_INF_EID, CFE_EVS_EventType_INFORMATION,
                              "ADCS: Device enabled successfully");
        }
        else
        {
            /* Increment device error counter */
            ADCS_AppData.HkTelemetryPkt.DeviceErrorCount++;

            /* Send device event failure to the console */
            CFE_EVS_SendEvent(ADCS_UART_INIT_ERR_EID, CFE_EVS_EventType_ERROR,
                              "ADCS: Device UART port initialization error %d", status);
        }
    }
    else
    {
        /* Increment command error count */
        ADCS_AppData.HkTelemetryPkt.CommandErrorCount++;

        /* Send command event failure to the console */
        CFE_EVS_SendEvent(ADCS_ENABLE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ADCS: Device enable failed, already enabled");
    }
    return;
}

/*
** Disable component
*/
void ADCS_Disable(void)
{
    int32 status = OS_SUCCESS;

    /* Do any necessary checks, confirm that device is currently enabled */
    if (ADCS_AppData.HkTelemetryPkt.DeviceEnabled == ADCS_DEVICE_ENABLED)
    {
        /* Increment command success counter */
        ADCS_AppData.HkTelemetryPkt.CommandCount++;

        /*
        ** Do the action, close hardware interface and set disabled
        */
        status = uart_close_port(&ADCS_AppData.AdcsUart);
        if (status == OS_SUCCESS)
        {
            ADCS_AppData.HkTelemetryPkt.DeviceEnabled = ADCS_DEVICE_DISABLED;

            /* Increment device success counter */
            ADCS_AppData.HkTelemetryPkt.DeviceCount++;

            /* Send device event success to the console */
            CFE_EVS_SendEvent(ADCS_DISABLE_INF_EID, CFE_EVS_EventType_INFORMATION,
                              "ADCS: Device disabled successfully");
        }
        else
        {
            /* Increment device error counter */
            ADCS_AppData.HkTelemetryPkt.DeviceErrorCount++;

            /* Send device event failure to the console */
            CFE_EVS_SendEvent(ADCS_UART_CLOSE_ERR_EID, CFE_EVS_EventType_ERROR,
                              "ADCS: Device UART port close error %d", status);
        }
    }
    else
    {
        /* Increment command error count */
        ADCS_AppData.HkTelemetryPkt.CommandErrorCount++;

        /* Send command event failure to the console */
        CFE_EVS_SendEvent(ADCS_DISABLE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ADCS: Device disable failed, already disabled");
    }
    return;
}

/*
** Verify command packet length matches expected
*/
int32 ADCS_VerifyCmdLength(CFE_MSG_Message_t *msg, uint16 expected_length)
{
    int32             status        = OS_SUCCESS;
    CFE_SB_MsgId_t    msg_id        = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t cmd_code      = 0;
    size_t            actual_length = 0;

    CFE_MSG_GetSize(msg, &actual_length);
    if (expected_length != actual_length)
    {
        CFE_MSG_GetMsgId(msg, &msg_id);
        CFE_MSG_GetFcnCode(msg, &cmd_code);

        CFE_EVS_SendEvent(ADCS_LEN_ERR_EID, CFE_EVS_EventType_ERROR,
            "Invalid msg length: ID = 0x%X,  CC = %d, Len = %zu, Expected = %d",
            CFE_SB_MsgIdToValue(msg_id), cmd_code, actual_length, expected_length);

        status = OS_ERROR;

        /* Increment the command error counter upon receipt of an invalid command length */
        ADCS_AppData.HkTelemetryPkt.CommandErrorCount++;
    }
    return status;
}
