#include "simulith_control_trace.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

static unsigned long long read_u64(const unsigned char *bytes)
{
    unsigned long long value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= (unsigned long long)bytes[i] << (8 * i);
    return value;
}


/* Fail actual I/O operations without requiring a full disk or thread exhaustion.
 * The flags are atomic because fwrite runs on the trace writer thread. */
static atomic_int write_countdown;
static int fail_thread, fail_flush, fail_sync, fail_close, fail_rename;

size_t __real_fwrite(const void *, size_t, size_t, FILE *);
size_t __wrap_fwrite(const void *ptr, size_t size, size_t count, FILE *file)
{
    int remaining = atomic_load(&write_countdown);
    if (remaining > 0 && atomic_fetch_sub(&write_countdown, 1) == 1) {
        errno = ENOSPC;
        return 0;
    }
    return __real_fwrite(ptr, size, count, file);
}
int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start)(void *), void *arg)
{
    if (fail_thread) {
        fail_thread = 0;
        return EAGAIN;
    }
    return __real_pthread_create(thread, attr, start, arg);
}
int __real_fflush(FILE *);
int __wrap_fflush(FILE *file)
{
    if (fail_flush) { fail_flush = 0; errno = EIO; return -1; }
    return __real_fflush(file);
}
int __real_fsync(int);
int __wrap_fsync(int fd)
{
    if (fail_sync) { fail_sync = 0; errno = EIO; return -1; }
    return __real_fsync(fd);
}
int __real_fclose(FILE *);
int __wrap_fclose(FILE *file)
{
    int result = __real_fclose(file);
    if (fail_close) { fail_close = 0; errno = EIO; return -1; }
    return result;
}
int __real_rename(const char *, const char *);
int __wrap_rename(const char *from, const char *to)
{
    if (fail_rename) { fail_rename = 0; errno = EACCES; return -1; }
    return __real_rename(from, to);
}

static uint32_t read_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}
static void expect_u32(const unsigned char **cursor, uint32_t value)
{
    assert(read_u32(*cursor) == value);
    *cursor += 4;
}
static void expect_double(const unsigned char **cursor, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    assert(read_u64(*cursor) == bits);
    *cursor += 8;
}
static void expect_vector(const unsigned char **cursor, const double *values, size_t count)
{
    for (size_t i = 0; i < count; i++) expect_double(cursor, values[i]);
}

