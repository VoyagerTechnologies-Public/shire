#include "radio_app.h"

RADIO_AppData_t RADIO_AppData;
CFE_TBL_Handle_t RADIO_SubsTblHandle;
RADIO_Subs_t *RADIO_SubsTblPtr = NULL;
uint32 RADIO_DownlinkPipe;

/* Static buffers to avoid stack overflow */
static uint8 static_tm_frame[RADIO_TM_FRAME_SIZE];
static uint8 static_tm_overflow[RADIO_TM_FRAME_SIZE * 4]; /* Overflow buffer */
static uint8 static_cadu_buffer[RADIO_TM_FRAME_SIZE + TM_SYNC_ASM_SIZE]; /* CADU with ASM space */

/* Persistent TM SDLP configuration */
static TM_SDLP_GlobalConfig_t radio_global_cfg = {0};
static TM_SDLP_ChannelConfig_t radio_channel_cfg = {0};
static TM_SDLP_FrameInfo_t radio_frame_info = {0};

/* Idle packet */
static uint8 idlePattern[32];
static CFE_MSG_Message_t *IdlePacket = CFE_MSG_PTR(RADIO_AppData.IdlePacket.TlmHeader);

/*
** Application entry point and main process loop
*/
void RADIO_AppMain(void)
{
    int32 status = OS_SUCCESS;

    /*
    ** Create the first performance log entry
    */
    CFE_ES_PerfLogEntry(RADIO_PERF_ID);

    /*
    ** Perform application initialization
    */
    status = RADIO_AppInit();
    if (status != CFE_SUCCESS)
    {
        RADIO_AppData.RunStatus = CFE_ES_RunStatus_APP_ERROR;
    }

    /*
    ** Main loop
    */
    while (CFE_ES_RunLoop(&RADIO_AppData.RunStatus) == true)
    {
        /*
        ** Performance log exit stamp
        */
        CFE_ES_PerfLogExit(RADIO_PERF_ID);

        /*
        ** Pend on the arrival of the next software bus message
        ** Note that this is the standard, but timeouts are available
        */
        status = CFE_SB_ReceiveBuffer((CFE_SB_Buffer_t **)&RADIO_AppData.MsgPtr, RADIO_AppData.CmdPipe, CFE_SB_PEND_FOREVER);

        /*
        ** Begin performance metrics on anything after this line.
        */
        CFE_ES_PerfLogEntry(RADIO_PERF_ID);

        /*
        ** If the CFE_SB_ReceiveBuffer was successful, then continue to process the command packet
        ** If not, then exit the application in error.
        ** Note that a SB read error should not always result in an app quitting.
        */
        if (status == CFE_SUCCESS)
        {
            RADIO_ProcessCommandPacket();
        }
        else
        {
            CFE_EVS_SendEvent(RADIO_PIPE_ERR_EID, CFE_EVS_EventType_ERROR, "RADIO: SB Pipe Read Error = %d",
                              (int)status);
            RADIO_AppData.RunStatus = CFE_ES_RunStatus_APP_ERROR;
        }
    }

    /*
    ** Disable component, which cleans up the interface, upon exit
    */
    RADIO_Disable();

    /*
    ** Performance log exit stamp
    */
    CFE_ES_PerfLogExit(RADIO_PERF_ID);

    /*
    ** Exit the application
    */
    CFE_ES_ExitApp(RADIO_AppData.RunStatus);
}

