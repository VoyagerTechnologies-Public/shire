/*
 * SHIRE-owned tests for the deployed Scheduler application.
 *
 * SCH predates the current cFS CMake coverage framework.  Keeping these tests
 * in the mission layer lets us test the configured app without modifying the
 * upstream submodule.
 */

#include "sch_api.h"
#include "sch_app.h"
#include "sch_cmds.h"
#include "sch_custom.h"
#include "sch_events.h"
#include "sch_msg.h"
#include "sch_msgids.h"
#include "cfe_time_msg.h"

#include "utassert.h"
#include "utstubs.h"
#include "uttest.h"

#include <pthread.h>
#include <string.h>

static SCH_ScheduleEntry_t ScheduleTable[SCH_TABLE_ENTRIES];
static SCH_MessageEntry_t  MessageTable[SCH_MAX_MESSAGES];
static int                 PthreadCreateResult;
static void              *(*PthreadStartRoutine)(void *);
static void                *PthreadStartArg;
static bool                 StopTickThreadOnSecondWait;
static unsigned int         TickWaitCount;

int32 SCH_LibInit(void);

/* Keep application cleanup deterministic when no real tick thread was started. */
int pthread_cancel(pthread_t thread)
{
    (void)thread;
    return 0;
}

int pthread_join(pthread_t thread, void **retval)
{
    (void)thread;
    (void)retval;
    return 0;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg)
{
    (void)attr;
    PthreadStartRoutine = start_routine;
    PthreadStartArg     = arg;
    *thread             = (pthread_t)1;
    return PthreadCreateResult;
}

/* sch_custom.c's Simulith wait is supplied by the PSP in flight. */
unsigned int CFE_PSP_WaitForSimulithTick(unsigned int ticks_to_wait)
{
    TickWaitCount++;
    if (StopTickThreadOnSecondWait && TickWaitCount == 2)
    {
        SCH_CustomCleanup();
    }
    return ticks_to_wait;
}

static void SetNoisyFrameOnSemaphoreTake(void *user_obj, UT_EntryKey_t func_key, const UT_StubContext_t *context)
{
    int32 status = OS_SUCCESS;

    (void)user_obj;
    (void)context;
    SCH_AppData.IgnoreMajorFrame        = true;
    SCH_AppData.IgnoreMajorFrameMsgSent = false;
    UT_Stub_SetReturnValue(func_key, status);
}

static void FailSemaphoreTake(void *user_obj, UT_EntryKey_t func_key, const UT_StubContext_t *context)
{
    int32 status = OS_ERROR;

    (void)user_obj;
    (void)context;
    UT_Stub_SetReturnValue(func_key, status);
}

static void SCH_Test_Setup(void)
{
    UT_ResetState(0);
    memset(&SCH_AppData, 0, sizeof(SCH_AppData));
    memset(ScheduleTable, 0, sizeof(ScheduleTable));
    memset(MessageTable, 0, sizeof(MessageTable));
    SCH_AppData.ScheduleTable = ScheduleTable;
    SCH_AppData.MessageTable  = MessageTable;
    PthreadCreateResult       = 0;
    PthreadStartRoutine       = NULL;
    PthreadStartArg           = NULL;
    StopTickThreadOnSecondWait = false;
    TickWaitCount              = 0;
}

static void SetMessageSize(CFE_MSG_Size_t size)
{
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), &size, sizeof(size), true);
}

static void SetMessageMetadata(CFE_SB_MsgId_Atom_t msgid, CFE_MSG_FcnCode_t code, CFE_MSG_Size_t size)
{
    CFE_SB_MsgId_t id = CFE_SB_ValueToMsgId(msgid);

    /* These values are local to this helper, so let UT assert retain copies. */
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &id, sizeof(id), true);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetFcnCode), &code, sizeof(code), true);
    SetMessageSize(size);
}

static void SetMessageTableMetadata(CFE_SB_MsgId_t msg_ids[SCH_MAX_MESSAGES],
                                    CFE_MSG_Size_t sizes[SCH_MAX_MESSAGES],
                                    CFE_SB_MsgId_Atom_t msg_id, CFE_MSG_Size_t size)
{
    size_t i;

    for (i = 0; i < SCH_MAX_MESSAGES; ++i)
    {
        msg_ids[i] = CFE_SB_ValueToMsgId(msg_id);
        sizes[i]   = size;
    }

    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), msg_ids, sizeof(*msg_ids) * SCH_MAX_MESSAGES, false);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetSize), sizes, sizeof(*sizes) * SCH_MAX_MESSAGES, false);
}

static void Test_SCH_Api(void)
{
    UtAssert_INT32_EQ(SCH_LibInit(), OS_SUCCESS);
    SCH_DisableProcessing();
    UtAssert_True(!SCH_GetProcessingState(), "processing disabled");
    SCH_EnableProcessing();
    UtAssert_True(SCH_GetProcessingState(), "processing enabled");
}

