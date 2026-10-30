/* Exercise POSIX queue error mapping and EINTR retries without host mqueues. */
#include "os-posix.h"
#include "os-impl-queues.h"
#include "os-shared-queue.h"
#include "os-shared-idmap.h"
#include "bsp-impl.h"
#include "utassert.h"
#include "uttest.h"

#include <errno.h>
#include <stdarg.h>
#include <string.h>

POSIX_GlobalVars_t POSIX_GlobalVars;
OS_queue_internal_record_t OS_queue_table[OS_MAX_QUEUES];

static int OpenError;
static int UnlinkError;
static int CloseError;
static int ReceiveError;
static int SendError;
static int ReceiveInterrupts;
static int SendInterrupts;
static ssize_t ReceiveLength;
static int OpenCalls;
static int UnlinkCalls;
static int CloseCalls;
static int ReceiveCalls;
static int TimedReceiveCalls;
static int SendCalls;
static int DelayCalls;
static struct mq_attr LastAttributes;

void OS_DebugPrintf(uint32 level, const char *function, uint32 line, const char *format, ...)
{
    (void)level;
    (void)function;
    (void)line;
    (void)format;
}

void OS_Posix_CompAbsDelayTime(uint32 msecs, struct timespec *tm)
{
    (void)msecs;
    DelayCalls++;
    tm->tv_sec = 100;
    tm->tv_nsec = 0;
}

mqd_t mq_open(const char *name, int flags, ...)
{
    va_list args;
    OpenCalls++;
    UtAssert_True(name[0] == '/', "queue name starts with a slash");
    UtAssert_True((flags & O_CREAT) != 0, "queue is created");
    va_start(args, flags);
    (void)va_arg(args, int);
    LastAttributes = *va_arg(args, struct mq_attr *);
    va_end(args);
    if (OpenError)
    {
        errno = OpenError;
        return (mqd_t)-1;
    }
    return (mqd_t)123;
}

int mq_unlink(const char *name)
{
    (void)name;
    UnlinkCalls++;
    errno = UnlinkError;
    return UnlinkError ? -1 : 0;
}

int mq_close(mqd_t queue)
{
    UtAssert_INT32_EQ(queue, 123);
    CloseCalls++;
    errno = CloseError;
    return CloseError ? -1 : 0;
}

static ssize_t MockReceive(void)
{
    if (ReceiveInterrupts > 0)
    {
        ReceiveInterrupts--;
        errno = EINTR;
        return -1;
    }
    if (ReceiveError)
    {
        errno = ReceiveError;
        return -1;
    }
    return ReceiveLength;
}

ssize_t mq_receive(mqd_t queue, char *buffer, size_t size, unsigned int *priority)
{
    (void)queue;
    (void)buffer;
    (void)size;
    (void)priority;
    ReceiveCalls++;
    return MockReceive();
}

ssize_t mq_timedreceive(mqd_t queue, char *buffer, size_t size,
                        unsigned int *priority, const struct timespec *timeout)
{
    (void)queue;
    (void)buffer;
    (void)size;
    (void)priority;
    UtAssert_NOT_NULL(timeout);
    TimedReceiveCalls++;
    return MockReceive();
}

int mq_timedsend(mqd_t queue, const char *buffer, size_t size,
                 unsigned int priority, const struct timespec *timeout)
{
    (void)queue;
    (void)buffer;
    (void)size;
    (void)priority;
    UtAssert_NOT_NULL(timeout);
    SendCalls++;
    if (SendInterrupts > 0)
    {
        SendInterrupts--;
        errno = EINTR;
        return -1;
    }
    errno = SendError;
    return SendError ? -1 : 0;
}

static OS_object_token_t QueueToken(void)
{
    OS_object_token_t token = {0};
    token.obj_type = OS_OBJECT_TYPE_OS_QUEUE;
    token.obj_idx = 0;
    token.obj_id = OS_ObjectIdFromInteger(OS_OBJECT_TYPE_OS_QUEUE << OS_OBJECT_TYPE_SHIFT);
    return token;
}

static void ResetQueue(void)
{
    OpenError = UnlinkError = CloseError = ReceiveError = SendError = 0;
    ReceiveInterrupts = SendInterrupts = 0;
    ReceiveLength = 4;
    OpenCalls = UnlinkCalls = CloseCalls = ReceiveCalls = TimedReceiveCalls = SendCalls = DelayCalls = 0;
    memset(&LastAttributes, 0, sizeof(LastAttributes));
    OS_Posix_QueueAPI_Impl_Init();
    strcpy(OS_queue_table[0].queue_name, "shire-test");
    OS_queue_table[0].max_depth = 8;
    OS_queue_table[0].max_size = 16;
    OS_impl_queue_table[0].id = (mqd_t)123;
}