/*
** Initialize application
*/
int32 RADIO_AppInit(void)
{
    int32 status = OS_SUCCESS;

    RADIO_AppData.RunStatus = CFE_ES_RunStatus_APP_RUN;

    /*
    ** Register the events
    */
    status = CFE_EVS_Register(NULL, 0, CFE_EVS_EventFilter_BINARY); /* as default, no filters are used */
    if (status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("RADIO: Error registering for event services: 0x%08X\n", (unsigned int)status);
        return status;
    }

    /* Register and load downlink subscription table */
    status = CFE_TBL_Register(&RADIO_SubsTblHandle, "RADIO_Subs", sizeof(RADIO_Subs_t), CFE_TBL_OPT_DEFAULT, NULL);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(RADIO_PIPE_ERR_EID, CFE_EVS_EventType_ERROR, "Error registering downlink table,RC=0x%08X", (unsigned int)status);
        return status;
    }
    status = CFE_TBL_Load(RADIO_SubsTblHandle, CFE_TBL_SRC_FILE, "/cf/radio_sub.tbl");
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(RADIO_PIPE_ERR_EID, CFE_EVS_EventType_ERROR, "Error loading downlink table,RC=0x%08X", (unsigned int)status);
        return status;
    }
    status = CFE_TBL_GetAddress((void **)&RADIO_SubsTblPtr, RADIO_SubsTblHandle);
    if (status != CFE_SUCCESS && status != CFE_TBL_INFO_UPDATED)
    {
        CFE_EVS_SendEvent(RADIO_PIPE_ERR_EID, CFE_EVS_EventType_ERROR, "Error getting downlink table addr,RC=0x%08X", (unsigned int)status);
        return status;
    }

    /* Create downlink pipe */
    status = CFE_SB_CreatePipe(&RADIO_DownlinkPipe, RADIO_DOWNLINK_PIPE_DEPTH, RADIO_DOWNLINK_PIPE_NAME);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(RADIO_PIPE_ERR_EID, CFE_EVS_EventType_ERROR, "Error Creating Downlink Pipe,RC=0x%08X", (unsigned int)status);
        return status;
    }

    /* Subscribe to downlink messages from table */
    if (RADIO_SubsTblPtr != NULL)
    {
        for (int i = 0; i < RADIO_CFG_MAX_SUBSCRIPTIONS; i++)
        {
            if (!CFE_SB_IsValidMsgId(RADIO_SubsTblPtr->Subs[i].Stream))
            {
                break;
            }
            status = CFE_SB_SubscribeEx(RADIO_SubsTblPtr->Subs[i].Stream, RADIO_DownlinkPipe, RADIO_SubsTblPtr->Subs[i].Flags, RADIO_SubsTblPtr->Subs[i].BufLimit);
            if (status != CFE_SUCCESS)
            {
                CFE_EVS_SendEvent(RADIO_PIPE_ERR_EID, CFE_EVS_EventType_ERROR, "Error subscribing to downlink stream 0x%X,RC=0x%08X", (unsigned int)CFE_SB_MsgIdToValue(RADIO_SubsTblPtr->Subs[i].Stream), (unsigned int)status);
            }
        }
    }

    /*
    ** Create the Software Bus command pipe
    */
    status = CFE_SB_CreatePipe(&RADIO_AppData.CmdPipe, RADIO_PIPE_DEPTH, "RADIO_CMD_PIPE");
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(RADIO_PIPE_ERR_EID, CFE_EVS_EventType_ERROR, "Error Creating SB Pipe,RC=0x%08X",
                          (unsigned int)status);
        return status;
    }

    /*
    ** Subscribe to ground commands
    */
    status = CFE_SB_SubscribeEx(CFE_SB_ValueToMsgId(RADIO_CMD_MID), RADIO_AppData.CmdPipe, CFE_SB_DEFAULT_QOS, 32);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(RADIO_SUB_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Subscribing to HK Gnd Cmds, MID=0x%04X, RC=0x%08X", RADIO_CMD_MID,
                          (unsigned int)status);
        return status;
    }

    /*
    ** Subscribe to housekeeping (hk) message requests
    */
    status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(RADIO_REQ_HK_MID), RADIO_AppData.CmdPipe);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(RADIO_SUB_REQ_HK_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Subscribing to HK Request, MID=0x%04X, RC=0x%08X", RADIO_REQ_HK_MID,
                          (unsigned int)status);
        return status;
    }

    /*
    ** Initialize the published HK message - this HK message will contain the
    ** telemetry that has been defined in the RADIO_Hk_tlm_t for this app.
    */
    CFE_MSG_Init(CFE_MSG_PTR(RADIO_AppData.HkTelemetryPkt.TlmHeader), CFE_SB_ValueToMsgId(RADIO_HK_TLM_MID),
                 RADIO_HK_TLM_LNGTH);

    /*
    ** Reset all counters during application initialization
    */
    RADIO_ResetCounters();

    /*
    ** Initialize application data
    ** Note that counters are excluded as they were reset in the previous code block
    */
    RADIO_AppData.HkTelemetryPkt.DeviceErrorCount = 0;
    RADIO_AppData.HkTelemetryPkt.DeviceCount      = 0;
    RADIO_AppData.HkTelemetryPkt.DeviceEnabled    = RADIO_DEVICE_DISABLED;

    /* Initialize SPI and GPIO devices */
    RADIO_AppData.RadioSpi.bus = RADIO_CFG_SPI_BUS;
    RADIO_AppData.RadioSpi.cs = RADIO_CFG_SPI_CS;
    RADIO_AppData.RadioSpi.isOpen = SPI_DEVICE_CLOSED;

    RADIO_AppData.RadioPowerGpio.pin = RADIO_CFG_GPIO_POWER_PIN;
    RADIO_AppData.RadioPowerGpio.direction = GPIO_OUTPUT;
    RADIO_AppData.RadioPowerGpio.isOpen = GPIO_CLOSED;

    RADIO_AppData.RadioInterruptGpio.pin = RADIO_CFG_GPIO_INTERRUPT_PIN;
    RADIO_AppData.RadioInterruptGpio.direction = GPIO_INPUT;
    RADIO_AppData.RadioInterruptGpio.isOpen = GPIO_CLOSED;

    /* Initialize receive buffer and length */
    memset(RADIO_AppData.ReceiveBuffer, 0, sizeof(RADIO_AppData.ReceiveBuffer));
    RADIO_AppData.ReceiveBuffLength = 0;

    /* Initialize TM SDLP channel */
    radio_global_cfg.scId = 0x0003; /* Spacecraft ID */
    radio_global_cfg.frameLength = RADIO_TM_FRAME_SIZE;
    radio_global_cfg.hasErrCtrl = 0;

    radio_channel_cfg.vcId = 1; /* Virtual channel ID */
    radio_channel_cfg.dataType = 0; /* VCP_SDU (packet) */
    radio_channel_cfg.fshFlag = 0;
    radio_channel_cfg.ocfFlag = 0;
    radio_channel_cfg.secHdrLength = 0;
    radio_channel_cfg.isMaster = 0;
    radio_channel_cfg.overflowSize = RADIO_TM_FRAME_SIZE;

    OS_printf("RADIO_AppInit: Initializing TM SDLP channel scId=%u vcId=%u frameLen=%u\n",
              radio_global_cfg.scId, radio_channel_cfg.vcId, radio_global_cfg.frameLength);

    /* Initialize TM SYNC library */
    status = TM_SYNC_LibInit();
    if (status != TM_SYNC_SUCCESS)
    {
        CFE_EVS_SendEvent(RADIO_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                          "RADIO: TM_SYNC_LibInit failed in init, status=%d", (int)status);
    }

    status = TM_SDLP_InitChannel(&radio_frame_info, static_tm_frame, static_tm_overflow,
                                 &radio_global_cfg, &radio_channel_cfg);
    if (status != TM_SDLP_SUCCESS)
    {
        CFE_EVS_SendEvent(RADIO_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                          "RADIO: TM_SDLP_InitChannel failed in init, status=%d", (int)status);
        /* Continue, ServiceDownlink will report errors when used */
    }

    /* Initialize Idle pattern as pseudo-random sequence. */
    IO_LIB_UTIL_GenPseudoRandomSeq(&idlePattern[0], 0xa9, 0xff);

    /* Initialize Idle packet with repeating idle pattern */
    TM_SDLP_InitIdlePacket(IdlePacket, &idlePattern[0], RADIO_TM_FRAME_SIZE, 255);

    /*
     ** Send an information event that the app has initialized.
     ** This is useful for debugging the loading of individual applications.
     */
    status = CFE_EVS_SendEvent(RADIO_STARTUP_INF_EID, CFE_EVS_EventType_INFORMATION,
                               "RADIO App Initialized. Version %d.%d.%d.%d", RADIO_MAJOR_VERSION,
                               RADIO_MINOR_VERSION, RADIO_REVISION, RADIO_MISSION_REV);
    if (status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("RADIO: Error sending initialization event: 0x%08X\n", (unsigned int)status);
    }
    return status;
}