static void Test_SCH_BasicCommands(void)
{
    SCH_NoArgsCmd_t command;

    memset(&command, 0, sizeof(command));
    SetMessageSize(sizeof(command));
    SCH_NoopCmd((CFE_MSG_Message_t *)&command);
    UtAssert_UINT32_EQ(SCH_AppData.CmdCounter, 1);
    UtAssert_STUB_COUNT(CFE_EVS_SendEvent, 1);

    SetMessageMetadata(SCH_CMD_MID, SCH_NOOP_CC, sizeof(command) - 1);
    SCH_NoopCmd((CFE_MSG_Message_t *)&command);
    UtAssert_UINT32_EQ(SCH_AppData.ErrCounter, 1);

    SCH_AppData.ScheduleActivitySuccessCount = 4;
    SCH_AppData.ScheduleActivityFailureCount = 5;
    SCH_AppData.SlotsProcessedCount          = 6;
    SCH_AppData.ValidMajorFrameCount         = 7;
    SetMessageSize(sizeof(command));
    SCH_ResetCmd((CFE_MSG_Message_t *)&command);
    UtAssert_UINT32_EQ(SCH_AppData.ScheduleActivitySuccessCount, 0);
    UtAssert_UINT32_EQ(SCH_AppData.ValidMajorFrameCount, 0);

    SCH_AppData.IgnoreMajorFrame             = true;
    SCH_AppData.UnexpectedMajorFrame         = true;
    SCH_AppData.ConsecutiveNoisyFrameCounter = 3;
    SetMessageSize(sizeof(command));
    SCH_EnableSyncCmd((CFE_MSG_Message_t *)&command);
    UtAssert_True(!SCH_AppData.IgnoreMajorFrame, "major-frame synchronization enabled");
}

static void Test_SCH_EntryCommands(void)
{
    SCH_EntryCmd_t command;

    memset(&command, 0, sizeof(command));
    SetMessageSize(sizeof(command));
    ScheduleTable[0].EnableState = SCH_DISABLED;
    SCH_EnableCmd((CFE_MSG_Message_t *)&command);
    UtAssert_UINT32_EQ(ScheduleTable[0].EnableState, SCH_ENABLED);
    UtAssert_STUB_COUNT(CFE_TBL_Modified, 1);

    ScheduleTable[0].EnableState = 99;
    SetMessageSize(sizeof(command));
    SCH_EnableCmd((CFE_MSG_Message_t *)&command);
    UtAssert_UINT32_EQ(SCH_AppData.ErrCounter, 1);

    command.SlotNumber = SCH_TOTAL_SLOTS;
    SetMessageSize(sizeof(command));
    SCH_EnableCmd((CFE_MSG_Message_t *)&command);
    command.SlotNumber  = 0;
    command.EntryNumber = SCH_ENTRIES_PER_SLOT;
    SetMessageSize(sizeof(command));
    SCH_EnableCmd((CFE_MSG_Message_t *)&command);
    UtAssert_UINT32_EQ(SCH_AppData.ErrCounter, 3);

    command.EntryNumber          = 0;
    ScheduleTable[0].EnableState = SCH_ENABLED;
    SetMessageSize(sizeof(command));
    SCH_DisableCmd((CFE_MSG_Message_t *)&command);
    UtAssert_UINT32_EQ(ScheduleTable[0].EnableState, SCH_DISABLED);

    ScheduleTable[0].EnableState = 99;
    SetMessageSize(sizeof(command));
    SCH_DisableCmd((CFE_MSG_Message_t *)&command);
    command.SlotNumber = SCH_TOTAL_SLOTS;
    SetMessageSize(sizeof(command));
    SCH_DisableCmd((CFE_MSG_Message_t *)&command);
    UtAssert_UINT32_EQ(SCH_AppData.ErrCounter, 5);

    SetMessageMetadata(SCH_CMD_MID, SCH_DISABLE_CC, sizeof(command) - 1);
    SCH_DisableCmd((CFE_MSG_Message_t *)&command);
    UtAssert_UINT32_EQ(SCH_AppData.ErrCounter, 6);
}

static void Test_SCH_GroupCommands(void)
{
    SCH_GroupCmd_t command;

    memset(&command, 0, sizeof(command));
    SetMessageSize(sizeof(command));
    SCH_EnableGroupCmd((CFE_MSG_Message_t *)&command);
    UtAssert_UINT32_EQ(SCH_AppData.ErrCounter, 1);

    command.GroupData = 0x01000000;
    SetMessageSize(sizeof(command));
    SCH_EnableGroupCmd((CFE_MSG_Message_t *)&command);
    UtAssert_UINT32_EQ(SCH_AppData.ErrCounter, 2);

    ScheduleTable[1].GroupData   = command.GroupData;
    ScheduleTable[1].EnableState = SCH_DISABLED;
    SetMessageSize(sizeof(command));
    SCH_EnableGroupCmd((CFE_MSG_Message_t *)&command);
    UtAssert_UINT32_EQ(ScheduleTable[1].EnableState, SCH_ENABLED);

    command.GroupData = 0x00000004;
    ScheduleTable[2].GroupData = 0x02000004;
    SetMessageSize(sizeof(command));
    SCH_EnableGroupCmd((CFE_MSG_Message_t *)&command);
    UtAssert_UINT32_EQ(ScheduleTable[2].EnableState, SCH_ENABLED);

    command.GroupData = 0;
    SetMessageSize(sizeof(command));
    SCH_DisableGroupCmd((CFE_MSG_Message_t *)&command);
    command.GroupData = 0x08000000;
    SetMessageSize(sizeof(command));
    SCH_DisableGroupCmd((CFE_MSG_Message_t *)&command);
    command.GroupData = 0x01000000;
    SetMessageSize(sizeof(command));
    SCH_DisableGroupCmd((CFE_MSG_Message_t *)&command);
    UtAssert_UINT32_EQ(ScheduleTable[1].EnableState, SCH_DISABLED);

    command.GroupData = 0x00000004;
    SetMessageSize(sizeof(command));
    SCH_DisableGroupCmd((CFE_MSG_Message_t *)&command);
    UtAssert_UINT32_EQ(ScheduleTable[2].EnableState, SCH_DISABLED);
    UtAssert_True(SCH_AppData.CmdCounter >= 4, "successful group commands counted");
}

