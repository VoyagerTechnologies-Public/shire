#include "eps_app_coveragetest_common.h"
#include "ut_eps_app.h"

typedef struct
{
    uint16      ExpectedEvent;
    uint32      MatchCount;
    const char *ExpectedFormat;
} UT_CheckEvent_t;

/*
 * An example hook function to check for a specific event.
 */
static int32 UT_CheckEvent_Hook(void *UserObj, int32 StubRetcode, uint32 CallCount, const UT_StubContext_t *Context,
                                va_list va)
{
    UT_CheckEvent_t *State = UserObj;
    uint16           EventId;
    const char      *Spec;

    /*
     * The CFE_EVS_SendEvent stub passes the EventID as the
     * first context argument.
     */
    if (Context->ArgCount > 0)
    {
        EventId = UT_Hook_GetArgValueByName(Context, "EventID", uint16);
        if (EventId == State->ExpectedEvent)
        {
            if (State->ExpectedFormat != NULL)
            {
                Spec = UT_Hook_GetArgValueByName(Context, "Spec", const char *);
                if (Spec != NULL)
                {
                    /*
                     * Example of how to validate the full argument set.
                     * ------------------------------------------------
                     *
                     * If really desired one can call something like:
                     *
                     * char TestText[CFE_MISSION_EVS_MAX_MESSAGE_LENGTH];
                     * vsnprintf(TestText, sizeof(TestText), Spec, va);
                     *
                     * And then compare the output (TestText) to the expected fully-rendered string.
                     *
                     * NOTE: While this can be done, use with discretion - This isn't really
                     * verifying that the FSW code unit generated the correct event text,
                     * rather it is validating what the system snprintf() library function
                     * produces when passed the format string and args.
                     *
                     * This type of check has been demonstrated to make tests very fragile,
                     * because it is influenced by many factors outside the control of the
                     * test case.
                     *
                     * __This derived string is not an actual output of the unit under test__
                     */
                    if (strcmp(Spec, State->ExpectedFormat) == 0)
                    {
                        ++State->MatchCount;
                    }
                }
            }
            else
            {
                ++State->MatchCount;
            }
        }
    }

    return 0;
}

/*
 * Helper function to set up for event checking
 * This attaches the hook function to CFE_EVS_SendEvent
 */
static void UT_CheckEvent_Setup(UT_CheckEvent_t *Evt, uint16 ExpectedEvent, const char *ExpectedFormat)
{
    memset(Evt, 0, sizeof(*Evt));
    Evt->ExpectedEvent  = ExpectedEvent;
    Evt->ExpectedFormat = ExpectedFormat;
    UT_SetVaHookFunction(UT_KEY(CFE_EVS_SendEvent), UT_CheckEvent_Hook, Evt);
}

/*
**********************************************************************************
**          TEST CASE FUNCTIONS
**********************************************************************************
*/

