#include "demo_app.h"

DEMO_AppData_t DEMO_AppData;

/*
** Application entry point and main process loop
*/
void DEMO_AppMain(void)
{
    int32 status = OS_SUCCESS;

    /*
    ** Create the first performance log entry
    */
    CFE_ES_PerfLogEntry(DEMO_PERF_ID);

    /*
    ** Perform application initialization
    */
    status = DEMO_AppInit();
    if (status != CFE_SUCCESS)
    {
        DEMO_AppData.RunStatus = CFE_ES_RunStatus_APP_ERROR;
    }

    /*
    ** Main loop
    */
    while (CFE_ES_RunLoop(&DEMO_AppData.RunStatus) == true)
    {
        /*
        ** Performance log exit stamp
        */
        CFE_ES_PerfLogExit(DEMO_PERF_ID);

        /*
        ** Pend on the arrival of the next software bus message
        ** Note that this is the standard, but timeouts are available
        */
        status = CFE_SB_ReceiveBuffer((CFE_SB_Buffer_t **)&DEMO_AppData.MsgPtr, DEMO_AppData.CmdPipe, CFE_SB_PEND_FOREVER);

        /*
        ** Begin performance metrics on anything after this line.
        */
        CFE_ES_PerfLogEntry(DEMO_PERF_ID);

        /*
        ** If the CFE_SB_ReceiveBuffer was successful, then continue to process the command packet
        ** If not, then exit the application in error.
        ** Note that a SB read error should not always result in an app quitting.
        */
        if (status == CFE_SUCCESS)
        {
            DEMO_ProcessCommandPacket();
        }
        else
        {
            CFE_EVS_SendEvent(DEMO_PIPE_ERR_EID, CFE_EVS_EventType_ERROR, "DEMO: SB Pipe Read Error = %d",
                              (int)status);
            DEMO_AppData.RunStatus = CFE_ES_RunStatus_APP_ERROR;
        }
    }

    /*
    ** Disable component, which cleans up the interface, upon exit
    */
    DEMO_Disable();

    /*
    ** Performance log exit stamp
    */
    CFE_ES_PerfLogExit(DEMO_PERF_ID);

    /*
    ** Exit the application
    */
    CFE_ES_ExitApp(DEMO_AppData.RunStatus);
}

/*
** Initialize application
*/
int32 DEMO_AppInit(void)
{
    int32 status = OS_SUCCESS;

    DEMO_AppData.RunStatus = CFE_ES_RunStatus_APP_RUN;

    /*
    ** Register the events
    */
    status = CFE_EVS_Register(NULL, 0, CFE_EVS_EventFilter_BINARY); /* as default, no filters are used */
    if (status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("DEMO: Error registering for event services: 0x%08X\n", (unsigned int)status);
        return status;
    }

    /*
    ** Create the Software Bus command pipe
    */
    status = CFE_SB_CreatePipe(&DEMO_AppData.CmdPipe, DEMO_PIPE_DEPTH, "DEMO_CMD_PIPE");
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(DEMO_PIPE_ERR_EID, CFE_EVS_EventType_ERROR, "Error Creating SB Pipe,RC=0x%08X",
                          (unsigned int)status);
        return status;
    }

    /*
    ** Subscribe to ground commands
    */
    status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(DEMO_CMD_MID), DEMO_AppData.CmdPipe);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(DEMO_SUB_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Subscribing to HK Gnd Cmds, MID=0x%04X, RC=0x%08X", DEMO_CMD_MID,
                          (unsigned int)status);
        return status;
    }

    /*
    ** Subscribe to housekeeping (hk) message requests
    */
    status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(DEMO_REQ_HK_MID), DEMO_AppData.CmdPipe);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(DEMO_SUB_REQ_HK_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Subscribing to HK Request, MID=0x%04X, RC=0x%08X", DEMO_REQ_HK_MID,
                          (unsigned int)status);
        return status;
    }

    /*
    ** Initialize the published HK message - this HK message will contain the
    ** telemetry that has been defined in the DEMO_Hk_tlm_t for this app.
    */
    CFE_MSG_Init(CFE_MSG_PTR(DEMO_AppData.HkTelemetryPkt.TlmHeader), CFE_SB_ValueToMsgId(DEMO_HK_TLM_MID),
                 DEMO_HK_TLM_LNGTH);

    /*
    ** Initialize the device packet message
    ** This packet is specific to your application
    */
    CFE_MSG_Init(CFE_MSG_PTR(DEMO_AppData.DevicePkt.TlmHeader), CFE_SB_ValueToMsgId(DEMO_DEVICE_TLM_MID),
                 DEMO_DEVICE_TLM_LNGTH);

    /*
    ** Reset all counters during application initialization
    */
    DEMO_ResetCounters();

    /*
    ** Initialize application data
    ** Note that counters are excluded as they were reset in the previous code block
    */
    DEMO_AppData.HkTelemetryPkt.DeviceErrorCount = 0;
    DEMO_AppData.HkTelemetryPkt.DeviceCount      = 0;
    DEMO_AppData.HkTelemetryPkt.DeviceEnabled    = DEMO_DEVICE_DISABLED;

    /*
     ** Send an information event that the app has initialized.
     ** This is useful for debugging the loading of individual applications.
     */
    status = CFE_EVS_SendEvent(DEMO_STARTUP_INF_EID, CFE_EVS_EventType_INFORMATION,
                               "DEMO App Initialized. Version %d.%d.%d.%d", DEMO_MAJOR_VERSION,
                               DEMO_MINOR_VERSION, DEMO_REVISION, DEMO_MISSION_REV);
    if (status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("DEMO: Error sending initialization event: 0x%08X\n", (unsigned int)status);
    }
    return status;
}