/*
** Process packets received on the RADIO command pipe
*/
void RADIO_ProcessCommandPacket(void)
{
    CFE_SB_MsgId_t MsgId = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_GetMsgId(RADIO_AppData.MsgPtr, &MsgId);
    switch (CFE_SB_MsgIdToValue(MsgId))
    {
        /*
        ** Ground Commands with command codes fall under the RADIO_CMD_MID (Message ID)
        */
        case RADIO_CMD_MID:
            RADIO_ProcessGroundCommand();
            break;

        /*
        ** Housekeeping requests with command codes fall under the RADIO_REQ_HK_MID (Message ID)
        */
        case RADIO_REQ_HK_MID:
            RADIO_ProcessTelemetryRequest();
            break;

        /*
        ** All other invalid messages that this app doesn't recognize,
        ** increment the command error counter and log as an error event.
        */
        default:
            /* Increment the command error counter upon receipt of an invalid command packet */
            RADIO_AppData.HkTelemetryPkt.CommandErrorCount++;

            /* Send event failure to the console*/
            CFE_EVS_SendEvent(RADIO_PROCESS_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                              "RADIO: Invalid command packet, MID = 0x%x", CFE_SB_MsgIdToValue(MsgId));
            break;
    }
    return;
}

/*
** Process ground commands
*/
void RADIO_ProcessGroundCommand(void)
{
    CFE_SB_MsgId_t    MsgId       = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t CommandCode = 0;

    /*
    ** MsgId is only needed if the command code is not recognized. See default case
    */
    CFE_MSG_GetMsgId(RADIO_AppData.MsgPtr, &MsgId);

    /*
    ** Ground Commands have a command code (_CC) associated with them
    ** Pull this command code from the message and then process
    */
    CFE_MSG_GetFcnCode(RADIO_AppData.MsgPtr, &CommandCode);
    switch (CommandCode)
    {
        /*
        ** NOOP Command
        */
        case RADIO_NOOP_CC:
            /*
            ** Verify the command length immediately after CC identification
            */
            if (RADIO_VerifyCmdLength(RADIO_AppData.MsgPtr, sizeof(RADIO_NoArgs_cmd_t)) == OS_SUCCESS)
            {
                #ifdef RADIO_CFG_DEBUG
                    OS_printf("RADIO: RADIO_NOOP_CC received \n");
                #endif

                /* Do any necessary checks, none for a NOOP */

                /* Increment command success or error counter, NOOP can only be successful */
                RADIO_AppData.HkTelemetryPkt.CommandCount++;

                /* Do the action, none for a NOOP */

                /* Increment device success or error counter, none for NOOP as application only */

                /* Send event success or failure to the console, NOOP can only be successful */
                CFE_EVS_SendEvent(RADIO_CMD_NOOP_INF_EID, CFE_EVS_EventType_INFORMATION,
                                    "RADIO: NOOP command received");
            }
            break;

        /*
        ** Reset Counters Command
        */
        case RADIO_RESET_COUNTERS_CC:
            if (RADIO_VerifyCmdLength(RADIO_AppData.MsgPtr, sizeof(RADIO_NoArgs_cmd_t)) == OS_SUCCESS)
            {
                #ifdef RADIO_CFG_DEBUG
                    OS_printf("RADIO: RADIO_RESET_COUNTERS_CC received \n");
                #endif
                RADIO_ResetCounters();
            }
            break;

        /*
        ** Enable Command
        */
        case RADIO_ENABLE_CC:
            if (RADIO_VerifyCmdLength(RADIO_AppData.MsgPtr, sizeof(RADIO_NoArgs_cmd_t)) == OS_SUCCESS)
            {
                #ifdef RADIO_CFG_DEBUG
                    OS_printf("RADIO: RADIO_ENABLE_CC received \n");
                #endif
                RADIO_Enable();
            }
            break;

        /*
        ** Disable Command
        */
        case RADIO_DISABLE_CC:
            if (RADIO_VerifyCmdLength(RADIO_AppData.MsgPtr, sizeof(RADIO_NoArgs_cmd_t)) == OS_SUCCESS)
            {
                #ifdef RADIO_CFG_DEBUG
                    OS_printf("RADIO: RADIO_DISABLE_CC received \n");
                #endif
                RADIO_Disable();
            }
            break;

        /*
        ** Set Configuration Command
        */
        case RADIO_CONFIG_CC:
            if (RADIO_VerifyCmdLength(RADIO_AppData.MsgPtr, sizeof(RADIO_Config_cmd_t)) == OS_SUCCESS)
            {
                #ifdef RADIO_CFG_DEBUG
                    OS_printf("RADIO: RADIO_CONFIG_CC received \n");
                #endif
                RADIO_Configure();
            }
            break;

        /*
        ** Radio Service
        */
       case RADIO_SERVICE_CC:
            if (RADIO_VerifyCmdLength(RADIO_AppData.MsgPtr, sizeof(RADIO_NoArgs_cmd_t)) == OS_SUCCESS)
            {
                #ifdef RADIO_CFG_DEBUG
                    OS_printf("RADIO: RADIO_SERVICE_CC received \n");
                #endif
                RADIO_Service();
            }
            break;  

        /*
        ** Invalid Command Codes
        */
        default:
            /* Increment the command error counter upon receipt of an invalid command */
            RADIO_AppData.HkTelemetryPkt.CommandErrorCount++;

            /* Send invalid command code failure to the console */
            CFE_EVS_SendEvent(RADIO_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                                "RADIO: Invalid command code for packet, MID = 0x%x, cmdCode = 0x%x",
                                CFE_SB_MsgIdToValue(MsgId), CommandCode);
            break;
    }
    return;
}

