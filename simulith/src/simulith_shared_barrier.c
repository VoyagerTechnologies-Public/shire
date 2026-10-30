#include "simulith_shared_barrier.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum {PROFILE_PUBLISH, PROFILE_RECEIVE, PROFILE_COMPLETE, PROFILE_WAIT};

static uint64_t barrier_now_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static void barrier_profile_lock(simulith_shared_barrier_t *barrier, unsigned operation)
{
    uint64_t started = barrier->profile_enabled ? barrier_now_ns() : 0;
    pthread_mutex_lock(&barrier->state->mutex);
    if (barrier->profile_enabled) {
        __atomic_add_fetch(&barrier->profile_lock_count[operation], 1,
                           __ATOMIC_RELAXED);
        __atomic_add_fetch(&barrier->profile_lock_wait_ns[operation],
                           barrier_now_ns() - started, __ATOMIC_RELAXED);
    }
}

static int map_barrier(simulith_shared_barrier_t *barrier, int fd)
{
    void *mapping = mmap(NULL, sizeof(*barrier->state), PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED)
        return -1;
    barrier->fd = fd;
    barrier->state = mapping;
    return 0;
}

int simulith_shared_barrier_create(simulith_shared_barrier_t *barrier)
{
    memset(barrier, 0, sizeof(*barrier));
    barrier->fd = -1;
    barrier->profile_enabled = getenv("SHIRE_BARRIER_PROFILE") != NULL &&
        strcmp(getenv("SHIRE_BARRIER_PROFILE"), "1") == 0;
    /* A crashed prior server can leave the pathname behind. Remove it before
     * creating a new object so clients cannot map an old initialized barrier
     * while this server rebuilds it in place. */
    (void)unlink(SIMULITH_SHARED_BARRIER_PATH);
    int fd = open(SIMULITH_SHARED_BARRIER_PATH, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0 || ftruncate(fd, (off_t)sizeof(simulith_shared_barrier_state_t)) != 0)
    {
        if (fd >= 0) close(fd);
        (void)unlink(SIMULITH_SHARED_BARRIER_PATH);
        return -1;
    }
    if (map_barrier(barrier, fd) != 0)
    {
        close(fd);
        (void)unlink(SIMULITH_SHARED_BARRIER_PATH);
        return -1;
    }
    barrier->owner = 1;
    barrier->slot = -1;
    memset(barrier->state, 0, sizeof(*barrier->state));

    pthread_mutexattr_t mutex_attr;
    pthread_condattr_t condition_attr;
    if (pthread_mutexattr_init(&mutex_attr) != 0 ||
        pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_condattr_init(&condition_attr) != 0 ||
        pthread_condattr_setpshared(&condition_attr, PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_condattr_setclock(&condition_attr, CLOCK_MONOTONIC) != 0 ||
        pthread_mutex_init(&barrier->state->mutex, &mutex_attr) != 0 ||
        pthread_cond_init(&barrier->state->completion_condition, &condition_attr) != 0)
    {
        simulith_shared_barrier_close(barrier);
        return -1;
    }
    for (int slot = 0; slot < SIMULITH_SHARED_BARRIER_SLOTS; ++slot)
    {
        if (pthread_cond_init(&barrier->state->phase_condition[slot], &condition_attr) != 0)
        {
            simulith_shared_barrier_close(barrier);
            return -1;
        }
    }
    pthread_mutexattr_destroy(&mutex_attr);
    pthread_condattr_destroy(&condition_attr);
    barrier->state->version = SIMULITH_SHARED_BARRIER_VERSION;
    /* Publish readiness only after every process-shared primitive exists. */
    __atomic_store_n(&barrier->state->magic, SIMULITH_SHARED_BARRIER_MAGIC,
                     __ATOMIC_RELEASE);
    return 0;
}

int simulith_shared_barrier_connect(simulith_shared_barrier_t *barrier, int slot)
{
    memset(barrier, 0, sizeof(*barrier));
    barrier->fd = -1;
    barrier->profile_enabled = getenv("SHIRE_BARRIER_PROFILE") != NULL &&
        strcmp(getenv("SHIRE_BARRIER_PROFILE"), "1") == 0;
    if (slot < 0 || slot >= SIMULITH_SHARED_BARRIER_SLOTS)
        return -1;
    int fd = -1;
    for (int attempt = 0; attempt < 100 && fd < 0; ++attempt)
    {
        fd = open(SIMULITH_SHARED_BARRIER_PATH, O_RDWR);
        if (fd < 0)
        {
            struct timespec delay = {.tv_nsec = 10000000L};
            nanosleep(&delay, NULL);
        }
    }
    if (fd < 0)
        return -1;

    int sized = 0;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        struct stat details;
        if (fstat(fd, &details) == 0 &&
            details.st_size >= (off_t)sizeof(simulith_shared_barrier_state_t))
        {
            sized = 1;
            break;
        }
        struct timespec delay = {.tv_nsec = 10000000L};
        nanosleep(&delay, NULL);
    }
    if (!sized || map_barrier(barrier, fd) != 0)
    {
        close(fd);
        return -1;
    }
    barrier->slot = slot;
    int ready = 0;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (__atomic_load_n(&barrier->state->magic, __ATOMIC_ACQUIRE) ==
                SIMULITH_SHARED_BARRIER_MAGIC &&
            barrier->state->version == SIMULITH_SHARED_BARRIER_VERSION)
        {
            ready = 1;
            break;
        }
        struct timespec delay = {.tv_nsec = 10000000L};
        nanosleep(&delay, NULL);
    }
    if (!ready)
    {
        simulith_shared_barrier_close(barrier);
        return -1;
    }
    pthread_mutex_lock(&barrier->state->mutex);
    uint64_t generation = __atomic_load_n(&barrier->state->generation,
                                          __ATOMIC_ACQUIRE);
    barrier->last_generation = generation > 0 ? generation - 1 : 0;
    pthread_mutex_unlock(&barrier->state->mutex);
    return 0;
}