void Test_EPS_AppMain(void)
{
    CFE_SB_MsgId_t MsgId = CFE_SB_INVALID_MSG_ID;

    /*
     * Test Case For:
     * void EPS_AppMain( void )
     */
    UT_CheckEvent_t EventTest;

    /*
     * EPS_AppMain does not return a value,
     * but it has several internal decision points
     * that need to be exercised here.
     *
     * First call it in "nominal" mode where all
     * dependent calls should be successful by default.
     */
    EPS_AppMain();

    /*
     * Confirm that CFE_ES_ExitApp() was called at the end of execution
     */
    UtAssert_True(UT_GetStubCount(UT_KEY(CFE_ES_ExitApp)) == 1, "CFE_ES_ExitApp() called");

    /*
     * Now set up individual cases for each of the error paths.
     * The first is for EPS_AppInit().  As this is in the same
     * code unit, it is not a stub where the return code can be
     * easily set.  In order to get this to fail, an underlying
     * call needs to fail, and the error gets propagated through.
     * The call to CFE_EVS_Register is the first opportunity.
     * Any identifiable (non-success) return code should work.
     */
    UT_SetDeferredRetcode(UT_KEY(CFE_EVS_Register), 1, CFE_EVS_INVALID_PARAMETER);

    /*
     * Just call the function again.  It does not return
     * the value, so there is nothing to test for here directly.
     * However, it should show up in the coverage report that
     * the EPS_AppInit() failure path was taken.
     */
    EPS_AppMain();

    /*
     * This can validate that the internal "RunStatus" was
     * set to CFE_ES_RunStatus_APP_ERROR, by querying the struct directly.
     *
     * It is always advisable to include the _actual_ values
     * when asserting on conditions, so if/when it fails, the
     * log will show what the incorrect value was.
     */
    UtAssert_True(EPS_AppData.RunStatus == CFE_ES_RunStatus_APP_ERROR,
                  "EPS_AppData.RunStatus (%lu) == CFE_ES_RunStatus_APP_ERROR",
                  (unsigned long)EPS_AppData.RunStatus);

    UT_SetDeferredRetcode(UT_KEY(CFE_EVS_SendEvent), 5, CFE_EVS_INVALID_PARAMETER);
    EPS_AppMain();

    /*
     * Note that CFE_ES_RunLoop returns a boolean value,
     * so in order to exercise the internal "while" loop,
     * it needs to return TRUE.  But this also needs to return
     * FALSE in order to get out of the loop, otherwise
     * it will stay there infinitely.
     *
     * The deferred retcode will accomplish this.
     */
    UT_SetDeferredRetcode(UT_KEY(CFE_ES_RunLoop), 1, true);

    /* Set up buffer for command processing */
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &MsgId, sizeof(MsgId), false);

    /*
     * Invoke again
     */
    EPS_AppMain();

    /*
     * Confirm that CFE_SB_ReceiveBuffer() (inside the loop) was called
     */
    UtAssert_True(UT_GetStubCount(UT_KEY(CFE_SB_ReceiveBuffer)) == 1, "CFE_SB_ReceiveBuffer() called");

    /*
     * Now also make the CFE_SB_ReceiveBuffer call fail,
     * to exercise that error path.  This sends an
     * event which can be checked with a hook function.
     */
    UT_SetDeferredRetcode(UT_KEY(CFE_ES_RunLoop), 1, true);
    UT_SetDeferredRetcode(UT_KEY(CFE_SB_ReceiveBuffer), 1, CFE_SB_PIPE_RD_ERR);
    UT_CheckEvent_Setup(&EventTest, EPS_PIPE_ERR_EID, "EPS: SB Pipe Read Error = %d");

    /*
     * Invoke again
     */
    EPS_AppMain();

    /*
     * Confirm that the event was generated
     */
    UtAssert_True(EventTest.MatchCount == 1, "EPS_PIPE_ERR_EID generated (%u)", (unsigned int)EventTest.MatchCount);
}

void Test_EPS_AppInit(void)
{
    /*
     * Test Case For:
     * int32 EPS_AppInit( void )
     */

    /* nominal case should return CFE_SUCCESS */
    UT_TEST_FUNCTION_RC(EPS_AppInit(), CFE_SUCCESS);

    /* trigger a failure for each of the sub-calls,
     * and confirm a write to syslog for each.
     * Note that this count accumulates, because the status
     * is _not_ reset between these test cases. */
    UT_SetDeferredRetcode(UT_KEY(CFE_EVS_Register), 1, CFE_EVS_INVALID_PARAMETER);
    UT_TEST_FUNCTION_RC(EPS_AppInit(), CFE_EVS_INVALID_PARAMETER);
    UtAssert_True(UT_GetStubCount(UT_KEY(CFE_ES_WriteToSysLog)) == 1, "CFE_ES_WriteToSysLog() called");

    UT_SetDeferredRetcode(UT_KEY(CFE_SB_CreatePipe), 1, CFE_SB_BAD_ARGUMENT);
    UT_TEST_FUNCTION_RC(EPS_AppInit(), CFE_SB_BAD_ARGUMENT);

    UT_SetDeferredRetcode(UT_KEY(CFE_SB_Subscribe), 1, CFE_SB_BAD_ARGUMENT);
    UT_TEST_FUNCTION_RC(EPS_AppInit(), CFE_SB_BAD_ARGUMENT);

    UT_SetDeferredRetcode(UT_KEY(CFE_SB_Subscribe), 1, CFE_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(CFE_SB_Subscribe), 1, CFE_SB_BAD_ARGUMENT);
    UT_TEST_FUNCTION_RC(EPS_AppInit(), CFE_SB_BAD_ARGUMENT);

    /* Test device initialization failure - device error event is sent but app init succeeds */
    UT_SetDeferredRetcode(UT_KEY(EPS_InitDevice), 1, I2C_ERROR);
    UT_TEST_FUNCTION_RC(EPS_AppInit(), CFE_SUCCESS);

    /* Test CFE_EVS_SendEvent failure in startup message (line 171) */
    uint32_t prev_syslog_count = UT_GetStubCount(UT_KEY(CFE_ES_WriteToSysLog));
    UT_SetDefaultReturnValue(UT_KEY(CFE_EVS_SendEvent), CFE_EVS_INVALID_PARAMETER);
    UT_TEST_FUNCTION_RC(EPS_AppInit(), CFE_EVS_INVALID_PARAMETER);
    UT_ClearDefaultReturnValue(UT_KEY(CFE_EVS_SendEvent));
    UtAssert_True(UT_GetStubCount(UT_KEY(CFE_ES_WriteToSysLog)) > prev_syslog_count, "CFE_ES_WriteToSysLog() called for SendEvent failure");
}

