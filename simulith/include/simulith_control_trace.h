#ifndef SIMULITH_CONTROL_TRACE_H
#define SIMULITH_CONTROL_TRACE_H

#include <stdint.h>
#include "simulith_42_context.h"
#include "simulith_42_commands.h"

/* Format v1: SHCT, little-endian version (u32), followed by ordered
 * length-prefixed tick records. Every floating value is its IEEE-754 bits. */
#define SIMULITH_CONTROL_TRACE_VERSION 1
int simulith_control_trace_open(const char *directory);
int simulith_control_trace_tick(uint64_t sequence, uint64_t time_ns,
                               const simulith_42_context_t *state,
                               const simulith_42_command_t *commands,
                               uint32_t command_count);
int simulith_control_trace_finish(void);
void simulith_control_trace_abort(void);

#endif
