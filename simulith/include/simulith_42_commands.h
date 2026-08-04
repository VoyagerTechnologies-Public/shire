#ifndef SIMULITH_42_COMMANDS_H
#define SIMULITH_42_COMMANDS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 42 Command Types
typedef enum {
    SIMULITH_42_CMD_NONE = 0,
    SIMULITH_42_CMD_MTB_TORQUE,      // Magnetic torquer bar command
    SIMULITH_42_CMD_WHEEL_TORQUE,    // Reaction wheel command
    SIMULITH_42_CMD_THRUSTER,        // Thruster command
    SIMULITH_42_CMD_SET_MODE,        // Set FSW/ADCS mode
    SIMULITH_42_CMD_COUNT
} simulith_42_cmd_type_t;

// MTB (Magnetic Torquer Bar) Command
typedef struct {
    double dipole[3];        // Magnetic dipole moment [A*m^2] in body frame
    int enable_mask;         // Bitmask for which MTBs to enable (bit 0=MTB0, bit 1=MTB1, etc.)
} simulith_42_mtb_cmd_t;

// Reaction Wheel Command
typedef struct {
    double torque[4];        // Wheel torque commands [N*m] (max 4 wheels)
    int enable_mask;         // Bitmask for which wheels to enable
} simulith_42_wheel_cmd_t;

// Thruster Command
typedef struct {
    double thrust[3];        // Thrust force [N] in body frame
    double torque[3];        // Thrust torque [N*m] in body frame
    int enable_mask;         // Bitmask for which thrusters to enable
} simulith_42_thruster_cmd_t;

// Generic 42 Command Structure
typedef struct {
    simulith_42_cmd_type_t type;
    uint64_t timestamp_ns;   // Command timestamp
    int spacecraft_id;       // Target spacecraft ID
    
    union {
        simulith_42_mtb_cmd_t mtb;
        simulith_42_wheel_cmd_t wheel;
        simulith_42_thruster_cmd_t thruster;
        // Extended SET_MODE payload allowing optional targets
        struct {
            int mode;           // mode value for SIMULITH_42_CMD_SET_MODE
            int parm;           // PARM_VECTORS or PARM_QUATERNION (optional)
            int frame;          // FRAME_N / FRAME_L etc. (optional)
            double qrn[4];      // quaternion target when parm==PARM_QUATERNION
            double priW[3];     // primary vector (when parm==PARM_VECTORS)
            double secW[3];     // secondary vector (when parm==PARM_VECTORS)
            int have_pri;       // 1 if priW is valid
            int have_sec;       // 1 if secW is valid
            int have_qrn;       // 1 if qrn is valid
        } setmode;
    } cmd;
    
    int valid;               // 1 if command is valid, 0 otherwise
} simulith_42_command_t;

// Command Queue (for buffering commands from multiple simulators)
#define SIMULITH_42_CMD_QUEUE_SIZE 64

typedef struct {
    simulith_42_command_t commands[SIMULITH_42_CMD_QUEUE_SIZE];
    int head;                // Next write position
    int tail;                // Next read position
    int count;               // Number of commands in queue
} simulith_42_cmd_queue_t;

// Function prototypes for command interface
int simulith_42_send_mtb_command(int spacecraft_id, const double dipole[3], int enable_mask);
int simulith_42_send_wheel_command(int spacecraft_id, const double torque[4], int enable_mask);
int simulith_42_send_thruster_command(int spacecraft_id, const double thrust[3], const double torque[3], int enable_mask);
// Set FSW/ADCS mode on a 42 spacecraft (producer-friendly helper)
// Set FSW/ADCS mode on a 42 spacecraft (producer-friendly helper)
// If `extra` is non-NULL, its fields will be copied into the queued command so
// the director can apply explicit target vectors or quaternions.
int simulith_42_send_set_mode(int spacecraft_id, int mode, const void* extra);

// Lower-level command queue accessors (implemented in simulith_42_command_api.c)
// Components normally call the simulith_42_send_* helpers above; these are
// provided for advanced use or diagnostics.
int enqueue_command(const simulith_42_command_t* cmd);
int dequeue_command(simulith_42_command_t* cmd);

#ifdef __cplusplus
}
#endif

#endif // SIMULITH_42_COMMANDS_H