static void test_trace_payload_and_fifo(void)
{
    char directory[] = "/tmp/shire-trace-payload-XXXXXX", path[600];
    assert(mkdtemp(directory));
    simulith_42_context_t state = {
        .valid = 1, .sim_time = 12.5, .dyn_time = 748177200.01,
        .qn = {1, 2, 3, 4}, .wn = {-1, -2, -3},
        .pos_n = {5, 6, 7}, .vel_n = {8, 9, 10},
        .pos_r = {11, 12, 13}, .vel_r = {14, 15, 16},
        .sun_vector_body = {17, 18, 19}, .mag_field_body = {20, 21, 22},
        .sun_vector_inertial = {23, 24, 25}, .mag_field_inertial = {26, 27, 28},
        .hvb = {29, 30, 31}, .mass = 32, .cm = {33, 34, 35},
        .inertia = {{36, 37, 38}, {39, 40, 41}, {42, 43, 44}},
        .eclipse = 1, .atmo_density = 1.25e-12, .spacecraft_id = 2,
        .exists = 1, .label = "payload-fixture",
    };
    simulith_42_command_t commands[] = {
        {.type = SIMULITH_42_CMD_NONE},
        {.type = SIMULITH_42_CMD_MTB_TORQUE, .spacecraft_id = 2, .valid = 1,
         .cmd.mtb = {.enable_mask = 5, .dipole = {0.125, -0.25, 0.5}}},
        {.type = SIMULITH_42_CMD_WHEEL_TORQUE, .spacecraft_id = 3, .valid = 1,
         .cmd.wheel = {.enable_mask = 15, .torque = {1, -2, 3, -4}}},
        {.type = SIMULITH_42_CMD_THRUSTER, .spacecraft_id = 4, .valid = 1,
         .cmd.thruster = {.enable_mask = 7, .thrust = {5, 6, 7}, .torque = {8, 9, 10}}},
        {.type = SIMULITH_42_CMD_SET_MODE, .spacecraft_id = 5, .valid = 1,
         .cmd.setmode = {.mode = 3, .parm = 2, .frame = 1,
                        .qrn = {0.5, -0.5, 0.5, -0.5}, .priW = {1, 2, 3},
                        .secW = {-4, -5, -6}, .have_pri = 1, .have_sec = 1, .have_qrn = 1}},
        {.type = SIMULITH_42_CMD_COUNT},
        {.type = (simulith_42_cmd_type_t)99},
    };
    const uint32_t count = sizeof(commands) / sizeof(commands[0]);
    assert(simulith_control_trace_open(directory) == 0);
    /* Exceed the writer queue capacity and check every record, including wraparound. */
    for (uint64_t tick = 0; tick < 256; tick++) {
        for (uint32_t i = 0; i < count; i++) commands[i].timestamp_ns = tick;
        assert(simulith_control_trace_tick(tick, tick * 10000000, &state, commands, count) == 0);
    }
    assert(simulith_control_trace_finish() == 0);
    snprintf(path, sizeof(path), "%s/control-trace.v1", directory);
    FILE *file = fopen(path, "rb");
    assert(file);
    unsigned char header[8], record[16384], first_payload[16384];
    assert(fread(header, 1, 8, file) == 8);
    assert(memcmp(header, "SHCT\001\000\000\000", 8) == 0);
    uint32_t first_length = 0;
    for (uint64_t tick = 0; tick < 256; tick++) {
        assert(fread(header, 1, 4, file) == 4);
        uint32_t length = read_u32(header);
        assert(length <= sizeof(record));
        assert(fread(record, 1, length, file) == length);
        const unsigned char *cursor = record;
        assert(read_u64(cursor) == tick); cursor += 8;
        assert(read_u64(cursor) == tick * 10000000); cursor += 8;
        expect_double(&cursor, state.sim_time); expect_double(&cursor, state.dyn_time);
        expect_vector(&cursor, state.qn, 4); expect_vector(&cursor, state.wn, 3);
        expect_vector(&cursor, state.pos_n, 3); expect_vector(&cursor, state.vel_n, 3);
        expect_vector(&cursor, state.pos_r, 3); expect_vector(&cursor, state.vel_r, 3);
        expect_vector(&cursor, state.sun_vector_body, 3);
        expect_vector(&cursor, state.mag_field_body, 3);
        expect_vector(&cursor, state.sun_vector_inertial, 3);
        expect_vector(&cursor, state.mag_field_inertial, 3);
        expect_vector(&cursor, state.hvb, 3); expect_double(&cursor, state.mass);
        expect_vector(&cursor, state.cm, 3); expect_vector(&cursor, &state.inertia[0][0], 9);
        expect_u32(&cursor, 1); expect_double(&cursor, state.atmo_density);
        expect_u32(&cursor, 2); expect_u32(&cursor, 1);
        assert(memcmp(cursor, state.label, sizeof(state.label)) == 0); cursor += sizeof(state.label);
        expect_u32(&cursor, 1); expect_u32(&cursor, count);
        for (uint32_t i = 0; i < count; i++) {
            const simulith_42_command_t *cmd = &commands[i];
            expect_u32(&cursor, cmd->type); expect_u32(&cursor, cmd->spacecraft_id);
            expect_u32(&cursor, cmd->valid);
            if (i == 1) { expect_u32(&cursor, 5); expect_vector(&cursor, cmd->cmd.mtb.dipole, 3); }
            if (i == 2) { expect_u32(&cursor, 15); expect_vector(&cursor, cmd->cmd.wheel.torque, 4); }
            if (i == 3) {
                expect_u32(&cursor, 7); expect_vector(&cursor, cmd->cmd.thruster.thrust, 3);
                expect_vector(&cursor, cmd->cmd.thruster.torque, 3);
            }
            if (i == 4) {
                expect_u32(&cursor, 3); expect_u32(&cursor, 2); expect_u32(&cursor, 1);
                expect_vector(&cursor, cmd->cmd.setmode.qrn, 4);
                expect_vector(&cursor, cmd->cmd.setmode.priW, 3);
                expect_vector(&cursor, cmd->cmd.setmode.secW, 3);
                expect_u32(&cursor, 1); expect_u32(&cursor, 1); expect_u32(&cursor, 1);
            }
        }
        assert(cursor == record + length);
        /* Wall-clock enqueue timestamps must not affect replay identity. */
        if (tick == 0) { memcpy(first_payload, record + 16, length - 16); first_length = length; }
        else { assert(length == first_length); assert(memcmp(first_payload, record + 16, length - 16) == 0); }
    }
    assert(fgetc(file) == EOF);
    fclose(file);
    unlink(path); rmdir(directory);
}