void Test_EPS_ProcessTelemetryRequest(void)
{
    CFE_SB_MsgId_t    TestMsgId;
    UT_CheckEvent_t   EventTest;
    CFE_MSG_FcnCode_t FcnCode;
    FcnCode = EPS_REQ_HK_TLM;

    TestMsgId = CFE_SB_ValueToMsgId(EPS_CMD_MID);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);

    UT_CheckEvent_Setup(&EventTest, EPS_REQ_DATA_ERR_EID, NULL);
    EPS_ProcessTelemetryRequest();
    UtAssert_True(EventTest.MatchCount == 0, "EPS_REQ_DATA_ERR_EID generated (%u)",
                  (unsigned int)EventTest.MatchCount);

    FcnCode = 99;
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    EPS_ProcessTelemetryRequest();
    UtAssert_True(EventTest.MatchCount == 0, "EPS_REQ_DATA_ERR_EID generated (%u)",
                  (unsigned int)EventTest.MatchCount);
}

void Test_EPS_ProcessCommandPacket(void)
{
    /*
     * Test Case For:
     * void EPS_ProcessCommandPacket
     */
    /* a buffer large enough for any command message */
    union
    {
        CFE_SB_Buffer_t     SBBuf;
        EPS_NoArgs_cmd_t Noop;
    } TestMsg;
    CFE_SB_MsgId_t    TestMsgId;
    CFE_MSG_FcnCode_t FcnCode;
    size_t            MsgSize;
    UT_CheckEvent_t   EventTest;

    memset(&TestMsg, 0, sizeof(TestMsg));
    UT_CheckEvent_Setup(&EventTest, EPS_PROCESS_CMD_ERR_EID, NULL);

    /*
     * The CFE_MSG_GetMsgId() stub uses a data buffer to hold the
     * message ID values to return.
     */
    TestMsgId = CFE_SB_ValueToMsgId(EPS_CMD_MID);
    FcnCode   = EPS_NOOP_CC;
    MsgSize   = sizeof(TestMsg.Noop);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &MsgSize, sizeof(MsgSize), false);
    EPS_ProcessCommandPacket();
    UtAssert_True(EventTest.MatchCount == 0, "EPS_CMD_ERR_EID not generated (%u)",
                  (unsigned int)EventTest.MatchCount);

    TestMsgId = CFE_SB_ValueToMsgId(EPS_REQ_HK_MID);
    FcnCode   = EPS_REQ_HK_TLM;
    MsgSize   = sizeof(TestMsg.Noop);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &MsgSize, sizeof(MsgSize), false);
    EPS_ProcessCommandPacket();
    UtAssert_True(EventTest.MatchCount == 0, "EPS_CMD_ERR_EID not generated (%u)",
                  (unsigned int)EventTest.MatchCount);

    /* invalid message id */
    TestMsgId = CFE_SB_INVALID_MSG_ID;
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    EPS_ProcessCommandPacket();
    UtAssert_True(EventTest.MatchCount == 1, "EPS_CMD_ERR_EID generated (%u)", (unsigned int)EventTest.MatchCount);
}

