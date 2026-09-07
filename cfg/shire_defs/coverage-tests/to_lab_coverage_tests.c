/* SHIRE-owned behavioral coverage for the deployed TO_LAB application. */

#include "to_lab_app.h"
#include "to_lab_cmds.h"
#include "to_lab_encode.h"
#include "to_lab_msgids.h"

#include "utassert.h"
#include "utstubs.h"
#include "uttest.h"

#include <string.h>

void TO_LAB_delete_callback(void);
CFE_Status_t TO_LAB_CmdSubscribe(CFE_SB_MsgId_Atom_t MsgIdValue);
void TO_LAB_ProcessGroundCommand(const CFE_SB_Buffer_t *SBBufPtr);

static TO_LAB_Subs_t SubsTable;

static void Test_Setup(void)
{
    UT_ResetState(0);
    memset(&TO_LAB_Global, 0, sizeof(TO_LAB_Global));
    memset(&SubsTable, 0, sizeof(SubsTable));
    TO_LAB_Global.SubsTblPtr = &SubsTable;
    TO_LAB_Global.AllowPassthru = true;
}

static void Test_Commands(void)
{
    TO_LAB_EnableOutputCmd_t enable = {0};
    TO_LAB_NoopCmd_t noop = {0};
    TO_LAB_ResetCountersCmd_t reset = {0};
    TO_LAB_SendDataTypesCmd_t data_types = {0};
    TO_LAB_SendHkCmd_t hk = {0};
    TO_LAB_AddPacketCmd_t add = {0};
    TO_LAB_RemovePacketCmd_t remove = {0};
    TO_LAB_RemoveAllCmd_t remove_all = {0};
    CFE_SB_MsgId_t stream = CFE_SB_ValueToMsgId(0x810);

    memcpy(enable.Payload.dest_IP, "127.0.0.1", sizeof("127.0.0.1"));
    UtAssert_INT32_EQ(TO_LAB_EnableOutputCmd(&enable), CFE_SUCCESS);
    UtAssert_True(TO_LAB_Global.downlink_on, "enable opens downlink");
    TO_LAB_EnableOutputCmd(&enable);
    UtAssert_INT32_EQ(TO_LAB_NoopCmd(&noop), CFE_SUCCESS);
    UtAssert_INT32_EQ(TO_LAB_SendDataTypesCmd(&data_types), CFE_SUCCESS);
    UtAssert_INT32_EQ(TO_LAB_SendHkCmd(&hk), CFE_SUCCESS);

    TO_LAB_Global.HkTlm.Payload.CommandErrorCounter = 9;
    UtAssert_INT32_EQ(TO_LAB_ResetCountersCmd(&reset), CFE_SUCCESS);
    UtAssert_UINT8_EQ(TO_LAB_Global.HkTlm.Payload.CommandErrorCounter, 0);

    add.Payload.Stream = stream;
    add.Payload.BufLimit = 1;
    TO_LAB_Global.ActiveSubCount = TO_LAB_MISSION_MAX_SUBSCRIPTIONS;
    UtAssert_INT32_EQ(TO_LAB_AddPacketCmd(&add), TO_LAB_ERROR_MAX_PACKET_LIMIT_REACHED);
    TO_LAB_Global.ActiveSubCount = 0;
    add.Payload.BufLimit = TO_LAB_PLATFORM_TLM_PIPE_DEPTH;
    UtAssert_INT32_EQ(TO_LAB_AddPacketCmd(&add), TO_LAB_ERROR_ADD_PACKET_BUF_LIMIT_ERR);
    add.Payload.BufLimit = 1;
    TO_LAB_Global.ActiveSubs[0] = stream;
    UtAssert_INT32_EQ(TO_LAB_AddPacketCmd(&add), TO_LAB_ERROR_ADD_PACKET_REDUNDANT_ERR);
    TO_LAB_Global.ActiveSubs[0] = CFE_SB_ValueToMsgId(0);
    UtAssert_INT32_EQ(TO_LAB_AddPacketCmd(&add), CFE_SUCCESS);
    UtAssert_UINT16_EQ(TO_LAB_Global.ActiveSubCount, 1);

    remove.Payload.Stream = CFE_SB_ValueToMsgId(0x811);
    UtAssert_INT32_EQ(TO_LAB_RemovePacketCmd(&remove), TO_LAB_ERROR_RM_PACKET_MISSING_ERR);
    remove.Payload.Stream = stream;
    UT_SetDefaultReturnValue(UT_KEY(CFE_SB_Unsubscribe), -1);
    UtAssert_INT32_EQ(TO_LAB_RemovePacketCmd(&remove), CFE_SUCCESS);
    UT_SetDefaultReturnValue(UT_KEY(CFE_SB_Unsubscribe), CFE_SUCCESS);
    UtAssert_INT32_EQ(TO_LAB_RemovePacketCmd(&remove), CFE_SUCCESS);
    UtAssert_INT32_EQ(TO_LAB_RemoveAllCmd(&remove_all), CFE_SUCCESS);
}