/*
** Process packets received on the DEMO command pipe
*/
void DEMO_ProcessCommandPacket(void)
{
    CFE_SB_MsgId_t MsgId = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_GetMsgId(DEMO_AppData.MsgPtr, &MsgId);
    switch (CFE_SB_MsgIdToValue(MsgId))
    {
        /*
        ** Ground Commands with command codes fall under the DEMO_CMD_MID (Message ID)
        */
        case DEMO_CMD_MID:
            DEMO_ProcessGroundCommand();
            break;

        /*
        ** Housekeeping requests with command codes fall under the DEMO_REQ_HK_MID (Message ID)
        */
        case DEMO_REQ_HK_MID:
            DEMO_ProcessTelemetryRequest();
            break;

        /*
        ** All other invalid messages that this app doesn't recognize,
        ** increment the command error counter and log as an error event.
        */
        default:
            /* Increment the command error counter upon receipt of an invalid command packet */
            DEMO_AppData.HkTelemetryPkt.CommandErrorCount++;

            /* Send event failure to the console*/
            CFE_EVS_SendEvent(DEMO_PROCESS_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                              "DEMO: Invalid command packet, MID = 0x%x", CFE_SB_MsgIdToValue(MsgId));
            break;
    }
    return;
}

/*
** Process ground commands
*/
void DEMO_ProcessGroundCommand(void)
{
    CFE_SB_MsgId_t    MsgId       = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t CommandCode = 0;

    /*
    ** MsgId is only needed if the command code is not recognized. See default case
    */
    CFE_MSG_GetMsgId(DEMO_AppData.MsgPtr, &MsgId);

    /*
    ** Ground Commands have a command code (_CC) associated with them
    ** Pull this command code from the message and then process
    */
    CFE_MSG_GetFcnCode(DEMO_AppData.MsgPtr, &CommandCode);
    switch (CommandCode)
    {
        /*
        ** NOOP Command
        */
        case DEMO_NOOP_CC:
            /*
            ** Verify the command length immediately after CC identification
            */
            if (DEMO_VerifyCmdLength(DEMO_AppData.MsgPtr, sizeof(DEMO_NoArgs_cmd_t)) == OS_SUCCESS)
            {
#ifdef DEMO_CFG_DEBUG
                OS_printf("DEMO: DEMO_NOOP_CC received \n");
#endif

                /* Do any necessary checks, none for a NOOP */

                /* Increment command success or error counter, NOOP can only be successful */
                DEMO_AppData.HkTelemetryPkt.CommandCount++;

                /* Do the action, none for a NOOP */

                /* Increment device success or error counter, none for NOOP as application only */

                /* Send event success or failure to the console, NOOP can only be successful */
                CFE_EVS_SendEvent(DEMO_CMD_NOOP_INF_EID, CFE_EVS_EventType_INFORMATION,
                                  "DEMO: NOOP command received");
            }
            break;

        /*
        ** Reset Counters Command
        */
        case DEMO_RESET_COUNTERS_CC:
            if (DEMO_VerifyCmdLength(DEMO_AppData.MsgPtr, sizeof(DEMO_NoArgs_cmd_t)) == OS_SUCCESS)
            {
#ifdef DEMO_CFG_DEBUG
                OS_printf("DEMO: DEMO_RESET_COUNTERS_CC received \n");
#endif
                DEMO_ResetCounters();
            }
            break;

        /*
        ** Enable Command
        */
        case DEMO_ENABLE_CC:
            if (DEMO_VerifyCmdLength(DEMO_AppData.MsgPtr, sizeof(DEMO_NoArgs_cmd_t)) == OS_SUCCESS)
            {
#ifdef DEMO_CFG_DEBUG
                OS_printf("DEMO: DEMO_ENABLE_CC received \n");
#endif
                DEMO_Enable();
            }
            break;

        /*
        ** Disable Command
        */
        case DEMO_DISABLE_CC:
            if (DEMO_VerifyCmdLength(DEMO_AppData.MsgPtr, sizeof(DEMO_NoArgs_cmd_t)) == OS_SUCCESS)
            {
#ifdef DEMO_CFG_DEBUG
                OS_printf("DEMO: DEMO_DISABLE_CC received \n");
#endif
                DEMO_Disable();
            }
            break;

        /*
        ** Set Configuration Command
        ** Note that this is an example of a command that has additional arguments
        */
        case DEMO_CONFIG_CC:
            if (DEMO_VerifyCmdLength(DEMO_AppData.MsgPtr, sizeof(DEMO_Config_cmd_t)) == OS_SUCCESS)
            {
#ifdef DEMO_CFG_DEBUG
                OS_printf("DEMO: DEMO_CONFIG_CC received \n");
#endif
                DEMO_Configure();
            }
            break;

        /*
        ** Invalid Command Codes
        */
        default:
            /* Increment the command error counter upon receipt of an invalid command */
            DEMO_AppData.HkTelemetryPkt.CommandErrorCount++;

            /* Send invalid command code failure to the console */
            CFE_EVS_SendEvent(DEMO_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                              "DEMO: Invalid command code for packet, MID = 0x%x, cmdCode = 0x%x",
                              CFE_SB_MsgIdToValue(MsgId), CommandCode);
            break;
    }
    return;
}

