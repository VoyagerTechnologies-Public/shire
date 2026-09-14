// Shared 42 command queue API for simulith components
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include "simulith_42_commands.h"

// Command queue stored in library so components and director can share it
static simulith_42_cmd_queue_t g_command_queue = {0};
static simulith_42_cmd_queue_stats_t g_queue_stats = {0};
static pthread_mutex_t g_command_queue_mutex = PTHREAD_MUTEX_INITIALIZER;

static int command_has_nonzero_actuation(const simulith_42_command_t *cmd)
{
    if (cmd->type == SIMULITH_42_CMD_MTB_TORQUE)
    {
        for (int i = 0; i < 3; ++i)
            if ((cmd->cmd.mtb.enable_mask & (1 << i)) != 0 &&
                fabs(cmd->cmd.mtb.dipole[i]) > 0.0)
                return 1;
    }
    else if (cmd->type == SIMULITH_42_CMD_WHEEL_TORQUE)
    {
        for (int i = 0; i < 4; ++i)
            if ((cmd->cmd.wheel.enable_mask & (1 << i)) != 0 &&
                fabs(cmd->cmd.wheel.torque[i]) > 0.0)
                return 1;
    }
    else if (cmd->type == SIMULITH_42_CMD_THRUSTER)
    {
        for (int i = 0; i < 3; ++i)
            if ((cmd->cmd.thruster.enable_mask & (1 << i)) != 0 &&
                (fabs(cmd->cmd.thruster.thrust[i]) > 0.0 ||
                 fabs(cmd->cmd.thruster.torque[i]) > 0.0))
                return 1;
    }
    return 0;
}

/* Internal helper: enqueue set_mode with optional extra payload. Declared
    here so the public shim can forward to it without implicit declaration. */
static int simulith_42_send_set_mode_with_extra(int spacecraft_id, int mode, const void* extra);

int enqueue_command(const simulith_42_command_t* cmd)
{
    if (!cmd) {
        fprintf(stderr, "simulith: enqueue_command FAILED - cmd=NULL\n");
        return -1;
    }
    pthread_mutex_lock(&g_command_queue_mutex);
    if (g_command_queue.count >= SIMULITH_42_CMD_QUEUE_SIZE) {
        g_queue_stats.overflows++;
        pthread_mutex_unlock(&g_command_queue_mutex);
        fprintf(stderr, "simulith: enqueue_command FAILED - queue full (type=%d sc=%d)\n", cmd->type, cmd->spacecraft_id);
        return -1;
    }

    g_command_queue.commands[g_command_queue.head] = *cmd;
    g_command_queue.head = (g_command_queue.head + 1) % SIMULITH_42_CMD_QUEUE_SIZE;
    g_command_queue.count++;
    g_queue_stats.enqueued++;
    if (cmd->type > SIMULITH_42_CMD_NONE &&
        cmd->type < SIMULITH_42_CMD_COUNT)
        g_queue_stats.by_type[cmd->type]++;
    if (command_has_nonzero_actuation(cmd))
        g_queue_stats.nonzero_actuator_commands++;
    if ((uint64_t)g_command_queue.count > g_queue_stats.high_watermark)
        g_queue_stats.high_watermark = (uint64_t)g_command_queue.count;
    pthread_mutex_unlock(&g_command_queue_mutex);

    if (cmd->type == SIMULITH_42_CMD_SET_MODE) {
        fprintf(stdout, "simulith: SET_MODE sc=%d mode=%d\n", cmd->spacecraft_id, cmd->cmd.setmode.mode);
    }
    return 0;
}

int dequeue_command(simulith_42_command_t* cmd)
{
    if (!cmd)
        return -1;
    pthread_mutex_lock(&g_command_queue_mutex);
    if (g_command_queue.count == 0) {
        pthread_mutex_unlock(&g_command_queue_mutex);
        return -1;
    }
    *cmd = g_command_queue.commands[g_command_queue.tail];
    g_command_queue.tail = (g_command_queue.tail + 1) % SIMULITH_42_CMD_QUEUE_SIZE;
    g_command_queue.count--;
    g_queue_stats.dequeued++;
    pthread_mutex_unlock(&g_command_queue_mutex);
    return 0;
}