/*
** Process Telemetry Request - Triggered in response to a telemetry request
*/
void RADIO_ProcessTelemetryRequest(void)
{
    CFE_SB_MsgId_t    MsgId       = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t CommandCode = 0;

    /* MsgId is only needed if the command code is not recognized. See default case */
    CFE_MSG_GetMsgId(RADIO_AppData.MsgPtr, &MsgId);

    /* Pull this command code from the message and then process */
    CFE_MSG_GetFcnCode(RADIO_AppData.MsgPtr, &CommandCode);
    switch (CommandCode)
    {
        case RADIO_REQ_HK_TLM:
            RADIO_ReportHousekeeping();
            break;

        /*
        ** Invalid Command Codes
        */
        default:
            /* Increment the error counter upon receipt of an invalid command */
            RADIO_AppData.HkTelemetryPkt.CommandErrorCount++;

            /* Send invalid command code failure to the console */
            CFE_EVS_SendEvent(RADIO_DEVICE_TLM_ERR_EID, CFE_EVS_EventType_ERROR,
                              "RADIO: Invalid command code for packet, MID = 0x%x, cmdCode = 0x%x",
                              CFE_SB_MsgIdToValue(MsgId), CommandCode);
            break;
    }
    return;
}

/*
** Report Application Housekeeping
*/
void RADIO_ReportHousekeeping(void)
{
    int32 status = OS_SUCCESS;

    /* Use SPI for HK request */
    if (RADIO_AppData.HkTelemetryPkt.DeviceEnabled == RADIO_DEVICE_ENABLED)
    {
        status = RADIO_RequestHK(&RADIO_AppData.RadioSpi,
                                 (RADIO_Device_HK_tlm_t *)&RADIO_AppData.HkTelemetryPkt.DeviceHK);
        if (status == OS_SUCCESS)
        {
            RADIO_AppData.HkTelemetryPkt.DeviceCount++;
        }
        else
        {
            RADIO_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(RADIO_REQ_HK_ERR_EID, CFE_EVS_EventType_ERROR,
                              "RADIO: Request device HK reported error %d", status);
        }
    }
    /* Intentionally do not report errors if disabled */

    /* Time stamp and publish housekeeping telemetry */
    CFE_SB_TimeStampMsg((CFE_MSG_Message_t *)&RADIO_AppData.HkTelemetryPkt);
    CFE_SB_TransmitMsg((CFE_MSG_Message_t *)&RADIO_AppData.HkTelemetryPkt, true);
    return;
}

/*
** Reset all global counter variables
*/
void RADIO_ResetCounters(void)
{
    /* Do any necessary checks, none for reset counters */

    /* Increment command success or error counter, omitted as action is to reset */

    /* Do the action, clear all global counter variables */
    RADIO_AppData.HkTelemetryPkt.CommandErrorCount = 0;
    RADIO_AppData.HkTelemetryPkt.CommandCount      = 0;
    RADIO_AppData.HkTelemetryPkt.DeviceErrorCount  = 0;
    RADIO_AppData.HkTelemetryPkt.DeviceCount       = 0;

    /* Increment device success or error counter, none as application only */

    /* Send event success to the console */
    CFE_EVS_SendEvent(RADIO_CMD_RESET_INF_EID, CFE_EVS_EventType_INFORMATION,
                      "RADIO: RESET counters command received");
    return;
}


/*
** Enable component
*/
void RADIO_Enable(void)
{
    int32 status = OS_SUCCESS;

    /* Do any necessary checks, confirm that device is currently disabled */
    if (RADIO_AppData.HkTelemetryPkt.DeviceEnabled == RADIO_DEVICE_DISABLED)
    {
        /* Increment command success counter */
        RADIO_AppData.HkTelemetryPkt.CommandCount++;

        /*
        ** Do the action, initialize hardware interface and set enabled
        */
        RADIO_AppData.RadioSpi.bus = RADIO_CFG_SPI_BUS;
        RADIO_AppData.RadioSpi.cs = RADIO_CFG_SPI_CS;
        RADIO_AppData.RadioSpi.isOpen = SPI_DEVICE_CLOSED;

        RADIO_AppData.RadioPowerGpio.pin = RADIO_CFG_GPIO_POWER_PIN;
        RADIO_AppData.RadioPowerGpio.direction = GPIO_OUTPUT;
        RADIO_AppData.RadioPowerGpio.isOpen = GPIO_CLOSED;

        RADIO_AppData.RadioInterruptGpio.pin = RADIO_CFG_GPIO_INTERRUPT_PIN;
        RADIO_AppData.RadioInterruptGpio.direction = GPIO_INPUT;
        RADIO_AppData.RadioInterruptGpio.isOpen = GPIO_CLOSED;

        status = RADIO_InitDevice(&RADIO_AppData.RadioSpi, &RADIO_AppData.RadioPowerGpio, &RADIO_AppData.RadioInterruptGpio);
        if (status == OS_SUCCESS)
        {
            /* Power on the radio */
            status = RADIO_PowerOn(&RADIO_AppData.RadioPowerGpio);
            if (status == OS_SUCCESS)
            {
                RADIO_AppData.HkTelemetryPkt.DeviceEnabled = RADIO_DEVICE_ENABLED;

                /* Increment device success counter */
                RADIO_AppData.HkTelemetryPkt.DeviceCount++;

                /* Send device event success to the console */
                CFE_EVS_SendEvent(RADIO_ENABLE_INF_EID, CFE_EVS_EventType_INFORMATION,
                                  "RADIO: Device enabled successfully");
            }
            else
            {
                /* Increment device error counter */
                RADIO_AppData.HkTelemetryPkt.DeviceErrorCount++;

                /* Send device event error to the console */
                CFE_EVS_SendEvent(RADIO_ENABLE_ERR_EID, CFE_EVS_EventType_ERROR,
                                  "RADIO: Device failed to power on, status=%d", status);
            }
        }
        else
        {
            /* Increment device error counter */
            RADIO_AppData.HkTelemetryPkt.DeviceErrorCount++;

            /* Send device event error to the console */
            CFE_EVS_SendEvent(RADIO_ENABLE_ERR_EID, CFE_EVS_EventType_ERROR,
                              "RADIO: Device failed to initialize, status=%d", status);
        }
    }
    else
    {
        /* Increment command error counter */
        RADIO_AppData.HkTelemetryPkt.CommandErrorCount++;

        /* Send command event error to the console */
        CFE_EVS_SendEvent(RADIO_ENABLE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "RADIO: Device enable rejected, device already enabled");
    }
    return;
}