/*
** Process Telemetry Request - Triggered in response to a telemetry request
*/
void DEMO_ProcessTelemetryRequest(void)
{
    CFE_SB_MsgId_t    MsgId       = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t CommandCode = 0;

    /* MsgId is only needed if the command code is not recognized. See default case */
    CFE_MSG_GetMsgId(DEMO_AppData.MsgPtr, &MsgId);

    /* Pull this command code from the message and then process */
    CFE_MSG_GetFcnCode(DEMO_AppData.MsgPtr, &CommandCode);
    switch (CommandCode)
    {
        case DEMO_REQ_HK_TLM:
            DEMO_ReportHousekeeping();
            break;

        case DEMO_REQ_DATA_TLM:
            DEMO_ReportDeviceTelemetry();
            break;

        /*
        ** Invalid Command Codes
        */
        default:
            /* Increment the error counter upon receipt of an invalid command */
            DEMO_AppData.HkTelemetryPkt.CommandErrorCount++;

            /* Send invalid command code failure to the console */
            CFE_EVS_SendEvent(DEMO_DEVICE_TLM_ERR_EID, CFE_EVS_EventType_ERROR,
                              "DEMO: Invalid command code for packet, MID = 0x%x, cmdCode = 0x%x",
                              CFE_SB_MsgIdToValue(MsgId), CommandCode);
            break;
    }
    return;
}

/*
** Report Application Housekeeping
*/
void DEMO_ReportHousekeeping(void)
{
    int32 status = OS_SUCCESS;

    /* Check that device is enabled */
    if (DEMO_AppData.HkTelemetryPkt.DeviceEnabled == DEMO_DEVICE_ENABLED)
    {
        status = DEMO_RequestHK(&DEMO_AppData.DemoUart,
                                  (DEMO_Device_HK_tlm_t *)&DEMO_AppData.HkTelemetryPkt.DeviceHK);
        if (status == OS_SUCCESS)
        {
            DEMO_AppData.HkTelemetryPkt.DeviceCount++;
        }
        else
        {
            DEMO_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(DEMO_REQ_HK_ERR_EID, CFE_EVS_EventType_ERROR,
                              "DEMO: Request device HK reported error %d", status);
        }
    }
    /* Intentionally do not report errors if disabled */

    /* Time stamp and publish housekeeping telemetry */
    CFE_SB_TimeStampMsg((CFE_MSG_Message_t *)&DEMO_AppData.HkTelemetryPkt);
    CFE_SB_TransmitMsg((CFE_MSG_Message_t *)&DEMO_AppData.HkTelemetryPkt, true);
    return;
}