static void Test_SCH_DiagnosticAndHousekeeping(void)
{
    SCH_NoArgsCmd_t command;
    CFE_SB_MsgId_t  message_ids[2] = {CFE_SB_ValueToMsgId(0x0810), CFE_SB_ValueToMsgId(0x0811)};

    memset(&command, 0, sizeof(command));
    ScheduleTable[0].EnableState  = SCH_ENABLED;
    ScheduleTable[0].MessageIndex = 1;
    ScheduleTable[1].EnableState  = SCH_DISABLED;
    ScheduleTable[1].MessageIndex = 2;
    SetMessageSize(sizeof(command));
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), message_ids, sizeof(message_ids), false);
    SCH_SendDiagTlmCmd((CFE_MSG_Message_t *)&command);
    UtAssert_True(SCH_AppData.DiagPacket.EntryStates[0] != 0, "entry states encoded");
    UtAssert_STUB_COUNT(CFE_SB_TransmitMsg, 1);

    SCH_AppData.CmdCounter                   = 11;
    SCH_AppData.ScheduleActivitySuccessCount = 12;
    SetMessageSize(sizeof(command));
    UT_SetDeferredRetcode(UT_KEY(CFE_TBL_GetAddress), 1, CFE_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(CFE_TBL_GetAddress), 1, CFE_SUCCESS);
    UtAssert_INT32_EQ(SCH_HousekeepingCmd((CFE_MSG_Message_t *)&command), CFE_SUCCESS);
    UtAssert_UINT32_EQ(SCH_AppData.HkPacket.CmdCounter, 11);
    UtAssert_UINT32_EQ(SCH_AppData.HkPacket.ScheduleActivitySuccessCount, 12);
}

static void Test_SCH_AppPipe(void)
{
    union
    {
        SCH_NoArgsCmd_t no_args;
        SCH_EntryCmd_t  entry;
        SCH_GroupCmd_t  group;
    } command;
    CFE_MSG_FcnCode_t codes[] = {SCH_NOOP_CC, SCH_RESET_CC, SCH_ENABLE_CC, SCH_DISABLE_CC,
                                 SCH_ENABLE_GROUP_CC, SCH_DISABLE_GROUP_CC, SCH_ENABLE_SYNC_CC,
                                 SCH_SEND_DIAG_TLM_CC, 99};
    size_t i;
    size_t command_size;

    memset(&command, 0, sizeof(command));
    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i)
    {
        command_size = sizeof(command.no_args);
        if (codes[i] == SCH_ENABLE_CC || codes[i] == SCH_DISABLE_CC)
        {
            command_size = sizeof(command.entry);
        }
        else if (codes[i] == SCH_ENABLE_GROUP_CC || codes[i] == SCH_DISABLE_GROUP_CC)
        {
            command_size = sizeof(command.group);
        }
        SetMessageMetadata(SCH_CMD_MID, codes[i], command_size);
        SCH_AppPipe((CFE_MSG_Message_t *)&command.no_args);
    }
    SetMessageMetadata(SCH_SEND_HK_MID, 0, sizeof(command.no_args));
    SCH_AppPipe((CFE_MSG_Message_t *)&command.no_args);
    SetMessageMetadata(0x0777, 0, sizeof(command.no_args));
    SCH_AppPipe((CFE_MSG_Message_t *)&command.no_args);
    UtAssert_True(UT_GetStubCount(UT_KEY(CFE_EVS_SendEvent)) > 0, "dispatch paths reported events");
}

static void Test_SCH_AcquirePointers(void)
{
    UT_SetDeferredRetcode(UT_KEY(CFE_TBL_GetAddress), 1, CFE_TBL_INFO_UPDATED);
    UT_SetDeferredRetcode(UT_KEY(CFE_TBL_GetAddress), 1, CFE_TBL_INFO_UPDATED);
    UtAssert_INT32_EQ(SCH_AcquirePointers(), CFE_SUCCESS);

    UT_ResetState(0);
    UT_SetDeferredRetcode(UT_KEY(CFE_TBL_GetAddress), 1, CFE_TBL_ERR_INVALID_HANDLE);
    UtAssert_INT32_EQ(SCH_AcquirePointers(), CFE_TBL_ERR_INVALID_HANDLE);
    UtAssert_STUB_COUNT(CFE_TBL_GetAddress, 1);
}

