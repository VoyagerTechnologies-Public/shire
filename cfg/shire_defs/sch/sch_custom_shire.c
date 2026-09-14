/*
 * SHIRE Scheduler clock and synchronized transaction customization.
 *
 * This source is selected only by the amd64-shire toolchain. It keeps the
 * Simulith clock, participant barrier, and output pacing out of the portable
 * Scheduler application and out of physical flight builds.
 */

#include "cfe.h"
#include "cfe_psp_timebase.h"

#include "sch_app.h"
#include "sch_custom.h"
#include "sch_events.h"
#include "sch_msgids.h"
#include "sch_platform_cfg.h"
#include "to_lab_msgids.h"

#include <pthread.h>
#include <time.h>

static pthread_t       SCH_ShireTickThread;
static pthread_mutex_t SCH_ShireTickMutex = PTHREAD_MUTEX_INITIALIZER;
static bool            SCH_ShireTickRunning;
static bool            SCH_ShireTickStarted;

static bool SCH_ShireTickIsRunning(void)
{
    bool Running;

    pthread_mutex_lock(&SCH_ShireTickMutex);
    Running = SCH_ShireTickRunning;
    pthread_mutex_unlock(&SCH_ShireTickMutex);
    return Running;
}

static void *SCH_ShireTickMain(void *Arg)
{
    (void)Arg;

    while (SCH_ShireTickIsRunning())
    {
        if (CFE_PSP_WaitForPendingSimulithTick() == 0 &&
            SCH_ShireTickIsRunning())
        {
            SCH_MinorFrameCallback(0);
        }
    }

    return NULL;
}

int32 SCH_CustomEarlyInit(void)
{
    /* Shire has a dedicated sequence consumer. Creating an OSAL timer would
     * provide a second callback source for the same simulated tick. */
    SCH_AppData.TimerId = OS_OBJECT_ID_UNDEFINED;
    SCH_AppData.ClockAccuracy = SCH_NORMAL_SLOT_PERIOD;
    return CFE_SUCCESS;
}

int32 SCH_CustomLateInit(void)
{
    int32 Status = CFE_SUCCESS;

    CFE_ES_WaitForStartupSync(SCH_STARTUP_SYNC_TIMEOUT);

    /* Give lower-priority applications time to finish post-sync setup before
     * the first synchronized mission tick is admitted. */
    {
        const struct timespec StartupSettle = {.tv_nsec = 100000000L};
        nanosleep(&StartupSettle, NULL);
    }

    CFE_PSP_EnableDeferredTickCompletion();
    if (CFE_PSP_StartSynchronizedTicks() != 0)
    {
        return CFE_STATUS_EXTERNAL_RESOURCE_FAIL;
    }

    SCH_AppData.MajorFrameSource = SCH_MAJOR_FS_MINOR_FRAME_TIMER;
    SCH_AppData.SyncToMET = SCH_NOT_SYNCHRONIZED;

    pthread_mutex_lock(&SCH_ShireTickMutex);
    SCH_ShireTickRunning = true;
    pthread_mutex_unlock(&SCH_ShireTickMutex);

    if (pthread_create(&SCH_ShireTickThread, NULL, SCH_ShireTickMain, NULL) != 0)
    {
        CFE_EVS_SendEvent(SCH_MAJOR_FRAME_SUB_ERR_EID,
                          CFE_EVS_EventType_CRITICAL,
                          "SCH: Failed to create SHIRE synchronized tick thread");
        pthread_mutex_lock(&SCH_ShireTickMutex);
        SCH_ShireTickRunning = false;
        pthread_mutex_unlock(&SCH_ShireTickMutex);
        CFE_PSP_StopSynchronizedTicks();
        Status = CFE_STATUS_EXTERNAL_RESOURCE_FAIL;
    }
    else
    {
        pthread_mutex_lock(&SCH_ShireTickMutex);
        SCH_ShireTickStarted = true;
        pthread_mutex_unlock(&SCH_ShireTickMutex);
    }

    return Status;
}

int32 SCH_CustomPrepareEntry(uint32                    ScheduleEntry,
                             const CFE_MSG_Message_t   *Message,
                             SCH_CustomEntryDecision_t *Decision)
{
    CFE_SB_MsgId_t MessageId;
    uint32         MessageIdValue;
    int            ParticipantToken;
    CFE_Status_t   Status;

    if (Message == NULL || Decision == NULL)
    {
        return CFE_SB_BAD_ARGUMENT;
    }

    Decision->Transmit = true;
    Decision->ProcessCommands = false;

    Status = CFE_MSG_GetMsgId(Message, &MessageId);
    if (Status != CFE_SUCCESS)
    {
        return Status;
    }

    MessageIdValue = CFE_SB_MsgIdToValue(MessageId);
    if (MessageIdValue == TO_LAB_WAKEUP_MID)
    {
        /* Periodic ground output is wall-clock paced and deliberately outside
         * the internal mission barrier. Its Software Bus pipe remains lossless. */
        Decision->Transmit = CFE_PSP_AllowPeriodicGroundOutput(MessageIdValue);
        return CFE_SUCCESS;
    }

    ParticipantToken = CFE_PSP_RegisterSimulithParticipant(
        ScheduleEntry, MessageIdValue);
    if (ParticipantToken <= 0)
    {
        return CFE_STATUS_EXTERNAL_RESOURCE_FAIL;
    }

    /* SCH owns both ends of its housekeeping request. Drain that one request
     * before declaring the synchronized slot complete. */
    Decision->ProcessCommands = MessageIdValue == SCH_SEND_HK_MID;
    return CFE_SUCCESS;
}

int32 SCH_CustomCompleteCurrentSlot(void)
{
    if (CFE_PSP_WaitForSimulithParticipants() != 0)
    {
        return CFE_STATUS_EXTERNAL_RESOURCE_FAIL;
    }

    return CFE_PSP_CompleteSimulithTick() == 0 ?
               CFE_SUCCESS : CFE_STATUS_EXTERNAL_RESOURCE_FAIL;
}

void SCH_CustomCleanup(void)
{
    bool Started;

    pthread_mutex_lock(&SCH_ShireTickMutex);
    Started = SCH_ShireTickStarted;
    SCH_ShireTickRunning = false;
    SCH_ShireTickStarted = false;
    pthread_mutex_unlock(&SCH_ShireTickMutex);

    if (Started)
    {
        CFE_PSP_StopSynchronizedTicks();
        pthread_join(SCH_ShireTickThread, NULL);
    }
}
