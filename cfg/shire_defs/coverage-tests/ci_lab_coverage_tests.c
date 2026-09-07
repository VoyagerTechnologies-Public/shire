/* SHIRE-owned behavioral coverage for the deployed CI_LAB application. */

#include "ci_lab_app.h"
#include "ci_lab_decode.h"
#include "ci_lab_msgids.h"

#include "utassert.h"
#include "utstubs.h"
#include "uttest.h"

#include <string.h>

void CI_LAB_delete_callback(void);
bool CI_LAB_VerifyCmdLength(const CFE_MSG_Message_t *MsgPtr, size_t ExpectedLength);
void CI_LAB_ProcessGroundCommand(const CFE_SB_Buffer_t *SBBufPtr);

static unsigned int SocketReceiveCalls;
static int32 SocketFirstReceiveSize;

static void ReceiveOneDatagram(void *user_obj, UT_EntryKey_t func_key, const UT_StubContext_t *context)
{
    int32 result;

    (void)user_obj;
    (void)context;
    result = SocketReceiveCalls++ == 0 ? SocketFirstReceiveSize : 0;
    UT_Stub_SetReturnValue(func_key, result);
}

static void Test_Setup(void)
{
    UT_ResetState(0);
    memset(&CI_LAB_Global, 0, sizeof(CI_LAB_Global));
    CI_LAB_Global.AllowPassthrough = true;
    SocketReceiveCalls = 0;
    SocketFirstReceiveSize = 0;
}

static void Test_CommandsAndDispatch(void)
{
    CFE_SB_Buffer_t packet = {0};
    CI_LAB_NoopCmd_t noop = {0};
    CI_LAB_ResetCountersCmd_t reset = {0};
    CI_LAB_SendHkCmd_t hk = {0};
    CI_LAB_ReadUplinkCmd_t uplink = {0};
    CFE_MSG_Size_t size;
    CFE_MSG_FcnCode_t fc = 0;
    CFE_SB_MsgId_t mid = CFE_SB_INVALID_MSG_ID;

    UtAssert_INT32_EQ(CI_LAB_NoopCmd(&noop), CFE_SUCCESS);
    UtAssert_INT32_EQ(CI_LAB_SendHkCmd(&hk), CFE_SUCCESS);
    CI_LAB_Global.HkTlm.Payload.CommandErrorCounter = 4;
    UtAssert_INT32_EQ(CI_LAB_ResetCountersCmd(&reset), CFE_SUCCESS);
    UtAssert_UINT8_EQ(CI_LAB_Global.HkTlm.Payload.CommandErrorCounter, 0);
    UT_SetDefaultReturnValue(UT_KEY(OS_SocketRecvFrom), 0);
    UtAssert_INT32_EQ(CI_LAB_ReadUplinkCmd(&uplink), CFE_SUCCESS);
    UtAssert_True(CI_LAB_Global.Scheduled, "uplink command selects scheduled mode");

    size = sizeof(CI_LAB_NoopCmd_t);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &size, sizeof(size), false);
    UtAssert_True(CI_LAB_VerifyCmdLength(&packet.Msg, size), "valid length accepted");
    size++;
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &size, sizeof(size), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &mid, sizeof(mid), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &fc, sizeof(fc), false);
    UtAssert_True(!CI_LAB_VerifyCmdLength(&packet.Msg, sizeof(CI_LAB_NoopCmd_t)), "bad length rejected");

    fc = CI_LAB_NOOP_CC;
    size = sizeof(CI_LAB_NoopCmd_t);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &fc, sizeof(fc), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &size, sizeof(size), false);
    CI_LAB_ProcessGroundCommand(&packet);
    fc = CI_LAB_RESET_COUNTERS_CC;
    size = sizeof(CI_LAB_ResetCountersCmd_t);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &fc, sizeof(fc), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &size, sizeof(size), false);
    CI_LAB_ProcessGroundCommand(&packet);
    fc = 255;
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &fc, sizeof(fc), false);
    CI_LAB_ProcessGroundCommand(&packet);

    mid = CFE_SB_ValueToMsgId(CI_LAB_SEND_HK_MID);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &mid, sizeof(mid), false);
    CI_LAB_TaskPipe(&packet);
    mid = CFE_SB_ValueToMsgId(CI_LAB_READ_UPLINK_MID);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &mid, sizeof(mid), false);
    CI_LAB_TaskPipe(&packet);
    mid = CFE_SB_ValueToMsgId(CI_LAB_CMD_MID);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &mid, sizeof(mid), false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &fc, sizeof(fc), false);
    CI_LAB_TaskPipe(&packet);
    mid = CFE_SB_ValueToMsgId(0x999);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &mid, sizeof(mid), false);
    CI_LAB_TaskPipe(&packet);
}