static void Test_SCH_Initialization(void)
{
    UtAssert_INT32_EQ(SCH_EvsInit(), CFE_SUCCESS);
    UtAssert_UINT32_EQ(SCH_AppData.EventFilters[0].EventID, SCH_SAME_SLOT_EID);

    UT_SetDeferredRetcode(UT_KEY(CFE_EVS_Register), 1, CFE_EVS_INVALID_PARAMETER);
    UtAssert_INT32_EQ(SCH_EvsInit(), CFE_EVS_INVALID_PARAMETER);
    UtAssert_STUB_COUNT(CFE_ES_WriteToSysLog, 1);

    UT_ResetState(0);
    UtAssert_INT32_EQ(SCH_SbInit(), CFE_SUCCESS);
    UtAssert_STUB_COUNT(CFE_SB_Subscribe, 2);
    UT_SetDeferredRetcode(UT_KEY(CFE_SB_CreatePipe), 1, CFE_SB_BAD_ARGUMENT);
    UtAssert_INT32_EQ(SCH_SbInit(), CFE_SB_BAD_ARGUMENT);
    UT_SetDeferredRetcode(UT_KEY(CFE_SB_Subscribe), 1, CFE_SB_BAD_ARGUMENT);
    UtAssert_INT32_EQ(SCH_SbInit(), CFE_SB_BAD_ARGUMENT);
    UT_SetDeferredRetcode(UT_KEY(CFE_SB_Subscribe), 2, CFE_SB_BAD_ARGUMENT);
    UtAssert_INT32_EQ(SCH_SbInit(), CFE_SB_BAD_ARGUMENT);

    UT_ResetState(0);
    UT_SetDeferredRetcode(UT_KEY(CFE_TBL_GetAddress), 1, CFE_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(CFE_TBL_GetAddress), 1, CFE_SUCCESS);
    UtAssert_INT32_EQ(SCH_TblInit(), CFE_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(CFE_TBL_Register), 1, CFE_TBL_ERR_INVALID_SIZE);
    UtAssert_INT32_EQ(SCH_TblInit(), CFE_TBL_ERR_INVALID_SIZE);
    UT_SetDeferredRetcode(UT_KEY(CFE_TBL_Register), 2, CFE_TBL_ERR_INVALID_SIZE);
    UtAssert_INT32_EQ(SCH_TblInit(), CFE_TBL_ERR_INVALID_SIZE);
    UT_SetDeferredRetcode(UT_KEY(CFE_TBL_Load), 1, CFE_TBL_ERR_FILE_FOR_WRONG_TABLE);
    UtAssert_INT32_EQ(SCH_TblInit(), CFE_TBL_ERR_FILE_FOR_WRONG_TABLE);
    UT_SetDeferredRetcode(UT_KEY(CFE_TBL_Load), 2, CFE_TBL_ERR_FILE_FOR_WRONG_TABLE);
    UtAssert_INT32_EQ(SCH_TblInit(), CFE_TBL_ERR_FILE_FOR_WRONG_TABLE);
    UT_SetDeferredRetcode(UT_KEY(CFE_TBL_GetAddress), 1, CFE_TBL_ERR_INVALID_HANDLE);
    UtAssert_INT32_EQ(SCH_TblInit(), CFE_TBL_ERR_INVALID_HANDLE);

    UT_ResetState(0);
    UT_SetDeferredRetcode(UT_KEY(CFE_ES_GetAppID), 1, -1);
    UtAssert_INT32_EQ(SCH_AppInit(), -1);
    UT_SetDeferredRetcode(UT_KEY(CFE_EVS_Register), 1, CFE_EVS_INVALID_PARAMETER);
    UtAssert_INT32_EQ(SCH_AppInit(), CFE_EVS_INVALID_PARAMETER);

    UT_ResetState(0);
    UT_SetDeferredRetcode(UT_KEY(CFE_SB_CreatePipe), 1, CFE_SB_BAD_ARGUMENT);
    UtAssert_INT32_EQ(SCH_AppInit(), CFE_SB_BAD_ARGUMENT);

    UT_ResetState(0);
    UT_SetDeferredRetcode(UT_KEY(CFE_TBL_Register), 1, CFE_TBL_ERR_INVALID_SIZE);
    UtAssert_INT32_EQ(SCH_AppInit(), CFE_TBL_ERR_INVALID_SIZE);

    UT_ResetState(0);
    UT_SetDeferredRetcode(UT_KEY(CFE_TBL_GetAddress), 1, CFE_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(CFE_TBL_GetAddress), 1, CFE_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(OS_TimerCreate), 1, OS_ERROR);
    UtAssert_INT32_EQ(SCH_AppInit(), OS_ERROR);

    UT_ResetState(0);
    UtAssert_INT32_EQ(SCH_TimerInit(), CFE_SUCCESS);
    UtAssert_UINT32_EQ(SCH_AppData.SyncToMET, SCH_MINOR_SYNCHRONIZED);
    UtAssert_True(SCH_AppData.WorstCaseSlotsPerMinorFrame > 1, "timer accuracy adjusts wakeup allowance");

    UT_ResetState(0);
    UT_SetDeferredRetcode(UT_KEY(OS_TimerCreate), 1, OS_ERROR);
    UtAssert_INT32_EQ(SCH_TimerInit(), OS_ERROR);
    UtAssert_STUB_COUNT(OS_BinSemCreate, 0);

    UT_ResetState(0);
    UT_SetDeferredRetcode(UT_KEY(OS_BinSemCreate), 1, OS_ERROR);
    UtAssert_INT32_EQ(SCH_TimerInit(), OS_ERROR);

    UT_ResetState(0);
    UT_SetDeferredRetcode(UT_KEY(CFE_TBL_GetAddress), 1, CFE_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(CFE_TBL_GetAddress), 1, CFE_SUCCESS);
    UtAssert_INT32_EQ(SCH_AppInit(), CFE_SUCCESS);
    UtAssert_UINT32_EQ(SCH_AppData.CmdCounter, 0);
    UtAssert_STUB_COUNT(OS_BinSemCreate, 1);
}

static void Test_SCH_AppMainInitFailure(void)
{
    UT_SetDeferredRetcode(UT_KEY(CFE_ES_GetAppID), 1, -1);
    UT_SetDefaultReturnValue(UT_KEY(CFE_ES_RunLoop), false);

    SCH_AppMain();

    UtAssert_STUB_COUNT(CFE_ES_ExitApp, 1);
    UtAssert_STUB_COUNT(CFE_TIME_UnregisterSynchCallback, 1);
    UtAssert_STUB_COUNT(CFE_ES_WriteToSysLog, 2);
}