void Test_EPS_ProcessGroundCommand(void)
{
    /*
     * Test Case For:
     * void EPS_ProcessGroundCommand
     */
    CFE_SB_MsgId_t    TestMsgId = CFE_SB_ValueToMsgId(EPS_CMD_MID);
    CFE_MSG_FcnCode_t FcnCode;
    size_t            Size;

    /* a buffer large enough for any command message */
    union
    {
        CFE_SB_Buffer_t     SBBuf;
        EPS_NoArgs_cmd_t Noop;
        EPS_NoArgs_cmd_t Reset;
        EPS_NoArgs_cmd_t Enable;
        EPS_NoArgs_cmd_t Disable;
    EPS_Switch_cmd_t Switch;
    } TestMsg;
    UT_CheckEvent_t EventTest;

    memset(&TestMsg, 0, sizeof(TestMsg));

    /*
     * call with each of the supported command codes
     * The CFE_MSG_GetFcnCode stub allows the code to be
     * set to whatever is needed.  There is no return
     * value here and the actual implementation of these
     * commands have separate test cases, so this just
     * needs to exercise the "switch" statement.
     */

    /* test dispatch of NOOP */
    FcnCode = EPS_NOOP_CC;
    Size    = sizeof(TestMsg.Noop);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &Size, sizeof(Size), false);
    UT_CheckEvent_Setup(&EventTest, EPS_CMD_NOOP_INF_EID, NULL);
    EPS_ProcessGroundCommand();
    UtAssert_True(EventTest.MatchCount == 1, "EPS_CMD_NOOP_INF_EID generated (%u)",
                  (unsigned int)EventTest.MatchCount);
    /* test failure of command length */
    FcnCode = EPS_NOOP_CC;
    Size    = sizeof(TestMsg.Switch);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &Size, sizeof(Size), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    UT_CheckEvent_Setup(&EventTest, EPS_LEN_ERR_EID, NULL);
    EPS_ProcessGroundCommand();
    UtAssert_True(EventTest.MatchCount == 1, "EPS_LEN_ERR_EID generated (%u)", (unsigned int)EventTest.MatchCount);

    /* test dispatch of RESET */
    FcnCode = EPS_RESET_COUNTERS_CC;
    Size    = sizeof(TestMsg.Reset);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &Size, sizeof(Size), false);
    UT_CheckEvent_Setup(&EventTest, EPS_CMD_RESET_INF_EID, NULL);
    EPS_ProcessGroundCommand();
    UtAssert_True(EventTest.MatchCount == 1, "EPS_CMD_RESET_INF_EID generated (%u)",
                  (unsigned int)EventTest.MatchCount);
    /* test failure of command length */
    FcnCode = EPS_RESET_COUNTERS_CC;
    Size    = sizeof(TestMsg.Switch);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &Size, sizeof(Size), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    UT_CheckEvent_Setup(&EventTest, EPS_LEN_ERR_EID, NULL);
    EPS_ProcessGroundCommand();
    UtAssert_True(EventTest.MatchCount == 1, "EPS_LEN_ERR_EID generated (%u)", (unsigned int)EventTest.MatchCount);

    /* test dispatch of SWITCH_ON */
    FcnCode = EPS_SWITCH_ON_CC;
    Size    = sizeof(TestMsg.Switch);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &Size, sizeof(Size), false);
    UT_CheckEvent_Setup(&EventTest, EPS_CMD_SWITCH_ON_INF_EID, NULL);
    UT_SetDeferredRetcode(UT_KEY(EPS_CommandDevice), 1, OS_ERROR);
    CFE_MSG_Message_t msgPtr;
    EPS_AppData.MsgPtr = &msgPtr;
    EPS_ProcessGroundCommand();
    UtAssert_True(EventTest.MatchCount == 1, "EPS_CMD_SWITCH_ON_INF_EID generated (%u)",
                  (unsigned int)EventTest.MatchCount);

    /* test failure of command length */
    FcnCode = EPS_SWITCH_ON_CC;
    Size    = sizeof(TestMsg.Reset);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &Size, sizeof(Size), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    UT_CheckEvent_Setup(&EventTest, EPS_LEN_ERR_EID, NULL);
    EPS_ProcessGroundCommand();
    UtAssert_True(EventTest.MatchCount == 1, "EPS_LEN_ERR_EID generated (%u)", (unsigned int)EventTest.MatchCount);

    /* test dispatch of SWITCH_OFF */
    FcnCode = EPS_SWITCH_OFF_CC;
    Size    = sizeof(TestMsg.Switch);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &Size, sizeof(Size), false);
    UT_CheckEvent_Setup(&EventTest, EPS_CMD_SWITCH_OFF_INF_EID, NULL);
    UT_SetDeferredRetcode(UT_KEY(EPS_CommandDevice), 1, OS_SUCCESS);
    EPS_AppData.MsgPtr = &msgPtr;
    EPS_ProcessGroundCommand();
    UtAssert_True(EventTest.MatchCount == 1, "EPS_CMD_SWITCH_OFF_INF_EID generated (%u)",
                  (unsigned int)EventTest.MatchCount);

    /* test failure of command length */
    FcnCode = EPS_SWITCH_OFF_CC;
    Size    = sizeof(TestMsg.Reset);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &Size, sizeof(Size), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    UT_CheckEvent_Setup(&EventTest, EPS_LEN_ERR_EID, NULL);
    EPS_ProcessGroundCommand();
    UtAssert_True(EventTest.MatchCount == 1, "EPS_LEN_ERR_EID generated (%u)", (unsigned int)EventTest.MatchCount);

    /* test an invalid CC */
    FcnCode = 99;
    Size    = sizeof(TestMsg.Noop);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &TestMsgId, sizeof(TestMsgId), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &FcnCode, sizeof(FcnCode), false);
    UT_CheckEvent_Setup(&EventTest, EPS_CMD_ERR_EID, NULL);
    EPS_ProcessGroundCommand();
    UtAssert_True(EventTest.MatchCount == 1, "EPS_CMD_ERR_EID generated (%u)", (unsigned int)EventTest.MatchCount);
}

