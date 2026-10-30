/* Runtime integration coverage for SHIRE's Simulith-backed PSP adapters. */

#include "cfe_psp.h"
#include "cfe_psp_config.h"
#include "cfe_psp_exceptionstorage_api.h"
#include "cfe_psp_exceptionstorage_types.h"
#include "cfe_psp_timebase.h"
#include "simulith.h"

#include "utassert.h"
#include "uttest.h"

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void  timebase_simulith_clock_Init(uint32 PspModuleId);
void  CFE_PSP_ExceptionSigHandler(int signo, siginfo_t *info, void *context);
void  CFE_PSP_AttachExceptions(void);
void  CFE_PSP_SetDefaultExceptionEnvironment(void);
int32 CFE_PSP_ExceptionGetSummary_Impl(const CFE_PSP_Exception_LogData_t *buffer, char *reason, uint32 size);

extern volatile uint64_t tick_generation;
extern bool              tick_thread_running;

CFE_PSP_IdleTaskState_t CFE_PSP_IdleTaskState;

static pthread_mutex_t FakeTickMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  FakeTickCondition = PTHREAD_COND_INITIALIZER;
static int             FakeInitStatus;
static int             FakeConfigureStatus;
static int             FakeHandshakeStatus;
static int             FakeCompletionStatus;
static bool            FakeClientStopped;
static bool            FakeStopPending;
static uint64_t        FakeNextTick;
static uint64_t        FakeNextSequence;
static unsigned int    FakePendingTicks;
static unsigned int    FakeInitCalls;
static unsigned int    FakeConfigureCalls;
static unsigned int    FakeHandshakeCalls;
static unsigned int    FakeCompletionCalls;
static unsigned int    FakeShutdownCalls;

static CFE_PSP_Exception_LogData_t ExceptionBuffer;
static bool                        ReturnExceptionBuffer;
static unsigned int                ExceptionWriteCount;
static unsigned int                ExceptionResetCount;

int simulith_client_init(const char *pub_addr, const char *rep_addr, const char *id, uint64_t rate_ns)
{
    UtAssert_NOT_NULL(pub_addr);
    UtAssert_NOT_NULL(rep_addr);
    UtAssert_StrCmp(id, "shire-fsw", "PSP identifies its Simulith client");
    UtAssert_True(rate_ns > 0, "PSP supplies a nonzero tick interval");
    FakeInitCalls++;
    return FakeInitStatus;
}

int simulith_client_handshake(void)
{
    FakeHandshakeCalls++;
    return FakeHandshakeStatus;
}

int simulith_client_configure_phases(uint32_t phase_mask)
{
    UtAssert_UINT32_EQ(phase_mask, SIMULITH_PHASE_MASK_EXECUTE);
    FakeConfigureCalls++;
    return FakeConfigureStatus;
}

int simulith_client_receive_phase(uint64_t *tick_time_ns, uint64_t *sequence,
                                  simulith_phase_t *phase)
{
    pthread_mutex_lock(&FakeTickMutex);
    while (FakePendingTicks == 0 && !FakeStopPending && !FakeClientStopped)
    {
        pthread_cond_wait(&FakeTickCondition, &FakeTickMutex);
    }
    if (FakeStopPending)
    {
        FakeStopPending = false;
        *phase = SIMULITH_PHASE_STOP;
        pthread_mutex_unlock(&FakeTickMutex);
        return 1;
    }
    if (FakeClientStopped)
    {
        pthread_mutex_unlock(&FakeTickMutex);
        return -1;
    }
    *tick_time_ns = FakeNextTick;
    *sequence = FakeNextSequence++;
    *phase = SIMULITH_PHASE_EXECUTE;
    FakeNextTick += INTERVAL_NS;
    FakePendingTicks--;
    pthread_mutex_unlock(&FakeTickMutex);
    return 0;
}

int simulith_client_complete_tick(uint64_t sequence, simulith_phase_t phase)
{
    (void)sequence;
    UtAssert_UINT32_EQ(phase, SIMULITH_PHASE_EXECUTE);
    FakeCompletionCalls++;
    return FakeCompletionStatus;
}

void simulith_client_request_stop(void)
{
    pthread_mutex_lock(&FakeTickMutex);
    FakeClientStopped = true;
    pthread_cond_broadcast(&FakeTickCondition);
    pthread_mutex_unlock(&FakeTickMutex);
}

void simulith_client_shutdown(void)
{
    pthread_mutex_lock(&FakeTickMutex);
    FakeClientStopped = true;
    FakeShutdownCalls++;
    pthread_cond_broadcast(&FakeTickCondition);
    pthread_mutex_unlock(&FakeTickMutex);
}

CFE_PSP_Exception_LogData_t *CFE_PSP_Exception_GetNextContextBuffer(void)
{
    return ReturnExceptionBuffer ? &ExceptionBuffer : NULL;
}

void CFE_PSP_Exception_WriteComplete(void)
{
    ExceptionWriteCount++;
}

