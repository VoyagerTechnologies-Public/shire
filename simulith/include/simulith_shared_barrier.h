#ifndef SIMULITH_SHARED_BARRIER_H
#define SIMULITH_SHARED_BARRIER_H

#include <pthread.h>
#include <stdint.h>

#define SIMULITH_SHARED_BARRIER_PATH "/tmp/simulith_barrier.v2"
#define SIMULITH_SHARED_BARRIER_MAGIC 0x53484252U
#define SIMULITH_SHARED_BARRIER_VERSION 2U
#define SIMULITH_SHARED_BARRIER_SLOTS 32

typedef struct
{
    uint32_t magic;
    uint32_t version;
    pthread_mutex_t mutex;
    pthread_cond_t phase_condition[SIMULITH_SHARED_BARRIER_SLOTS];
    pthread_cond_t completion_condition;
    uint64_t generation;
    uint64_t sequence;
    uint64_t time_ns;
    uint32_t phase;
    uint32_t participant_mask;
    uint32_t completion_mask;
    int stopping;
} simulith_shared_barrier_state_t;

typedef struct
{
    int fd;
    int owner;
    int slot;
    int interrupted;
    uint64_t last_generation;
    simulith_shared_barrier_state_t *state;
    int fast_receive_safe; /* caller participates in every published phase */
    int profile_enabled;
    uint64_t profile_lock_count[4];
    uint64_t profile_lock_wait_ns[4];
    uint64_t profile_fast_receive;
    uint64_t profile_fast_wait;
} simulith_shared_barrier_t;

int simulith_shared_barrier_create(simulith_shared_barrier_t *barrier);
int simulith_shared_barrier_connect(simulith_shared_barrier_t *barrier, int slot);
void simulith_shared_barrier_close(simulith_shared_barrier_t *barrier);
int simulith_shared_barrier_publish(simulith_shared_barrier_t *barrier,
                                    uint64_t sequence, uint64_t time_ns, uint32_t phase,
                                    uint32_t participant_mask);
int simulith_shared_barrier_receive(simulith_shared_barrier_t *barrier,
                                    uint64_t *sequence, uint64_t *time_ns, uint32_t *phase);
int simulith_shared_barrier_complete(simulith_shared_barrier_t *barrier,
                                     uint64_t sequence, uint32_t phase);
uint32_t simulith_shared_barrier_wait(simulith_shared_barrier_t *barrier,
                                      uint32_t required_mask, int timeout_ms);
void simulith_shared_barrier_stop(simulith_shared_barrier_t *barrier);
void simulith_shared_barrier_interrupt(simulith_shared_barrier_t *barrier);

#endif
