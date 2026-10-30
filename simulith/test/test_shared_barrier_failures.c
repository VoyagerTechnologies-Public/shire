#include "simulith_shared_barrier.h"
#include "unity.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

enum { FAIL_NONE, FAIL_OPEN, FAIL_TRUNCATE, FAIL_MAP, FAIL_STAT, FAIL_SHORT,
       FAIL_MUTEX_ATTR, FAIL_MUTEX_SHARED, FAIL_COND_ATTR, FAIL_COND_SHARED,
       FAIL_COND_CLOCK, FAIL_MUTEX, FAIL_COMPLETION_COND, FAIL_PHASE_COND };
static int failure, cond_calls, open_calls, last_open_fd, stat_failures, retry_sleeps;
static simulith_shared_barrier_t owner, participant;
static char directory[128], private_path[160];

int __real_open(const char *, int, ...);
int __wrap_open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args; va_start(args, flags); mode = (mode_t)va_arg(args, int); va_end(args);
    }
    if (strcmp(path, SIMULITH_SHARED_BARRIER_PATH) == 0) {
        open_calls++;
        if (failure == FAIL_OPEN) { errno = EMFILE; return -1; }
        path = private_path;
    }
    int fd = __real_open(path, flags, mode);
    if (fd >= 0) last_open_fd = fd;
    return fd;
}
int __real_unlink(const char *);
int __wrap_unlink(const char *path)
{
    return __real_unlink(strcmp(path, SIMULITH_SHARED_BARRIER_PATH) == 0 ? private_path : path);
}
int __real_ftruncate(int, off_t);
int __wrap_ftruncate(int fd, off_t length)
{
    if (failure == FAIL_TRUNCATE) { errno = ENOSPC; return -1; }
    return __real_ftruncate(fd, length);
}
void *__real_mmap(void *, size_t, int, int, int, off_t);
void *__wrap_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    if (failure == FAIL_MAP) { errno = ENOMEM; return MAP_FAILED; }
    return __real_mmap(addr, length, prot, flags, fd, offset);
}
int __real_fstat(int, struct stat *);
int __wrap_fstat(int fd, struct stat *details)
{
    if (failure == FAIL_STAT || stat_failures > 0) {
        if (stat_failures > 0) stat_failures--;
        errno = EIO; return -1;
    }
    int result = __real_fstat(fd, details);
    if (!result && failure == FAIL_SHORT) details->st_size = sizeof(simulith_shared_barrier_state_t) - 1;
    return result;
}
int __wrap_nanosleep(const struct timespec *delay, struct timespec *remaining)
{
    (void)remaining;
    TEST_ASSERT_EQUAL_INT(10000000, delay->tv_nsec);
    retry_sleeps++;
    return 0; /* Exercise the bounded retry loop without spending a second per fault. */
}
int __real_pthread_mutexattr_init(pthread_mutexattr_t *);
int __wrap_pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    return failure == FAIL_MUTEX_ATTR ? EAGAIN : __real_pthread_mutexattr_init(attr);
}
int __real_pthread_mutexattr_setpshared(pthread_mutexattr_t *, int);
int __wrap_pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int shared)
{
    return failure == FAIL_MUTEX_SHARED ? EINVAL : __real_pthread_mutexattr_setpshared(attr, shared);
}
int __real_pthread_condattr_init(pthread_condattr_t *);
int __wrap_pthread_condattr_init(pthread_condattr_t *attr)
{
    return failure == FAIL_COND_ATTR ? EAGAIN : __real_pthread_condattr_init(attr);
}
int __real_pthread_condattr_setpshared(pthread_condattr_t *, int);
int __wrap_pthread_condattr_setpshared(pthread_condattr_t *attr, int shared)
{
    return failure == FAIL_COND_SHARED ? EINVAL : __real_pthread_condattr_setpshared(attr, shared);
}
int __real_pthread_condattr_setclock(pthread_condattr_t *, clockid_t);
int __wrap_pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock)
{
    return failure == FAIL_COND_CLOCK ? EINVAL : __real_pthread_condattr_setclock(attr, clock);
}
int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    return failure == FAIL_MUTEX ? EAGAIN : __real_pthread_mutex_init(mutex, attr);
}
int __real_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int __wrap_pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    cond_calls++;
    if ((failure == FAIL_COMPLETION_COND && cond_calls == 1) ||
        (failure == FAIL_PHASE_COND && cond_calls == 2)) return EAGAIN;
    return __real_pthread_cond_init(cond, attr);
}