static void Test_DecodeAndIngest(void)
{
    CFE_SB_Buffer_t packet = {0};
    CFE_SB_Buffer_t *decoded = NULL;
    CFE_MSG_Size_t msg_size;
    void *buffer = NULL;
    size_t buffer_size = 0;

    UtAssert_INT32_EQ(CI_LAB_GetInputBuffer(&buffer, &buffer_size), CFE_SB_BUF_ALOC_ERR);
    UtAssert_True(buffer == NULL && buffer_size == 0, "allocation failure clears outputs");
    {
        CFE_SB_Buffer_t *allocated = &packet;
        UT_SetDataBuffer(UT_KEY(CFE_SB_AllocateMessageBuffer), &allocated, sizeof(allocated), true);
    }
    UtAssert_INT32_EQ(CI_LAB_GetInputBuffer(&buffer, &buffer_size), CFE_SUCCESS);
    UtAssert_True(buffer == &packet, "allocated SB buffer returned");

    UtAssert_INT32_EQ(CI_LAB_DecodeInputMessage(&packet, 1, &decoded), CFE_STATUS_WRONG_MSG_LENGTH);
    msg_size = sizeof(packet) + 1;
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &msg_size, sizeof(msg_size), false);
    UtAssert_INT32_EQ(CI_LAB_DecodeInputMessage(&packet, sizeof(packet), &decoded), CFE_STATUS_WRONG_MSG_LENGTH);
    msg_size = sizeof(packet);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &msg_size, sizeof(msg_size), false);
    UtAssert_INT32_EQ(CI_LAB_DecodeInputMessage(&packet, sizeof(packet), &decoded), CFE_SUCCESS);
    UtAssert_True(decoded == &packet, "valid ingest buffer passes through");

    CI_LAB_Global.NetBufPtr = &packet;
    CI_LAB_Global.NetBufSize = sizeof(packet);
    SocketFirstReceiveSize = sizeof(packet);
    UT_SetHandlerFunction(UT_KEY(OS_SocketRecvFrom), ReceiveOneDatagram, NULL);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &msg_size, sizeof(msg_size), false);
    CI_LAB_ReadUpLink();
    UtAssert_True(CI_LAB_Global.HkTlm.Payload.IngestPackets > 0, "valid uplink counted");

    CI_LAB_Global.NetBufPtr = &packet;
    SocketReceiveCalls = 0;
    UT_SetDefaultReturnValue(UT_KEY(CFE_SB_TransmitBuffer), -1);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &msg_size, sizeof(msg_size), false);
    {
        uint32 event_count = UT_GetStubCount(UT_KEY(CFE_EVS_SendEvent));
        CI_LAB_ReadUpLink();
        UtAssert_True(UT_GetStubCount(UT_KEY(CFE_EVS_SendEvent)) > event_count,
                      "failed software-bus transmit emits an event");
    }
}

static void Test_Lifecycle(void)
{
    CI_LAB_Global.HkTlm.Payload.CommandCounter = 5;
    CI_LAB_ResetCounters_Internal();
    UtAssert_UINT8_EQ(CI_LAB_Global.HkTlm.Payload.CommandCounter, 0);
    CI_LAB_TaskInit();
    UtAssert_True(CI_LAB_Global.AllowPassthrough, "initialization enables passthrough");
    CI_LAB_delete_callback();

    UT_SetDefaultReturnValue(UT_KEY(CFE_SB_CreatePipe), -1);
    UT_SetDefaultReturnValue(UT_KEY(OS_SocketOpen), -1);
    CI_LAB_TaskInit();
    CI_LAB_AppMain();
}

void UtTest_Setup(void)
{
    UtTest_Add(Test_CommandsAndDispatch, Test_Setup, NULL, "CI commands and dispatch");
    UtTest_Add(Test_DecodeAndIngest, Test_Setup, NULL, "CI decoding and ingest");
    UtTest_Add(Test_Lifecycle, Test_Setup, NULL, "CI initialization and lifecycle");
}
