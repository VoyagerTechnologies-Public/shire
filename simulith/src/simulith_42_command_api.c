// Shared 42 command queue API for simulith components
#include <stdio.h>
#include <string.h>
#include "simulith_42_commands.h"

// Command queue stored in library so components and director can share it
static simulith_42_cmd_queue_t g_command_queue = {0};

/* Internal helper: enqueue set_mode with optional extra payload. Declared
    here so the public shim can forward to it without implicit declaration. */
static int simulith_42_send_set_mode_with_extra(int spacecraft_id, int mode, const void* extra);

int enqueue_command(const simulith_42_command_t* cmd)
{
    if (g_command_queue.count >= SIMULITH_42_CMD_QUEUE_SIZE) {
        if (cmd) {
            fprintf(stderr, "simulith: enqueue_command FAILED - queue full (type=%d sc=%d)\n", cmd->type, cmd->spacecraft_id);
        } else {
            fprintf(stderr, "simulith: enqueue_command FAILED - queue full (cmd=NULL)\n");
        }
        return -1;
    }

    g_command_queue.commands[g_command_queue.head] = *cmd;
    g_command_queue.head = (g_command_queue.head + 1) % SIMULITH_42_CMD_QUEUE_SIZE;
    g_command_queue.count++;

    if (cmd) {
        if (cmd->type == SIMULITH_42_CMD_SET_MODE) {
            fprintf(stdout, "simulith: SET_MODE sc=%d mode=%d\n", cmd->spacecraft_id, cmd->cmd.setmode.mode);
        }
    }
    return 0;
}

int dequeue_command(simulith_42_command_t* cmd)
{
    if (g_command_queue.count == 0) {
        return -1;
    }
    *cmd = g_command_queue.commands[g_command_queue.tail];
    g_command_queue.tail = (g_command_queue.tail + 1) % SIMULITH_42_CMD_QUEUE_SIZE;
    g_command_queue.count--;
    return 0;
}

// Public helpers that enqueue typed commands
int simulith_42_send_mtb_command(int spacecraft_id, const double dipole[3], int enable_mask)
{
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
