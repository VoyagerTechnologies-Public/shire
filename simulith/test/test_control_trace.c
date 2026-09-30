#include "simulith_control_trace.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned long long read_u64(const unsigned char *bytes)
{
    unsigned long long value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= (unsigned long long)bytes[i] << (8 * i);
    return value;
}

int main(void)
{
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