static void Test_SCH_AppMainOneFrame(void)
{
    void *table_ptrs[2] = {ScheduleTable, MessageTable};

    UT_SetDataBuffer(UT_KEY(CFE_TBL_GetAddress), table_ptrs, sizeof(table_ptrs), false);
    UT_SetDeferredRetcode(UT_KEY(CFE_ES_RunLoop), 1, true);
    UT_SetDefaultReturnValue(UT_KEY(CFE_ES_RunLoop), false);
    UT_SetHandlerFunction(UT_KEY(OS_BinSemTake), SetNoisyFrameOnSemaphoreTake, NULL);
    SCH_EnableProcessing();

    SCH_AppMain();

    UtAssert_STUB_COUNT(CFE_ES_RunLoop, 2);
    UtAssert_STUB_COUNT(OS_BinSemTake, 1);
    UtAssert_STUB_COUNT(CFE_ES_ExitApp, 1);
    UtAssert_True(SCH_AppData.IgnoreMajorFrameMsgSent, "main loop reports noisy major frame");
    UtAssert_True(SCH_AppData.SlotsProcessedCount > 0, "main loop processed a schedule slot");
}

static void Test_SCH_AppMainFailures(void)
{
    void *table_ptrs[2] = {ScheduleTable, MessageTable};

    UT_SetDataBuffer(UT_KEY(CFE_TBL_GetAddress), table_ptrs, sizeof(table_ptrs), false);
    UT_SetDefaultReturnValue(UT_KEY(CFE_ES_RunLoop), false);
    PthreadCreateResult = 1;
    SCH_AppMain();
    UtAssert_STUB_COUNT(CFE_ES_ExitApp, 1);

    SCH_Test_Setup();
    UT_SetDataBuffer(UT_KEY(CFE_TBL_GetAddress), table_ptrs, sizeof(table_ptrs), false);
    UT_SetDeferredRetcode(UT_KEY(CFE_ES_RunLoop), 1, true);
    UT_SetDefaultReturnValue(UT_KEY(CFE_ES_RunLoop), false);
    UT_SetHandlerFunction(UT_KEY(OS_BinSemTake), FailSemaphoreTake, NULL);
    SCH_AppMain();
    UtAssert_STUB_COUNT(OS_BinSemTake, 1);
    UtAssert_STUB_COUNT(CFE_ES_ExitApp, 1);
}

static void Test_SCH_CustomLateInitFailures(void)
{
    UT_SetDeferredRetcode(UT_KEY(CFE_TIME_RegisterSynchCallback), 1, CFE_STATUS_EXTERNAL_RESOURCE_FAIL);
    UtAssert_INT32_EQ(SCH_CustomLateInit(), CFE_STATUS_EXTERNAL_RESOURCE_FAIL);

    UT_ResetState(0);
    PthreadCreateResult = 1;
    UtAssert_INT32_EQ(SCH_CustomLateInit(), CFE_STATUS_EXTERNAL_RESOURCE_FAIL);
    UtAssert_STUB_COUNT(CFE_EVS_SendEvent, 1);
}

static void Test_SCH_CustomTickThread(void)
{
    StopTickThreadOnSecondWait = true;
    UtAssert_INT32_EQ(SCH_CustomLateInit(), CFE_SUCCESS);
    UtAssert_True(PthreadStartRoutine != NULL, "tick thread start routine captured");

    PthreadStartRoutine(PthreadStartArg);

    UtAssert_UINT32_EQ(TickWaitCount, 2);
    UtAssert_STUB_COUNT(OS_BinSemGive, 1);
}

static void Test_SCH_ScheduleExecution(void)
{
    SCH_ScheduleEntry_t entry;
    CFE_SB_Buffer_t      command;
    CFE_SB_Buffer_t     *command_ptr = &command;
    CFE_SB_MsgId_t       command_id  = CFE_SB_ValueToMsgId(0x0777);
    uint32               receive_count;

    memset(&entry, 0, sizeof(entry));
    entry.EnableState = SCH_ENABLED;
    SCH_ProcessNextEntry(&entry, 0);
    UtAssert_UINT32_EQ(entry.EnableState, SCH_DISABLED);
    UtAssert_UINT32_EQ(SCH_AppData.BadTableDataCount, 1);

    entry.EnableState = SCH_ENABLED;
    entry.Type         = SCH_ACTIVITY_SEND_MSG;
    entry.Frequency    = 1;
    entry.Remainder    = 0;
    entry.MessageIndex = 1;
    SCH_ProcessNextEntry(&entry, 1);
    UtAssert_UINT32_EQ(SCH_AppData.ScheduleActivitySuccessCount, 1);
    UT_SetDeferredRetcode(UT_KEY(CFE_SB_TransmitMsg), 1, CFE_SB_BAD_ARGUMENT);
    SCH_ProcessNextEntry(&entry, 2);
    UtAssert_UINT32_EQ(SCH_AppData.ScheduleActivityFailureCount, 1);

    entry.Frequency = 2;
    entry.Remainder = 1;
    SCH_AppData.TablePassCount = 0;
    SCH_ProcessNextEntry(&entry, 3);
    UtAssert_UINT32_EQ(SCH_AppData.ScheduleActivitySuccessCount, 1);

    ScheduleTable[0] = entry;
    ScheduleTable[0].Frequency = 1;
    ScheduleTable[0].Remainder = 0;
    SCH_AppData.NextSlotNumber = 0;
    UtAssert_INT32_EQ(SCH_ProcessNextSlot(), CFE_SUCCESS);
    UtAssert_UINT32_EQ(SCH_AppData.NextSlotNumber, 1);

    SCH_AppData.NextSlotNumber = SCH_TIME_SYNC_SLOT;
    UT_SetDeferredRetcode(UT_KEY(CFE_SB_ReceiveBuffer), 1, CFE_SB_NO_MESSAGE);
    UtAssert_INT32_EQ(SCH_ProcessNextSlot(), CFE_SUCCESS);
    UtAssert_UINT32_EQ(SCH_AppData.NextSlotNumber, 0);

    memset(&command, 0, sizeof(command));
    receive_count = UT_GetStubCount(UT_KEY(CFE_SB_ReceiveBuffer));
    UT_SetDataBuffer(UT_KEY(CFE_SB_ReceiveBuffer), &command_ptr, sizeof(command_ptr), false);
    UT_SetDeferredRetcode(UT_KEY(CFE_SB_ReceiveBuffer), 1, CFE_SUCCESS);
    UT_SetDeferredRetcode(UT_KEY(CFE_SB_ReceiveBuffer), 1, CFE_SB_NO_MESSAGE);
    UT_SetDataBuffer(UT_KEY(CFE_MSG_GetMsgId), &command_id, sizeof(command_id), false);
    UtAssert_INT32_EQ(SCH_ProcessCommands(), CFE_SUCCESS);
    UtAssert_UINT32_EQ(UT_GetStubCount(UT_KEY(CFE_SB_ReceiveBuffer)), receive_count + 2);
}

