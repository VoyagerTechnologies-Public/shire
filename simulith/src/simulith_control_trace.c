#include "simulith_control_trace.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TRACE_QUEUE_CAPACITY 64
#define TRACE_RECORD_CAPACITY 16384

typedef struct {
    uint32_t length;
    unsigned char bytes[TRACE_RECORD_CAPACITY];
} trace_record_t;

static struct {
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    pthread_t writer;
    FILE *file;
    char partial_path[512];
    char complete_path[512];
    trace_record_t queue[TRACE_QUEUE_CAPACITY];
    unsigned head, tail, count;
    int opened, stop, error, writer_started;
} trace = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .not_empty = PTHREAD_COND_INITIALIZER,
    .not_full = PTHREAD_COND_INITIALIZER,
};

static void put_u32(trace_record_t *r, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        r->bytes[r->length++] = (unsigned char)(value >> (8 * i));
}

static void put_u64(trace_record_t *r, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i)
        r->bytes[r->length++] = (unsigned char)(value >> (8 * i));
}

static void put_double(trace_record_t *r, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put_u64(r, bits);
}

static void put_doubles(trace_record_t *r, const double *values, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        put_double(r, values[i]);
}

static void encode_state(trace_record_t *r, const simulith_42_context_t *s)
{
    put_double(r, s->sim_time);
    put_double(r, s->dyn_time);
    put_doubles(r, s->qn, 4);
    put_doubles(r, s->wn, 3);
    put_doubles(r, s->pos_n, 3);
    put_doubles(r, s->vel_n, 3);
    put_doubles(r, s->pos_r, 3);
    put_doubles(r, s->vel_r, 3);
    put_doubles(r, s->sun_vector_body, 3);
    put_doubles(r, s->mag_field_body, 3);
    put_doubles(r, s->sun_vector_inertial, 3);
    put_doubles(r, s->mag_field_inertial, 3);
    put_doubles(r, s->hvb, 3);
    put_double(r, s->mass);
    put_doubles(r, s->cm, 3);
    put_doubles(r, &s->inertia[0][0], 9);
    put_u32(r, (uint32_t)s->eclipse);
    put_double(r, s->atmo_density);
    put_u32(r, (uint32_t)s->spacecraft_id);
    put_u32(r, (uint32_t)s->exists);
    memcpy(&r->bytes[r->length], s->label, sizeof(s->label));
    r->length += sizeof(s->label);
    put_u32(r, (uint32_t)s->valid);
}

static void encode_command(trace_record_t *r, const simulith_42_command_t *c)
{
    put_u32(r, (uint32_t)c->type);
    put_u32(r, (uint32_t)c->spacecraft_id);
    put_u32(r, (uint32_t)c->valid);
    /* Wall-clock enqueue timestamp is intentionally excluded: it does not
     * affect the actuator batch sent to 42 and differs across paced runs. */
    switch (c->type) {
    case SIMULITH_42_CMD_NONE:
    case SIMULITH_42_CMD_COUNT:
        break;
    case SIMULITH_42_CMD_MTB_TORQUE:
        put_u32(r, (uint32_t)c->cmd.mtb.enable_mask);
        put_doubles(r, c->cmd.mtb.dipole, 3);
        break;
    case SIMULITH_42_CMD_WHEEL_TORQUE:
        put_u32(r, (uint32_t)c->cmd.wheel.enable_mask);
        put_doubles(r, c->cmd.wheel.torque, 4);
        break;
    case SIMULITH_42_CMD_THRUSTER:
        put_u32(r, (uint32_t)c->cmd.thruster.enable_mask);
        put_doubles(r, c->cmd.thruster.thrust, 3);
        put_doubles(r, c->cmd.thruster.torque, 3);
        break;
    case SIMULITH_42_CMD_SET_MODE:
        put_u32(r, (uint32_t)c->cmd.setmode.mode);
        put_u32(r, (uint32_t)c->cmd.setmode.parm);
        put_u32(r, (uint32_t)c->cmd.setmode.frame);
        put_doubles(r, c->cmd.setmode.qrn, 4);
        put_doubles(r, c->cmd.setmode.priW, 3);
        put_doubles(r, c->cmd.setmode.secW, 3);
        put_u32(r, (uint32_t)c->cmd.setmode.have_pri);
        put_u32(r, (uint32_t)c->cmd.setmode.have_sec);
        put_u32(r, (uint32_t)c->cmd.setmode.have_qrn);
        break;
    default:
        break;
    }
}