void Test_EPS_ResetCounters(void)
{
    /*
     * Test Case For:
     * void EPS_ResetCounters( void )
     */
    UT_CheckEvent_t EventTest;
    
    /* Set some counter values */
    EPS_AppData.HkTelemetryPkt.CommandErrorCount = 5;
    EPS_AppData.HkTelemetryPkt.CommandCount = 10;
    EPS_AppData.HkTelemetryPkt.DeviceErrorCount = 3;
    EPS_AppData.HkTelemetryPkt.DeviceCount = 15;
    
    /* Call reset */
    UT_CheckEvent_Setup(&EventTest, EPS_CMD_RESET_INF_EID, NULL);
    EPS_ResetCounters();
    
    /* Verify counters are reset */
    UtAssert_True(EPS_AppData.HkTelemetryPkt.CommandErrorCount == 0, "CommandErrorCount reset to 0");
    UtAssert_True(EPS_AppData.HkTelemetryPkt.CommandCount == 0, "CommandCount reset to 0");
    UtAssert_True(EPS_AppData.HkTelemetryPkt.DeviceErrorCount == 0, "DeviceErrorCount reset to 0");
    UtAssert_True(EPS_AppData.HkTelemetryPkt.DeviceCount == 0, "DeviceCount reset to 0");
    UtAssert_True(EventTest.MatchCount == 1, "EPS_CMD_RESET_INF_EID generated");
}