static void Test_SCH_ProcessScheduleTable(void)
{
    SCH_AppData.SyncToMET                    = SCH_NOT_SYNCHRONIZED;
    SCH_AppData.WorstCaseSlotsPerMinorFrame = 1;

    SCH_AppData.NextSlotNumber       = 0;
    SCH_AppData.MinorFramesSinceTone = 0;
    UtAssert_INT32_EQ(SCH_ProcessScheduleTable(), CFE_SUCCESS);
    UtAssert_UINT32_EQ(SCH_AppData.NextSlotNumber, 1);

    SCH_AppData.NextSlotNumber       = 0;
    SCH_AppData.MinorFramesSinceTone = 1;
    SCH_AppData.LastProcessCount     = 1;
    UtAssert_INT32_EQ(SCH_ProcessScheduleTable(), CFE_SUCCESS);
    UtAssert_UINT32_EQ(SCH_AppData.NextSlotNumber, 1);

    SCH_AppData.NextSlotNumber       = 0;
    SCH_AppData.MinorFramesSinceTone = 1;
    SCH_AppData.LastProcessCount     = 3;
    UtAssert_INT32_EQ(SCH_ProcessScheduleTable(), CFE_SUCCESS);
    UtAssert_UINT32_EQ(SCH_AppData.NextSlotNumber, 2);
    UtAssert_True(SCH_AppData.MultipleSlotsCount > 0, "multiple slot processing counted");

    SCH_AppData.NextSlotNumber       = 1;
    SCH_AppData.MinorFramesSinceTone = 0;
    SCH_AppData.LastProcessCount     = SCH_TOTAL_SLOTS;
    UtAssert_INT32_EQ(SCH_ProcessScheduleTable(), CFE_SUCCESS);
    UtAssert_UINT32_EQ(SCH_AppData.SameSlotCount, 1);

    SCH_AppData.NextSlotNumber       = 1;
    SCH_AppData.MinorFramesSinceTone = 0;
    SCH_AppData.LastProcessCount     = 0;
    UtAssert_INT32_EQ(SCH_ProcessScheduleTable(), CFE_SUCCESS);
    UtAssert_UINT32_EQ(SCH_AppData.NextSlotNumber, 2);

    SCH_AppData.NextSlotNumber       = 90;
    SCH_AppData.MinorFramesSinceTone = 50;
    SCH_AppData.LastProcessCount     = 1;
    UT_SetDeferredRetcode(UT_KEY(CFE_SB_ReceiveBuffer), 1, CFE_SB_NO_MESSAGE);
    UtAssert_INT32_EQ(SCH_ProcessScheduleTable(), CFE_SUCCESS);
    UtAssert_UINT32_EQ(SCH_AppData.NextSlotNumber, 51);
    UtAssert_UINT32_EQ(SCH_AppData.SkippedSlotsCount, 1);
    UtAssert_True(SCH_AppData.TablePassCount > 0, "skipped rollover advances pass count");

    SCH_AppData.NextSlotNumber       = 0;
    SCH_AppData.MinorFramesSinceTone = 10;
    SCH_AppData.LastProcessCount     = 1;
    UtAssert_INT32_EQ(SCH_ProcessScheduleTable(), CFE_SUCCESS);
    UtAssert_UINT32_EQ(SCH_AppData.NextSlotNumber, SCH_MAX_SLOTS_PER_WAKEUP);
}

