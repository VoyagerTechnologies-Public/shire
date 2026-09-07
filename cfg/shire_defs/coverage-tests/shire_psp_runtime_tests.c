/* Runtime integration coverage for SHIRE's Simulith-backed PSP adapters. */

#include "cfe_psp.h"
#include "cfe_psp_config.h"
#include "cfe_psp_exceptionstorage_api.h"
#include "cfe_psp_exceptionstorage_types.h"
#include "cfe_psp_timebase.h"

#include "utassert.h"
#include "uttest.h"

#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

void  timebase_simulith_clock_Init(uint32 PspModuleId);
void  CFE_PSP_ExceptionSigHandler(int signo, siginfo_t *info, void *context);
void  CFE_PSP_AttachExceptions(void);
void  CFE_PSP_SetDefaultExceptionEnvironment(void);
int32 CFE_PSP_ExceptionGetSummary_Impl(const CFE_PSP_Exception_LogData_t *buffer, char *reason, uint32 size);

extern volatile uint64_t tick_generation;

CFE_PSP_IdleTaskState_t CFE_PSP_IdleTaskState;

static pthread_mutex_t FakeTickMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  FakeTickCondition = PTHREAD_COND_INITIALIZER;
static int             FakeInitStatus;
static int             FakeHandshakeStatus;
static bool            FakeClientStopped;
static uint64_t        FakeNextTick;
static unsigned int    FakePendingTicks;
static unsigned int    FakeInitCalls;
static unsigned int    FakeHandshakeCalls;
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

int simulith_client_wait_for_tick(uint64_t *tick_time_ns)
{
    pthread_mutex_lock(&FakeTickMutex);
    while (FakePendingTicks == 0 && !FakeClientStopped)
    {
        pthread_cond_wait(&FakeTickCondition, &FakeTickMutex);
    }
    if (FakeClientStopped)
    {
        pthread_mutex_unlock(&FakeTickMutex);
        return -1;
    }
    *tick_time_ns = FakeNextTick;
    FakeNextTick += INTERVAL_NS;
    FakePendingTicks--;
    pthread_mutex_unlock(&FakeTickMutex);
    return 0;
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
    FakeHandshakeStatus = 0;
    FakeClientStopped   = false;
    FakeNextTick        = 1000000000ULL;
    FakePendingTicks    = 0;
    FakeInitCalls       = 0;
    FakeHandshakeCalls  = 0;
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

static bool WaitForGeneration(uint64_t expected)
{
    unsigned int retries;

    for (retries = 0; retries < 1000 && tick_generation < expected; ++retries)
    {
        usleep(1000);
    }
    return tick_generation >= expected;
}

typedef struct
{
    volatile bool started;
    unsigned int  result;
} TickWaitContext_t;

static void *WaitForTwoTicks(void *arg)
{
    TickWaitContext_t *ctx = arg;

    ctx->started = true;
    ctx->result  = CFE_PSP_WaitForSimulithTick(2);
    return NULL;
}

static void Test_TimebaseInitializationFailures(void)
{
    ResetFakeClient();
    FakeInitStatus = -11;
    CFE_PSP_InitSimulithTime();
    UtAssert_INT32_EQ(FakeInitCalls, 1);
    UtAssert_INT32_EQ(FakeHandshakeCalls, 0);

    ResetFakeClient();
    FakeHandshakeStatus = -12;
    CFE_PSP_InitSimulithTime();
    UtAssert_INT32_EQ(FakeHandshakeCalls, 1);
    UtAssert_INT32_EQ(FakeShutdownCalls, 1);
}

static void Test_TimebaseTickDistribution(void)
{
    TickWaitContext_t wait_ctx = {0};
    struct timespec   timespec;
    OS_time_t         os_time;
    uint32            upper;
    uint32            lower;
    pthread_t         waiter;
    uint64_t          generation;

    ResetFakeClient();
    CFE_PSP_InitSimulithTime();
    UtAssert_INT32_EQ(FakeInitCalls, 1);
    UtAssert_INT32_EQ(FakeHandshakeCalls, 1);
    CFE_PSP_InitSimulithTime();
    UtAssert_INT32_EQ(FakeInitCalls, 1);

    QueueTicks(1);
    UtAssert_True(WaitForGeneration(1), "PSP distribution thread published a tick");
    UtAssert_True(CFE_PSP_GetSimulithTimeNs() == 1000000000ULL, "latest Simulith time is retained");
    CFE_PSP_GetSimulithTimespec(&timespec);
    UtAssert_True(timespec.tv_sec == 1 && timespec.tv_nsec == 0, "Simulith time converts to timespec");
    CFE_PSP_Get_Timebase(&upper, &lower);
    UtAssert_True(upper == 1 && lower == 0, "legacy timebase uses Simulith time");
    CFE_PSP_GetTime(&os_time);
    UtAssert_True(OS_TimeGetTotalNanoseconds(os_time) == 1000000000LL, "OS time uses Simulith time");

    generation = tick_generation;
    UtAssert_INT32_EQ(pthread_create(&waiter, NULL, WaitForTwoTicks, &wait_ctx), 0);
    while (!wait_ctx.started)
    {
        sched_yield();
    }
    QueueTicks(2);
    UtAssert_True(WaitForGeneration(generation + 2), "two ticks were distributed");
    UtAssert_INT32_EQ(pthread_join(waiter, NULL), 0);
    UtAssert_INT32_EQ(wait_ctx.result, 2);

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

void UtTest_Setup(void)
{
    UtTest_Add(Test_TimebaseInitializationFailures, NULL, NULL, "SHIRE PSP timebase initialization failures");
    UtTest_Add(Test_TimebaseTickDistribution, NULL, NULL, "SHIRE PSP tick distribution runtime");
    UtTest_Add(Test_ExceptionSignalIntegration, NULL, NULL, "SHIRE PSP exception signal integration");
    UtTest_Add(Test_ExceptionSummaries, NULL, NULL, "SHIRE PSP exception summaries");
}