/*
** Disable component
*/
void RADIO_Disable(void)
{
    /* Do any necessary checks, confirm that device is currently enabled */
    if (RADIO_AppData.HkTelemetryPkt.DeviceEnabled == RADIO_DEVICE_ENABLED)
    {
        /* Increment command success counter */
        RADIO_AppData.HkTelemetryPkt.CommandCount++;

        /*
        ** Do the action, close hardware interface and set disabled
        */
        
        /* Power off the radio */
        RADIO_PowerOff(&RADIO_AppData.RadioPowerGpio);
        
        /* Close SPI device */
        if (RADIO_AppData.RadioSpi.isOpen == SPI_DEVICE_OPEN)
        {
            spi_close_device(&RADIO_AppData.RadioSpi);
            RADIO_AppData.RadioSpi.isOpen = SPI_DEVICE_CLOSED;
        }
        
        /* Close GPIO devices */
        if (RADIO_AppData.RadioPowerGpio.isOpen == GPIO_OPEN)
        {
            gpio_close(&RADIO_AppData.RadioPowerGpio);
            RADIO_AppData.RadioPowerGpio.isOpen = GPIO_CLOSED;
        }
        
        if (RADIO_AppData.RadioInterruptGpio.isOpen == GPIO_OPEN)
        {
            gpio_close(&RADIO_AppData.RadioInterruptGpio);
            RADIO_AppData.RadioInterruptGpio.isOpen = GPIO_CLOSED;
        }

        RADIO_AppData.HkTelemetryPkt.DeviceEnabled = RADIO_DEVICE_DISABLED;

        /* Increment device success counter */
        RADIO_AppData.HkTelemetryPkt.DeviceCount++;

        /* Send device event success to the console */
        CFE_EVS_SendEvent(RADIO_DISABLE_INF_EID, CFE_EVS_EventType_INFORMATION,
                          "RADIO: Device disabled successfully");
    }
    else
    {
        /* Increment command error counter */
        RADIO_AppData.HkTelemetryPkt.CommandErrorCount++;

        /* Send command event error to the console */
        CFE_EVS_SendEvent(RADIO_DISABLE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "RADIO: Device disable rejected, device already disabled");
    }
    return;
}

/*
** Configure component
*/
void RADIO_Configure(void)
{
    int32 status        = OS_SUCCESS;
    int32 device_status = OS_SUCCESS;
    RADIO_Config_cmd_t *config_cmd    = (RADIO_Config_cmd_t *)RADIO_AppData.MsgPtr;

    /* Do any necessary checks, confirm that device is currently enabled */
    if (RADIO_AppData.HkTelemetryPkt.DeviceEnabled != RADIO_DEVICE_ENABLED)
    {
        status = OS_ERROR;
        /* Increment command error count */
        RADIO_AppData.HkTelemetryPkt.CommandErrorCount++;

        /* Send event logging failure of check to the console */
        CFE_EVS_SendEvent(RADIO_CMD_CONFIG_EN_ERR_EID, CFE_EVS_EventType_ERROR,
                          "RADIO: Configuration command invalid when device disabled");
    }

    /* Do any necessary checks, confirm valid configuration value */
    if (config_cmd->DeviceCfg.Mode > RADIO_MODE_DUPLEX)
    {
        status = OS_ERROR;
        /* Increment command error count */
        RADIO_AppData.HkTelemetryPkt.CommandErrorCount++;

        /* Send event logging failure of check to the console */
        CFE_EVS_SendEvent(RADIO_CMD_CONFIG_VAL_ERR_EID, CFE_EVS_EventType_ERROR,
                          "RADIO: Configuration command with mode %u is invalid", config_cmd->DeviceCfg.Mode);
    }

    if (status == OS_SUCCESS)
    {
        /* Increment command success counter */
        RADIO_AppData.HkTelemetryPkt.CommandCount++;

        /* Do the action, command device with new configuration using SPI */
        device_status = RADIO_SetConfiguration(&RADIO_AppData.RadioSpi, &config_cmd->DeviceCfg);
        if (device_status == OS_SUCCESS)
        {
            /* Increment device success counter */
            RADIO_AppData.HkTelemetryPkt.DeviceCount++;

            /* Send device event success to the console */
            CFE_EVS_SendEvent(RADIO_CMD_CONFIG_INF_EID, CFE_EVS_EventType_INFORMATION,
                              "RADIO: Configuration command received");
        }
        else
        {
            /* Increment device error counter */
            RADIO_AppData.HkTelemetryPkt.DeviceErrorCount++;

            /* Send device event failure to the console */
            CFE_EVS_SendEvent(RADIO_CMD_CONFIG_DEV_ERR_EID, CFE_EVS_EventType_ERROR,
                              "RADIO: Configuration command failed");
        }
    }
    return;
}

