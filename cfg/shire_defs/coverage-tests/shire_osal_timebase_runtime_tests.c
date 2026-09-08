/* Runtime integration coverage for SHIRE's Simulith-backed OSAL timebase. */

#include "os-posix.h"
#include "os-impl-timebase.h"
#include "os-shared-common.h"
#include "os-shared-idmap.h"
#include "os-shared-time.h"
#include "os-shared-timebase.h"

#include "utassert.h"
#include "uttest.h"

#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

POSIX_GlobalVars_t            POSIX_GlobalVars;
OS_SharedGlobalVars_t         OS_SharedGlobalVars;
OS_timebase_internal_record_t OS_timebase_table[OS_MAX_TIMEBASES];
OS_timecb_internal_record_t   OS_timecb_table[OS_MAX_TIMERS];

static pthread_mutex_t TickMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  TickCondition = PTHREAD_COND_INITIALIZER;
static unsigned int    TickPermits;
static unsigned int    SimulithInitCalls;
static volatile unsigned int CallbackCalls;
static volatile unsigned int TickWaitCalls;

void OS_DebugPrintf(uint32 level, const char *function, uint32 line, const char *format, ...)
{
    (void)level;
    (void)function;
    (void)line;
    (void)format;
}

void CFE_PSP_InitSimulithTime(void)
{
    SimulithInitCalls++;
}

unsigned int CFE_PSP_WaitForSimulithTick(unsigned int timebase_id)
{
    pthread_mutex_lock(&TickMutex);
    while (TickPermits == 0)
    {
        pthread_cond_wait(&TickCondition, &TickMutex);
    }
    TickPermits--;
    TickWaitCalls++;
    pthread_mutex_unlock(&TickMutex);
    return timebase_id;
}

int32 OS_ObjectIdGetById(OS_lock_mode_t lock_mode, osal_objtype_t idtype, osal_id_t id, OS_object_token_t *token)
{
    (void)lock_mode;
    token->lock_mode = OS_LOCK_MODE_NONE;
    token->obj_type  = idtype;
    token->obj_idx   = 0;
    token->obj_id    = id;
    return OS_SUCCESS;
}

static OS_object_token_t MakeTimebaseToken(void)
{
    OS_object_token_t token = {0};

    token.obj_type = OS_OBJECT_TYPE_OS_TIMEBASE;
    token.obj_idx  = 0;
    token.obj_id   = OS_ObjectIdFromInteger(OS_OBJECT_TYPE_OS_TIMEBASE << OS_OBJECT_TYPE_SHIFT);
    return token;
}

static void TimerCallback(osal_id_t timer_id, void *arg)
{
    unsigned int *marker = arg;

    (void)timer_id;
    (*marker)++;
    CallbackCalls++;
}

static void QueueTick(void)
{
    pthread_mutex_lock(&TickMutex);
    TickPermits++;
    pthread_cond_broadcast(&TickCondition);
    pthread_mutex_unlock(&TickMutex);
}

static bool WaitForCallback(void)
{
    unsigned int retries;

    for (retries = 0; CallbackCalls == 0 && retries < 1000; ++retries)
    {
        usleep(1000);
    }
    return CallbackCalls > 0;
}

static bool WaitForTickWaits(unsigned int expected)
{
    unsigned int retries;

    for (retries = 0; TickWaitCalls < expected && retries < 1000; ++retries)
    {
        usleep(1000);
    }
    return TickWaitCalls >= expected;
}

static void Test_TimebaseLifecycle(void)
{
    OS_object_token_t token = MakeTimebaseToken();
    OS_timebase_prop_t properties = {0};
    unsigned int       marker = 0;
    osal_id_t          callback_id = OS_ObjectIdFromInteger(OS_OBJECT_TYPE_OS_TIMECB << OS_OBJECT_TYPE_SHIFT);

    memset(&POSIX_GlobalVars, 0, sizeof(POSIX_GlobalVars));
    memset(&OS_SharedGlobalVars, 0, sizeof(OS_SharedGlobalVars));
    memset(OS_timebase_table, 0, sizeof(OS_timebase_table));
    memset(OS_timecb_table, 0, sizeof(OS_timecb_table));
    TickPermits      = 0;
    SimulithInitCalls = 0;
    CallbackCalls    = 0;
    TickWaitCalls    = 0;

    UtAssert_INT32_EQ(OS_Posix_TimeBaseAPI_Impl_Init(), OS_SUCCESS);
    UtAssert_INT32_EQ(SimulithInitCalls, 1);
    UtAssert_UINT32_EQ(POSIX_GlobalVars.ClockAccuracyNsec, INTERVAL_NS);
    UtAssert_UINT32_EQ(OS_SharedGlobalVars.TicksPerSecond, 1000000000UL / INTERVAL_NS);
    UtAssert_UINT32_EQ(OS_SharedGlobalVars.MicroSecPerTick, INTERVAL_NS / 1000UL);

    OS_timebase_table[0].first_cb = callback_id;
    OS_timecb_table[0].callback_ptr = TimerCallback;
    OS_timecb_table[0].callback_arg = &marker;
    OS_timecb_table[0].next_cb = callback_id;
    UtAssert_INT32_EQ(OS_TimeBaseCreate_Impl(&token), OS_SUCCESS);
    UtAssert_True(OS_timebase_table[0].external_sync == CFE_PSP_WaitForSimulithTick,
                  "timebase installs the PSP tick synchronizer");
    UtAssert_INT32_EQ(OS_TimeBaseCreate_Impl(&token), OS_ERROR);

    QueueTick();
    UtAssert_True(WaitForCallback(), "handler thread dispatched a timer callback");
    UtAssert_INT32_EQ(marker, 1);

    OS_timebase_table[0].first_cb = OS_OBJECT_ID_UNDEFINED;
    QueueTick();
    UtAssert_True(WaitForTickWaits(2), "handler accepts a tick with an empty callback ring");
    usleep(10000);
    OS_timebase_table[0].first_cb = callback_id;
    OS_timecb_table[0].callback_ptr = NULL;
    QueueTick();
    UtAssert_True(WaitForTickWaits(3), "handler accepts a callback entry without a function");
    usleep(10000);

    OS_TimeBaseLock_Impl(&token);
    OS_TimeBaseUnlock_Impl(&token);
    UtAssert_INT32_EQ(OS_TimeBaseSet_Impl(&token, 10, 25), OS_SUCCESS);
    UtAssert_UINT32_EQ(OS_timebase_table[0].accuracy_usec, 25);
    UtAssert_INT32_EQ(OS_TimeBaseSet_Impl(&token, 10, 0), OS_SUCCESS);
    UtAssert_UINT32_EQ(OS_timebase_table[0].accuracy_usec, 10);
    UtAssert_INT32_EQ(OS_TimeBaseGetInfo_Impl(&token, &properties), OS_SUCCESS);
    UtAssert_INT32_EQ(OS_TimeBaseDelete_Impl(&token), OS_SUCCESS);
    UtAssert_True(OS_timebase_table[0].external_sync == NULL, "deletion disconnects the PSP tick source");
    usleep(10000);
}

void UtTest_Setup(void)
{
    UtTest_Add(Test_TimebaseLifecycle, NULL, NULL, "SHIRE OSAL timebase lifecycle and callback thread");
}