void CFE_PSP_Exception_Reset(void)
{
    ExceptionResetCount++;
}

static void ResetFakeClient(void)
{
    pthread_mutex_lock(&FakeTickMutex);
    FakeInitStatus      = 0;
    FakeConfigureStatus = 0;
    FakeHandshakeStatus = 0;
    FakeCompletionStatus = 0;
    FakeClientStopped   = false;
    FakeStopPending     = false;
    FakeNextTick        = 1000000000ULL;
    FakeNextSequence    = 0;
    FakePendingTicks    = 0;
    FakeInitCalls       = 0;
    FakeConfigureCalls  = 0;
    FakeHandshakeCalls  = 0;
    FakeCompletionCalls = 0;
    FakeShutdownCalls   = 0;
    pthread_mutex_unlock(&FakeTickMutex);
}

static void QueueTicks(unsigned int count)
{
    pthread_mutex_lock(&FakeTickMutex);
    FakePendingTicks += count;
    pthread_cond_broadcast(&FakeTickCondition);
    pthread_mutex_unlock(&FakeTickMutex);
}

static void QueueStop(void)
{
    pthread_mutex_lock(&FakeTickMutex);
    FakeStopPending = true;
    pthread_cond_broadcast(&FakeTickCondition);
    pthread_mutex_unlock(&FakeTickMutex);
}

static bool WaitForGeneration(uint64_t expected)
{
    unsigned int retries;

    for (retries = 0; retries < 1000 && tick_generation < expected; ++retries)
    {
        usleep(1000);
    }
    return tick_generation >= expected;
}

static bool WaitForStoppedClock(void)
{
    unsigned int retries;

    for (retries = 0; retries < 1000 && tick_thread_running; ++retries)
    {
        usleep(1000);
    }
    return !tick_thread_running;
}

static void Test_TimebaseInitializationFailures(void)
{
    ResetFakeClient();
    FakeInitStatus = -11;
    CFE_PSP_InitSimulithTime();
    UtAssert_INT32_EQ(FakeInitCalls, 1);
    UtAssert_INT32_EQ(FakeConfigureCalls, 0);
    UtAssert_INT32_EQ(FakeHandshakeCalls, 0);

    ResetFakeClient();
    FakeConfigureStatus = -12;
    CFE_PSP_InitSimulithTime();
    UtAssert_INT32_EQ(FakeConfigureCalls, 1);
    UtAssert_INT32_EQ(FakeHandshakeCalls, 0);
    UtAssert_INT32_EQ(FakeShutdownCalls, 1);

    ResetFakeClient();
    FakeHandshakeStatus = -13;
    CFE_PSP_InitSimulithTime();
    UtAssert_INT32_EQ(FakeHandshakeCalls, 0);
    UtAssert_INT32_EQ(CFE_PSP_StartSynchronizedTicks(), -1);
    UtAssert_INT32_EQ(FakeHandshakeCalls, 1);
    UtAssert_INT32_EQ(FakeShutdownCalls, 1);
    UtAssert_True(!tick_thread_running, "failed handshake stops the tick clock");

    /* A failed handshake must completely release the client so a later
     * initialization attempt can recover rather than remaining half-open. */
    FakeHandshakeStatus = 0;
    FakeClientStopped   = false;
    CFE_PSP_InitSimulithTime();
    UtAssert_INT32_EQ(FakeInitCalls, 2);
    CFE_PSP_ShutdownSimulithTime();
}