void setUp(void)
{
    failure = FAIL_NONE; cond_calls = open_calls = stat_failures = retry_sleeps = 0;
    last_open_fd = -1;
    memset(&owner, 0, sizeof(owner)); owner.fd = -1;
    memset(&participant, 0, sizeof(participant)); participant.fd = -1;
    strcpy(directory, "/tmp/shire-barrier-failures-XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(directory));
    snprintf(private_path, sizeof(private_path), "%s/barrier", directory);
    unsetenv("SHIRE_BARRIER_PROFILE");
}
void tearDown(void)
{
    simulith_shared_barrier_close(&participant);
    simulith_shared_barrier_close(&owner);
    __real_unlink(private_path); rmdir(directory);
    unsetenv("SHIRE_BARRIER_PROFILE");
}
static void assert_released(const simulith_shared_barrier_t *barrier)
{
    TEST_ASSERT_NULL(barrier->state);
    TEST_ASSERT_EQUAL_INT(-1, barrier->fd);
    if (last_open_fd >= 0) {
        errno = 0;
        TEST_ASSERT_EQUAL_INT(-1, fcntl(last_open_fd, F_GETFD));
        TEST_ASSERT_EQUAL_INT(EBADF, errno);
    }
}
static void test_create_failure_cleanup_and_restart(void)
{
    const int cases[] = {FAIL_OPEN, FAIL_TRUNCATE, FAIL_MAP, FAIL_MUTEX_ATTR,
        FAIL_MUTEX_SHARED, FAIL_COND_ATTR, FAIL_COND_SHARED, FAIL_COND_CLOCK,
        FAIL_MUTEX, FAIL_COMPLETION_COND, FAIL_PHASE_COND};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        failure = cases[i]; cond_calls = 0; last_open_fd = -1;
        TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_create(&owner));
        assert_released(&owner);
        TEST_ASSERT_EQUAL_INT(-1, access(private_path, F_OK));
        failure = FAIL_NONE;
        TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_create(&owner));
        TEST_ASSERT_EQUAL_UINT32(SIMULITH_SHARED_BARRIER_MAGIC, owner.state->magic);
        TEST_ASSERT_EQUAL_UINT32(SIMULITH_SHARED_BARRIER_VERSION, owner.state->version);
        simulith_shared_barrier_close(&owner);
    }
}
static void test_connect_failure_cleanup_and_recovery(void)
{
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_create(&owner));
    const int cases[] = {FAIL_OPEN, FAIL_STAT, FAIL_SHORT, FAIL_MAP};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        failure = cases[i]; last_open_fd = -1; retry_sleeps = 0;
        TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_connect(&participant, 0));
        assert_released(&participant);
        TEST_ASSERT_EQUAL_INT(cases[i] == FAIL_MAP ? 0 : 100, retry_sleeps);
        failure = FAIL_NONE;
        TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_connect(&participant, 0));
        simulith_shared_barrier_close(&participant);
    }
    owner.state->version++;
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_connect(&participant, 0));
    assert_released(&participant);
    owner.state->version = SIMULITH_SHARED_BARRIER_VERSION;
    stat_failures = 2; retry_sleeps = 0;
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_connect(&participant, 0));
    TEST_ASSERT_EQUAL_INT(2, retry_sleeps);
    TEST_ASSERT_EQUAL_INT(0, stat_failures);
}
static void test_profiled_fast_and_locked_paths_with_late_participant(void)
{
    setenv("SHIRE_BARRIER_PROFILE", "0", 1);
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_create(&owner));
    TEST_ASSERT_FALSE(owner.profile_enabled);
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_connect(&participant, 0));
    TEST_ASSERT_FALSE(participant.profile_enabled);
    simulith_shared_barrier_close(&participant); simulith_shared_barrier_close(&owner);
    setenv("SHIRE_BARRIER_PROFILE", "1", 1);
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_create(&owner));
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_publish(&owner, 1, 1000, 2, 1));
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_connect(&participant, 0));
    TEST_ASSERT_TRUE(owner.profile_enabled); TEST_ASSERT_TRUE(participant.profile_enabled);
    TEST_ASSERT_EQUAL_UINT64(0, participant.last_generation);
    uint64_t sequence, time_ns; uint32_t phase;
    participant.fast_receive_safe = 1;
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_receive(&participant, &sequence, &time_ns, &phase));
    TEST_ASSERT_EQUAL_UINT64(1, sequence); TEST_ASSERT_EQUAL_UINT64(1000, time_ns);
    TEST_ASSERT_EQUAL_UINT32(2, phase);
    TEST_ASSERT_EQUAL_UINT64(1, participant.profile_fast_receive);
    TEST_ASSERT_EQUAL_UINT32(0, simulith_shared_barrier_wait(&owner, 1, 0));
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_complete(&participant, 1, 2));
    TEST_ASSERT_EQUAL_UINT32(1, simulith_shared_barrier_wait(&owner, 1, 0));
    TEST_ASSERT_EQUAL_UINT64(1, owner.profile_fast_wait);
    TEST_ASSERT_EQUAL_UINT64(1, owner.profile_lock_count[3]);
    participant.fast_receive_safe = 0;
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_publish(&owner, 2, 2000, 3, 1));
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_receive(&participant, &sequence, &time_ns, &phase));
    TEST_ASSERT_EQUAL_UINT64(2, sequence); TEST_ASSERT_EQUAL_UINT64(2000, time_ns);
    TEST_ASSERT_EQUAL_UINT32(3, phase);
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_complete(&participant, 2, 3));
    TEST_ASSERT_EQUAL_UINT64(1, participant.profile_lock_count[1]);
    TEST_ASSERT_EQUAL_UINT64(2, participant.profile_lock_count[2]);
    TEST_ASSERT_EQUAL_UINT64(2, owner.profile_lock_count[0]);
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_publish(&owner, 3, 3000, 1, 2));
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_complete(&participant, 3, 1));
}
static void test_unmapped_and_missing_output_arguments(void)
{
    uint64_t sequence, time_ns; uint32_t phase;
    simulith_shared_barrier_t empty = {.fd = -1, .slot = -1};
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_publish(&empty, 1, 1, 1, 1));
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_receive(&empty, &sequence, &time_ns, &phase));
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_complete(&empty, 1, 1));
    TEST_ASSERT_EQUAL_UINT32(0, simulith_shared_barrier_wait(&empty, 1, 0));
    simulith_shared_barrier_close(NULL);
    simulith_shared_barrier_stop(&empty); simulith_shared_barrier_interrupt(&empty);
    TEST_ASSERT_EQUAL_INT(0, simulith_shared_barrier_create(&owner));
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_complete(&owner, 1, 1));
    simulith_shared_barrier_interrupt(&owner);
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_receive(&owner, NULL, &time_ns, &phase));
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_receive(&owner, &sequence, NULL, &phase));
    TEST_ASSERT_EQUAL_INT(-1, simulith_shared_barrier_receive(&owner, &sequence, &time_ns, NULL));
}
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_create_failure_cleanup_and_restart);
    RUN_TEST(test_connect_failure_cleanup_and_recovery);
    RUN_TEST(test_profiled_fast_and_locked_paths_with_late_participant);
    RUN_TEST(test_unmapped_and_missing_output_arguments);
    return UNITY_END();
}