static void *trace_writer(void *unused)
{
    (void)unused;
    trace_record_t local;
    for (;;) {
        pthread_mutex_lock(&trace.mutex);
        while (!trace.count && !trace.stop)
            pthread_cond_wait(&trace.not_empty, &trace.mutex);
        if (!trace.count) {
            pthread_mutex_unlock(&trace.mutex);
            return NULL;
        }
        trace_record_t *r = &trace.queue[trace.tail];
        uint32_t n = r->length;
        local.length = n;
        memcpy(local.bytes, r->bytes, n);
        trace.tail = (trace.tail + 1) % TRACE_QUEUE_CAPACITY;
        trace.count--;
        pthread_cond_signal(&trace.not_full);
        pthread_mutex_unlock(&trace.mutex);
        unsigned char header[4];
        for (unsigned i = 0; i < 4; ++i)
            header[i] = (unsigned char)(n >> (8 * i));
        int failed = fwrite(header, 1, 4, trace.file) != 4 ||
                     fwrite(local.bytes, 1, n, trace.file) != n;
        if (failed) {
            pthread_mutex_lock(&trace.mutex);
            trace.error = 1;
            pthread_cond_broadcast(&trace.not_full);
            pthread_mutex_unlock(&trace.mutex);
            return NULL;
        }
    }
}

int simulith_control_trace_open(const char *directory)
{
    if (!directory || !*directory)
        return 0;
    trace.head = trace.tail = trace.count = 0;
    trace.stop = trace.error = 0;
    if (snprintf(trace.partial_path, sizeof(trace.partial_path),
                 "%s/control-trace.v1.partial", directory) >=
        (int)sizeof(trace.partial_path) ||
        snprintf(trace.complete_path, sizeof(trace.complete_path),
                 "%s/control-trace.v1", directory) >=
        (int)sizeof(trace.complete_path))
        return -1;
    trace.file = fopen(trace.partial_path, "wb");
    if (!trace.file) {
        perror("control trace open");
        return -1;
    }
    static const unsigned char header[] = {'S','H','C','T',1,0,0,0};
    if (fwrite(header, 1, sizeof(header), trace.file) != sizeof(header)) {
        simulith_control_trace_abort();
        return -1;
    }
    trace.opened = 1;
    if (pthread_create(&trace.writer, NULL, trace_writer, NULL) != 0) {
        simulith_control_trace_abort();
        return -1;
    }
    trace.writer_started = 1;
    return 0;
}

int simulith_control_trace_tick(uint64_t sequence, uint64_t time_ns,
                                const simulith_42_context_t *state,
                                const simulith_42_command_t *commands,
                                uint32_t command_count)
{
    if (!trace.opened)
        return 0;
    if (command_count > SIMULITH_42_CMD_QUEUE_SIZE)
        return -1;
    trace_record_t r = {0};
    put_u64(&r, sequence);
    put_u64(&r, time_ns);
    encode_state(&r, state);
    put_u32(&r, command_count);
    for (uint32_t i = 0; i < command_count; ++i)
        encode_command(&r, &commands[i]);
    if (r.length > TRACE_RECORD_CAPACITY)
        return -1;
    pthread_mutex_lock(&trace.mutex);
    while (trace.count == TRACE_QUEUE_CAPACITY && !trace.error)
        pthread_cond_wait(&trace.not_full, &trace.mutex);
    if (trace.error) {
        pthread_mutex_unlock(&trace.mutex);
        return -1;
    }
    trace.queue[trace.head].length = r.length;
    memcpy(trace.queue[trace.head].bytes, r.bytes, r.length);
    trace.head = (trace.head + 1) % TRACE_QUEUE_CAPACITY;
    trace.count++;
    pthread_cond_signal(&trace.not_empty);
    pthread_mutex_unlock(&trace.mutex);
    return 0;
}

int simulith_control_trace_finish(void)
{
    if (!trace.opened)
        return 0;
    pthread_mutex_lock(&trace.mutex);
    trace.stop = 1;
    pthread_cond_signal(&trace.not_empty);
    pthread_mutex_unlock(&trace.mutex);
    pthread_join(trace.writer, NULL);
    trace.writer_started = 0;
    int failed = trace.error;
    if (fflush(trace.file) != 0)
        failed = 1;
    if (fsync(fileno(trace.file)) != 0)
        failed = 1;
    if (fclose(trace.file) != 0)
        failed = 1;
    trace.file = NULL;
    trace.opened = 0;
    if (!failed && rename(trace.partial_path, trace.complete_path) != 0)
        failed = 1;
    if (failed)
        fprintf(stderr, "Control trace write failed: %s\n", strerror(errno));
    return failed ? -1 : 0;
}

void simulith_control_trace_abort(void)
{
    if (trace.writer_started) {
        pthread_mutex_lock(&trace.mutex);
        trace.stop = 1;
        pthread_cond_signal(&trace.not_empty);
        pthread_mutex_unlock(&trace.mutex);
        pthread_join(trace.writer, NULL);
        trace.writer_started = 0;
    }
    if (trace.file) {
        fclose(trace.file);
        trace.file = NULL;
    }
    trace.opened = 0;
}
