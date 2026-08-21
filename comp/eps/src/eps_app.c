#include "eps_app.h"

EPS_AppData_t EPS_AppData;

/*
** Application entry point and main process loop
*/
void EPS_AppMain(void)
{
    int32 status = OS_SUCCESS;

    /*
    ** Create the first performance log entry
    */
    CFE_ES_PerfLogEntry(EPS_PERF_ID);

    /*
    ** Perform application initialization
    */
    status = EPS_AppInit();
    if (status != CFE_SUCCESS)
    {
        EPS_AppData.RunStatus = CFE_ES_RunStatus_APP_ERROR;
    }

    /*
    ** Main loop
    */
    while (CFE_ES_RunLoop(&EPS_AppData.RunStatus) == true)
    {
        /*
        ** Performance log exit stamp
        */
        CFE_ES_PerfLogExit(EPS_PERF_ID);

        /*
        ** Pend on the arrival of the next software bus message
        ** Note that this is the standard, but timeouts are available
        */
        status = CFE_SB_ReceiveBuffer((CFE_SB_Buffer_t **)&EPS_AppData.MsgPtr, EPS_AppData.CmdPipe, CFE_SB_PEND_FOREVER);

        /*
        ** Begin performance metrics on anything after this line.
        */
        CFE_ES_PerfLogEntry(EPS_PERF_ID);

        /*
        ** If the CFE_SB_ReceiveBuffer was successful, then continue to process the command packet
        ** If not, then exit the application in error.
        ** Note that a SB read error should not always result in an app quitting.
        */
        if (status == CFE_SUCCESS)
        {
            EPS_ProcessCommandPacket();
        }
        else
        {
            CFE_EVS_SendEvent(EPS_PIPE_ERR_EID, CFE_EVS_EventType_ERROR, "EPS: SB Pipe Read Error = %d",
                              (int)status);
            EPS_AppData.RunStatus = CFE_ES_RunStatus_APP_ERROR;
        }
    }

    /*
    ** Performance log exit stamp
    */
    CFE_ES_PerfLogExit(EPS_PERF_ID);

    /*
    ** Exit the application
    */
    CFE_ES_ExitApp(EPS_AppData.RunStatus);
}

/*
** Initialize application
*/
int32 EPS_AppInit(void)
{
    int32 status = OS_SUCCESS;

    EPS_AppData.RunStatus = CFE_ES_RunStatus_APP_RUN;

    /*
    ** Register the events
    */
    status = CFE_EVS_Register(NULL, 0, CFE_EVS_EventFilter_BINARY); /* as default, no filters are used */
    if (status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("EPS: Error registering for event services: 0x%08X\n", (unsigned int)status);
        return status;
    }

    /*
    ** Create the Software Bus command pipe
    */
    status = CFE_SB_CreatePipe(&EPS_AppData.CmdPipe, EPS_PIPE_DEPTH, "EPS_CMD_PIPE");
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(EPS_PIPE_ERR_EID, CFE_EVS_EventType_ERROR, "Error Creating SB Pipe,RC=0x%08X",
                          (unsigned int)status);
        return status;
    }

    /*
    ** Subscribe to ground commands
    */
    status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(EPS_CMD_MID), EPS_AppData.CmdPipe);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(EPS_SUB_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Subscribing to HK Gnd Cmds, MID=0x%04X, RC=0x%08X", EPS_CMD_MID,
                          (unsigned int)status);
        return status;
    }

    /*
    ** Subscribe to housekeeping (hk) message requests
    */
    status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(EPS_REQ_HK_MID), EPS_AppData.CmdPipe);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(EPS_SUB_REQ_HK_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Subscribing to HK Request, MID=0x%04X, RC=0x%08X", EPS_REQ_HK_MID,
                          (unsigned int)status);
        return status;
    }

    /*
    ** Initialize the published HK message - this HK message will contain the
    ** telemetry that has been defined in the EPS_Hk_tlm_t for this app.
    */
    CFE_MSG_Init(CFE_MSG_PTR(EPS_AppData.HkTelemetryPkt.TlmHeader), CFE_SB_ValueToMsgId(EPS_HK_TLM_MID),
                 EPS_HK_TLM_LNGTH);

    /*
    ** Reset all counters during application initialization
    */
    EPS_ResetCounters();

    /*
    ** Initialize application data
    ** Note that counters are excluded as they were reset in the previous code block
    */
    EPS_AppData.HkTelemetryPkt.DeviceErrorCount = 0;
    EPS_AppData.HkTelemetryPkt.DeviceCount      = 0;

    /*
    ** Initialize the device
    */
    status = EPS_InitDevice(&EPS_AppData.EpsI2c);
    if (status != I2C_SUCCESS)
    {
        /* Increment device error counter */
        EPS_AppData.HkTelemetryPkt.DeviceErrorCount++;

        /* Send device event failure to the console */
        CFE_EVS_SendEvent(EPS_I2C_INIT_ERR_EID, CFE_EVS_EventType_ERROR,
                            "EPS: Device I2C port initialization error %d", status);
    }

    /*
     ** Send an information event that the app has initialized.
     ** This is useful for debugging the loading of individual applications.
     */
    status = CFE_EVS_SendEvent(EPS_STARTUP_INF_EID, CFE_EVS_EventType_INFORMATION,
                               "EPS App Initialized. Version %d.%d.%d.%d", EPS_MAJOR_VERSION,
                               EPS_MINOR_VERSION, EPS_REVISION, EPS_MISSION_REV);
    if (status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("EPS: Error sending initialization event: 0x%08X\n", (unsigned int)status);
    }
    return status;
}