static void Test_TimebaseTickDistribution(void)
{
    struct timespec   timespec;
    OS_time_t         os_time;
    uint32            upper;
    uint32            lower;
    uint64_t          generation;

    ResetFakeClient();
    CFE_PSP_InitSimulithTime();
    UtAssert_INT32_EQ(FakeInitCalls, 1);
    UtAssert_INT32_EQ(FakeConfigureCalls, 1);
    UtAssert_INT32_EQ(FakeHandshakeCalls, 0);
    CFE_PSP_Get_Timebase(&upper, &lower);
    UtAssert_True(upper == 0 && lower == 0,
                  "bootstrap exposes logical zero rather than a clock that would jump backwards");
    CFE_PSP_GetTime(&os_time);
    UtAssert_True(OS_TimeGetTotalNanoseconds(os_time) == 0,
                  "EVS monotonic accounting starts in the simulation clock domain");
    CFE_PSP_InitSimulithTime();
    UtAssert_INT32_EQ(FakeInitCalls, 1);
    UtAssert_INT32_EQ(CFE_PSP_StartSynchronizedTicks(), 0);
    UtAssert_INT32_EQ(FakeHandshakeCalls, 1);
    CFE_PSP_GetTime(&os_time);
    UtAssert_True(OS_TimeGetTotalNanoseconds(os_time) == 0,
                  "handshake alone does not advance the platform clock");
    UtAssert_INT32_EQ(CFE_PSP_StartSynchronizedTicks(), 0);

    generation = tick_generation;
    QueueTicks(1);
    UtAssert_True(WaitForGeneration(generation + 1), "PSP distribution thread published a tick");
    UtAssert_True(CFE_PSP_GetSimulithTimeNs() == 1000000000ULL, "latest Simulith time is retained");
    CFE_PSP_GetSimulithTimespec(&timespec);
    UtAssert_True(timespec.tv_sec == 1 && timespec.tv_nsec == 0, "Simulith time converts to timespec");
    CFE_PSP_Get_Timebase(&upper, &lower);
    UtAssert_True(upper == 1 && lower == 0, "legacy timebase uses Simulith time");
    CFE_PSP_GetTime(&os_time);
    UtAssert_True(OS_TimeGetTotalNanoseconds(os_time) == 1000000000LL, "OS time uses Simulith time");

    generation = tick_generation;
    /* Establish the caller's cursor before publishing ticks so an unbounded
     * producer cannot race ahead of the wait's baseline. */
    UtAssert_UINT32_EQ(CFE_PSP_WaitForSimulithTick(0), 0);
    QueueTicks(2);
    UtAssert_True(WaitForGeneration(generation + 2), "two ticks were distributed");
    UtAssert_UINT32_EQ(CFE_PSP_WaitForSimulithTick(2), 2);
    UtAssert_True(FakeCompletionCalls >= 3, "startup-mode ticks are explicitly completed");

    UtAssert_UINT32_EQ(CFE_PSP_GetTimerTicksPerSecond(), 1000000000UL / INTERVAL_NS);
    UtAssert_UINT32_EQ(CFE_PSP_GetTimerLow32Rollover(), 1000000000UL / INTERVAL_NS);
    CFE_PSP_ShutdownSimulithTime();
    UtAssert_INT32_EQ(FakeShutdownCalls, 1);
    UtAssert_True(tick_generation == 0 && CFE_PSP_GetSimulithTimeNs() == 0,
                  "shutdown clears distributed clock state");
    CFE_PSP_ShutdownSimulithTime();
    UtAssert_INT32_EQ(FakeShutdownCalls, 1);

    CFE_PSP_Get_Timebase(&upper, &lower);
    UtAssert_True(upper > 0 || lower > 0, "timebase falls back to the monotonic host clock");
    CFE_PSP_GetTime(&os_time);
    UtAssert_True(OS_TimeGetTotalNanoseconds(os_time) > 0, "OS time falls back to the host clock");

    ResetFakeClient();
    timebase_simulith_clock_Init(42);
    UtAssert_INT32_EQ(FakeInitCalls, 1);
    CFE_PSP_ShutdownSimulithTime();
}

static void Test_ParticipantDeliveryAccounting(void)
{
    static const uint32_t root_mid = 0x1881;
    static const uint32_t child_mid = 0x1882;
    static const uint32_t pipe_a = 11;
    static const uint32_t pipe_b = 12;
    static const uint32_t pipe_child = 13;
    static const uint32_t pipe_failed = 14;
    static const uint32_t pipe_polled = 15;
    int root_message_a;
    int root_message_b;
    int child_message;
    int failed_message;
    int polled_message;
    int root_token;
    int second_root_token;
    int child_token;
    int failed_token;
    uint64_t generation;

    ResetFakeClient();
    CFE_PSP_InitSimulithTime();
    CFE_PSP_EnableDeferredTickCompletion();
    UtAssert_INT32_EQ(CFE_PSP_StartSynchronizedTicks(), 0);

    generation = tick_generation;
    QueueTicks(1);
    UtAssert_True(WaitForGeneration(generation + 1),
                  "participant test received a synchronized tick");

    root_token = CFE_PSP_RegisterSimulithParticipant(21, root_mid);
    UtAssert_True(root_token > 0, "scheduled participant registered");
    UtAssert_INT32_EQ(CFE_PSP_ReserveSimulithMessageDelivery(
                          root_mid, pipe_a, &root_message_a),
                      root_token);
    second_root_token = CFE_PSP_ReserveSimulithMessageDelivery(
        root_mid, pipe_b, &root_message_b);
    UtAssert_True(second_root_token > root_token,
                  "a second successful destination gets its own participant");
    CFE_PSP_EndSimulithMessagePublication();

    /* Neither a matching MID on the wrong pipe nor the right pipe with a
     * different buffer may claim this delivery. */
    CFE_PSP_SimulithMessageReceived(root_mid, pipe_b, &root_message_a);
    CFE_PSP_SimulithMessageReceived(root_mid, pipe_a, &root_message_b);

    CFE_PSP_SimulithMessageReceived(root_mid, pipe_a, &root_message_a);
    CFE_PSP_SimulithTaskBeginReceive(pipe_polled, true);
    UtAssert_INT32_EQ(CFE_PSP_ReserveSimulithMessageDelivery(
                          child_mid, pipe_polled, &polled_message),
                      0);
    UtAssert_INT32_EQ(CFE_PSP_ReserveSimulithMessageDelivery(
                          child_mid, pipe_polled, &polled_message),
                      0);
    child_token = CFE_PSP_ReserveSimulithMessageDelivery(
        child_mid, pipe_child, &child_message);
    failed_token = CFE_PSP_ReserveSimulithMessageDelivery(
        child_mid, pipe_failed, &failed_message);
    UtAssert_True(child_token > second_root_token && failed_token > child_token,
                  "transitive command deliveries get distinct participants");
    CFE_PSP_EndSimulithMessageDelivery(child_token, true);
    CFE_PSP_EndSimulithMessageDelivery(failed_token, false);
    CFE_PSP_SimulithTaskBeginReceive(pipe_a, false);

    CFE_PSP_SimulithMessageReceived(root_mid, pipe_b, &root_message_b);
    CFE_PSP_SimulithTaskBeginReceive(pipe_b, false);
    CFE_PSP_SimulithMessageReceived(child_mid, pipe_child, &child_message);
    CFE_PSP_SimulithTaskBeginReceive(pipe_child, false);

    /* A scheduled publication with no successful destinations is explicitly
     * retired rather than leaving an unclaimable barrier participant. */
    UtAssert_True(CFE_PSP_RegisterSimulithParticipant(22, root_mid) > 0,
                  "second scheduled participant registered");
    CFE_PSP_EndSimulithMessagePublication();

    UtAssert_INT32_EQ(CFE_PSP_WaitForSimulithParticipants(), 0);
    UtAssert_INT32_EQ(CFE_PSP_CompleteSimulithTick(), 0);
    CFE_PSP_ShutdownSimulithTime();
}