static void Test_CreateAndDelete(void)
{
    OS_object_token_t token = QueueToken();
    ResetQueue();
    POSIX_GlobalVars.TruncateQueueDepth = 2;
    UtAssert_INT32_EQ(OS_QueueCreate_Impl(&token, 0), OS_SUCCESS);
    UtAssert_INT32_EQ(LastAttributes.mq_maxmsg, 2);
    UtAssert_INT32_EQ(LastAttributes.mq_msgsize, 16);
    UtAssert_INT32_EQ(UnlinkCalls, 1);
    UtAssert_INT32_EQ(OS_QueueDelete_Impl(&token), OS_SUCCESS);

    ResetQueue();
    UnlinkError = EACCES;
    UtAssert_INT32_EQ(OS_QueueCreate_Impl(&token, 0), OS_SUCCESS);
    UtAssert_INT32_EQ(LastAttributes.mq_maxmsg, 8);
    OpenError = EINVAL;
    UtAssert_INT32_EQ(OS_QueueCreate_Impl(&token, 0), OS_ERROR);
    OpenError = EACCES;
    UtAssert_INT32_EQ(OS_QueueCreate_Impl(&token, 0), OS_ERROR);
    CloseError = EBADF;
    UtAssert_INT32_EQ(OS_QueueDelete_Impl(&token), OS_ERROR);
}

static void Test_ReceiveErrorsAndRetries(void)
{
    OS_object_token_t token = QueueToken();
    size_t copied = 0;
    char data[16];
    ResetQueue();
    ReceiveInterrupts = 1;
    UtAssert_INT32_EQ(OS_QueueGet_Impl(&token, data, sizeof(data), &copied, OS_PEND), OS_SUCCESS);
    UtAssert_UINT32_EQ(ReceiveCalls, 2);
    UtAssert_UINT32_EQ(copied, 4);

    ReceiveError = EMSGSIZE;
    UtAssert_INT32_EQ(OS_QueueGet_Impl(&token, data, sizeof(data), &copied, OS_CHECK), OS_QUEUE_INVALID_SIZE);
    UtAssert_UINT32_EQ(copied, 0);
    ReceiveError = ETIMEDOUT;
    UtAssert_INT32_EQ(OS_QueueGet_Impl(&token, data, sizeof(data), &copied, OS_CHECK), OS_QUEUE_EMPTY);
    UtAssert_INT32_EQ(OS_QueueGet_Impl(&token, data, sizeof(data), &copied, 5), OS_QUEUE_TIMEOUT);
    UtAssert_UINT32_EQ(DelayCalls, 1);
    ReceiveError = EACCES;
    UtAssert_INT32_EQ(OS_QueueGet_Impl(&token, data, sizeof(data), &copied, 5), OS_ERROR);
    UtAssert_INT32_EQ(OS_QueueGet_Impl(&token, data, sizeof(data), &copied, OS_PEND), OS_ERROR);

    ReceiveError = 0;
    ReceiveInterrupts = 1;
    UtAssert_INT32_EQ(OS_QueueGet_Impl(&token, data, sizeof(data), &copied, 5), OS_SUCCESS);
    UtAssert_UINT32_EQ(copied, 4);
    ReceiveError = ETIMEDOUT;
    ReceiveInterrupts = 1;
    UtAssert_INT32_EQ(OS_QueueGet_Impl(&token, data, sizeof(data), &copied, OS_CHECK), OS_ERROR);
}

static void Test_SendErrorsAndRetries(void)
{
    OS_object_token_t token = QueueToken();
    const char data[4] = {1, 2, 3, 4};
    ResetQueue();
    SendInterrupts = 1;
    UtAssert_INT32_EQ(OS_QueuePut_Impl(&token, data, sizeof(data), 0), OS_SUCCESS);
    UtAssert_UINT32_EQ(SendCalls, 2);
    SendError = ETIMEDOUT;
    UtAssert_INT32_EQ(OS_QueuePut_Impl(&token, data, sizeof(data), 0), OS_QUEUE_FULL);
    SendError = EACCES;
    UtAssert_INT32_EQ(OS_QueuePut_Impl(&token, data, sizeof(data), 0), OS_ERROR);
}

void UtTest_Setup(void)
{
    UtTest_Add(Test_CreateAndDelete, NULL, NULL, "POSIX queue create and delete failures");
    UtTest_Add(Test_ReceiveErrorsAndRetries, NULL, NULL, "POSIX queue receive errors and retries");
    UtTest_Add(Test_SendErrorsAndRetries, NULL, NULL, "POSIX queue send errors and retries");
}