/*
** Process packets received on the EPS command pipe
*/
void EPS_ProcessCommandPacket(void)
{
    CFE_SB_MsgId_t MsgId = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_GetMsgId(EPS_AppData.MsgPtr, &MsgId);
    switch (CFE_SB_MsgIdToValue(MsgId))
    {
        /*
        ** Ground Commands with command codes fall under the EPS_CMD_MID (Message ID)
        */
        case EPS_CMD_MID:
            EPS_ProcessGroundCommand();
            break;

        /*
        ** Housekeeping requests with command codes fall under the EPS_REQ_HK_MID (Message ID)
        */
        case EPS_REQ_HK_MID:
            EPS_ProcessTelemetryRequest();
            break;

        /*
        ** All other invalid messages that this app doesn't recognize,
        ** increment the command error counter and log as an error event.
        */
        default:
            /* Increment the command error counter upon receipt of an invalid command packet */
            EPS_AppData.HkTelemetryPkt.CommandErrorCount++;

            /* Send event failure to the console*/
            CFE_EVS_SendEvent(EPS_PROCESS_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                              "EPS: Invalid command packet, MID = 0x%x", CFE_SB_MsgIdToValue(MsgId));
            break;
    }
    return;
}

/*
** Process ground commands
*/
void EPS_ProcessGroundCommand(void)
{
    CFE_SB_MsgId_t    MsgId       = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t CommandCode = 0;

    /*
    ** MsgId is only needed if the command code is not recognized. See default case
    */
    CFE_MSG_GetMsgId(EPS_AppData.MsgPtr, &MsgId);

    /*
    ** Ground Commands have a command code (_CC) associated with them
    ** Pull this command code from the message and then process
    */
    CFE_MSG_GetFcnCode(EPS_AppData.MsgPtr, &CommandCode);
    switch (CommandCode)
    {
        /*
        ** NOOP Command
        */
        case EPS_NOOP_CC:
            /*
            ** Verify the command length immediately after CC identification
            */
            if (EPS_VerifyCmdLength(EPS_AppData.MsgPtr, sizeof(EPS_NoArgs_cmd_t)) == OS_SUCCESS)
            {
#ifdef EPS_CFG_DEBUG
                OS_printf("EPS: EPS_NOOP_CC received \n");
#endif

                /* Do any necessary checks, none for a NOOP */

                /* Increment command success or error counter, NOOP can only be successful */
                EPS_AppData.HkTelemetryPkt.CommandCount++;

                /* Do the action, none for a NOOP */

                /* Increment device success or error counter, none for NOOP as application only */

                /* Send event success or failure to the console, NOOP can only be successful */
                CFE_EVS_SendEvent(EPS_CMD_NOOP_INF_EID, CFE_EVS_EventType_INFORMATION,
                                  "EPS: NOOP command received");
            }
            break;

        /*
        ** Reset Counters Command
        */
        case EPS_RESET_COUNTERS_CC:
            if (EPS_VerifyCmdLength(EPS_AppData.MsgPtr, sizeof(EPS_NoArgs_cmd_t)) == OS_SUCCESS)
            {
#ifdef EPS_CFG_DEBUG
                OS_printf("EPS: EPS_RESET_COUNTERS_CC received \n");
#endif
                EPS_ResetCounters();
            }
            break;

        /*
        ** Set Switch OFF Command
        */
        case EPS_SWITCH_OFF_CC:
            if (EPS_VerifyCmdLength(EPS_AppData.MsgPtr, sizeof(EPS_Switch_cmd_t)) == OS_SUCCESS)
            {
#ifdef EPS_CFG_DEBUG
                OS_printf("EPS: EPS_SWITCH_OFF_CC received \n");
#endif
                EPS_SetSwitchOff();
            }
            break;

        /*
        ** Set Switch ON Command
        */
        case EPS_SWITCH_ON_CC:
            if (EPS_VerifyCmdLength(EPS_AppData.MsgPtr, sizeof(EPS_Switch_cmd_t)) == OS_SUCCESS)
            {
#ifdef EPS_CFG_DEBUG
                OS_printf("EPS: EPS_SWITCH_ON_CC received \n");
#endif
                EPS_SetSwitchOn();
            }
            break;

        /*
        ** Invalid Command Codes
        */
        default:
            /* Increment the command error counter upon receipt of an invalid command */
            EPS_AppData.HkTelemetryPkt.CommandErrorCount++;

            /* Send invalid command code failure to the console */
            CFE_EVS_SendEvent(EPS_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                              "EPS: Invalid command code for packet, MID = 0x%x, cmdCode = 0x%x",
                              CFE_SB_MsgIdToValue(MsgId), CommandCode);
            break;
    }
    return;
}