static void Test_DeferredTickCompletionFailures(void)
{
    uint64_t generation;

    ResetFakeClient();
    CFE_PSP_InitSimulithTime();
    CFE_PSP_EnableDeferredTickCompletion();
    UtAssert_INT32_EQ(CFE_PSP_StartSynchronizedTicks(), 0);

    generation = tick_generation;
    QueueTicks(1);
    UtAssert_True(WaitForGeneration(generation + 1),
                  "deferred tick is published before SCH waits");
    UtAssert_INT32_EQ(CFE_PSP_WaitForPendingSimulithTick(), 0);
    UtAssert_INT32_EQ(CFE_PSP_WaitForSimulithParticipants(), 0);

    FakeCompletionStatus = -1;
    UtAssert_INT32_EQ(CFE_PSP_CompleteSimulithTick(), -1);
    UtAssert_INT32_EQ(FakeCompletionCalls, 1);
    UtAssert_INT32_EQ(CFE_PSP_CompleteSimulithTick(), -1);
    UtAssert_INT32_EQ(FakeCompletionCalls, 1);

    CFE_PSP_StopSynchronizedTicks();
    UtAssert_INT32_EQ(CFE_PSP_WaitForPendingSimulithTick(), -1);
    CFE_PSP_ShutdownSimulithTime();
}

static void Test_ParticipantAccountingFailures(void)
{
    uint64_t generation;
    int token;

    ResetFakeClient();
    CFE_PSP_InitSimulithTime();
    CFE_PSP_EnableDeferredTickCompletion();
    UtAssert_INT32_EQ(CFE_PSP_StartSynchronizedTicks(), 0);
    generation = tick_generation;
    QueueTicks(1);
    UtAssert_True(WaitForGeneration(generation + 1),
                  "accounting test received a synchronized tick");

    token = CFE_PSP_RegisterSimulithParticipant(31, 0x1883);
    UtAssert_True(token > 0, "first publication reserves a participant");
    UtAssert_INT32_EQ(CFE_PSP_RegisterSimulithParticipant(32, 0x1884), -1);
    UtAssert_INT32_EQ(CFE_PSP_WaitForSimulithParticipants(), -1);
    CFE_PSP_EndSimulithMessagePublication();
    CFE_PSP_CancelSimulithParticipant(token);
    CFE_PSP_CancelSimulithParticipant(0);
    UtAssert_INT32_EQ(CFE_PSP_CompleteSimulithTick(), 0);
    CFE_PSP_ShutdownSimulithTime();
}

static void Test_ServerStopWithTerminalAccounting(void)
{
    uint64_t generation;
    int message;
    int token;

    ResetFakeClient();
    CFE_PSP_InitSimulithTime();
    CFE_PSP_EnableDeferredTickCompletion();
    UtAssert_INT32_EQ(CFE_PSP_StartSynchronizedTicks(), 0);
    generation = tick_generation;
    QueueTicks(1);
    UtAssert_True(WaitForGeneration(generation + 1),
                  "terminal test received a synchronized tick");

    token = CFE_PSP_RegisterSimulithParticipant(41, 0x1885);
    UtAssert_True(token > 0, "terminal metric participant registered");
    UtAssert_INT32_EQ(CFE_PSP_ReserveSimulithMessageDelivery(0x1885, 17, &message), token);
    CFE_PSP_EndSimulithMessagePublication();
    CFE_PSP_SimulithMessageReceived(0x1885, 17, &message);
    CFE_PSP_SimulithTaskBeginReceive(17, false);
    UtAssert_INT32_EQ(CFE_PSP_WaitForSimulithParticipants(), 0);
    UtAssert_INT32_EQ(CFE_PSP_CompleteSimulithTick(), 0);

    QueueStop();
    UtAssert_True(WaitForStoppedClock(), "server STOP terminates the distribution thread");
    UtAssert_INT32_EQ(CFE_PSP_WaitForPendingSimulithTick(), -1);
    UtAssert_INT32_EQ(FakeCompletionCalls, 1);
    CFE_PSP_ShutdownSimulithTime();
}