static void test_trace_failure_recovery(void)
{
    simulith_42_context_t state = {0};
    assert(simulith_control_trace_open(NULL) == 0);
    assert(simulith_control_trace_open("") == 0);
    assert(simulith_control_trace_tick(0, 0, NULL, NULL, UINT32_MAX) == 0);
    assert(simulith_control_trace_finish() == 0);
    simulith_control_trace_abort();
    char too_long[600];
    memset(too_long, 'x', sizeof(too_long)); too_long[sizeof(too_long) - 1] = 0;
    assert(simulith_control_trace_open(too_long) == -1);
    char directory[] = "/tmp/shire-trace-failures-XXXXXX", path[600];
    assert(mkdtemp(directory));
    /* An existing regular file cannot be used as the output directory. */
    snprintf(path, sizeof(path), "%s/file", directory);
    FILE *file = fopen(path, "wb"); assert(file); fclose(file);
    assert(simulith_control_trace_open(path) == -1); unlink(path);
    snprintf(path, sizeof(path), "%s/control-trace.v1.partial", directory);
    for (int failure = 0; failure < 8; failure++) {
        if (failure == 0) atomic_store(&write_countdown, 1); /* file header */
        if (failure == 1) fail_thread = 1;
        int result = simulith_control_trace_open(directory);
        if (failure < 2) { assert(result == -1); simulith_control_trace_abort(); }
        else {
            assert(result == 0);
            assert(simulith_control_trace_tick(0, 0, &state, NULL,
                                              SIMULITH_42_CMD_QUEUE_SIZE + 1) == -1);
            if (failure == 2 || failure == 3) {
                atomic_store(&write_countdown, failure == 2 ? 1 : 2); /* record header / body */
                int rejected = 0;
                for (unsigned i = 0; i < 1000 && !rejected; i++) {
                    rejected = simulith_control_trace_tick(i, i, &state, NULL, 0) == -1;
                    if (!rejected) nanosleep(&(struct timespec){.tv_nsec = 1000000}, NULL);
                }
                assert(rejected); /* producer must observe and stop on writer failure */
            } else {
                assert(simulith_control_trace_tick(0, 0, &state, NULL, 0) == 0);
                if (failure == 4) fail_flush = 1;
                if (failure == 5) fail_sync = 1;
                if (failure == 6) fail_close = 1;
                if (failure == 7) fail_rename = 1;
            }
            assert(simulith_control_trace_finish() == -1);
        }
        assert(access(path, F_OK) == 0); unlink(path);
        /* Every failure must release resources so the next trace can complete. */
        assert(simulith_control_trace_open(directory) == 0);
        assert(simulith_control_trace_tick(0, 0, &state, NULL, 0) == 0);
        assert(simulith_control_trace_finish() == 0);
    }
    snprintf(path, sizeof(path), "%s/control-trace.v1", directory);
    unlink(path); rmdir(directory);
}

int main(void)
{
    test_trace_payload_and_fifo();
    test_trace_failure_recovery();
    char directory[] = "/tmp/shire-control-trace-XXXXXX";
    assert(mkdtemp(directory) != NULL);
    simulith_42_context_t state = {0};
    state.valid = 1;
    state.dyn_time = 748177200.01;
    simulith_42_command_t command = {0};
    command.type = SIMULITH_42_CMD_WHEEL_TORQUE;
    command.valid = 1;
    command.cmd.wheel.enable_mask = 1;
    command.cmd.wheel.torque[0] = 0.25;
    assert(simulith_control_trace_open(directory) == 0);
    for (uint64_t sequence = 0; sequence < 256; ++sequence)
        assert(simulith_control_trace_tick(sequence, sequence * 10000000,
                                          &state, &command, 1) == 0);
    assert(simulith_control_trace_finish() == 0);

    char complete[600], partial[600];
    assert(snprintf(complete, sizeof(complete), "%s/control-trace.v1", directory) > 0);
    assert(snprintf(partial, sizeof(partial), "%s/control-trace.v1.partial", directory) > 0);
    assert(access(partial, F_OK) != 0);
    FILE *file = fopen(complete, "rb");
    assert(file != NULL);
    unsigned char header[40];
    assert(fread(header, 1, sizeof(header), file) == sizeof(header));
    assert(memcmp(header, "SHCT\001\000\000\000", 8) == 0);
    assert(read_u64(header + 12) == 0);
    assert(read_u64(header + 20) == 0);
    fclose(file);

    assert(simulith_control_trace_open(directory) == 0);
    assert(simulith_control_trace_tick(0, 0, &state, NULL, 0) == 0);
    simulith_control_trace_abort();
    assert(access(partial, F_OK) == 0);
    assert(unlink(partial) == 0);
    assert(unlink(complete) == 0);
    assert(rmdir(directory) == 0);

    /* A failed final flush must leave the partial diagnostic in place. */
    char failing_directory[] = "/tmp/shire-control-trace-full-XXXXXX";
    assert(mkdtemp(failing_directory) != NULL);
    assert(snprintf(partial, sizeof(partial), "%s/control-trace.v1.partial",
                    failing_directory) > 0);
    assert(symlink("/dev/full", partial) == 0);
    assert(simulith_control_trace_open(failing_directory) == 0);
    assert(simulith_control_trace_tick(0, 0, &state, NULL, 0) == 0);
    assert(simulith_control_trace_finish() == -1);
    assert(access(partial, F_OK) == 0);
    assert(unlink(partial) == 0);
    assert(rmdir(failing_directory) == 0);
    return 0;
}
