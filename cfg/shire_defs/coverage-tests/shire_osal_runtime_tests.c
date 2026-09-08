/* Runtime integration coverage for SHIRE's pthread-backed OSAL adapters. */

#include "os-posix.h"
#include "os-impl-binsem.h"
#include "os-shared-binsem.h"
#include "os-shared-idmap.h"

#include "utassert.h"
#include "uttest.h"

#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

int32 OS_API_Impl_Init(osal_objtype_t idtype);
void  OS_IdleLoop_Impl(void);
void  OS_ApplicationShutdown_Impl(void);

typedef struct
{
    OS_object_token_t token;
    volatile bool     started;
    int32             status;
    uint32            timeout_ms;
} BinSemWaitContext_t;

static int32 AdapterInitStatus;
static int   AdapterInitCalls[OS_OBJECT_TYPE_OS_CONDVAR + 1];

void OS_DebugPrintf(uint32 level, const char *function, uint32 line, const char *format, ...)
{
    (void)level;
    (void)function;
    (void)line;
    (void)format;
}

/* Adjacent adapter seams used by os-impl-common.c. */
int32 OS_Posix_TableMutex_Init(osal_objtype_t idtype)
{
    if (idtype <= OS_OBJECT_TYPE_OS_CONDVAR)
    {
        AdapterInitCalls[idtype]++;
    }
    return AdapterInitStatus;
}

#define DEFINE_ADAPTER_INIT(name, type) \
    int32 name(void)                    \
    {                                   \
        AdapterInitCalls[type]++;       \
        return AdapterInitStatus;       \
    }

DEFINE_ADAPTER_INIT(OS_Posix_TaskAPI_Impl_Init, OS_OBJECT_TYPE_OS_TASK)
DEFINE_ADAPTER_INIT(OS_Posix_QueueAPI_Impl_Init, OS_OBJECT_TYPE_OS_QUEUE)
DEFINE_ADAPTER_INIT(OS_Posix_CountSemAPI_Impl_Init, OS_OBJECT_TYPE_OS_COUNTSEM)
DEFINE_ADAPTER_INIT(OS_Posix_MutexAPI_Impl_Init, OS_OBJECT_TYPE_OS_MUTEX)
DEFINE_ADAPTER_INIT(OS_Posix_ModuleAPI_Impl_Init, OS_OBJECT_TYPE_OS_MODULE)
DEFINE_ADAPTER_INIT(OS_Posix_TimeBaseAPI_Impl_Init, OS_OBJECT_TYPE_OS_TIMEBASE)
DEFINE_ADAPTER_INIT(OS_Posix_StreamAPI_Impl_Init, OS_OBJECT_TYPE_OS_STREAM)
DEFINE_ADAPTER_INIT(OS_Posix_DirAPI_Impl_Init, OS_OBJECT_TYPE_OS_DIR)
DEFINE_ADAPTER_INIT(OS_Posix_FileSysAPI_Impl_Init, OS_OBJECT_TYPE_OS_FILESYS)
DEFINE_ADAPTER_INIT(OS_Posix_CondVarAPI_Impl_Init, OS_OBJECT_TYPE_OS_CONDVAR)

void CFE_PSP_GetSimulithTimespec(struct timespec *ts)
{
    clock_gettime(CLOCK_REALTIME, ts);
}

static OS_object_token_t MakeToken(osal_index_t index)
{
    OS_object_token_t token = {0};

    token.obj_type = OS_OBJECT_TYPE_OS_BINSEM;
    token.obj_idx  = index;
    token.obj_id   = OS_ObjectIdFromInteger((OS_OBJECT_TYPE_OS_BINSEM << OS_OBJECT_TYPE_SHIFT) | index);
    return token;
}

static void Test_Setup(void)
{
    memset(AdapterInitCalls, 0, sizeof(AdapterInitCalls));
    AdapterInitStatus = OS_SUCCESS;
    OS_Posix_BinSemAPI_Impl_Init();
}

static void *TimedWaitThread(void *arg)
{
    BinSemWaitContext_t *ctx = arg;

    ctx->started = true;
    ctx->status  = OS_BinSemTimedWait_Impl(&ctx->token, ctx->timeout_ms);
    return NULL;
}

static void WaitUntilStarted(BinSemWaitContext_t *ctx)
{
    unsigned int spins;

    for (spins = 0; !ctx->started && spins < 10000; ++spins)
    {
        sched_yield();
    }
    UtAssert_True(ctx->started, "worker reached semaphore wait");
    usleep(10000);
}

static void Test_BinarySemaphoreLifecycle(void)
{
    OS_object_token_t token = MakeToken(0);
    OS_bin_sem_prop_t info = {0};

    UtAssert_INT32_EQ(OS_BinSemCreate_Impl(&token, 7, 0), OS_SUCCESS);
    UtAssert_INT32_EQ(OS_BinSemGetInfo_Impl(&token, &info), OS_SUCCESS);
    UtAssert_INT32_EQ(info.value, 1);
    UtAssert_INT32_EQ(OS_BinSemTake_Impl(&token), OS_SUCCESS);
    UtAssert_INT32_EQ(OS_BinSemGetInfo_Impl(&token, &info), OS_SUCCESS);
    UtAssert_INT32_EQ(info.value, 0);
    UtAssert_INT32_EQ(OS_BinSemTimedWait_Impl(&token, 15), OS_SEM_TIMEOUT);
    UtAssert_INT32_EQ(OS_BinSemGive_Impl(&token), OS_SUCCESS);
    UtAssert_INT32_EQ(OS_BinSemGive_Impl(&token), OS_SUCCESS);
    UtAssert_INT32_EQ(OS_BinSemTake_Impl(&token), OS_SUCCESS);
    UtAssert_INT32_EQ(OS_BinSemDelete_Impl(&token), OS_SUCCESS);
}