static void Test_GroundOutputRateAndMidConsistency(void)
{
    UtAssert_True(CFE_PSP_AllowPeriodicGroundOutput(0x1881),
                  "first periodic ground output is allowed");
    (void)CFE_PSP_AllowPeriodicGroundOutput(0x1881);
    (void)CFE_PSP_AllowPeriodicGroundOutput(0x1882);
    UtAssert_INT32_EQ(CFE_PSP_WaitForSimulithParticipants(), -1);
}

static void ConsumeExceptionWakeup(void)
{
    sigset_t event_set;
    int      signal_number = 0;

    sigemptyset(&event_set);
    sigaddset(&event_set, CFE_PSP_EXCEPTION_EVENT_SIGNAL);
    UtAssert_INT32_EQ(sigwait(&event_set, &signal_number), 0);
    UtAssert_INT32_EQ(signal_number, CFE_PSP_EXCEPTION_EVENT_SIGNAL);
}

static void Test_ExceptionSignalIntegration(void)
{
    struct sigaction previous_term;
    siginfo_t        info = {0};
    sigset_t         event_set;
    sigset_t         previous_mask;

    memset(&ExceptionBuffer, 0, sizeof(ExceptionBuffer));
    ExceptionWriteCount = 0;
    ExceptionResetCount = 0;
    ReturnExceptionBuffer = true;
    CFE_PSP_IdleTaskState.ThreadID = pthread_self();
    sigemptyset(&event_set);
    sigaddset(&event_set, CFE_PSP_EXCEPTION_EVENT_SIGNAL);
    pthread_sigmask(SIG_BLOCK, &event_set, &previous_mask);

    info.si_signo = SIGINT;
    CFE_PSP_ExceptionSigHandler(SIGINT, &info, NULL);
    ConsumeExceptionWakeup();
    UtAssert_INT32_EQ(ExceptionWriteCount, 1);
    UtAssert_INT32_EQ(ExceptionBuffer.context_info.si.si_signo, SIGINT);
    UtAssert_True(ExceptionBuffer.context_size > 0, "exception context includes a backtrace");

    ReturnExceptionBuffer = false;
    CFE_PSP_ExceptionSigHandler(SIGINT, &info, NULL);
    ConsumeExceptionWakeup();
    UtAssert_INT32_EQ(ExceptionWriteCount, 1);

    sigaction(SIGTERM, NULL, &previous_term);
    ReturnExceptionBuffer = true;
    CFE_PSP_AttachExceptions();
    UtAssert_INT32_EQ(ExceptionResetCount, 1);
    raise(SIGTERM);
    ConsumeExceptionWakeup();
    UtAssert_INT32_EQ(ExceptionWriteCount, 2);
    sigaction(SIGTERM, &previous_term, NULL);

    CFE_PSP_SetDefaultExceptionEnvironment();
    pthread_sigmask(SIG_SETMASK, &previous_mask, NULL);
}

static void Test_ExceptionSummaries(void)
{
    static const int fpe_codes[] = {FPE_INTDIV, FPE_INTOVF, FPE_FLTDIV, FPE_FLTOVF, FPE_FLTUND,
                                    FPE_FLTRES, FPE_FLTINV, FPE_FLTSUB, 0x7fff};
    CFE_PSP_Exception_LogData_t buffer = {0};
    char                        reason[128];
    size_t                      i;

    buffer.context_info.si.si_signo = SIGFPE;
    buffer.context_info.si.si_addr  = (void *)(uintptr_t)0x1234;
    for (i = 0; i < sizeof(fpe_codes) / sizeof(fpe_codes[0]); ++i)
    {
        buffer.context_info.si.si_code = fpe_codes[i];
        memset(reason, 0, sizeof(reason));
        UtAssert_INT32_EQ(CFE_PSP_ExceptionGetSummary_Impl(&buffer, reason, sizeof(reason)), CFE_PSP_SUCCESS);
        UtAssert_True(reason[0] != '\0', "floating-point exception has a reason");
    }

    buffer.context_info.si.si_signo = SIGINT;
    CFE_PSP_ExceptionGetSummary_Impl(&buffer, reason, sizeof(reason));
    UtAssert_StrCmp(reason, "Caught SIGINT", "SIGINT summary is stable");
    buffer.context_info.si.si_signo = SIGTERM;
    CFE_PSP_ExceptionGetSummary_Impl(&buffer, reason, sizeof(reason));
    UtAssert_True(strstr(reason, "15") != NULL, "generic signal summary includes its number");
}