/*
** Collect and report device telemetry
*/
void DEMO_ReportDeviceTelemetry(void)
{
    int32 status = OS_SUCCESS;

    /* Check that device is enabled */
    if (DEMO_AppData.HkTelemetryPkt.DeviceEnabled == DEMO_DEVICE_ENABLED)
    {
        status = DEMO_RequestData(&DEMO_AppData.DemoUart,
                                    (DEMO_Device_Data_tlm_t *)&DEMO_AppData.DevicePkt.Demo);
        if (status == OS_SUCCESS)
        {
            /* Update packet count */
            DEMO_AppData.HkTelemetryPkt.DeviceCount++;

            /* Time stamp and publish data telemetry */
            CFE_SB_TimeStampMsg((CFE_MSG_Message_t *)&DEMO_AppData.DevicePkt);
            CFE_SB_TransmitMsg((CFE_MSG_Message_t *)&DEMO_AppData.DevicePkt, true);
        }
        else
        {
            DEMO_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(DEMO_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                              "DEMO: Request device data reported error %d", status);
        }
    }
    /* Intentionally do not report errors if device disabled */
    return;
}

/*
** Reset all global counter variables
*/
void DEMO_ResetCounters(void)
{
    /* Do any necessary checks, none for reset counters */

    /* Increment command success or error counter, omitted as action is to reset */

    /* Do the action, clear all global counter variables */
    DEMO_AppData.HkTelemetryPkt.CommandErrorCount = 0;
    DEMO_AppData.HkTelemetryPkt.CommandCount      = 0;
    DEMO_AppData.HkTelemetryPkt.DeviceErrorCount  = 0;
    DEMO_AppData.HkTelemetryPkt.DeviceCount       = 0;

    /* Increment device success or error counter, none as application only */

    /* Send event success to the console */
    CFE_EVS_SendEvent(DEMO_CMD_RESET_INF_EID, CFE_EVS_EventType_INFORMATION,
                      "DEMO: RESET counters command received");
    return;
}

/*
** Enable component
*/
void DEMO_Enable(void)
{
    int32 status = OS_SUCCESS;

    /* Do any necessary checks, confirm that device is currently disabled */
    if (DEMO_AppData.HkTelemetryPkt.DeviceEnabled == DEMO_DEVICE_DISABLED)
    {
        /* Increment command success counter */
        DEMO_AppData.HkTelemetryPkt.CommandCount++;

        /*
        ** Do the action, initialize hardware interface and set enabled
        */
        DEMO_AppData.DemoUart.deviceString  = DEMO_CFG_STRING;
        DEMO_AppData.DemoUart.handle        = DEMO_CFG_HANDLE;
        DEMO_AppData.DemoUart.isOpen        = PORT_CLOSED;
        DEMO_AppData.DemoUart.baud          = DEMO_CFG_BAUDRATE_HZ;
        DEMO_AppData.DemoUart.access_option = uart_access_flag_RDWR;

        status = uart_init_port(&DEMO_AppData.DemoUart);
        if (status == OS_SUCCESS)
        {
            DEMO_AppData.HkTelemetryPkt.DeviceEnabled = DEMO_DEVICE_ENABLED;

            /* Increment device success counter */
            DEMO_AppData.HkTelemetryPkt.DeviceCount++;

            /* Send device event success to the console */
            CFE_EVS_SendEvent(DEMO_ENABLE_INF_EID, CFE_EVS_EventType_INFORMATION,
                              "DEMO: Device enabled successfully");
        }
        else
        {
            /* Increment device error counter */
            DEMO_AppData.HkTelemetryPkt.DeviceErrorCount++;

            /* Send device event failure to the console */
            CFE_EVS_SendEvent(DEMO_UART_INIT_ERR_EID, CFE_EVS_EventType_ERROR,
                              "DEMO: Device UART port initialization error %d", status);
        }
    }
    else
    {
        /* Increment command error count */
        DEMO_AppData.HkTelemetryPkt.CommandErrorCount++;

        /* Send command event failure to the console */
        CFE_EVS_SendEvent(DEMO_ENABLE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "DEMO: Device enable failed, already enabled");
    }
    return;
}