/*
** Service uplink
*/
void RADIO_ServiceUplink(void)
{
    uint16_t actual_length = 0;
    uint32 max_rx_transactions = RADIO_CFG_MAX_RX_MSGS_PER_POLL;

    /* Outer loop - perform up to max_rx_transactions receive attempts */
    while (max_rx_transactions > 0)
    {
        /* Receive data from the radio into the tail of the buffer */
        int32 recv_status = RADIO_ReceiveData(&RADIO_AppData.RadioSpi,
                                             RADIO_AppData.ReceiveBuffer + RADIO_AppData.ReceiveBuffLength,
                                             RADIO_MAX_PAYLOAD_SIZE - RADIO_AppData.ReceiveBuffLength,
                                             &actual_length);

        /* If receive failed, record error and stop trying this poll */
        if (recv_status != OS_SUCCESS)
        {
            RADIO_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(RADIO_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                              "RADIO: RADIO_ReceiveData failed, status=%d", (int)recv_status);
            break;
        }

        /* Advance the buffer length, but guard against overflow */
        RADIO_AppData.ReceiveBuffLength += actual_length;
        if (RADIO_AppData.ReceiveBuffLength > RADIO_MAX_PAYLOAD_SIZE)
        {
            /* Buffer overflow - drop contents and report error */
            RADIO_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(RADIO_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                            "RADIO: receive buffer overflow, len=%u", (unsigned int)RADIO_AppData.ReceiveBuffLength);
            RADIO_AppData.ReceiveBuffLength = 0;
            break;
        }

        /* --- TC Frame Processing with CryptoLib --- */
        if (RADIO_AppData.ReceiveBuffLength > 0) 
        {
            #ifdef RADIO_CFG_DEBUG
            OS_printf("RADIO_Service: Received TC frame (%u bytes): ", RADIO_AppData.ReceiveBuffLength);
            for (uint32 i = 0; i < RADIO_AppData.ReceiveBuffLength && i < 16; ++i) 
            {
                OS_printf("%02X ", RADIO_AppData.ReceiveBuffer[i]);
            }
            OS_printf("\n");
            #endif
            
            /* Process TC frames through CryptoLib: split concatenated TFs and handle partial frames */
            {
                TC_t crypto_tc_frame;
                uint32_t offset = 0;
                uint32_t buf_len = RADIO_AppData.ReceiveBuffLength;

                while (offset + 5 <= buf_len) /* need at least primary header */
                {
                    uint8_t *cur = RADIO_AppData.ReceiveBuffer + offset;
                    uint16_t fl = (uint16_t)((cur[2] & 0x03) << 8) | (uint16_t)cur[3];
                    uint32_t frame_len = (uint32_t)fl + 1U;

                    /* Sanity check frame_len */
                    if (frame_len < 5)
                    {
                        /* Dump the offending header bytes to help diagnose misalignment/padding */
                        OS_printf("RADIO_Service: Invalid TF length parsed (%u) at offset %u, header bytes: %02X %02X %02X %02X\n",
                                  (unsigned)frame_len, (unsigned)offset, cur[0], cur[1], cur[2], cur[3]);

                        /* Try to resynchronize: scan forward to find a plausible TF header instead of dropping entire buffer */
                        uint32_t new_offset = offset + 1;
                        uint8_t *base = RADIO_AppData.ReceiveBuffer;
                        int found = 0;
                        for (; new_offset + 5 <= buf_len; ++new_offset)
                        {
                            uint8_t *probe = base + new_offset;
                            uint16_t pfl = (uint16_t)((probe[2] & 0x03) << 8) | (uint16_t)probe[3];
                            uint32_t pframe_len = (uint32_t)pfl + 1U;
                            if (pframe_len >= 5 && new_offset + pframe_len <= buf_len)
                            {
                                found = 1;
                                break;
                            }
                        }

                        if (found)
                        {
                            OS_printf("RADIO_Service: Resynced: skipping %u bytes, new offset=%u\n", (unsigned)(new_offset - offset), (unsigned)new_offset);
                            offset = new_offset;
                            continue; /* re-evaluate at new offset */
                        }

                        /* If resync failed, drop the buffer as before */
                        CFE_EVS_SendEvent(RADIO_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                                          "RADIO: Invalid TF length parsed (%u) at offset %u, dropping buffer", (unsigned)frame_len, (unsigned)offset);
                        offset = buf_len; /* force exit */
                        break;
                    }

                    if (offset + frame_len > buf_len)
                    {
                        /* Partial frame - keep for next poll */
                        break;
                    }

                    /* Parse TF header fields for logging and GVCID checks */
                    uint8_t tfvn = (uint8_t)((cur[0] & 0xC0) >> 6);
                    uint16_t scid = (uint16_t)(((cur[0] & 0x03) << 8) | cur[1]);
                    uint8_t vcid = (uint8_t)(((cur[2] & 0xFC) >> 2) & 0x3F);
                    uint8_t segmentation_hdr = 0;
                    uint8_t map_id = 0;
                    if (frame_len >= 6)
                    {
                        segmentation_hdr = cur[5];
                        map_id = segmentation_hdr & 0x3F;
                    }

                    #ifdef RADIO_CFG_DEBUG
                    /* Debug: print the TF length and first bytes to verify splitting */
                    OS_printf("RADIO_Service: Passing TF to Crypto (len=%u) header:", (unsigned)frame_len);
                    for (uint32 dbg_i = 0; dbg_i < frame_len && dbg_i < 16; ++dbg_i)
                    {
                        OS_printf(" %02X", cur[dbg_i]);
                    }
                    OS_printf("\n");
                    #endif

                    /* Process this single TC frame */
                    int32 frame_size = (int32)frame_len;
                    int32 crypto_status = Crypto_TC_ProcessSecurity((uint8_t *)cur, &frame_size, &crypto_tc_frame);

                    /* If managed parameters missing, log the TF header GVCID for diagnostics */
                    if (crypto_status == MANAGED_PARAMETERS_FOR_GVCID_NOT_FOUND)
                    {
                        CFE_EVS_SendEvent(RADIO_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                                          "RADIO: Crypto missing managed params for GVCID tfvn=%u scid=%u vcid=%u map_id=%u",
                                          (unsigned)tfvn, (unsigned)scid, (unsigned)vcid, (unsigned)map_id);
                    }

                    if (crypto_status == CRYPTO_LIB_SUCCESS)
                    {
                        #ifdef RADIO_CFG_DEBUG
                        /* Debug prints for processed payload */
                        OS_printf("RADIO_Service: Processed TC payload (%u bytes): ", crypto_tc_frame.tc_pdu_len);
                        for (uint16 i = 0; i < crypto_tc_frame.tc_pdu_len && i < 16; i++)
                        {
                            OS_printf("%02X ", crypto_tc_frame.tc_pdu[i]);
                        }
                        OS_printf("\n");
                        #endif

                        /* Look for valid CCSDS Space Packets in the processed payload */
                        for (uint32_t scan_offset = 0; scan_offset <= crypto_tc_frame.tc_pdu_len && (crypto_tc_frame.tc_pdu_len - scan_offset) >= 6; scan_offset++)
                        {
                            uint8 *potential_packet = crypto_tc_frame.tc_pdu + scan_offset;
                            uint16 packet_id = (potential_packet[0] << 8) | potential_packet[1];
                            uint16 packet_seq = (potential_packet[2] << 8) | potential_packet[3];
                            uint16 packet_len = (potential_packet[4] << 8) | potential_packet[5];
                            uint8 version = (packet_id >> 13) & 0x07;
                            uint8 type = (packet_id >> 12) & 0x01;
                            uint8 seq_flags = (packet_seq >> 14) & 0x03;
                            uint16 total_packet_size = packet_len + 7;

                            if (version == 0 && type == 1 && seq_flags <= 3 && total_packet_size >= 7 &&
                                scan_offset + total_packet_size <= crypto_tc_frame.tc_pdu_len)
                                {
                                #ifdef RADIO_CFG_DEBUG
                                uint16 apid = packet_id & 0x07FF;
                                uint16 seq_count = packet_seq & 0x3FFF;
                                OS_printf("RADIO_Service: Found valid CCSDS command packet at offset %u\n", scan_offset);
                                OS_printf("  PacketID=0x%04X, APID=%u, Type=%u, SeqCount=%u, Length=%u\n",
                                          packet_id, apid, type, seq_count, total_packet_size);

                                CFE_SB_MsgId_t msg_id = CFE_SB_INVALID_MSG_ID;
                                size_t msg_len = 0;
                                if (CFE_MSG_GetMsgId((CFE_MSG_Message_t *)&sb_buf->Msg, &msg_id) == CFE_SUCCESS &&
                                    CFE_MSG_GetSize((CFE_MSG_Message_t *)&sb_buf->Msg, &msg_len) == CFE_SUCCESS)
                                {
                                    OS_printf("RADIO: forwarding space packet to SB MsgId=0x%04X len=%zu\n", CFE_SB_MsgIdToValue(msg_id), msg_len);
                                }
                                else
                                {
                                    OS_printf("RADIO: forwarding space packet to SB (MsgId/len parse failed)\n");
                                }
                                #endif
                                CFE_SB_TransmitMsg((CFE_MSG_Message_t *)potential_packet, true);
                                scan_offset += total_packet_size - 1;
                            }
                        }
                    }
                    else
                    {
                        RADIO_AppData.HkTelemetryPkt.DeviceErrorCount++;
                        CFE_EVS_SendEvent(RADIO_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                                          "RADIO: Crypto_TC_ProcessSecurity failed, status=%d", crypto_status);
                    }

                    /* Advance to next TF in buffer */
                    offset += frame_len;
                }

                /* If there are trailing partial bytes, preserve them */
                if (offset < buf_len)
                {
                    uint32_t remaining = buf_len - offset;
                    memmove(RADIO_AppData.ReceiveBuffer, RADIO_AppData.ReceiveBuffer + offset, remaining);
                    RADIO_AppData.ReceiveBuffLength = (uint16_t)remaining;
                }
                else
                {
                    RADIO_AppData.ReceiveBuffLength = 0;
                }
            }
        }

        /* Decrement receive attempts and continue to try to read more packets */
        max_rx_transactions--;
    }
}