void simulith_shared_barrier_close(simulith_shared_barrier_t *barrier)
{
    if (!barrier) return;
    if (barrier->profile_enabled) {
        static const char *names[] = {"publish", "receive", "complete", "wait"};
        printf("SIMULITH_BARRIER_TIMING {\"owner\":%d,\"operations\":[",
               barrier->owner);
        for (unsigned i = 0; i < 4; ++i)
            printf("%s{\"name\":\"%s\",\"locks\":%lu,\"mean_lock_us\":%.3f}",
                   i ? "," : "", names[i],
                   (unsigned long)barrier->profile_lock_count[i],
                   barrier->profile_lock_count[i] ?
                       (double)barrier->profile_lock_wait_ns[i] /
                       (double)barrier->profile_lock_count[i] / 1000.0 : 0.0);
        printf("],\"fast_receive\":%lu,\"fast_wait\":%lu}\n",
               (unsigned long)barrier->profile_fast_receive,
               (unsigned long)barrier->profile_fast_wait);
        fflush(stdout);
    }
    if (barrier->state)
        munmap(barrier->state, sizeof(*barrier->state));
    if (barrier->fd >= 0)
        close(barrier->fd);
    if (barrier->owner)
        unlink(SIMULITH_SHARED_BARRIER_PATH);
    memset(barrier, 0, sizeof(*barrier));
    barrier->fd = -1;
}

int simulith_shared_barrier_publish(simulith_shared_barrier_t *barrier,
                                    uint64_t sequence, uint64_t time_ns, uint32_t phase,
                                    uint32_t participant_mask)
{
    if (!barrier || !barrier->state) return -1;
    barrier_profile_lock(barrier, PROFILE_PUBLISH);
    barrier->state->sequence = sequence;
    barrier->state->time_ns = time_ns;
    barrier->state->phase = phase;
    barrier->state->participant_mask = participant_mask;
    __atomic_store_n(&barrier->state->completion_mask, 0, __ATOMIC_RELEASE);
    __atomic_add_fetch(&barrier->state->generation, 1, __ATOMIC_RELEASE);
    for (int slot = 0; slot < SIMULITH_SHARED_BARRIER_SLOTS; ++slot)
        if ((participant_mask & (UINT32_C(1) << slot)) != 0)
            pthread_cond_signal(&barrier->state->phase_condition[slot]);
    pthread_mutex_unlock(&barrier->state->mutex);
    return 0;
}

int simulith_shared_barrier_receive(simulith_shared_barrier_t *barrier,
                                    uint64_t *sequence, uint64_t *time_ns, uint32_t *phase)
{
    if (!barrier || !barrier->state || !sequence || !time_ns || !phase) return -1;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t spin_until = (uint64_t)now.tv_sec * UINT64_C(1000000000) +
                          (uint64_t)now.tv_nsec + UINT64_C(80000);
    while (__atomic_load_n(&barrier->state->generation, __ATOMIC_ACQUIRE) ==
               barrier->last_generation &&
           !__atomic_load_n(&barrier->interrupted, __ATOMIC_ACQUIRE))
    {
        clock_gettime(CLOCK_MONOTONIC, &now);
        if ((uint64_t)now.tv_sec * UINT64_C(1000000000) +
            (uint64_t)now.tv_nsec >= spin_until)
            break;
#if defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();
#endif
    }
    /* A participant's phase stays published until it completes. The release
     * generation publication makes its payload visible on this common path. */
    uint64_t published = __atomic_load_n(&barrier->state->generation,
                                         __ATOMIC_ACQUIRE);
    if (barrier->fast_receive_safe && published != barrier->last_generation &&
        !__atomic_load_n(&barrier->interrupted, __ATOMIC_ACQUIRE))
    {
        if (barrier->profile_enabled)
            __atomic_add_fetch(&barrier->profile_fast_receive, 1,
                               __ATOMIC_RELAXED);
        *sequence = barrier->state->sequence;
        *time_ns = barrier->state->time_ns;
        *phase = barrier->state->phase;
        barrier->last_generation = published;
        return 0;
    }
    barrier_profile_lock(barrier, PROFILE_RECEIVE);
    while (!barrier->state->stopping &&
           __atomic_load_n(&barrier->state->generation, __ATOMIC_ACQUIRE) ==
               barrier->last_generation &&
           !__atomic_load_n(&barrier->interrupted, __ATOMIC_ACQUIRE))
        pthread_cond_wait(&barrier->state->phase_condition[barrier->slot],
                          &barrier->state->mutex);
    if (__atomic_load_n(&barrier->interrupted, __ATOMIC_ACQUIRE))
    {
        pthread_mutex_unlock(&barrier->state->mutex);
        return -1;
    }
    if (barrier->state->stopping)
    {
        pthread_mutex_unlock(&barrier->state->mutex);
        return 1;
    }
    barrier->last_generation = __atomic_load_n(&barrier->state->generation,
                                               __ATOMIC_ACQUIRE);
    *sequence = barrier->state->sequence;
    *time_ns = barrier->state->time_ns;
    *phase = barrier->state->phase;
    pthread_mutex_unlock(&barrier->state->mutex);
    return 0;
}