void Test_EPS_SetSwitchOff(void)
{
    /*
     * Test Case For:
     * void EPS_SetSwitchOff( void )
     */
    UT_CheckEvent_t EventTest;
    EPS_Switch_cmd_t SwitchCmd;
    
    /* Set up message pointer */
    EPS_AppData.MsgPtr = (CFE_MSG_Message_t *)&SwitchCmd;
    
    /* Test invalid switch number */
    SwitchCmd.SwitchNumber = EPS_NUM_SWITCHES;
    EPS_AppData.HkTelemetryPkt.CommandErrorCount = 0;
    UT_CheckEvent_Setup(&EventTest, EPS_CMD_SWITCH_OFF_ERR_EID, NULL);
    EPS_SetSwitchOff();
    UtAssert_True(EPS_AppData.HkTelemetryPkt.CommandErrorCount == 1, "Command error count incremented");
    UtAssert_True(EventTest.MatchCount == 1, "EPS_CMD_SWITCH_OFF_ERR_EID generated for invalid switch");
    
    /* Test successful switch off */
    SwitchCmd.SwitchNumber = 3;
    EPS_AppData.HkTelemetryPkt.CommandCount = 0;
    EPS_AppData.HkTelemetryPkt.DeviceCount = 0;
    UT_SetDeferredRetcode(UT_KEY(EPS_SetSwitch), 1, I2C_SUCCESS);
    UT_CheckEvent_Setup(&EventTest, EPS_CMD_SWITCH_OFF_INF_EID, NULL);
    EPS_SetSwitchOff();
    UtAssert_True(EPS_AppData.HkTelemetryPkt.CommandCount == 1, "Command count incremented");
    UtAssert_True(EPS_AppData.HkTelemetryPkt.DeviceCount == 1, "Device count incremented");
    UtAssert_True(EventTest.MatchCount == 1, "EPS_CMD_SWITCH_OFF_INF_EID generated");
    
    /* Test device error */
    SwitchCmd.SwitchNumber = 2;
    EPS_AppData.HkTelemetryPkt.DeviceErrorCount = 0;
    UT_SetDeferredRetcode(UT_KEY(EPS_SetSwitch), 1, I2C_ERROR);
    UT_CheckEvent_Setup(&EventTest, EPS_CMD_SWITCH_OFF_ERR_EID, NULL);
    EPS_SetSwitchOff();
    UtAssert_True(EPS_AppData.HkTelemetryPkt.DeviceErrorCount == 1, "Device error count incremented");
    UtAssert_True(EventTest.MatchCount == 1, "EPS_CMD_SWITCH_OFF_ERR_EID generated for device error");
}

void Test_EPS_SetSwitchOn(void)
{
    /*
     * Test Case For:
     * void EPS_SetSwitchOn( void )
     */
    UT_CheckEvent_t EventTest;
    EPS_Switch_cmd_t SwitchCmd;
    
    /* Set up message pointer */
    EPS_AppData.MsgPtr = (CFE_MSG_Message_t *)&SwitchCmd;
    
    /* Test invalid switch number */
    SwitchCmd.SwitchNumber = EPS_NUM_SWITCHES + 1;
    EPS_AppData.HkTelemetryPkt.CommandErrorCount = 0;
    UT_CheckEvent_Setup(&EventTest, EPS_CMD_SWITCH_ON_ERR_EID, NULL);
    EPS_SetSwitchOn();
    UtAssert_True(EPS_AppData.HkTelemetryPkt.CommandErrorCount == 1, "Command error count incremented");
    UtAssert_True(EventTest.MatchCount == 1, "EPS_CMD_SWITCH_ON_ERR_EID generated for invalid switch");
    
    /* Test successful switch on */
    SwitchCmd.SwitchNumber = 5;
    EPS_AppData.HkTelemetryPkt.CommandCount = 0;
    EPS_AppData.HkTelemetryPkt.DeviceCount = 0;
    UT_SetDeferredRetcode(UT_KEY(EPS_SetSwitch), 1, I2C_SUCCESS);
    UT_CheckEvent_Setup(&EventTest, EPS_CMD_SWITCH_ON_INF_EID, NULL);
    EPS_SetSwitchOn();
    UtAssert_True(EPS_AppData.HkTelemetryPkt.CommandCount == 1, "Command count incremented");
    UtAssert_True(EPS_AppData.HkTelemetryPkt.DeviceCount == 1, "Device count incremented");
    UtAssert_True(EventTest.MatchCount == 1, "EPS_CMD_SWITCH_ON_INF_EID generated");
    
    /* Test device error */
    SwitchCmd.SwitchNumber = 0;
    EPS_AppData.HkTelemetryPkt.DeviceErrorCount = 0;
    UT_SetDeferredRetcode(UT_KEY(EPS_SetSwitch), 1, I2C_ERROR);
    UT_CheckEvent_Setup(&EventTest, EPS_CMD_SWITCH_ON_ERR_EID, NULL);
    EPS_SetSwitchOn();
    UtAssert_True(EPS_AppData.HkTelemetryPkt.DeviceErrorCount == 1, "Device error count incremented");
    UtAssert_True(EventTest.MatchCount == 1, "EPS_CMD_SWITCH_ON_ERR_EID generated for device error");
}