/*
** Disable component
*/
void DEMO_Disable(void)
{
    int32 status = OS_SUCCESS;

    /* Do any necessary checks, confirm that device is currently enabled */
    if (DEMO_AppData.HkTelemetryPkt.DeviceEnabled == DEMO_DEVICE_ENABLED)
    {
        /* Increment command success counter */
        DEMO_AppData.HkTelemetryPkt.CommandCount++;

        /*
        ** Do the action, close hardware interface and set disabled
        */
        status = uart_close_port(&DEMO_AppData.DemoUart);
        if (status == OS_SUCCESS)
        {
            DEMO_AppData.HkTelemetryPkt.DeviceEnabled = DEMO_DEVICE_DISABLED;

            /* Increment device success counter */
            DEMO_AppData.HkTelemetryPkt.DeviceCount++;

            /* Send device event success to the console */
            CFE_EVS_SendEvent(DEMO_DISABLE_INF_EID, CFE_EVS_EventType_INFORMATION,
                              "DEMO: Device disabled successfully");
        }
        else
        {
            /* Increment device error counter */
            DEMO_AppData.HkTelemetryPkt.DeviceErrorCount++;

            /* Send device event failure to the console */
            CFE_EVS_SendEvent(DEMO_UART_CLOSE_ERR_EID, CFE_EVS_EventType_ERROR,
                              "DEMO: Device UART port close error %d", status);
        }
    }
    else
    {
        /* Increment command error count */
        DEMO_AppData.HkTelemetryPkt.CommandErrorCount++;

        /* Send command event failure to the console */
        CFE_EVS_SendEvent(DEMO_DISABLE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "DEMO: Device disable failed, already disabled");
    }
    return;
}

/*
** Configure component
*/
void DEMO_Configure(void)
{
    int32 status        = OS_SUCCESS;
    int32 device_status = OS_SUCCESS;
    DEMO_Config_cmd_t config_cmd;
    memcpy(&config_cmd, DEMO_AppData.MsgPtr, sizeof(config_cmd));
    DEMO_Config_cmd_t *cmd_ptr = &config_cmd;

    /* Do any necessary checks, confirm that device is currently enabled */
    if (DEMO_AppData.HkTelemetryPkt.DeviceEnabled != DEMO_DEVICE_ENABLED)
    {
        status = OS_ERROR;
        /* Increment command error count */
        DEMO_AppData.HkTelemetryPkt.CommandErrorCount++;

        /* Send event logging failure of check to the console */
        CFE_EVS_SendEvent(DEMO_CMD_CONFIG_EN_ERR_EID, CFE_EVS_EventType_ERROR,
                          "DEMO: Configuration command invalid when device disabled");
    }

    /* Do any necessary checks, confirm valid configuration value */
    if (cmd_ptr->DeviceCfg == 65535)
    {
        status = OS_ERROR;
        /* Increment command error count */
        DEMO_AppData.HkTelemetryPkt.CommandErrorCount++;

        /* Send event logging failure of check to the console */
        CFE_EVS_SendEvent(DEMO_CMD_CONFIG_VAL_ERR_EID, CFE_EVS_EventType_ERROR,
                          "DEMO: Configuration command with value %u is invalid", cmd_ptr->DeviceCfg);
    }

    if (status == OS_SUCCESS)
    {
        /* Increment command success counter */
        DEMO_AppData.HkTelemetryPkt.CommandCount++;

        /* Do the action, command device to with a new configuration */
        device_status = DEMO_CommandDevice(&DEMO_AppData.DemoUart, DEMO_DEVICE_CFG_CMD, cmd_ptr->DeviceCfg);
        if (device_status == OS_SUCCESS)
        {
            /* Increment device success counter */
            DEMO_AppData.HkTelemetryPkt.DeviceCount++;

            /* Send device event success to the console */
            CFE_EVS_SendEvent(DEMO_CMD_CONFIG_INF_EID, CFE_EVS_EventType_INFORMATION,
                              "DEMO: Configuration command received: %u", cmd_ptr->DeviceCfg);
        }
        else
        {
            /* Increment device error counter */
            DEMO_AppData.HkTelemetryPkt.DeviceErrorCount++;

            /* Send device event failure to the console */
            CFE_EVS_SendEvent(DEMO_CMD_CONFIG_DEV_ERR_EID, CFE_EVS_EventType_ERROR,
                              "DEMO: Configuration command received: %u", cmd_ptr->DeviceCfg);
        }
    }
    return;
}

/*
** Verify command packet length matches expected
*/
int32 DEMO_VerifyCmdLength(CFE_MSG_Message_t *msg, uint16 expected_length)
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

        CFE_EVS_SendEvent(DEMO_LEN_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Invalid msg length: ID = 0x%X,  CC = %d, Len = %zu, Expected = %d",
                          CFE_SB_MsgIdToValue(msg_id), cmd_code, actual_length, expected_length);

        status = OS_ERROR;

        /* Increment the command error counter upon receipt of an invalid command length */
        DEMO_AppData.HkTelemetryPkt.CommandErrorCount++;
    }
    return status;
}
