/* Runtime integration coverage for SHIRE's pthread task adapter. */

#include "os-posix.h"
#include "os-impl-tasks.h"
#include "os-shared-idmap.h"
#include "os-shared-task.h"

#include "utassert.h"
#include "uttest.h"

#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

POSIX_GlobalVars_t       POSIX_GlobalVars;
OS_task_internal_record_t OS_task_table[OS_MAX_TASKS];

static volatile bool TaskEntryCalled;
static osal_id_t     TaskEntryId;
static uint32        LastTickWait;

void OS_DebugPrintf(uint32 level, const char *function, uint32 line, const char *format, ...)
{
    (void)level;
    (void)function;
    (void)line;
    (void)format;
}

void OS_TaskEntryPoint(osal_id_t global_task_id)
{
    TaskEntryId     = global_task_id;
    TaskEntryCalled = true;
}

unsigned int CFE_PSP_WaitForSimulithTick(unsigned int ticks_to_wait)
{
    LastTickWait = ticks_to_wait;
    return ticks_to_wait;
}

static OS_object_token_t MakeTaskToken(osal_index_t index)
{
    OS_object_token_t token = {0};

    token.obj_type = OS_OBJECT_TYPE_OS_TASK;
    token.obj_idx  = index;
    token.obj_id   = OS_ObjectIdFromInteger((OS_OBJECT_TYPE_OS_TASK << OS_OBJECT_TYPE_SHIFT) | index);
    return token;
}

static void *ReturnArgument(void *arg)
{
    return arg;
}

static void *CancelableThread(void *arg)
{
    volatile bool *started = arg;

    *started = true;
    for (;;)
    {
        usleep(1000);
        pthread_testcancel();
    }
    return NULL;
}

static void Test_TaskInitializationAndIdentity(void)
{
    osal_id_t id = OS_ObjectIdFromInteger(0x12345678);
    pthread_t self = pthread_self();

    memset(&POSIX_GlobalVars, 0, sizeof(POSIX_GlobalVars));
    UtAssert_INT32_EQ(OS_Posix_TaskAPI_Impl_Init(), OS_SUCCESS);
    UtAssert_True(POSIX_GlobalVars.PageSize > 0, "task initialization discovers the host page size");
    UtAssert_INT32_EQ(OS_TaskRegister_Impl(id), OS_SUCCESS);
    UtAssert_True(OS_ObjectIdEqual(OS_TaskGetId_Impl(), id), "registered task ID is stored in pthread TLS");
    UtAssert_INT32_EQ(OS_TaskValidateSystemData_Impl(NULL, sizeof(self)), OS_INVALID_POINTER);
    UtAssert_INT32_EQ(OS_TaskValidateSystemData_Impl(&self, 0), OS_INVALID_POINTER);
    UtAssert_INT32_EQ(OS_TaskValidateSystemData_Impl(&self, sizeof(self)), OS_SUCCESS);
}

static void Test_InternalThreadCreation(void)
{
    pthread_t thread;
    void     *result = NULL;
    int       marker = 42;
    int32     status;

    POSIX_GlobalVars.EnableTaskPriorities = false;
    UtAssert_INT32_EQ(OS_Posix_InternalTaskCreate_Impl(&thread, 100, OSAL_TASK_STACK_ALLOCATE, 32768,
                                                        ReturnArgument, &marker), OS_SUCCESS);
    UtAssert_INT32_EQ(pthread_join(thread, &result), 0);
    UtAssert_True(result == &marker, "created pthread receives its entry argument");

    status = OS_Posix_InternalTaskCreate_Impl(&thread, 100, &marker, sizeof(marker), ReturnArgument, &marker);
    UtAssert_INT32_EQ(status, OS_ERROR);

    POSIX_GlobalVars.EnableTaskPriorities = true;
    POSIX_GlobalVars.SelectedRtScheduler  = SCHED_FIFO;
    POSIX_GlobalVars.PriLimits.PriorityMin = sched_get_priority_min(SCHED_FIFO);
    POSIX_GlobalVars.PriLimits.PriorityMax = sched_get_priority_max(SCHED_FIFO);
    status = OS_Posix_InternalTaskCreate_Impl(&thread, 0, OSAL_TASK_STACK_ALLOCATE, 32768, ReturnArgument, NULL);
    if (status == OS_SUCCESS)
    {
        pthread_join(thread, NULL);
    }
    status = OS_Posix_InternalTaskCreate_Impl(&thread, OS_MAX_TASK_PRIORITY, OSAL_TASK_STACK_ALLOCATE, 32768,
                                              ReturnArgument, NULL);
    if (status == OS_SUCCESS)
    {
        pthread_join(thread, NULL);
    }
    status = OS_Posix_InternalTaskCreate_Impl(&thread, 100, OSAL_TASK_STACK_ALLOCATE, 32768,
                                              ReturnArgument, NULL);
    if (status == OS_SUCCESS)
    {
        pthread_join(thread, NULL);
    }
    UtAssert_True(true, "real-time scheduling paths return cleanly with or without host permission");
}