void simulith_42_get_command_queue_stats(simulith_42_cmd_queue_stats_t *stats)
{
    if (!stats)
        return;
    pthread_mutex_lock(&g_command_queue_mutex);
    *stats = g_queue_stats;
    pthread_mutex_unlock(&g_command_queue_mutex);
}

// Public helpers that enqueue typed commands
int simulith_42_send_mtb_command(int spacecraft_id, const double dipole[3], int enable_mask)
{
    if (!dipole || spacecraft_id < 0)
        return -1;
    simulith_42_command_t cmd = {0};
    cmd.type = SIMULITH_42_CMD_MTB_TORQUE;
    cmd.spacecraft_id = spacecraft_id;
    cmd.valid = 1;
    for (int i = 0; i < 3; i++) cmd.cmd.mtb.dipole[i] = dipole[i];
    cmd.cmd.mtb.enable_mask = enable_mask;
    return enqueue_command(&cmd);
}

int simulith_42_send_wheel_command(int spacecraft_id, const double torque[4], int enable_mask)
{
    if (!torque || spacecraft_id < 0)
        return -1;
    simulith_42_command_t cmd = {0};
    cmd.type = SIMULITH_42_CMD_WHEEL_TORQUE;
    cmd.spacecraft_id = spacecraft_id;
    cmd.valid = 1;
    for (int i = 0; i < 4; i++) cmd.cmd.wheel.torque[i] = torque[i];
    cmd.cmd.wheel.enable_mask = enable_mask;
    return enqueue_command(&cmd);
}

int simulith_42_send_thruster_command(int spacecraft_id, const double thrust[3], const double torque[3], int enable_mask)
{
    if (!thrust || !torque || spacecraft_id < 0)
        return -1;
    simulith_42_command_t cmd = {0};
    cmd.type = SIMULITH_42_CMD_THRUSTER;
    cmd.spacecraft_id = spacecraft_id;
    cmd.valid = 1;
    for (int i = 0; i < 3; i++) {
        cmd.cmd.thruster.thrust[i] = thrust[i];
        cmd.cmd.thruster.torque[i] = torque[i];
    }
    cmd.cmd.thruster.enable_mask = enable_mask;
    return enqueue_command(&cmd);
}

int simulith_42_send_set_mode(int spacecraft_id, int mode, const void* extra)
{
    /* Forward to the richer helper that accepts an optional extra payload.
       This keeps the public API consistent with the header. */
    return simulith_42_send_set_mode_with_extra(spacecraft_id, mode, extra);
}

// New helper with optional extra payload (caller supplies a pointer to a
// simulith_42_command_t->cmd.setmode-compatible struct or NULL)
static int simulith_42_send_set_mode_with_extra(int spacecraft_id, int mode, const void* extra)
{
    if (spacecraft_id < 0)
        return -1;
    simulith_42_command_t cmd = {0};
    cmd.type = SIMULITH_42_CMD_SET_MODE;
    cmd.spacecraft_id = spacecraft_id;
    cmd.valid = 1;
    // Initialize defaults
    cmd.cmd.setmode.mode = mode;
    cmd.cmd.setmode.parm = 0;
    cmd.cmd.setmode.frame = 0;
    cmd.cmd.setmode.have_pri = 0;
    cmd.cmd.setmode.have_sec = 0;
    cmd.cmd.setmode.have_qrn = 0;
    if (extra) {
        // Copy user-provided bytes into the setmode slot. Caller must ensure
        // the layout matches the setmode struct in the header.
        memcpy(&cmd.cmd.setmode, extra, sizeof(cmd.cmd.setmode));
        // Ensure mode gets set/overridden by explicit extra if provided
        cmd.cmd.setmode.mode = mode;
        cmd.cmd.setmode.have_pri = cmd.cmd.setmode.have_pri ? 1 : 0;
        cmd.cmd.setmode.have_sec = cmd.cmd.setmode.have_sec ? 1 : 0;
        cmd.cmd.setmode.have_qrn = cmd.cmd.setmode.have_qrn ? 1 : 0;
    }
    return enqueue_command(&cmd);
}