/*
** Service downlink
*/
void RADIO_ServiceDownlink(void)
{
    /* Downlink: wrap packets from downlink pipe into a TM frame and transmit */
    /* Use persistent configs initialized in RADIO_AppInit */
    TM_SDLP_GlobalConfig_t *global_cfg = &radio_global_cfg;
    TM_SDLP_ChannelConfig_t *channel_cfg = &radio_channel_cfg;
    TM_SDLP_FrameInfo_t *frame_info = &radio_frame_info;
    int32 status;
    uint32 pkt_count = 0;    
    CFE_SB_Buffer_t *SBBufPtr;
    size_t pkt_len = 0;

    #ifdef RADIO_CFG_DEBUG
    /* global_cfg/channel_cfg were set during RADIO_AppInit; log active values */
    OS_printf("RADIO_ServiceDownlink: scId = %u, vcId = %u\n", (unsigned)global_cfg->scId, (unsigned)channel_cfg->vcId);
    #endif

    /* Initialize channel and start start if not ready */
    if (!frame_info->isReady) 
    {
        status = TM_SDLP_InitChannel(frame_info, static_tm_frame, static_tm_overflow, global_cfg, channel_cfg);
        if (status != TM_SDLP_SUCCESS) 
        {
            RADIO_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(RADIO_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                              "RADIO: TM frame init failed, status=%d", (int)status);
            return;
        }

        status = TM_SDLP_StartFrame(frame_info);
        if (status != TM_SDLP_SUCCESS) 
        {
            RADIO_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(RADIO_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                            "RADIO: TM frame start failed, status=%d", (int)status);
            return;
        }
    }

    /* Process each TM frame */
    while (pkt_count < RADIO_CFG_MAX_TX_MSGS_PER_POLL)
    {

        /* Process each packet */
        while (pkt_count < RADIO_CFG_MAX_TX_MSGS_PER_POLL)
        {

            if (pkt_len == 0)
            {
                /* Read one packet */
                CFE_Status_t sb_status = CFE_SB_ReceiveBuffer(&SBBufPtr, RADIO_DownlinkPipe, CFE_SB_POLL);
                if (sb_status != CFE_SUCCESS || SBBufPtr == NULL) 
                {
                    /* No more packets available */
                    pkt_count = RADIO_CFG_MAX_TX_MSGS_PER_POLL;
                    break;
                }
            }

            /* Check frame ready for packet */
            if (!frame_info->isReady) 
            {
                status = TM_SDLP_StartFrame(frame_info);
                if (status != TM_SDLP_SUCCESS) 
                {
                    RADIO_AppData.HkTelemetryPkt.DeviceErrorCount++;
                    CFE_EVS_SendEvent(RADIO_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                                    "RADIO: TM frame start failed for packet, status=%d", (int)status);
                    break;
                }
            }

            CFE_MSG_GetSize((CFE_MSG_Message_t *)&SBBufPtr->Msg, &pkt_len);
            if (pkt_len < frame_info->freeOctets)
            {
                /* Add this single packet to the frame */
                int32 add_status = TM_SDLP_AddPacket(frame_info, (CFE_MSG_Message_t *)&SBBufPtr->Msg);
                if (add_status < 0) 
                {
                    RADIO_AppData.HkTelemetryPkt.DeviceErrorCount++;
                    CFE_EVS_SendEvent(RADIO_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                                    "RADIO: Failed to add packet to TM frame, status=%d", (int)add_status);
                    break;
                }
                else
                {
                    pkt_len = 0;
                }
                pkt_count++;
            }
            else
            {
                /* Packet too large to fit in frame, leave for next frame */
                break;
            }
        }

        /* Add idle packet to fill remaining free space */
        TM_SDLP_AddIdlePacket(frame_info, IdlePacket);

        /* Finalize the frame (frame count and OCF can be customized if needed) */
        uint8 mc_frame_cnt = 0;
        uint8 ocf_local[4] = {0};
        status = TM_SDLP_CompleteFrame(frame_info, &mc_frame_cnt, ocf_local);
        if (status != TM_SDLP_SUCCESS) 
        {
            RADIO_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(RADIO_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                            "RADIO: TM frame finalize failed, status=%d", (int)status);
            return;
        }

        uint8 *pframe = (uint8 *)frame_info->frame;
        size_t frame_len = (size_t)global_cfg->frameLength;
        #ifdef RADIO_CFG_DEBUG
        OS_printf("TM frame completed for packet %u, ptr=%p len=%u: 0x", pkt_count, (void*)pframe, (unsigned)frame_len);
        for (int i = 0; i < (int)frame_len && i < 32; ++i) {
            OS_printf("%02X", pframe[i]);
        }
        if (frame_len > 32) OS_printf("...");
        OS_printf("\n");
        #endif

        /* Apply TM frame security using CryptoLib */
        SaInterface sa_if_local = get_sa_interface_inmemory();
        SecurityAssociation_t *sa_ptr = NULL;
        int32 sa_status = -1;
        if (sa_if_local && sa_if_local->sa_get_operational_sa_from_gvcid) 
        {
            sa_status = sa_if_local->sa_get_operational_sa_from_gvcid(0, global_cfg->scId, channel_cfg->vcId, 0, &sa_ptr);
        }
        if (sa_status == 0 && sa_ptr != NULL) 
        {
            /* Call Crypto on the exact frame and length produced by TM SDLP */
            int32 sec_status = Crypto_TM_ApplySecurity(pframe, (uint16_t)frame_len);
            if (sec_status != 0) 
            {
                RADIO_AppData.HkTelemetryPkt.DeviceErrorCount++;
                CFE_EVS_SendEvent(RADIO_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                                "RADIO: TM_ApplySecurity failed, status=%d", (int)sec_status);
            }
        } 
        else 
        {
            RADIO_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(RADIO_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                            "RADIO: Could not get SecurityAssociation for TM frame");
        }

        /* Copy the TM frame to the CADU buffer after ASM space */
        memcpy(static_cadu_buffer + TM_SYNC_ASM_SIZE, pframe, frame_len);
        
        int32 cadu_size = TM_SYNC_Synchronize(static_cadu_buffer, (char*)TM_SYNC_ASM_STR, 
                                            (uint8)TM_SYNC_ASM_SIZE,
                                            (uint16)frame_len, 
                                            (bool)false);
        if (cadu_size < 0)
        {
            RADIO_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(RADIO_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                            "RADIO: TM_SYNC_Synchronize failed, status=%d", (int)cadu_size);
            return;
        }

        /* Update pointers to use the synchronized CADU buffer */
        uint8 *final_frame = static_cadu_buffer;
        size_t final_frame_len = (size_t)cadu_size;

        #ifdef RADIO_CFG_DEBUG
        OS_printf("RADIO: Packet %u - Original frame_len=%d, CADU size=%d, transmitting final_frame_len=%d\n", 
                pkt_count, (int)frame_len, (int)cadu_size, (int)final_frame_len);
        #endif

        /* Transmit the TM frame over the radio interface */
        int32 tx_status = RADIO_SendData(&RADIO_AppData.RadioSpi, final_frame, (uint16_t)final_frame_len);
        if (tx_status == OS_SUCCESS) 
        {
            RADIO_AppData.HkTelemetryPkt.DeviceCount++;
        } 
        else 
        {
            RADIO_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(RADIO_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                            "RADIO: Failed to transmit TM frame, status=%d", (int)tx_status);
        }
    }
}