static void Test_SCH_ScheduleValidation(void)
{
    SCH_ScheduleEntry_t table[SCH_TABLE_ENTRIES];
    int32 expected[] = {SCH_SDT_GARBAGE_ENTRY, SCH_SDT_NO_FREQUENCY, SCH_SDT_BAD_REMAINDER,
                        SCH_SDT_BAD_ACTIVITY, SCH_SDT_BAD_MSG_INDEX, SCH_SDT_BAD_MSG_INDEX,
                        SCH_SDT_BAD_ENABLE_STATE};
    size_t i;

    memset(table, 0, sizeof(table));
    UtAssert_INT32_EQ(SCH_ValidateScheduleData(table), CFE_SUCCESS);
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
    {
        memset(table, 0, sizeof(table));
        if (i == 0)
        {
            table[0].Frequency = 1;
        }
        else
        {
            table[0].EnableState  = (i == 6) ? 99 : SCH_ENABLED;
            table[0].Type         = SCH_ACTIVITY_SEND_MSG;
            table[0].Frequency    = 2;
            table[0].Remainder    = 0;
            table[0].MessageIndex = 1;
            if (i == 1) table[0].Frequency = 0;
            if (i == 2) table[0].Remainder = 2;
            if (i == 3) table[0].Type = SCH_ACTIVITY_NONE;
            if (i == 4) table[0].MessageIndex = 0;
            if (i == 5) table[0].MessageIndex = SCH_MAX_MESSAGES;
        }
        UtAssert_INT32_EQ(SCH_ValidateScheduleData(table), expected[i]);
    }

    memset(table, 0, sizeof(table));
    table[0].EnableState  = SCH_DISABLED;
    table[0].Type         = SCH_ACTIVITY_SEND_MSG;
    table[0].Frequency    = 2;
    table[0].MessageIndex = 1;
    UtAssert_INT32_EQ(SCH_ValidateScheduleData(table), CFE_SUCCESS);
}

static void Test_SCH_MessageValidation(void)
{
    SCH_MessageEntry_t table[SCH_MAX_MESSAGES];
    CFE_SB_MsgId_t     msg_ids[SCH_MAX_MESSAGES];
    CFE_MSG_Size_t     sizes[SCH_MAX_MESSAGES];

    memset(table, 0, sizeof(table));
    SetMessageTableMetadata(msg_ids, sizes, 0x0810, sizeof(CFE_MSG_CommandHeader_t));
    UtAssert_INT32_EQ(SCH_ValidateMessageData(table), CFE_SUCCESS);
    UtAssert_UINT32_EQ(SCH_AppData.TableVerifySuccessCount, 1);

    UT_ResetState(0);
    SetMessageTableMetadata(msg_ids, sizes, 0x0810, sizeof(CFE_MSG_CommandHeader_t));
    sizes[0] = (SCH_MAX_MSG_WORDS * 2) + 2;
    UtAssert_INT32_EQ(SCH_ValidateMessageData(table), SCH_MDT_INVALID_LENGTH);

    UT_ResetState(0);
    SetMessageTableMetadata(msg_ids, sizes, 0x0810, sizeof(CFE_MSG_CommandHeader_t));
    sizes[0] = (SCH_MIN_MSG_WORDS * 2) - 2;
    UtAssert_INT32_EQ(SCH_ValidateMessageData(table), SCH_MDT_INVALID_LENGTH);

    UT_ResetState(0);
    SetMessageTableMetadata(msg_ids, sizes, 0x0810, sizeof(CFE_MSG_CommandHeader_t));
    sizes[0] = (SCH_MIN_MSG_WORDS * 2) + 1;
    UtAssert_INT32_EQ(SCH_ValidateMessageData(table), SCH_MDT_INVALID_LENGTH);

    UT_ResetState(0);
    SetMessageTableMetadata(msg_ids, sizes, CFE_PLATFORM_SB_HIGHEST_VALID_MSGID + 1,
                            sizeof(CFE_MSG_CommandHeader_t));
    UtAssert_INT32_EQ(SCH_ValidateMessageData(table), SCH_MDT_BAD_MSG_ID);

    UT_ResetState(0);
    SetMessageTableMetadata(msg_ids, sizes, SCH_UNUSED_MID, 0);
    UtAssert_INT32_EQ(SCH_ValidateMessageData(table), CFE_SUCCESS);

    UT_ResetState(0);
    memset(table, 0, sizeof(table));
    table[0].MessageBuffer[SCH_MAX_MSG_WORDS - 1] = 1;
    SetMessageTableMetadata(msg_ids, sizes, SCH_UNUSED_MID, 0);
    UtAssert_INT32_EQ(SCH_ValidateMessageData(table), SCH_MDT_GARBAGE_ENTRY);
}