static void Test_BinarySemaphoreGiveAndFlush(void)
{
    OS_object_token_t   token = MakeToken(1);
    BinSemWaitContext_t ctx = {.token = token, .timeout_ms = 500, .status = OS_ERROR};
    OS_bin_sem_prop_t   info = {0};
    pthread_t           thread;

    UtAssert_INT32_EQ(OS_BinSemCreate_Impl(&token, 0, 0), OS_SUCCESS);
    UtAssert_INT32_EQ(pthread_create(&thread, NULL, TimedWaitThread, &ctx), 0);
    WaitUntilStarted(&ctx);
    UtAssert_INT32_EQ(OS_BinSemGive_Impl(&token), OS_SUCCESS);
    UtAssert_INT32_EQ(pthread_join(thread, NULL), 0);
    UtAssert_INT32_EQ(ctx.status, OS_SUCCESS);

    ctx.started = false;
    ctx.status  = OS_ERROR;
    UtAssert_INT32_EQ(pthread_create(&thread, NULL, TimedWaitThread, &ctx), 0);
    WaitUntilStarted(&ctx);
    UtAssert_INT32_EQ(OS_BinSemFlush_Impl(&token), OS_SUCCESS);
    UtAssert_INT32_EQ(pthread_join(thread, NULL), 0);
    UtAssert_INT32_EQ(ctx.status, OS_SUCCESS);
    UtAssert_INT32_EQ(OS_BinSemGetInfo_Impl(&token, &info), OS_SUCCESS);
    UtAssert_INT32_EQ(info.value, 0);
    UtAssert_INT32_EQ(OS_BinSemDelete_Impl(&token), OS_SUCCESS);
}

static void Test_CommonInitializationDispatch(void)
{
    const osal_objtype_t types[] = {
        OS_OBJECT_TYPE_OS_TASK,     OS_OBJECT_TYPE_OS_QUEUE,   OS_OBJECT_TYPE_OS_BINSEM,
        OS_OBJECT_TYPE_OS_COUNTSEM, OS_OBJECT_TYPE_OS_MUTEX,   OS_OBJECT_TYPE_OS_MODULE,
        OS_OBJECT_TYPE_OS_TIMEBASE, OS_OBJECT_TYPE_OS_STREAM,  OS_OBJECT_TYPE_OS_DIR,
        OS_OBJECT_TYPE_OS_FILESYS,  OS_OBJECT_TYPE_OS_CONDVAR, OS_OBJECT_TYPE_UNDEFINED};
    size_t i;

    for (i = 0; i < sizeof(types) / sizeof(types[0]); ++i)
    {
        UtAssert_INT32_EQ(OS_API_Impl_Init(types[i]), OS_SUCCESS);
    }
    UtAssert_True(AdapterInitCalls[OS_OBJECT_TYPE_OS_TASK] >= 2,
                  "table mutex and task adapter were initialized");

    AdapterInitStatus = OS_ERROR;
    UtAssert_INT32_EQ(OS_API_Impl_Init(OS_OBJECT_TYPE_OS_TASK), OS_ERROR);
    UtAssert_INT32_EQ(AdapterInitCalls[OS_OBJECT_TYPE_OS_TASK], 3);
}

static volatile sig_atomic_t IdleThreadWoke;

static void NoopSignalHandler(int signo)
{
    (void)signo;
}

static void *IdleThread(void *arg)
{
    (void)arg;
    OS_IdleLoop_Impl();
    IdleThreadWoke = 1;
    return NULL;
}

static void Test_TimeAndShutdownAdapters(void)
{
    struct timespec before;
    struct timespec delayed;
    struct sigaction action = {0};
    sigset_t         blocked;
    sigset_t         previous;
    pthread_t        thread;

    clock_gettime(CLOCK_REALTIME, &before);
    OS_Posix_CompAbsDelayTime(2501, &delayed);
    UtAssert_True(delayed.tv_sec > before.tv_sec || delayed.tv_nsec > before.tv_nsec,
                  "absolute deadline advances");
    UtAssert_True(delayed.tv_nsec >= 0 && delayed.tv_nsec < 1000000000L,
                  "absolute deadline is normalized");

    action.sa_handler = NoopSignalHandler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGHUP, &action, NULL);
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    POSIX_GlobalVars.NormalSigMask = previous;
    sigdelset(&POSIX_GlobalVars.NormalSigMask, SIGHUP);
    IdleThreadWoke = 0;
    UtAssert_INT32_EQ(pthread_create(&thread, NULL, IdleThread, NULL), 0);
    usleep(10000);
    OS_ApplicationShutdown_Impl();
    UtAssert_INT32_EQ(pthread_join(thread, NULL), 0);
    UtAssert_INT32_EQ(IdleThreadWoke, 1);
    pthread_sigmask(SIG_SETMASK, &previous, NULL);
}

void UtTest_Setup(void)
{
    UtTest_Add(Test_BinarySemaphoreLifecycle, Test_Setup, NULL, "SHIRE binary semaphore lifecycle");
    UtTest_Add(Test_BinarySemaphoreGiveAndFlush, Test_Setup, NULL, "SHIRE binary semaphore concurrency");
    UtTest_Add(Test_CommonInitializationDispatch, Test_Setup, NULL, "SHIRE common adapter dispatch");
    UtTest_Add(Test_TimeAndShutdownAdapters, Test_Setup, NULL, "SHIRE time and shutdown adapters");
}
