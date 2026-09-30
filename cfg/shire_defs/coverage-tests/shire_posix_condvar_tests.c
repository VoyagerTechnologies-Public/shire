/* Deterministic failure coverage for the POSIX condition-variable adapter. */
#include "os-posix.h"
#include "os-impl-condvar.h"
#include "os-shared-condvar.h"
#include "os-shared-idmap.h"
#include "utassert.h"
#include "uttest.h"

#include <errno.h>
#include <string.h>

static int MutexInitStatus;
static int CondInitStatus;
static int CondDestroyStatus;
static int MutexDestroyStatus;
static int LockStatus;
static int UnlockStatus;
static int SignalStatus;
static int BroadcastStatus;
static int WaitStatus;
static int TimedWaitStatus;
static unsigned int MutexDestroyCalls;

void OS_DebugPrintf(uint32 level, const char *function, uint32 line, const char *format, ...)
{
    (void)level;
    (void)function;
    (void)line;
    (void)format;
}

int ShireTest_MutexInit(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    (void)mutex;
    (void)attr;
    return MutexInitStatus;
}

int ShireTest_CondInit(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    (void)cond;
    (void)attr;
    return CondInitStatus;
}

int ShireTest_CondDestroy(pthread_cond_t *cond)
{
    (void)cond;
    return CondDestroyStatus;
}

int ShireTest_MutexDestroy(pthread_mutex_t *mutex)
{
    (void)mutex;
    MutexDestroyCalls++;
    return MutexDestroyStatus;
}

int ShireTest_MutexLock(pthread_mutex_t *mutex)
{
    (void)mutex;
    return LockStatus;
}

int ShireTest_MutexUnlock(pthread_mutex_t *mutex)
{
    (void)mutex;
    return UnlockStatus;
}

int ShireTest_CondSignal(pthread_cond_t *cond)
{
    (void)cond;
    return SignalStatus;
}

int ShireTest_CondBroadcast(pthread_cond_t *cond)
{
    (void)cond;
    return BroadcastStatus;
}

int ShireTest_CondWait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    (void)cond;
    (void)mutex;
    return WaitStatus;
}

int ShireTest_CondTimedWait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                            const struct timespec *timeout)
{
    (void)cond;
    (void)mutex;
    UtAssert_NOT_NULL(timeout);
    return TimedWaitStatus;
}

static OS_object_token_t CondToken(void)
{
    OS_object_token_t token = {0};
    token.obj_type = OS_OBJECT_TYPE_OS_CONDVAR;
    token.obj_idx = 0;
    token.obj_id = OS_ObjectIdFromInteger(OS_OBJECT_TYPE_OS_CONDVAR << OS_OBJECT_TYPE_SHIFT);
    return token;
}

static void ResetCondvar(void)
{
    MutexInitStatus = CondInitStatus = CondDestroyStatus = MutexDestroyStatus = 0;
    LockStatus = UnlockStatus = SignalStatus = BroadcastStatus = 0;
    WaitStatus = TimedWaitStatus = 0;
    MutexDestroyCalls = 0;
    UtAssert_INT32_EQ(OS_Posix_CondVarAPI_Impl_Init(), OS_SUCCESS);
}

static void Test_CreateAndDeleteFailures(void)
{
    OS_object_token_t token = CondToken();
    ResetCondvar();
    MutexInitStatus = EINVAL;
    UtAssert_INT32_EQ(OS_CondVarCreate_Impl(&token, 0), OS_ERROR);
    UtAssert_UINT32_EQ(MutexDestroyCalls, 0);

    MutexInitStatus = 0;
    CondInitStatus = EINVAL;
    UtAssert_INT32_EQ(OS_CondVarCreate_Impl(&token, 0), OS_ERROR);
    UtAssert_UINT32_EQ(MutexDestroyCalls, 1);

    CondInitStatus = 0;
    UtAssert_INT32_EQ(OS_CondVarCreate_Impl(&token, 0), OS_SUCCESS);
    CondDestroyStatus = EINVAL;
    UtAssert_INT32_EQ(OS_CondVarDelete_Impl(&token), OS_ERROR);
    CondDestroyStatus = 0;
    MutexDestroyStatus = EINVAL;
    UtAssert_INT32_EQ(OS_CondVarDelete_Impl(&token), OS_ERROR);
    MutexDestroyStatus = 0;
    UtAssert_INT32_EQ(OS_CondVarDelete_Impl(&token), OS_SUCCESS);
}

static void Test_OperationsAndWaitResults(void)
{
    OS_object_token_t token = CondToken();
    OS_time_t deadline = OS_TimeAssembleFromNanoseconds(42, 100);
    OS_condvar_prop_t info = {0};
    ResetCondvar();

    UtAssert_INT32_EQ(OS_CondVarLock_Impl(&token), OS_SUCCESS);
    LockStatus = EINVAL;
    UtAssert_INT32_EQ(OS_CondVarLock_Impl(&token), OS_ERROR);
    UtAssert_INT32_EQ(OS_CondVarUnlock_Impl(&token), OS_SUCCESS);
    UnlockStatus = EINVAL;
    UtAssert_INT32_EQ(OS_CondVarUnlock_Impl(&token), OS_ERROR);

    UtAssert_INT32_EQ(OS_CondVarSignal_Impl(&token), OS_SUCCESS);
    SignalStatus = EINVAL;
    UtAssert_INT32_EQ(OS_CondVarSignal_Impl(&token), OS_ERROR);
    UtAssert_INT32_EQ(OS_CondVarBroadcast_Impl(&token), OS_SUCCESS);
    BroadcastStatus = EINVAL;
    UtAssert_INT32_EQ(OS_CondVarBroadcast_Impl(&token), OS_ERROR);

    UtAssert_INT32_EQ(OS_CondVarWait_Impl(&token), OS_SUCCESS);
    WaitStatus = EINVAL;
    UtAssert_INT32_EQ(OS_CondVarWait_Impl(&token), OS_ERROR);
    UtAssert_INT32_EQ(OS_CondVarTimedWait_Impl(&token, &deadline), OS_SUCCESS);
    TimedWaitStatus = ETIMEDOUT;
    UtAssert_INT32_EQ(OS_CondVarTimedWait_Impl(&token, &deadline), OS_ERROR_TIMEOUT);
    TimedWaitStatus = EINVAL;
    UtAssert_INT32_EQ(OS_CondVarTimedWait_Impl(&token, &deadline), OS_ERROR);
    UtAssert_INT32_EQ(OS_CondVarGetInfo_Impl(&token, &info), OS_SUCCESS);
}

void UtTest_Setup(void)
{
    UtTest_Add(Test_CreateAndDeleteFailures, NULL, NULL, "POSIX condvar create and delete failures");
    UtTest_Add(Test_OperationsAndWaitResults, NULL, NULL, "POSIX condvar operation and wait results");
}