/*
** Process Telemetry Request - Triggered in response to a telemetry request
*/
void EPS_ProcessTelemetryRequest(void)
{
    CFE_SB_MsgId_t    MsgId       = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t CommandCode = 0;

    /* MsgId is only needed if the command code is not recognized. See default case */
    CFE_MSG_GetMsgId(EPS_AppData.MsgPtr, &MsgId);

    /* Pull this command code from the message and then process */
    CFE_MSG_GetFcnCode(EPS_AppData.MsgPtr, &CommandCode);
    switch (CommandCode)
    {
        case EPS_REQ_HK_TLM:
            EPS_ReportHousekeeping();
            break;

        /*
        ** Invalid Command Codes
        */
        default:
            /* Increment the error counter upon receipt of an invalid command */
            EPS_AppData.HkTelemetryPkt.CommandErrorCount++;

            /* Send invalid command code failure to the console */
            CFE_EVS_SendEvent(EPS_DEVICE_TLM_ERR_EID, CFE_EVS_EventType_ERROR,
                              "EPS: Invalid command code for packet, MID = 0x%x, cmdCode = 0x%x",
                              CFE_SB_MsgIdToValue(MsgId), CommandCode);
            break;
    }
    return;
}

/*
** Report Application Housekeeping
*/
void EPS_ReportHousekeeping(void)
{
    int32 status = OS_SUCCESS;

    status = EPS_RequestHK(&EPS_AppData.EpsI2c,
                                (EPS_Device_HK_tlm_t *)&EPS_AppData.HkTelemetryPkt.DeviceHK);
    if (status == OS_SUCCESS)
    {
        EPS_AppData.HkTelemetryPkt.DeviceCount++;
    }
    else
    {
        EPS_AppData.HkTelemetryPkt.DeviceErrorCount++;
        CFE_EVS_SendEvent(EPS_REQ_HK_ERR_EID, CFE_EVS_EventType_ERROR,
                            "EPS: Request device HK reported error %d", status);
    }

    /* Time stamp and publish housekeeping telemetry */
    CFE_SB_TimeStampMsg((CFE_MSG_Message_t *)&EPS_AppData.HkTelemetryPkt);
    CFE_SB_TransmitMsg((CFE_MSG_Message_t *)&EPS_AppData.HkTelemetryPkt, true);
    return;
}

/*
** Reset all global counter variables
*/
void EPS_ResetCounters(void)
{
    /* Do any necessary checks, none for reset counters */

    /* Increment command success or error counter, omitted as action is to reset */

    /* Do the action, clear all global counter variables */
    EPS_AppData.HkTelemetryPkt.CommandErrorCount = 0;
    EPS_AppData.HkTelemetryPkt.CommandCount      = 0;
    EPS_AppData.HkTelemetryPkt.DeviceErrorCount  = 0;
    EPS_AppData.HkTelemetryPkt.DeviceCount       = 0;

    /* Increment device success or error counter, none as application only */

    /* Send event success to the console */
    CFE_EVS_SendEvent(EPS_CMD_RESET_INF_EID, CFE_EVS_EventType_INFORMATION,
                      "EPS: RESET counters command received");
    return;
}