/*
** Service the radio, sending and receiving data available
*/
void RADIO_Service(void)
{
    CFE_SB_Buffer_t *discard_buf = NULL;
    
    /* Service uplink if enabled and in correct mode */
    if  ((RADIO_AppData.HkTelemetryPkt.DeviceEnabled == RADIO_DEVICE_ENABLED) &&
            (RADIO_AppData.HkTelemetryPkt.DeviceHK.Mode == RADIO_MODE_RX ||
            RADIO_AppData.HkTelemetryPkt.DeviceHK.Mode == RADIO_MODE_DUPLEX))
    {
        RADIO_ServiceUplink();
    }

    /* Service downlink if enabled and in correct mode */
    if  ((RADIO_AppData.HkTelemetryPkt.DeviceEnabled == RADIO_DEVICE_ENABLED) &&
         (RADIO_AppData.HkTelemetryPkt.DeviceHK.Mode == RADIO_MODE_TX ||
          RADIO_AppData.HkTelemetryPkt.DeviceHK.Mode == RADIO_MODE_DUPLEX))
    {
        RADIO_ServiceDownlink();
    }
    else
    {
        /* Clear the downlink pipe by reading and discarding all messages */
        while (CFE_SB_ReceiveBuffer(&discard_buf, RADIO_DownlinkPipe, CFE_SB_POLL) == CFE_SUCCESS)
        {
            /* Discard the message */
        }
    }
    return;
}

/*
** Verify command packet length matches expected
*/
int32 RADIO_VerifyCmdLength(CFE_MSG_Message_t *msg, uint16 expected_length)
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

        CFE_EVS_SendEvent(RADIO_LEN_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Invalid msg length: ID = 0x%X,  CC = %d, Len = %zu, Expected = %d",
                          CFE_SB_MsgIdToValue(msg_id), cmd_code, actual_length, expected_length);

        status = OS_ERROR;

        /* Increment the command error counter upon receipt of an invalid command length */
        RADIO_AppData.HkTelemetryPkt.CommandErrorCount++;
    }
    return status;
}