int simulith_shared_barrier_complete(simulith_shared_barrier_t *barrier,
                                     uint64_t sequence, uint32_t phase)
{
    if (!barrier || !barrier->state || barrier->slot < 0) return -1;
    barrier_profile_lock(barrier, PROFILE_COMPLETE);
    uint32_t slot_bit = UINT32_C(1) << barrier->slot;
    if (barrier->state->stopping ||
        (barrier->state->participant_mask & slot_bit) == 0 ||
        barrier->state->sequence != sequence || barrier->state->phase != phase ||
        (__atomic_load_n(&barrier->state->completion_mask, __ATOMIC_ACQUIRE) & slot_bit) != 0)
    {
        pthread_mutex_unlock(&barrier->state->mutex);
        return -1;
    }
    __atomic_fetch_or(&barrier->state->completion_mask, slot_bit, __ATOMIC_RELEASE);
    pthread_cond_signal(&barrier->state->completion_condition);
    pthread_mutex_unlock(&barrier->state->mutex);
    return 0;
}

uint32_t simulith_shared_barrier_wait(simulith_shared_barrier_t *barrier,
                                      uint32_t required_mask, int timeout_ms)
{
    if (!barrier || !barrier->state) return 0;
    /* The server normally receives the required completions within one tick.
     * A bounded spin avoids a process wakeup on that short critical path. */
    if (timeout_ms > 0)
    {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t spin_until = (uint64_t)now.tv_sec * UINT64_C(1000000000) +
                              (uint64_t)now.tv_nsec + UINT64_C(80000);
        while ((__atomic_load_n(&barrier->state->completion_mask,
                                __ATOMIC_ACQUIRE) & required_mask) != required_mask)
        {
            clock_gettime(CLOCK_MONOTONIC, &now);
            if ((uint64_t)now.tv_sec * UINT64_C(1000000000) +
                (uint64_t)now.tv_nsec >= spin_until)
                break;
#if defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
#endif
        }
    }
    uint32_t ready = __atomic_load_n(&barrier->state->completion_mask,
                                     __ATOMIC_ACQUIRE);
    if ((ready & required_mask) == required_mask)
    {
        if (barrier->profile_enabled)
            __atomic_add_fetch(&barrier->profile_fast_wait, 1,
                               __ATOMIC_RELAXED);
        return ready;
    }
    barrier_profile_lock(barrier, PROFILE_WAIT);
    if ((__atomic_load_n(&barrier->state->completion_mask,
                         __ATOMIC_ACQUIRE) & required_mask) != required_mask &&
        !barrier->state->stopping)
    {
        struct timespec deadline;
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        deadline.tv_nsec += (long)timeout_ms * 1000000L;
        deadline.tv_sec += deadline.tv_nsec / 1000000000L;
        deadline.tv_nsec %= 1000000000L;
        (void)pthread_cond_timedwait(&barrier->state->completion_condition,
                                     &barrier->state->mutex, &deadline);
    }
    uint32_t completed = __atomic_load_n(&barrier->state->completion_mask,
                                         __ATOMIC_ACQUIRE);
    pthread_mutex_unlock(&barrier->state->mutex);
    return completed;
}

void simulith_shared_barrier_stop(simulith_shared_barrier_t *barrier)
{
    if (!barrier || !barrier->state) return;
    pthread_mutex_lock(&barrier->state->mutex);
    barrier->state->stopping = 1;
    pthread_cond_broadcast(&barrier->state->completion_condition);
    for (int slot = 0; slot < SIMULITH_SHARED_BARRIER_SLOTS; ++slot)
        pthread_cond_broadcast(&barrier->state->phase_condition[slot]);
    pthread_mutex_unlock(&barrier->state->mutex);
}

void simulith_shared_barrier_interrupt(simulith_shared_barrier_t *barrier)
{
    if (!barrier || !barrier->state || barrier->slot < 0) return;
    __atomic_store_n(&barrier->interrupted, 1, __ATOMIC_RELEASE);
    pthread_mutex_lock(&barrier->state->mutex);
    pthread_cond_broadcast(&barrier->state->phase_condition[barrier->slot]);
    pthread_mutex_unlock(&barrier->state->mutex);
}
