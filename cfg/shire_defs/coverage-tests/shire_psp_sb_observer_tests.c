/* Verify that only command traffic participates in Simulith delivery accounting. */

#include "cfe.h"
#include "cfe_psp_timebase.h"
#include "cfe_sb_observer.h"
#include "utassert.h"
#include "uttest.h"

#include <string.h>

static CFE_Status_t FakeTypeStatus;
static CFE_MSG_Type_t FakeType;
static int FakeReservation;
static unsigned int ReserveCalls;
static unsigned int EndCalls;
static unsigned int TransmitCalls;
static unsigned int BeginReceiveCalls;
static unsigned int ReceivedCalls;
static uint32_t LastMsgId;
static uint32_t LastPipeId;
static const void *LastBuffer;
static int LastToken;
static bool LastDelivered;
static bool LastPolling;

CFE_Status_t CFE_MSG_GetTypeFromMsgId(CFE_SB_MsgId_t MsgId, CFE_MSG_Type_t *Type)
{
    (void)MsgId;
    if (FakeTypeStatus == CFE_SUCCESS)
    {
        *Type = FakeType;
    }
    return FakeTypeStatus;
}

int CFE_PSP_ReserveSimulithMessageDelivery(uint32_t message_id, uint32_t pipe_id,
                                           const void *message_ref)
{
    ReserveCalls++;
    LastMsgId = message_id;
    LastPipeId = pipe_id;
    LastBuffer = message_ref;
    return FakeReservation;
}

void CFE_PSP_EndSimulithMessageDelivery(int token, bool delivered)
{
    EndCalls++;
    LastToken = token;
    LastDelivered = delivered;
}

void CFE_PSP_EndSimulithMessagePublication(void)
{
    TransmitCalls++;
}

void CFE_PSP_SimulithTaskBeginReceive(uint32_t pipe_id, bool polling)
{
    BeginReceiveCalls++;
    LastPipeId = pipe_id;
    LastPolling = polling;
}

void CFE_PSP_SimulithMessageReceived(uint32_t message_id, uint32_t pipe_id,
                                     const void *message_ref)
{
    ReceivedCalls++;
    LastMsgId = message_id;
    LastPipeId = pipe_id;
    LastBuffer = message_ref;
}

static void ResetObserver(void)
{
    FakeTypeStatus = CFE_SUCCESS;
    FakeType = CFE_MSG_Type_Cmd;
    FakeReservation = 7;
    ReserveCalls = EndCalls = TransmitCalls = BeginReceiveCalls = ReceivedCalls = 0;
    LastMsgId = LastPipeId = 0;
    LastBuffer = NULL;
    LastToken = 0;
    LastDelivered = LastPolling = false;
}

static void Test_DeliveryFiltering(void)
{
    CFE_SB_MsgId_t msg_id = CFE_SB_ValueToMsgId(0x1801);
    CFE_SB_PipeId_t pipe_id = CFE_SB_PIPEID_C(0x1234);
    CFE_SB_Buffer_t buffer = {0};

    ResetObserver();
    FakeTypeStatus = CFE_MSG_BAD_ARGUMENT;
    UtAssert_UINT32_EQ(CFE_SB_Observer_BeginDelivery(msg_id, pipe_id, &buffer),
                       CFE_SB_OBSERVER_INVALID_TOKEN);
    UtAssert_UINT32_EQ(ReserveCalls, 0);

    FakeTypeStatus = CFE_SUCCESS;
    FakeType = CFE_MSG_Type_Tlm;
    UtAssert_UINT32_EQ(CFE_SB_Observer_BeginDelivery(msg_id, pipe_id, &buffer),
                       CFE_SB_OBSERVER_INVALID_TOKEN);
    UtAssert_UINT32_EQ(ReserveCalls, 0);

    FakeType = CFE_MSG_Type_Cmd;
    FakeReservation = -1;
    UtAssert_UINT32_EQ(CFE_SB_Observer_BeginDelivery(msg_id, pipe_id, &buffer),
                       CFE_SB_OBSERVER_INVALID_TOKEN);
    FakeReservation = 0;
    UtAssert_UINT32_EQ(CFE_SB_Observer_BeginDelivery(msg_id, pipe_id, &buffer),
                       CFE_SB_OBSERVER_INVALID_TOKEN);
    FakeReservation = 7;
    UtAssert_UINT32_EQ(CFE_SB_Observer_BeginDelivery(msg_id, pipe_id, &buffer), 7);
    UtAssert_UINT32_EQ(ReserveCalls, 3);
    UtAssert_UINT32_EQ(LastMsgId, 0x1801);
    UtAssert_UINT32_EQ(LastPipeId, 0x1234);
    UtAssert_True(LastBuffer == &buffer, "reservation uses the original buffer");

    CFE_SB_Observer_EndDelivery(7, true);
    UtAssert_UINT32_EQ(EndCalls, 1);
    UtAssert_INT32_EQ(LastToken, 7);
    UtAssert_True(LastDelivered, "successful delivery is confirmed");
    CFE_SB_Observer_EndDelivery(8, false);
    UtAssert_UINT32_EQ(EndCalls, 2);
    UtAssert_INT32_EQ(LastToken, 8);
    UtAssert_True(!LastDelivered, "failed delivery is cancelled");
    CFE_SB_Observer_EndTransmit();
    UtAssert_UINT32_EQ(TransmitCalls, 1);
}

static void Test_ReceiveFiltering(void)
{
    CFE_SB_MsgId_t msg_id = CFE_SB_ValueToMsgId(0x1801);
    CFE_SB_PipeId_t pipe_id = CFE_SB_PIPEID_C(0x1234);
    CFE_SB_Buffer_t buffer = {0};

    ResetObserver();
    CFE_SB_Observer_BeginReceive(pipe_id, true);
    UtAssert_UINT32_EQ(BeginReceiveCalls, 1);
    UtAssert_UINT32_EQ(LastPipeId, 0x1234);
    UtAssert_True(LastPolling, "polling receive is forwarded");
    CFE_SB_Observer_BeginReceive(pipe_id, false);
    UtAssert_True(!LastPolling, "blocking receive is forwarded");

    FakeTypeStatus = CFE_MSG_BAD_ARGUMENT;
    CFE_SB_Observer_MessageReceived(msg_id, pipe_id, &buffer);
    FakeTypeStatus = CFE_SUCCESS;
    FakeType = CFE_MSG_Type_Tlm;
    CFE_SB_Observer_MessageReceived(msg_id, pipe_id, &buffer);
    UtAssert_UINT32_EQ(ReceivedCalls, 0);

    FakeType = CFE_MSG_Type_Cmd;
    CFE_SB_Observer_MessageReceived(msg_id, pipe_id, &buffer);
    UtAssert_UINT32_EQ(ReceivedCalls, 1);
    UtAssert_UINT32_EQ(LastMsgId, 0x1801);
    UtAssert_UINT32_EQ(LastPipeId, 0x1234);
    UtAssert_True(LastBuffer == &buffer, "received command retains its buffer identity");
}

void UtTest_Setup(void)
{
    UtTest_Add(Test_DeliveryFiltering, NULL, NULL, "SHIRE PSP command delivery observation");
    UtTest_Add(Test_ReceiveFiltering, NULL, NULL, "SHIRE PSP command receive observation");
}