static void Test_SCH_TimingCallbacks(void)
{
    uint32 micros;
    uint16 clock_info;

    SCH_AppData.SyncToMET            = SCH_NOT_SYNCHRONIZED;
    SCH_AppData.MinorFramesSinceTone = 9;
    UtAssert_UINT32_EQ(SCH_CustomGetCurrentSlotNumber(), 9);

    micros = SCH_NORMAL_SLOT_PERIOD - 1;
    UT_SetDefaultReturnValue(UT_KEY(CFE_TIME_Sub2MicroSecs), micros);
    UtAssert_UINT32_EQ(SCH_GetMETSlotNumber(), 1);

    SCH_AppData.MajorFrameSource = SCH_MAJOR_FS_NONE;
    SCH_AppData.SyncAttemptsLeft = 1;
    SCH_MinorFrameCallback(0);
    UtAssert_UINT32_EQ(SCH_AppData.MajorFrameSource, SCH_MAJOR_FS_MINOR_FRAME_TIMER);
    UtAssert_STUB_COUNT(OS_BinSemGive, 0);
    UT_SetDefaultReturnValue(UT_KEY(CFE_TIME_Sub2MicroSecs), 0);
    SCH_MinorFrameCallback(0);
    UtAssert_STUB_COUNT(OS_BinSemGive, 1);

    SCH_AppData.MajorFrameSource     = SCH_MAJOR_FS_CFE_TIME;
    SCH_AppData.MinorFramesSinceTone = SCH_TOTAL_SLOTS - 1;
    SCH_MinorFrameCallback(0);
    UtAssert_UINT32_EQ(SCH_AppData.MinorFramesSinceTone, 0);
    UtAssert_UINT32_EQ(SCH_AppData.MissedMajorFrameCount, 1);

    clock_info = CFE_TIME_FLAG_FLYING;
    UT_SetDefaultReturnValue(UT_KEY(CFE_TIME_GetClockInfo), clock_info);
    SCH_MajorFrameCallback();
    UtAssert_UINT32_EQ(SCH_AppData.ValidMajorFrameCount, 0);

    clock_info = 0;
    UT_SetDefaultReturnValue(UT_KEY(CFE_TIME_GetClockInfo), clock_info);
    SCH_AppData.MinorFramesSinceTone = SCH_TIME_SYNC_SLOT;
    SCH_MajorFrameCallback();
    UtAssert_UINT32_EQ(SCH_AppData.ValidMajorFrameCount, 1);

    UT_SetDefaultReturnValue(UT_KEY(CFE_TIME_Sub2MicroSecs), SCH_MICROS_PER_MAJOR_FRAME - 1);
    UtAssert_UINT32_EQ(SCH_GetMETSlotNumber(), 0);

    SCH_AppData.SyncToMET       = SCH_MINOR_SYNCHRONIZED;
    SCH_AppData.LastSyncMETSlot = 4;
    UT_SetDefaultReturnValue(UT_KEY(CFE_TIME_Sub2MicroSecs), (2 * SCH_NORMAL_SLOT_PERIOD));
    UtAssert_UINT32_EQ(SCH_CustomGetCurrentSlotNumber(), SCH_TOTAL_SLOTS - 2);
    SCH_AppData.LastSyncMETSlot = 1;
    UtAssert_UINT32_EQ(SCH_CustomGetCurrentSlotNumber(), 1);

    SCH_AppData.SyncToMET                    = SCH_NOT_SYNCHRONIZED;
    SCH_AppData.MinorFramesSinceTone         = 1;
    SCH_AppData.ConsecutiveNoisyFrameCounter = SCH_MAX_NOISY_MAJORF - 1;
    SCH_AppData.IgnoreMajorFrame             = false;
    UT_SetDefaultReturnValue(UT_KEY(CFE_TIME_GetClockInfo), 0);
    SCH_MajorFrameCallback();
    UtAssert_True(SCH_AppData.UnexpectedMajorFrame, "unexpected major frame detected");
    UtAssert_True(SCH_AppData.IgnoreMajorFrame, "repeated noisy frame ignored");

    SCH_AppData.SyncToMET                   = SCH_MINOR_SYNCHRONIZED;
    SCH_AppData.NextSlotNumber              = 1;
    SCH_AppData.WorstCaseSlotsPerMinorFrame = 1;
    SCH_AppData.IgnoreMajorFrame             = false;
    SCH_AppData.ConsecutiveNoisyFrameCounter = 0;
    SCH_MajorFrameCallback();
    UtAssert_True(SCH_AppData.UnexpectedMajorFrame, "early MET-synchronized major frame detected");
}

void UtTest_Setup(void)
{
    UtTest_Add(Test_SCH_Api, SCH_Test_Setup, NULL, "SCH API state");
    UtTest_Add(Test_SCH_BasicCommands, SCH_Test_Setup, NULL, "SCH basic commands");
    UtTest_Add(Test_SCH_EntryCommands, SCH_Test_Setup, NULL, "SCH entry commands");
    UtTest_Add(Test_SCH_GroupCommands, SCH_Test_Setup, NULL, "SCH group commands");
    UtTest_Add(Test_SCH_DiagnosticAndHousekeeping, SCH_Test_Setup, NULL, "SCH diagnostic and housekeeping");
    UtTest_Add(Test_SCH_AppPipe, SCH_Test_Setup, NULL, "SCH command dispatch");
    UtTest_Add(Test_SCH_AcquirePointers, SCH_Test_Setup, NULL, "SCH table pointers");
    UtTest_Add(Test_SCH_Initialization, SCH_Test_Setup, NULL, "SCH initialization failures");
    UtTest_Add(Test_SCH_AppMainInitFailure, SCH_Test_Setup, NULL, "SCH main initialization failure");
    UtTest_Add(Test_SCH_AppMainOneFrame, SCH_Test_Setup, NULL, "SCH main schedule frame");
    UtTest_Add(Test_SCH_AppMainFailures, SCH_Test_Setup, NULL, "SCH main runtime failures");
    UtTest_Add(Test_SCH_CustomLateInitFailures, SCH_Test_Setup, NULL, "SCH custom startup failures");
    UtTest_Add(Test_SCH_CustomTickThread, SCH_Test_Setup, NULL, "SCH custom tick thread");
    UtTest_Add(Test_SCH_ScheduleExecution, SCH_Test_Setup, NULL, "SCH schedule execution");
    UtTest_Add(Test_SCH_ProcessScheduleTable, SCH_Test_Setup, NULL, "SCH schedule timing recovery");
    UtTest_Add(Test_SCH_ScheduleValidation, SCH_Test_Setup, NULL, "SCH schedule validation");
    UtTest_Add(Test_SCH_MessageValidation, SCH_Test_Setup, NULL, "SCH message validation");
    UtTest_Add(Test_SCH_TimingCallbacks, SCH_Test_Setup, NULL, "SCH timing callbacks");
}