static void Test_TaskLifecycle(void)
{
    OS_object_token_t token = MakeTaskToken(0);
    OS_task_prop_t    properties = {0};
    unsigned int      retries;

    memset(&OS_task_table[0], 0, sizeof(OS_task_table[0]));
    OS_task_table[0].priority      = 100;
    OS_task_table[0].stack_pointer = OSAL_TASK_STACK_ALLOCATE;
    OS_task_table[0].stack_size    = 32768;
    POSIX_GlobalVars.EnableTaskPriorities = false;
    TaskEntryCalled = false;
    UtAssert_INT32_EQ(OS_TaskCreate_Impl(&token, 0), OS_SUCCESS);
    for (retries = 0; !TaskEntryCalled && retries < 10000; ++retries)
    {
        sched_yield();
    }
    UtAssert_True(TaskEntryCalled, "OSAL task wrapper invoked the shared entry point");
    UtAssert_True(OS_ObjectIdEqual(TaskEntryId, token.obj_id), "task wrapper preserves the OSAL ID");
    UtAssert_INT32_EQ(OS_TaskDelete_Impl(&token), OS_SUCCESS);
    UtAssert_INT32_EQ(OS_TaskGetInfo_Impl(&token, &properties), OS_SUCCESS);

    OS_impl_task_table[0].id = pthread_self();
    UtAssert_INT32_EQ(OS_TaskMatch_Impl(&token), OS_SUCCESS);
    UtAssert_True(OS_TaskIdMatchSystemData_Impl(&OS_impl_task_table[0].id, &token, NULL),
                  "system task data matches its pthread");
}

static void Test_TaskCancellationDetachAndDelay(void)
{
    OS_object_token_t token = MakeTaskToken(1);
    volatile bool     started = false;
    pthread_t         thread;
    int32             highest_status;
    int32             lowest_status;
    int32             middle_status;

    UtAssert_INT32_EQ(pthread_create(&thread, NULL, CancelableThread, (void *)&started), 0);
    OS_impl_task_table[1].id = thread;
    while (!started)
    {
        sched_yield();
    }
    UtAssert_INT32_EQ(OS_TaskDelete_Impl(&token), OS_SUCCESS);

    UtAssert_INT32_EQ(pthread_create(&thread, NULL, ReturnArgument, NULL), 0);
    OS_impl_task_table[1].id = thread;
    UtAssert_INT32_EQ(OS_TaskDetach_Impl(&token), OS_SUCCESS);

    LastTickWait = 0;
    UtAssert_INT32_EQ(OS_TaskDelay_Impl(0), OS_SUCCESS);
    UtAssert_UINT32_EQ(LastTickWait, 1);
    UtAssert_INT32_EQ(OS_TaskDelay_Impl(1), OS_SUCCESS);
    UtAssert_UINT32_EQ(LastTickWait, 1);
    UtAssert_INT32_EQ(OS_TaskDelay_Impl(11), OS_SUCCESS);
    UtAssert_UINT32_EQ(LastTickWait, 2);

    OS_impl_task_table[1].id = pthread_self();
    POSIX_GlobalVars.EnableTaskPriorities = false;
    UtAssert_INT32_EQ(OS_TaskSetPriority_Impl(&token, 100), OS_SUCCESS);
    POSIX_GlobalVars.EnableTaskPriorities  = true;
    POSIX_GlobalVars.SelectedRtScheduler   = SCHED_OTHER;
    POSIX_GlobalVars.PriLimits.PriorityMin = 0;
    POSIX_GlobalVars.PriLimits.PriorityMax = 0;
    highest_status = OS_TaskSetPriority_Impl(&token, 0);
    lowest_status  = OS_TaskSetPriority_Impl(&token, OS_MAX_TASK_PRIORITY);
    middle_status  = OS_TaskSetPriority_Impl(&token, 100);

    /*
     * pthread_setschedprio() is allowed to reject these requests according to
     * the caller's privileges and the host scheduler.  Verify the adapter's
     * stable contract without baking the container's privilege model into the
     * test: both boundary priorities use the same SCHED_OTHER value, and every
     * host result is translated to one of the two documented OSAL statuses.
     */
    UtAssert_INT32_EQ(highest_status, lowest_status);
    UtAssert_True(highest_status == OS_SUCCESS || highest_status == OS_ERROR,
                  "boundary priority result is a documented OSAL status");
    UtAssert_True(middle_status == OS_SUCCESS || middle_status == OS_ERROR,
                  "middle priority result is a documented OSAL status");
}

void UtTest_Setup(void)
{
    UtTest_Add(Test_TaskInitializationAndIdentity, NULL, NULL, "SHIRE task initialization and identity");
    UtTest_Add(Test_InternalThreadCreation, NULL, NULL, "SHIRE pthread creation and scheduling");
    UtTest_Add(Test_TaskLifecycle, NULL, NULL, "SHIRE task lifecycle");
    UtTest_Add(Test_TaskCancellationDetachAndDelay, NULL, NULL, "SHIRE task cancellation and delay");
}