void Test_EPS_ReportHousekeeping(void)
{
    /*
     * Test Case For:
     * void EPS_ReportHousekeeping()
     */
    CFE_MSG_Message_t *MsgSend;
    CFE_MSG_Message_t *MsgTimestamp;
    CFE_SB_MsgId_t     MsgId = CFE_SB_ValueToMsgId(EPS_REQ_HK_TLM);

    /* Set message id to return so EPS_Housekeeping will be called */
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &MsgId, sizeof(MsgId), false);

    /* Set up to capture send message address */
    UT_SetDataBuffer(UT_KEY(CFE_SB_TransmitMsg), &MsgSend, sizeof(MsgSend), false);

    /* Set up to capture timestamp message address */
    UT_SetDataBuffer(UT_KEY(CFE_SB_TimeStampMsg), &MsgTimestamp, sizeof(MsgTimestamp), false);

    /* Call unit under test, NULL pointer confirms command access is through APIs */
    EPS_ReportHousekeeping();

    /* Confirm message sent*/
    UtAssert_True(UT_GetStubCount(UT_KEY(CFE_SB_TransmitMsg)) == 1, "CFE_SB_TransmitMsg() called once");
    UtAssert_True(MsgSend == &EPS_AppData.HkTelemetryPkt.TlmHeader.Msg,
                  "CFE_SB_TransmitMsg() address matches expected");

    /* Confirm timestamp msg address */
    UtAssert_True(UT_GetStubCount(UT_KEY(CFE_SB_TimeStampMsg)) == 1, "CFE_SB_TimeStampMsg() called once");
    UtAssert_True(MsgTimestamp == &EPS_AppData.HkTelemetryPkt.TlmHeader.Msg,
                  "CFE_SB_TimeStampMsg() address matches expected");

    UT_CheckEvent_t EventTest;
    UT_SetDeferredRetcode(UT_KEY(EPS_RequestHK), 1, OS_ERROR);
    EPS_ReportHousekeeping();
    UT_CheckEvent_Setup(&EventTest, EPS_REQ_HK_ERR_EID, "EPS: Request device HK reported error -1");
}

void Test_EPS_VerifyCmdLength(void)
{
    /*
     * Test Case For:
     * bool EPS_VerifyCmdLength
     */
    UT_CheckEvent_t   EventTest;
    size_t            size    = 1;
    CFE_MSG_FcnCode_t fcncode = 2;
    CFE_SB_MsgId_t    msgid   = CFE_SB_ValueToMsgId(EPS_CMD_MID);

    /*
     * test a match case
     */
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &size, sizeof(size), false);
    UT_CheckEvent_Setup(&EventTest, EPS_LEN_ERR_EID, NULL);

    EPS_VerifyCmdLength(NULL, size);

    /*
     * Confirm that the event was NOT generated
     */
    UtAssert_True(EventTest.MatchCount == 0, "EPS_LEN_ERR_EID NOT generated (%u)",
                  (unsigned int)EventTest.MatchCount);

    /*
     * test a mismatch case
     */
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &size, sizeof(size), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &msgid, sizeof(msgid), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &fcncode, sizeof(fcncode), false);
    UT_CheckEvent_Setup(&EventTest, EPS_LEN_ERR_EID, NULL);
    EPS_VerifyCmdLength(NULL, size + 1);

    /*
     * Confirm that the event WAS generated
     */
    UtAssert_True(EventTest.MatchCount == 1, "EPS_LEN_ERR_EID generated (%u)", (unsigned int)EventTest.MatchCount);
}

/*
 * Setup function prior to every test
 */
void Eps_UT_Setup(void)
{
    UT_ResetState(0);
}

/*
 * Teardown function after every test
 */
void Eps_UT_TearDown(void) {}

/*
 * Register the test cases to execute with the unit test tool
 */
void UtTest_Setup(void)
{
    ADD_TEST(EPS_AppMain);
    ADD_TEST(EPS_AppInit);
    ADD_TEST(EPS_ProcessCommandPacket);
    ADD_TEST(EPS_ProcessGroundCommand);
    ADD_TEST(EPS_ReportHousekeeping);
    ADD_TEST(EPS_ResetCounters);
    ADD_TEST(EPS_SetSwitchOff);
    ADD_TEST(EPS_SetSwitchOn);
    ADD_TEST(EPS_VerifyCmdLength);
    ADD_TEST(EPS_ProcessTelemetryRequest);
}