/*
** Set EPS switch OFF
*/
void EPS_SetSwitchOff(void)
{
    int32 status = OS_SUCCESS;
    EPS_Switch_cmd_t *switch_cmd = (EPS_Switch_cmd_t *)EPS_AppData.MsgPtr;

    /* Verify switch number is valid */
    if (switch_cmd->SwitchNumber >= EPS_NUM_SWITCHES)
    {
        status = OS_ERROR;
        EPS_AppData.HkTelemetryPkt.CommandErrorCount++;
        CFE_EVS_SendEvent(EPS_CMD_SWITCH_OFF_ERR_EID, CFE_EVS_EventType_ERROR,
                          "EPS: Invalid switch number %d", switch_cmd->SwitchNumber);
    }

    if (status == OS_SUCCESS)
    {
        /* Increment command success counter */
        EPS_AppData.HkTelemetryPkt.CommandCount++;

        /* Send switch OFF command to device */
        status = EPS_SetSwitch(&EPS_AppData.EpsI2c, switch_cmd->SwitchNumber, false);
        if (status == I2C_SUCCESS)
        {
            /* Increment device success counter */
            EPS_AppData.HkTelemetryPkt.DeviceCount++;
            CFE_EVS_SendEvent(EPS_CMD_SWITCH_OFF_INF_EID, CFE_EVS_EventType_INFORMATION,
                              "EPS: Switch %d turned OFF", switch_cmd->SwitchNumber);
        }
        else
        {
            /* Increment device error counter */
            EPS_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(EPS_CMD_SWITCH_OFF_ERR_EID, CFE_EVS_EventType_ERROR,
                              "EPS: Switch OFF command failed for switch %d", switch_cmd->SwitchNumber);
        }
    }
    return;
}

/*
** Set EPS switch ON
*/
void EPS_SetSwitchOn(void)
{
    int32 status = OS_SUCCESS;
    EPS_Switch_cmd_t *switch_cmd = (EPS_Switch_cmd_t *)EPS_AppData.MsgPtr;

    /* Verify switch number is valid */
    if (switch_cmd->SwitchNumber >= EPS_NUM_SWITCHES)
    {
        status = OS_ERROR;
        EPS_AppData.HkTelemetryPkt.CommandErrorCount++;
        CFE_EVS_SendEvent(EPS_CMD_SWITCH_ON_ERR_EID, CFE_EVS_EventType_ERROR,
                          "EPS: Invalid switch number %d", switch_cmd->SwitchNumber);
    }

    if (status == OS_SUCCESS)
    {
        /* Increment command success counter */
        EPS_AppData.HkTelemetryPkt.CommandCount++;

        /* Send switch ON command to device */
        status = EPS_SetSwitch(&EPS_AppData.EpsI2c, switch_cmd->SwitchNumber, true);
        if (status == I2C_SUCCESS)
        {
            /* Increment device success counter */
            EPS_AppData.HkTelemetryPkt.DeviceCount++;
            CFE_EVS_SendEvent(EPS_CMD_SWITCH_ON_INF_EID, CFE_EVS_EventType_INFORMATION,
                              "EPS: Switch %d turned ON", switch_cmd->SwitchNumber);
        }
        else
        {
            /* Increment device error counter */
            EPS_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(EPS_CMD_SWITCH_ON_ERR_EID, CFE_EVS_EventType_ERROR,
                              "EPS: Switch ON command failed for switch %d", switch_cmd->SwitchNumber);
        }
    }
    return;
}

/*
** Verify command packet length matches expected
*/
int32 EPS_VerifyCmdLength(CFE_MSG_Message_t *msg, uint16 expected_length)
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

        CFE_EVS_SendEvent(EPS_LEN_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Invalid msg length: ID = 0x%X,  CC = %d, Len = %zu, Expected = %d",
                          CFE_SB_MsgIdToValue(msg_id), cmd_code, actual_length, expected_length);

        status = OS_ERROR;

        /* Increment the command error counter upon receipt of an invalid command length */
        EPS_AppData.HkTelemetryPkt.CommandErrorCount++;
    }
    return status;
}
