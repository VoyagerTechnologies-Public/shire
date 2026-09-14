#include "simulith_shared_barrier.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

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
    barrier->last_generation = barrier->state->generation > 0 ?
        barrier->state->generation - 1 : 0;
    pthread_mutex_unlock(&barrier->state->mutex);
    return 0;
}

void simulith_shared_barrier_close(simulith_shared_barrier_t *barrier)
{
    if (!barrier) return;
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
    pthread_mutex_lock(&barrier->state->mutex);
    barrier->state->sequence = sequence;
    barrier->state->time_ns = time_ns;
    barrier->state->phase = phase;
    barrier->state->participant_mask = participant_mask;
    barrier->state->completion_mask = 0;
    barrier->state->generation++;
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
    pthread_mutex_lock(&barrier->state->mutex);
    while (!barrier->state->stopping &&
           barrier->state->generation == barrier->last_generation &&
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
    barrier->last_generation = barrier->state->generation;
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
    pthread_mutex_lock(&barrier->state->mutex);
    uint32_t slot_bit = UINT32_C(1) << barrier->slot;
    if (barrier->state->stopping ||
        (barrier->state->participant_mask & slot_bit) == 0 ||
        barrier->state->sequence != sequence || barrier->state->phase != phase ||
        (barrier->state->completion_mask & slot_bit) != 0)
    {
        pthread_mutex_unlock(&barrier->state->mutex);
        return -1;
    }
    barrier->state->completion_mask |= slot_bit;
    pthread_cond_signal(&barrier->state->completion_condition);
    pthread_mutex_unlock(&barrier->state->mutex);
    return 0;
}

uint32_t simulith_shared_barrier_wait(simulith_shared_barrier_t *barrier,
                                      uint32_t required_mask, int timeout_ms)
{
    if (!barrier || !barrier->state) return 0;
    pthread_mutex_lock(&barrier->state->mutex);
    if ((barrier->state->completion_mask & required_mask) != required_mask &&
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
    uint32_t completed = barrier->state->completion_mask;
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
