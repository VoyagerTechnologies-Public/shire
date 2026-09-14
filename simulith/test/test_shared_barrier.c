#include "simulith_shared_barrier.h"
#include "unity.h"

#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static simulith_shared_barrier_t owner;
static simulith_shared_barrier_t participant;

typedef struct
{
    simulith_shared_barrier_t *barrier;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int entered;
    uint32_t result;
} barrier_waiter_t;

static void *wait_for_barrier_stop(void *argument)
{
    barrier_waiter_t *waiter = argument;
    pthread_mutex_lock(&waiter->mutex);
    waiter->entered = 1;
    pthread_cond_signal(&waiter->condition);
    pthread_mutex_unlock(&waiter->mutex);
    waiter->result = simulith_shared_barrier_wait(waiter->barrier,
                                                   UINT32_C(1), 1000);
    return NULL;
}

void setUp(void)
{
    memset(&owner, 0, sizeof(owner));
    memset(&participant, 0, sizeof(participant));
    owner.fd = -1;
    participant.fd = -1;
    unlink(SIMULITH_SHARED_BARRIER_PATH);
}

void tearDown(void)
{
    simulith_shared_barrier_close(&participant);
    simulith_shared_barrier_close(&owner);
    unlink(SIMULITH_SHARED_BARRIER_PATH);
}

static void test_shared_barrier_rejects_invalid_inputs(void)
{
    uint64_t sequence = 0;
    uint64_t time_ns = 0;
    uint32_t phase = 0;
    simulith_shared_barrier_t empty = {.fd = -1, .slot = -1};

    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_connect(&empty, -1));
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_connect(
                                  &empty, SIMULITH_SHARED_BARRIER_SLOTS));
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_publish(NULL, 1, 2, 3, 1));
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_receive(
                                  NULL, &sequence, &time_ns, &phase));
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_receive(
                                  &empty, NULL, &time_ns, &phase));
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_complete(NULL, 1, 3));
    TEST_ASSERT_EQUAL_UINT32(0, simulith_shared_barrier_wait(NULL, 1, 1));
    simulith_shared_barrier_stop(NULL);
    simulith_shared_barrier_interrupt(NULL);
}

static void test_shared_barrier_lifecycle_and_completion(void)
{
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_create(&owner));
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_connect(&participant, 0));

    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_publish(
                                  &owner, 42, 123456, 7, UINT32_C(1)));
    uint64_t sequence = 0;
    uint64_t time_ns = 0;
    uint32_t phase = 0;
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_receive(
                                  &participant, &sequence, &time_ns, &phase));
    TEST_ASSERT_EQUAL_UINT64(42, sequence);
    TEST_ASSERT_EQUAL_UINT64(123456, time_ns);
    TEST_ASSERT_EQUAL_UINT32(7, phase);

    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_complete(&participant, 42, 7));
    TEST_ASSERT_EQUAL_UINT32(UINT32_C(1),
                             simulith_shared_barrier_wait(&owner, UINT32_C(1), 1));
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_complete(&participant, 42, 7));
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_complete(&participant, 41, 7));
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_complete(&participant, 42, 6));

    simulith_shared_barrier_stop(&owner);
    TEST_ASSERT_EQUAL_UINT32(UINT32_C(1),
                             simulith_shared_barrier_wait(&owner, UINT32_C(2), 1));
    TEST_ASSERT_EQUAL_INT(1, simulith_shared_barrier_receive(
                                  &participant, &sequence, &time_ns, &phase));
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_complete(
                                  &participant, 42, 7));

    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_publish(
                                  &owner, 43, 123457, 8, UINT32_C(2)));
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_complete(
                                  &participant, 43, 8));
}

static void test_shared_barrier_wait_times_out(void)
{
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_create(&owner));

    TEST_ASSERT_EQUAL_UINT32(0,
                             simulith_shared_barrier_wait(&owner,
                                                          UINT32_C(1), 1));
}

static void test_shared_barrier_stop_wakes_waiter(void)
{
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_create(&owner));
    barrier_waiter_t waiter = {
        .barrier = &owner,
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .condition = PTHREAD_COND_INITIALIZER,
    };
    pthread_t thread;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&thread, NULL,
                                            wait_for_barrier_stop, &waiter));
    pthread_mutex_lock(&waiter.mutex);
    while (!waiter.entered)
        pthread_cond_wait(&waiter.condition, &waiter.mutex);
    pthread_mutex_unlock(&waiter.mutex);

    simulith_shared_barrier_stop(&owner);
    TEST_ASSERT_EQUAL_INT(0, pthread_join(thread, NULL));
    TEST_ASSERT_EQUAL_UINT32(0, waiter.result);
    pthread_cond_destroy(&waiter.condition);
    pthread_mutex_destroy(&waiter.mutex);
}

static void test_shared_barrier_interrupts_receive(void)
{
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_create(&owner));
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_connect(&participant, 1));

    simulith_shared_barrier_interrupt(&participant);
    uint64_t sequence = 0;
    uint64_t time_ns = 0;
    uint32_t phase = 0;
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_receive(
                                  &participant, &sequence, &time_ns, &phase));
}

static void test_shared_barrier_reports_filesystem_and_readiness_failures(void)
{
    /* An existing directory makes creation fail without altering production
     * code or relying on allocation-failure hooks. */
    TEST_ASSERT_EQUAL_INT(0, mkdir(SIMULITH_SHARED_BARRIER_PATH, 0700));
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_create(&owner));
    TEST_ASSERT_EQUAL_INT(0, rmdir(SIMULITH_SHARED_BARRIER_PATH));

    /* A missing owner is retried for the documented connection window. */
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_connect(&participant, 0));

    int fd = open(SIMULITH_SHARED_BARRIER_PATH, O_RDWR | O_CREAT | O_EXCL, 0600);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, fd);
    close(fd);
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_connect(&participant, 0));
    unlink(SIMULITH_SHARED_BARRIER_PATH);

    fd = open(SIMULITH_SHARED_BARRIER_PATH, O_RDWR | O_CREAT | O_EXCL, 0600);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, fd);
    TEST_ASSERT_EQUAL_INT(0, ftruncate(
                                 fd,
                                 (off_t)sizeof(simulith_shared_barrier_state_t)));
    close(fd);
    /* Correct size but no published magic/version must also be rejected. */
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_connect(&participant, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_shared_barrier_rejects_invalid_inputs);
    RUN_TEST(test_shared_barrier_lifecycle_and_completion);
    RUN_TEST(test_shared_barrier_wait_times_out);
    RUN_TEST(test_shared_barrier_stop_wakes_waiter);
    RUN_TEST(test_shared_barrier_interrupts_receive);
    RUN_TEST(test_shared_barrier_reports_filesystem_and_readiness_failures);
    return UNITY_END();
}
