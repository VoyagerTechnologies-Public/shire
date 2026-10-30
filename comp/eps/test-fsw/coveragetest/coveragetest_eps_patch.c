/*
 * Coverage tests for the uplink patch build of the EPS app, which the ground uploads and loads with CFE_ES_RELOAD_APP.
 * The same source is built with EPS_PATCH_PROTECTED_SWITCH=5, so it refuses to switch ON the 20 W load bank. The
 * checked in build is covered by coveragetest_eps_app.c, where switch 5 can still be switched on.
 */
#include "eps_app_coveragetest_common.h"
#include "ut_eps_app.h"

#ifndef EPS_PATCH_PROTECTED_SWITCH
#error "This test must be built with EPS_PATCH_PROTECTED_SWITCH, as the uplink patch is"
#endif

#define PROTECTED_SWITCH 5

static uint32 EventCount;
static uint16 LastEventId;

static int32 CaptureEvent(void *UserObj, int32 StubRetcode, uint32 CallCount, const UT_StubContext_t *Context,
                          va_list va)
{
    (void)UserObj;
    (void)CallCount;
    (void)va;
    if (Context->ArgCount > 0)
    {
        LastEventId = UT_Hook_GetArgValueByName(Context, "EventID", uint16);
        EventCount++;
    }
    return StubRetcode;
}

static EPS_Switch_cmd_t SwitchCmd;

static void SendSwitchCommand(uint8 SwitchNumber)
{
    SwitchCmd.SwitchNumber = SwitchNumber;
    EPS_AppData.MsgPtr     = (CFE_MSG_Message_t *)&SwitchCmd;
    EventCount             = 0;
    LastEventId            = 0;
}

void Test_EPS_PatchReportsItsRevision(void)
{
    UtAssert_True(EPS_REVISION == 1, "the uplink patch reports revision 1 so its start up event identifies it");
}

void Test_EPS_PatchRefusesToSwitchOnTheProtectedSwitch(void)
{
    UT_SetVaHookFunction(UT_KEY(CFE_EVS_SendEvent), CaptureEvent, NULL);
    EPS_AppData.HkTelemetryPkt.CommandCount      = 0;
    EPS_AppData.HkTelemetryPkt.CommandErrorCount = 0;
    EPS_AppData.HkTelemetryPkt.DeviceCount       = 0;

    SendSwitchCommand(PROTECTED_SWITCH);
    EPS_SetSwitchOn();

    UtAssert_True(EPS_AppData.HkTelemetryPkt.CommandErrorCount == 1, "Command error count incremented");
    UtAssert_True(EPS_AppData.HkTelemetryPkt.CommandCount == 0, "the refused command is not counted as accepted");
    UtAssert_True(EPS_AppData.HkTelemetryPkt.DeviceCount == 0, "the device is not commanded");
    UtAssert_True(UT_GetStubCount(UT_KEY(EPS_SetSwitch)) == 0, "EPS_SetSwitch() is not called for the protected switch");
    UtAssert_True(EventCount == 1 && LastEventId == EPS_CMD_SWITCH_ON_ERR_EID,
                  "only EPS_CMD_SWITCH_ON_ERR_EID is generated (%u events, last %u)", (unsigned int)EventCount,
                  (unsigned int)LastEventId);
}

void Test_EPS_PatchStillSwitchesOnTheOtherSwitches(void)
{
    uint8 SwitchNumber;

    UT_SetVaHookFunction(UT_KEY(CFE_EVS_SendEvent), CaptureEvent, NULL);
    for (SwitchNumber = 0; SwitchNumber < EPS_NUM_SWITCHES; SwitchNumber++)
    {
        if (SwitchNumber == PROTECTED_SWITCH)
        {
            continue;
        }
        UT_ResetState(UT_KEY(EPS_SetSwitch));
        EPS_AppData.HkTelemetryPkt.CommandCount      = 0;
        EPS_AppData.HkTelemetryPkt.CommandErrorCount = 0;
        SendSwitchCommand(SwitchNumber);
        UT_SetDeferredRetcode(UT_KEY(EPS_SetSwitch), 1, I2C_SUCCESS);
        EPS_SetSwitchOn();
        UtAssert_True(EPS_AppData.HkTelemetryPkt.CommandCount == 1, "switch %u is switched on", (unsigned int)SwitchNumber);
        UtAssert_True(EPS_AppData.HkTelemetryPkt.CommandErrorCount == 0, "switch %u has no command error",
                      (unsigned int)SwitchNumber);
        UtAssert_True(LastEventId == EPS_CMD_SWITCH_ON_INF_EID, "switch %u generates EPS_CMD_SWITCH_ON_INF_EID",
                      (unsigned int)SwitchNumber);
    }
}

void Test_EPS_PatchDoesNotHideAnInvalidSwitch(void)
{
    UT_SetVaHookFunction(UT_KEY(CFE_EVS_SendEvent), CaptureEvent, NULL);
    EPS_AppData.HkTelemetryPkt.CommandErrorCount = 0;
    SendSwitchCommand(EPS_NUM_SWITCHES + 1);
    EPS_SetSwitchOn();
    UtAssert_True(EPS_AppData.HkTelemetryPkt.CommandErrorCount == 1,
                  "an invalid switch is one error, and the protected check does not count it again");
    UtAssert_True(EventCount == 1 && LastEventId == EPS_CMD_SWITCH_ON_ERR_EID, "one error event for an invalid switch");
}

void Test_EPS_PatchAllowsSwitchingTheProtectedSwitchOff(void)
{
    UT_SetVaHookFunction(UT_KEY(CFE_EVS_SendEvent), CaptureEvent, NULL);
    EPS_AppData.HkTelemetryPkt.CommandCount      = 0;
    EPS_AppData.HkTelemetryPkt.CommandErrorCount = 0;
    SendSwitchCommand(PROTECTED_SWITCH);
    UT_SetDeferredRetcode(UT_KEY(EPS_SetSwitch), 1, I2C_SUCCESS);
    EPS_SetSwitchOff();
    UtAssert_True(EPS_AppData.HkTelemetryPkt.CommandCount == 1, "the protected switch can be switched off");
    UtAssert_True(EPS_AppData.HkTelemetryPkt.CommandErrorCount == 0, "switching it off is not an error");
    UtAssert_True(LastEventId == EPS_CMD_SWITCH_OFF_INF_EID, "EPS_CMD_SWITCH_OFF_INF_EID generated");
}

void Eps_UT_Setup(void)
{
    UT_ResetState(0);
}

void Eps_UT_TearDown(void) {}

void UtTest_Setup(void)
{
    ADD_TEST(EPS_PatchReportsItsRevision);
    ADD_TEST(EPS_PatchRefusesToSwitchOnTheProtectedSwitch);
    ADD_TEST(EPS_PatchStillSwitchesOnTheOtherSwitches);
    ADD_TEST(EPS_PatchDoesNotHideAnInvalidSwitch);
    ADD_TEST(EPS_PatchAllowsSwitchingTheProtectedSwitchOff);
}