/* Runs CFE_PSP_WaitForSimulithParticipants() on its own thread, so a participant that was wrongly left outstanding fails
 * the test and does not hang it. */
typedef struct
{
    pthread_t       thread;
    volatile bool   done;
    volatile int    result;
} ParticipantWait_t;

static void *ParticipantWaitThread(void *arg)
{
    ParticipantWait_t *wait = arg;
    wait->result = CFE_PSP_WaitForSimulithParticipants();
    wait->done = true;
    return NULL;
}

static void StartParticipantWait(ParticipantWait_t *wait)
{
    wait->done = false;
    wait->result = -2;
    UtAssert_INT32_EQ(pthread_create(&wait->thread, NULL, ParticipantWaitThread, wait), 0);
}

static bool ParticipantWaitFinishes(ParticipantWait_t *wait, unsigned int milliseconds)
{
    unsigned int elapsed;

    for (elapsed = 0; elapsed < milliseconds && !wait->done; ++elapsed)
    {
        usleep(1000);
    }
    return wait->done;
}

static void JoinParticipantWait(ParticipantWait_t *wait)
{
    if (!ParticipantWaitFinishes(wait, 5000))
    {
        UtAssert_Failed("an outstanding participant held the tick open");
        QueueStop();
    }
    pthread_join(wait->thread, NULL);
}

static void *TaskExitWithoutParticipant(void *arg)
{
    (void)arg;
    CFE_PSP_SimulithTaskExit();
    return NULL;
}

static void Test_TaskExitRetiresTheExitingTasksPipe(void)
{
    static const uint32_t mid = 0x1891;
    static const uint32_t other_mid = 0x1892;
    static const uint32_t pipe_exit = 201;
    static const uint32_t pipe_other = 202;
    static const uint32_t pipe_new = 203;
    int exit_message;
    int other_message;
    int new_message;
    int blocked_message;
    int exit_token;
    int other_token;
    int new_token;
    uint64_t generation;
    ParticipantWait_t wait;
    pthread_t thread;

    ResetFakeClient();
    CFE_PSP_InitSimulithTime();
    CFE_PSP_EnableDeferredTickCompletion();
    UtAssert_INT32_EQ(CFE_PSP_StartSynchronizedTicks(), 0);
    generation = tick_generation;
    QueueTicks(1);
    UtAssert_True(WaitForGeneration(generation + 1), "task exit test received a synchronized tick");

    /* A task that never received anything has nothing to retire, and neither has a thread that is not a task. */
    CFE_PSP_SimulithTaskExit();
    UtAssert_INT32_EQ(pthread_create(&thread, NULL, TaskExitWithoutParticipant, NULL), 0);
    pthread_join(thread, NULL);

    exit_token = CFE_PSP_RegisterSimulithParticipant(51, mid);
    UtAssert_INT32_EQ(CFE_PSP_ReserveSimulithMessageDelivery(mid, pipe_exit, &exit_message), exit_token);
    other_token = CFE_PSP_ReserveSimulithMessageDelivery(mid, pipe_other, &other_message);
    UtAssert_True(other_token > exit_token, "a second destination has its own participant");
    CFE_PSP_EndSimulithMessagePublication();

    /* The task takes its activation and then exits without ever receiving again. Its delivery must not hold the
     * tick, but the delivery for another task's pipe must. */
    CFE_PSP_SimulithMessageReceived(mid, pipe_exit, &exit_message);
    CFE_PSP_SimulithTaskExit();
    StartParticipantWait(&wait);
    UtAssert_True(!ParticipantWaitFinishes(&wait, 150), "another pipe's delivery still holds the tick open");
    CFE_PSP_SimulithMessageReceived(mid, pipe_other, &other_message);
    CFE_PSP_SimulithTaskBeginReceive(pipe_other, false);
    JoinParticipantWait(&wait);
    UtAssert_INT32_EQ(wait.result, 0);

    /* The exited task's pipe is retired, so work routed to it later in the tick is not tracked. */
    new_token = CFE_PSP_RegisterSimulithParticipant(52, other_mid);
    UtAssert_True(new_token > 0, "a later publication registered");
    UtAssert_INT32_EQ(CFE_PSP_ReserveSimulithMessageDelivery(other_mid, pipe_exit, &blocked_message), 0);
    UtAssert_INT32_EQ(CFE_PSP_ReserveSimulithMessageDelivery(other_mid, pipe_new, &new_message), new_token);
    CFE_PSP_EndSimulithMessagePublication();
    CFE_PSP_SimulithMessageReceived(other_mid, pipe_new, &new_message);
    CFE_PSP_SimulithTaskBeginReceive(pipe_new, false);

    /* A second exit has no pipe left to retire. */
    CFE_PSP_SimulithTaskExit();
    UtAssert_INT32_EQ(CFE_PSP_WaitForSimulithParticipants(), 0);
    UtAssert_INT32_EQ(CFE_PSP_CompleteSimulithTick(), 0);
    CFE_PSP_ShutdownSimulithTime();
}