static void Test_TablesAndLifecycle(void)
{
    CFE_SB_MsgId_t stream = CFE_SB_ValueToMsgId(0x820);

    UtAssert_INT32_EQ(TO_LAB_ValidateSubTable(NULL), CFE_STATUS_VALIDATION_FAILURE);
    UtAssert_INT32_EQ(TO_LAB_ValidateSubTable(&SubsTable), CFE_SUCCESS);
    SubsTable.Subs[0].Stream = stream;
    SubsTable.Subs[0].BufLimit = TO_LAB_PLATFORM_TLM_PIPE_DEPTH + 1;
    UtAssert_INT32_EQ(TO_LAB_ValidateSubTable(&SubsTable), CFE_STATUS_VALIDATION_FAILURE);
    SubsTable.Subs[0].BufLimit = 1;
    UtAssert_INT32_EQ(TO_LAB_ValidateSubTable(&SubsTable), CFE_SUCCESS);

    TO_LAB_Global.ActiveSubs[0] = stream;
    TO_LAB_Global.ActiveSubCount = 1;
    UT_SetDefaultReturnValue(UT_KEY(CFE_SB_Unsubscribe), -1);
    UtAssert_UINT16_EQ(TO_LAB_UnsubscribeFromTlmPipe(), 0);
    UT_SetDefaultReturnValue(UT_KEY(CFE_SB_Unsubscribe), CFE_SUCCESS);
    UtAssert_UINT16_EQ(TO_LAB_UnsubscribeFromTlmPipe(), 1);

    SubsTable.Subs[0].Stream = stream;
    SubsTable.Subs[0].BufLimit = 1;
    SubsTable.Subs[2].Stream = CFE_SB_ValueToMsgId(0x821);
    SubsTable.Subs[2].BufLimit = 1;
    UtAssert_UINT16_EQ(TO_LAB_UpdateSubscriptionsFromTable(), 2);
    UT_SetDefaultReturnValue(UT_KEY(CFE_SB_SubscribeEx), -1);
    UtAssert_UINT16_EQ(TO_LAB_UpdateSubscriptionsFromTable(), 0);

    UT_SetDefaultReturnValue(UT_KEY(CFE_TBL_GetAddress), CFE_TBL_INFO_UPDATED);
    TO_LAB_ManageTables();
    UT_SetDefaultReturnValue(UT_KEY(CFE_TBL_GetAddress), -1);
    TO_LAB_ManageTables();

    UtAssert_INT32_EQ(TO_LAB_CmdSubscribe(TO_LAB_CMD_MID), CFE_SUCCESS);
    UT_SetDefaultReturnValue(UT_KEY(CFE_SB_Subscribe), -1);
    UtAssert_INT32_EQ(TO_LAB_CmdSubscribe(TO_LAB_CMD_MID), -1);
    TO_LAB_Global.downlink_on = false;
    TO_LAB_delete_callback();
    TO_LAB_openTLM();
    TO_LAB_Global.downlink_on = true;
    TO_LAB_delete_callback();
}

static void Test_RunAndTransport(void)
{
    CFE_SB_Buffer_t packet = {0};
    CFE_SB_MsgId_t mid;
    CFE_MSG_FcnCode_t fc;
    CFE_MSG_Size_t size = sizeof(packet);
    const void *encoded;
    size_t encoded_size;

    UT_SetDefaultReturnValue(UT_KEY(CFE_SB_ReceiveBuffer), CFE_SB_NO_MESSAGE);
    TO_LAB_process_commands();
    TO_LAB_forward_telemetry();
    TO_LAB_openTLM();
    UT_SetDefaultReturnValue(UT_KEY(OS_SocketOpen), -1);
    TO_LAB_openTLM();
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &size, sizeof(size), false);
    UtAssert_INT32_EQ(TO_LAB_EncodeOutputMessage(&packet, &encoded, &encoded_size), CFE_SUCCESS);
    UtAssert_True(encoded == &packet, "passthrough encoder preserves buffer");

    mid = CFE_SB_ValueToMsgId(TO_LAB_SEND_HK_MID);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &mid, sizeof(mid), false);
    TO_LAB_TaskPipe(&packet);
    mid = CFE_SB_ValueToMsgId(TO_LAB_CMD_MID);
    fc = TO_LAB_NOOP_CC;
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &mid, sizeof(mid), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &fc, sizeof(fc), false);
    TO_LAB_TaskPipe(&packet);
    fc = 255;
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &fc, sizeof(fc), false);
    TO_LAB_ProcessGroundCommand(&packet);
    mid = CFE_SB_ValueToMsgId(0x999);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &mid, sizeof(mid), false);
    TO_LAB_TaskPipe(&packet);

    UtAssert_INT32_EQ(TO_LAB_init(), CFE_SUCCESS);
    UT_SetDefaultReturnValue(UT_KEY(CFE_EVS_Register), -1);
    UtAssert_INT32_EQ(TO_LAB_init(), -1);
    TO_LAB_AppMain();
}

void UtTest_Setup(void)
{
    UtTest_Add(Test_Commands, Test_Setup, NULL, "TO commands and subscription state");
    UtTest_Add(Test_TablesAndLifecycle, Test_Setup, NULL, "TO tables and lifecycle");
    UtTest_Add(Test_RunAndTransport, Test_Setup, NULL, "TO dispatch and transport edges");
}