static void RetirePipe(uint32_t schedule_entry, uint32_t mid, uint32_t pipe_id, int *message)
{
    int token = CFE_PSP_RegisterSimulithParticipant(schedule_entry, mid);

    UtAssert_True(token > 0, "an exiting task's activation registered");
    UtAssert_INT32_EQ(CFE_PSP_ReserveSimulithMessageDelivery(mid, pipe_id, message), token);
    CFE_PSP_EndSimulithMessagePublication();
    CFE_PSP_SimulithMessageReceived(mid, pipe_id, message);
    CFE_PSP_SimulithTaskExit();
}

static void Test_RetiredPipesAreTheMostRecentExits(void)
{
    enum { Retired = 64, PerTick = 32 };
    static const uint32_t mid = 0x1893;
    static const uint32_t first_pipe = 400;
    static const uint32_t evicted_pipe = 201;
    static int messages[Retired];
    int evicted_message;
    int blocked_message;
    uint64_t generation;
    unsigned int index;
    unsigned int tick;

    ResetFakeClient();
    CFE_PSP_InitSimulithTime();
    CFE_PSP_EnableDeferredTickCompletion();
    UtAssert_INT32_EQ(CFE_PSP_StartSynchronizedTicks(), 0);

    /* Retiring as many pipes as the list holds replaces every earlier entry, including the one retired above. */
    for (tick = 0; tick < Retired / PerTick; ++tick)
    {
        generation = tick_generation;
        QueueTicks(1);
        UtAssert_True(WaitForGeneration(generation + 1), "retirement test received a synchronized tick");
        for (index = 0; index < PerTick; ++index)
        {
            unsigned int n = tick * PerTick + index;
            RetirePipe(60 + n, mid, first_pipe + n, &messages[n]);
        }
        UtAssert_INT32_EQ(CFE_PSP_WaitForSimulithParticipants(), 0);
        UtAssert_INT32_EQ(CFE_PSP_CompleteSimulithTick(), 0);
    }

    generation = tick_generation;
    QueueTicks(1);
    UtAssert_True(WaitForGeneration(generation + 1), "reservation test received a synchronized tick");
    UtAssert_True(CFE_PSP_RegisterSimulithParticipant(70, mid) > 0, "a publication registered");
    UtAssert_INT32_EQ(CFE_PSP_ReserveSimulithMessageDelivery(mid, first_pipe, &blocked_message), 0);
    UtAssert_INT32_EQ(CFE_PSP_ReserveSimulithMessageDelivery(mid, first_pipe + Retired - 1, &blocked_message), 0);
    UtAssert_True(CFE_PSP_ReserveSimulithMessageDelivery(mid, evicted_pipe, &evicted_message) > 0,
                  "a pipe older than the list is tracked again");
    CFE_PSP_EndSimulithMessagePublication();
    CFE_PSP_SimulithMessageReceived(mid, evicted_pipe, &evicted_message);
    CFE_PSP_SimulithTaskBeginReceive(evicted_pipe, false);
    UtAssert_INT32_EQ(CFE_PSP_WaitForSimulithParticipants(), 0);
    UtAssert_INT32_EQ(CFE_PSP_CompleteSimulithTick(), 0);
    CFE_PSP_ShutdownSimulithTime();
}

static void *CancelledTickWaiter(void *arg)
{
    (void)arg;
    CFE_PSP_WaitForSimulithTick(1000000);
    return NULL;
}

typedef struct
{
    volatile bool done;
} MutexProbe_t;

static void *TickMutexProbe(void *arg)
{
    MutexProbe_t *probe = arg;

    /* Takes the tick mutex, as every reader of the simulated clock does. */
    CFE_PSP_SimulithTaskBeginReceive(999, true);
    probe->done = true;
    return NULL;
}

static void Test_CancelledTickWaiterReleasesTheTickMutex(void)
{
    pthread_t waiter;
    pthread_t prober;
    MutexProbe_t probe = {.done = false};
    unsigned int elapsed;

    ResetFakeClient();
    CFE_PSP_InitSimulithTime();
    UtAssert_INT32_EQ(CFE_PSP_StartSynchronizedTicks(), 0);

    UtAssert_INT32_EQ(pthread_create(&waiter, NULL, CancelledTickWaiter, NULL), 0);
    usleep(100000);
    /* OSAL stops an application's tasks this way, and the task is blocked inside the wait. */
    UtAssert_INT32_EQ(pthread_cancel(waiter), 0);
    UtAssert_INT32_EQ(pthread_join(waiter, NULL), 0);

    UtAssert_INT32_EQ(pthread_create(&prober, NULL, TickMutexProbe, &probe), 0);
    for (elapsed = 0; elapsed < 5000 && !probe.done; ++elapsed)
    {
        usleep(1000);
    }
    UtAssert_True(probe.done, "the cancelled task did not leave the tick mutex locked");
    if (!probe.done)
    {
        /* The mutex stays locked, so every later test would block on it. */
        UtAssert_Abort("the tick mutex is still locked by the cancelled task");
    }
    pthread_join(prober, NULL);
    CFE_PSP_ShutdownSimulithTime();
}

static void Test_StallReportNamesTheOutstandingParticipants(void)
{
    static const uint32_t mid = 0x1894;
    static const uint32_t pipe_id = 211;
    int message;
    int token;
    uint64_t generation;
    ParticipantWait_t wait;
    FILE *capture;
    int saved_stdout;
    char output[2048];
    size_t length;

    ResetFakeClient();
    CFE_PSP_InitSimulithTime();
    CFE_PSP_EnableDeferredTickCompletion();
    UtAssert_INT32_EQ(CFE_PSP_StartSynchronizedTicks(), 0);
    generation = tick_generation;
    QueueTicks(1);
    UtAssert_True(WaitForGeneration(generation + 1), "stall test received a synchronized tick");

    token = CFE_PSP_RegisterSimulithParticipant(81, mid);
    UtAssert_INT32_EQ(CFE_PSP_ReserveSimulithMessageDelivery(mid, pipe_id, &message), token);
    CFE_PSP_EndSimulithMessagePublication();

    capture = tmpfile();
    UtAssert_True(capture != NULL, "a file to capture the stall report");
    fflush(stdout);
    saved_stdout = dup(STDOUT_FILENO);
    UtAssert_True(dup2(fileno(capture), STDOUT_FILENO) >= 0, "stdout redirected to capture the report");

    /* The report is made by elapsed time, once a second, whether or not the wait itself timed out. The first second
     * only starts the clock, so the unclaimed delivery is named on the second. */
    StartParticipantWait(&wait);
    sleep(2);
    usleep(400000);
    CFE_PSP_SimulithMessageReceived(mid, pipe_id, &message);
    usleep(1300000);
    CFE_PSP_SimulithTaskBeginReceive(pipe_id, false);
    JoinParticipantWait(&wait);

    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    rewind(capture);
    length = fread(output, 1, sizeof(output) - 1, capture);
    output[length] = '\0';
    fclose(capture);

    UtAssert_INT32_EQ(wait.result, 0);
    UtAssert_True(strstr(output, "stalled; waiting for schedule_entry[81]/mid[0x1894]/pipe[211]/unclaimed") != NULL,
                  "the stall report names the unclaimed delivery");
    UtAssert_True(strstr(output, "schedule_entry[81]/mid[0x1894]/pipe[211]/running") != NULL,
                  "the stall report names the running delivery");
    UtAssert_INT32_EQ(CFE_PSP_CompleteSimulithTick(), 0);
    CFE_PSP_ShutdownSimulithTime();
}

void UtTest_Setup(void)
{
    if (getenv("SHIRE_PSP_COVERAGE_GROUND_OUTPUT_ONLY") != NULL)
    {
        UtTest_Add(Test_GroundOutputRateAndMidConsistency, NULL, NULL,
                   "SHIRE PSP ground output rate and MID consistency");
        return;
    }
    UtTest_Add(Test_TimebaseInitializationFailures, NULL, NULL, "SHIRE PSP timebase initialization failures");
    UtTest_Add(Test_TimebaseTickDistribution, NULL, NULL, "SHIRE PSP tick distribution runtime");
    UtTest_Add(Test_ParticipantDeliveryAccounting, NULL, NULL,
               "SHIRE PSP exact message delivery accounting");
    UtTest_Add(Test_DeferredTickCompletionFailures, NULL, NULL,
               "SHIRE PSP deferred tick completion failures");
    UtTest_Add(Test_ParticipantAccountingFailures, NULL, NULL,
               "SHIRE PSP participant accounting failures");
    UtTest_Add(Test_ServerStopWithTerminalAccounting, NULL, NULL,
               "SHIRE PSP server STOP and terminal accounting");
    UtTest_Add(Test_GroundOutputRateAndMidConsistency, NULL, NULL,
               "SHIRE PSP ground output rate and MID consistency");
    UtTest_Add(Test_TaskExitRetiresTheExitingTasksPipe, NULL, NULL, "SHIRE PSP task exit retires its pipe");
    UtTest_Add(Test_RetiredPipesAreTheMostRecentExits, NULL, NULL, "SHIRE PSP retired pipes are the latest exits");
    UtTest_Add(Test_CancelledTickWaiterReleasesTheTickMutex, NULL, NULL,
               "SHIRE PSP cancelled tick waiter releases the tick mutex");
    UtTest_Add(Test_StallReportNamesTheOutstandingParticipants, NULL, NULL,
               "SHIRE PSP stall report names outstanding participants");
    UtTest_Add(Test_ExceptionSignalIntegration, NULL, NULL, "SHIRE PSP exception signal integration");
    UtTest_Add(Test_ExceptionSummaries, NULL, NULL, "SHIRE PSP exception summaries");
}
