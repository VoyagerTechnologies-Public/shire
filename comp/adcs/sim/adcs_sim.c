#include "adcs_sim.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Forward declarations for component registration exports so -Wmissing-prototypes is happy
    (the REGISTER_COMPONENT macro expands to an exported getter symbol). */
const component_interface_t* get_adcs_sim_component_interface(void);
const component_interface_t* get_component_interface(void);

// Globals
static adcs_sim_state_t* g_state = NULL;
static transport_port_t g_uart_port = {0};
static double g_inertial_target[3] = {1.0, 0.0, 0.0};

// ADCS Controller Utility Functions
static void cross_product(const double a[3], const double b[3], double result[3]) {
    result[0] = a[1] * b[2] - a[2] * b[1];
    result[1] = a[2] * b[0] - a[0] * b[2];
    result[2] = a[0] * b[1] - a[1] * b[0];
}

static double dot_product(const double a[3], const double b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static double vector_magnitude(const double v[3]) {
    return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static void normalize_vector(double v[3]) {
    double mag = vector_magnitude(v);
    if (mag > 1e-9) {
        v[0] /= mag;
        v[1] /= mag;
        v[2] /= mag;
    }
}

static void quat_mul(const double a[4], const double b[4], double out[4]) {
    out[0] = a[0]*b[0] - a[1]*b[1] - a[2]*b[2] - a[3]*b[3];
    out[1] = a[0]*b[1] + a[1]*b[0] + a[2]*b[3] - a[3]*b[2];
    out[2] = a[0]*b[2] - a[1]*b[3] + a[2]*b[0] + a[3]*b[1];
    out[3] = a[0]*b[3] + a[1]*b[2] - a[2]*b[1] + a[3]*b[0];
}


// ADCS B-dot Detumbling Controller
static void adcs_bdot_controller(adcs_sim_state_t* state, const simulith_42_context_t* context_42) {
    if (!context_42 || !context_42->valid) {
        return;
    }
    
    // B-dot detumble: M = -k * (w x B) to remove angular momentum
    double w[3] = {context_42->wn[0], context_42->wn[1], context_42->wn[2]};
    double b[3] = {context_42->mag_field_body[0], context_42->mag_field_body[1], context_42->mag_field_body[2]};
    double rate_magnitude = vector_magnitude(w);
    
    // Adaptive gain based on rate magnitude - higher rates need more aggressive detumbling
    double detumble_gain;
    if (rate_magnitude > ADCS_HIGH_RATE_THRESHOLD) {
        detumble_gain = ADCS_DETUMBLE_GAIN_HIGH;
    } else {
        // Linear interpolation between base and high gain
        double gain_ratio = rate_magnitude / ADCS_HIGH_RATE_THRESHOLD;
        detumble_gain = ADCS_DETUMBLE_GAIN_BASE + gain_ratio * (ADCS_DETUMBLE_GAIN_HIGH - ADCS_DETUMBLE_GAIN_BASE);
    }
    
    // Compute w x B
    double w_cross_b[3];
    cross_product(w, b, w_cross_b);
    
    // Scale by adaptive detumble gain and negate
    double dipole_cmd[3];
    for (int i = 0; i < 3; i++) {
        dipole_cmd[i] = -detumble_gain * w_cross_b[i];
        
        // Limit to MTB saturation
        if (dipole_cmd[i] > ADCS_MTB_MAX_DIPOLE) dipole_cmd[i] = ADCS_MTB_MAX_DIPOLE;
        else if (dipole_cmd[i] < -ADCS_MTB_MAX_DIPOLE) dipole_cmd[i] = -ADCS_MTB_MAX_DIPOLE;
    }
    
    simulith_42_send_mtb_command(0, dipole_cmd, 0x07); // Enable all 3 MTBs
    #ifdef ADCS_CFG_DEBUG
    printf("ADCS B-DOT: w=[%.6f,%.6f,%.6f] b=[%.6f,%.6f,%.6f] gain=%.3f dipole=[%.6f,%.6f,%.6f]\n",
           w[0], w[1], w[2], b[0], b[1], b[2], detumble_gain, dipole_cmd[0], dipole_cmd[1], dipole_cmd[2]);
    #endif
}

// Robust inertial->body rotation that tests both quaternion conventions and
// picks the one that gives the largest alignment with the +X body axis.
static void rotate_inertial_to_body_safe(const double q[4], const double vin[3], double vout[3]) {
    double v1[3], v2[3];
    // v1 = q_conj * vin * q
    {
        double qc[4] = { q[0], -q[1], -q[2], -q[3] };
        double vq[4] = {0.0, vin[0], vin[1], vin[2]};
        double tmp[4]; quat_mul(qc, vq, tmp);
        double res[4]; quat_mul(tmp, q, res);
        v1[0] = res[1]; v1[1] = res[2]; v1[2] = res[3];
    }
    // v2 = q * vin * q_conj
    {
        double qc[4] = { q[0], -q[1], -q[2], -q[3] };
        double vq[4] = {0.0, vin[0], vin[1], vin[2]};
        double tmp[4]; quat_mul((double*)q, vq, tmp); // q * v
        double res[4]; quat_mul(tmp, qc, res); // (q*v)*q_conj
        v2[0] = res[1]; v2[1] = res[2]; v2[2] = res[3];
    }

    // Choose the vector that gives larger dot with +X (1,0,0)
    double dot1 = v1[0];
    double dot2 = v2[0];
    if (dot1 >= dot2) {
        vout[0] = v1[0]; vout[1] = v1[1]; vout[2] = v1[2];
    } else {
        vout[0] = v2[0]; vout[1] = v2[1]; vout[2] = v2[2];
    }
}

// Align body +X axis (1,0,0) with the provided vector expressed in body frame
static void adcs_point_vector_controller(adcs_sim_state_t* state, const simulith_42_context_t* context_42,
                                         const double vec_body[3], double dt, const char* tag)
{
    if (!context_42 || !context_42->valid) return;

    // Target axis is +X
    double target_body[3] = {1.0, 0.0, 0.0};

    // Work on a local copy of the incoming vector and normalize
    double v[3] = { vec_body[0], vec_body[1], vec_body[2] };
    double vmag = vector_magnitude(v);
    if (vmag < 1e-6) {
        printf("ADCS %s: Invalid input vector magnitude %.6f\n", tag ? tag : "POINT", vmag);
        return;
    }
    normalize_vector(v);

    // Compute attitude error (vector x target)
    double attitude_error[3];
    cross_product(v, target_body, attitude_error);
    double rate_magnitude = vector_magnitude(context_42->wn);
    
    #ifdef ADCS_CFG_DEBUG
    double sun_dot_target = dot_product(v, target_body);
    double angle_error = acos(fmax(-1.0, fmin(1.0, sun_dot_target)));
    double error_magnitude = vector_magnitude(attitude_error);
    printf("ADCS %s: WHEELS-ONLY mode (rates=%.6f rad/s, error=%.1f deg)\n", tag ? tag : "POINT",
           rate_magnitude, angle_error * 57.2958);
    #endif

    // PD control
    double control_torque[3];
    double max_rate = 0.1;
    for (int i = 0; i < 3; i++) {
        double u1 = ADCS_SUN_POINT_KP / ADCS_SUN_POINT_KD * attitude_error[i];
        if (u1 > max_rate) u1 = max_rate;
        else if (u1 < -max_rate) u1 = -max_rate;
        double rate_error = context_42->wn[i] - 0.0;
        control_torque[i] = -ADCS_SUN_POINT_KD * (u1 + rate_error);
        control_torque[i] = -control_torque[i];
        if (control_torque[i] > ADCS_WHEEL_MAX_TORQUE) control_torque[i] = ADCS_WHEEL_MAX_TORQUE;
        else if (control_torque[i] < -ADCS_WHEEL_MAX_TORQUE) control_torque[i] = -ADCS_WHEEL_MAX_TORQUE;
    }

    // Simple MTB assist logic (reduced influence compared to wheels)
    bool wheels_saturated = false;
    for (int i = 0; i < 3; i++) if (fabs(control_torque[i]) >= ADCS_WHEEL_MAX_TORQUE * 0.95) wheels_saturated = true;

    if (wheels_saturated || rate_magnitude > 0.1) {
        double w[3] = {context_42->wn[0], context_42->wn[1], context_42->wn[2]};
        double b[3] = {context_42->mag_field_body[0], context_42->mag_field_body[1], context_42->mag_field_body[2]};
        double mtb_gain = ADCS_DETUMBLE_GAIN_BASE * 0.5;
        if (wheels_saturated) mtb_gain *= 1.5;
        if (rate_magnitude > 0.2) mtb_gain *= 1.2;
        double w_cross_b[3]; cross_product(w, b, w_cross_b);
        double dipole_cmd[3];
        for (int i = 0; i < 3; i++) {
            dipole_cmd[i] = -mtb_gain * w_cross_b[i];
            if (dipole_cmd[i] > ADCS_MTB_MAX_DIPOLE) dipole_cmd[i] = ADCS_MTB_MAX_DIPOLE;
            else if (dipole_cmd[i] < -ADCS_MTB_MAX_DIPOLE) dipole_cmd[i] = -ADCS_MTB_MAX_DIPOLE;
        }
        simulith_42_send_mtb_command(0, dipole_cmd, 0x07);
    } else {
        double zero_dipole[3] = {0.0,0.0,0.0};
        simulith_42_send_mtb_command(0, zero_dipole, 0x00);
    }

    double wheel_torques[4] = {control_torque[0], control_torque[1], control_torque[2], 0.0};
    simulith_42_send_wheel_command(0, wheel_torques, 0x07);

    #ifdef ADCS_CFG_DEBUG
    printf("ADCS %s: Wheel torques=[%.6f,%.6f,%.6f] (max=%.6f)\n",
           tag ? tag : "POINT", control_torque[0], control_torque[1], control_torque[2], ADCS_WHEEL_MAX_TORQUE);
    // Debug output: print normalized error axis and the actual angle in degrees.
    // Note: when targeting +X (1,0,0) the x-component of the cross-product will be zero by construction.
    double axis_norm[3] = {0.0, 0.0, 0.0};
    if (error_magnitude > 1e-9) {
        axis_norm[0] = attitude_error[0] / error_magnitude;
        axis_norm[1] = attitude_error[1] / error_magnitude;
        axis_norm[2] = attitude_error[2] / error_magnitude;
    }
    printf("ADCS %s: Error_axis=[%.6f,%.6f,%.6f] angle=%.1f deg (mag=%.6f)\n",
           tag ? tag : "POINT", axis_norm[0], axis_norm[1], axis_norm[2], angle_error * 57.2958, error_magnitude);
    #endif
}

// ADCS Hybrid Sun Pointing Controller with Momentum Management
static void adcs_hybrid_sun_pointing_controller(adcs_sim_state_t* state, const simulith_42_context_t* context_42, double dt) {
    if (!context_42 || !context_42->valid) {
        printf("ADCS HYBRID: No valid 42 context\n");
        return;
    }
    
    if (context_42->eclipse) {
        #ifdef ADCS_CFG_DEBUG
        printf("ADCS HYBRID: In eclipse - maintaining current attitude\n");
        #endif
        // In eclipse, just do rate damping with MTBs
        adcs_bdot_controller(state, context_42);
        double zero_torques_local[4] = {0.0, 0.0, 0.0, 0.0};
        simulith_42_send_wheel_command(0, zero_torques_local, 0x07);
        return;
    }
    
    // Check current angular rates
    double rate_magnitude = vector_magnitude(context_42->wn);
    
    // Target: align +X body axis with sun vector (X+ sun pointing) 
    double target_body[3] = {1.0, 0.0, 0.0};  // Our "sside" equivalent
    double sun_body[3] = {context_42->sun_vector_body[0], 
                          context_42->sun_vector_body[1], 
                          context_42->sun_vector_body[2]};
    
    // Normalize sun vector and check magnitude
    double sun_mag = vector_magnitude(sun_body);
    if (sun_mag < 1e-6) {
        printf("ADCS HYBRID: Invalid sun vector magnitude %.6f, context_42->valid=%d, svb=[%.6f,%.6f,%.6f]\n", 
                   sun_mag, context_42 ? context_42->valid : -1,
                   sun_body[0], sun_body[1], sun_body[2]);
        return;
    }
    normalize_vector(sun_body);
    
    double attitude_error[3];
    double sun_dot_target = dot_product(sun_body, target_body);
    double EPS = 1.0E-6;
    
    if ((sun_dot_target > (EPS - 1.0)) && (sun_dot_target < (1.0 - EPS))) {
        // Normal case: sun vector × target side
        cross_product(sun_body, target_body, attitude_error);
        #ifdef ADCS_CFG_DEBUG
        printf("ADCS ERROR CALC: Normal case, dot=%.6f\n", sun_dot_target);
        #endif
    } else if (sun_dot_target >= (1.0 - EPS)) {
        // Nearly aligned case - no error
        attitude_error[0] = attitude_error[1] = attitude_error[2] = 0.0;
        #ifdef ADCS_CFG_DEBUG
        printf("ADCS ERROR CALC: Aligned case, dot=%.6f\n", sun_dot_target);
        #endif
    } else {
        // Anti-aligned case - need special handling
        #ifdef ADCS_CFG_DEBUG
        printf("ADCS ERROR CALC: Anti-aligned case, dot=%.6f\n", sun_dot_target);
        #endif
        double err_b[3] = {target_body[1], target_body[2], target_body[0]};
        if (fabs(err_b[0] - err_b[1]) < EPS && fabs(err_b[0] - err_b[2]) < EPS) {
            err_b[0] = -err_b[0];
        }
        double temp_target[3];
        cross_product(target_body, err_b, temp_target);
        cross_product(sun_body, temp_target, attitude_error);
    }
    
    #ifdef ADCS_CFG_DEBUG
    double error_magnitude = vector_magnitude(attitude_error);
    double angle_error = acos(fmax(-1.0, fmin(1.0, sun_dot_target))); // Clamp to [-1,1]
    printf("ADCS HYBRID: rates=%.6f rad/s, error=%.1f deg\n",
            rate_magnitude, angle_error * 57.2958);
    #endif
    
    double control_torque[3];
    double max_rate = 0.1; // Max slew rate limit (rad/s)
    
    for (int i = 0; i < 3; i++) {
        // Rate-limited attitude command
        double u1 = ADCS_SUN_POINT_KP / ADCS_SUN_POINT_KD * attitude_error[i];
        if (u1 > max_rate) u1 = max_rate;
        else if (u1 < -max_rate) u1 = -max_rate;
        
        // Rate error (actual rate - commanded rate)
        double rate_error = context_42->wn[i] - 0.0; // commanding zero rates for now
        
        // PD control law: T = -Kr * (u1 + rate_error)
        control_torque[i] = -ADCS_SUN_POINT_KD * (u1 + rate_error);
        
        // Apply final sign flip
        control_torque[i] = -control_torque[i];
        
        // Limit torque to wheel capability
        if (control_torque[i] > ADCS_WHEEL_MAX_TORQUE) {
            control_torque[i] = ADCS_WHEEL_MAX_TORQUE;
        } else if (control_torque[i] < -ADCS_WHEEL_MAX_TORQUE) {
            control_torque[i] = -ADCS_WHEEL_MAX_TORQUE;
        }
    }
    
    // Check if wheels are saturating or need momentum management
    bool wheels_saturated = false;
    double total_wheel_torque = 0.0;
    for (int i = 0; i < 3; i++) {
        if (fabs(control_torque[i]) >= ADCS_WHEEL_MAX_TORQUE * 0.95) { // 95% of max
            wheels_saturated = true;
        }
        total_wheel_torque += fabs(control_torque[i]);
    }
    
    // Use MTBs for momentum management when wheels are saturated or rates are high
    if (wheels_saturated || rate_magnitude > 0.1) { // Raised threshold from 0.05 to 0.1 rad/s
        #ifdef ADCS_CFG_DEBUG
        printf("ADCS MTB ASSIST: Wheels saturated=%d, high rates=%d\n", 
               wheels_saturated, rate_magnitude > 0.1);
        #endif
        
        // MTB B-dot damping to reduce angular rates
        double w[3] = {context_42->wn[0], context_42->wn[1], context_42->wn[2]};
        double b[3] = {context_42->mag_field_body[0], context_42->mag_field_body[1], context_42->mag_field_body[2]};
        
        // Reduced MTB gains to avoid overpowering stronger wheels
        double mtb_gain = ADCS_DETUMBLE_GAIN_BASE * 0.5; // Reduced base gain
        if (wheels_saturated) {
            mtb_gain *= 1.5; // Less aggressive when wheels saturated (was 2.0)
        }
        if (rate_magnitude > 0.2) { // Higher threshold for aggressive MTB
            mtb_gain *= 1.2; // Less aggressive for high rates (was 1.5)
        }
        
        // Compute w x B for B-dot control
        double w_cross_b[3];
        cross_product(w, b, w_cross_b);
        
        // Apply MTB dipole command: M = -k * (w x B)
        double dipole_cmd[3];
        for (int i = 0; i < 3; i++) {
            dipole_cmd[i] = -mtb_gain * w_cross_b[i];
            
            // Limit to MTB saturation
            if (dipole_cmd[i] > ADCS_MTB_MAX_DIPOLE) dipole_cmd[i] = ADCS_MTB_MAX_DIPOLE;
            else if (dipole_cmd[i] < -ADCS_MTB_MAX_DIPOLE) dipole_cmd[i] = -ADCS_MTB_MAX_DIPOLE;
        }
        
        simulith_42_send_mtb_command(0, dipole_cmd, 0x07); // Enable all 3 MTBs
        
        #ifdef ADCS_CFG_DEBUG
        printf("ADCS MTB: gain=%.4f, dipole=[%.4f,%.4f,%.4f]\n",
               mtb_gain, dipole_cmd[0], dipole_cmd[1], dipole_cmd[2]);
        #endif
    } else {
        // Low rates and unsaturated wheels - disable MTBs
        double zero_dipole[3] = {0.0, 0.0, 0.0};
        simulith_42_send_mtb_command(0, zero_dipole, 0x00);
        #ifdef ADCS_CFG_DEBUG
        printf("ADCS MTB: Disabled (low rates, unsaturated wheels)\n");
        #endif
    }
    
    double wheel_torques[4] = {control_torque[0], control_torque[1], control_torque[2], 0.0};
    simulith_42_send_wheel_command(0, wheel_torques, 0x07);
    
    #ifdef ADCS_CFG_DEBUG           
    printf("ADCS NOS3-STYLE: Wheel torques=[%.6f,%.6f,%.6f] (max=%.6f)\n",
           control_torque[0], control_torque[1], control_torque[2], ADCS_WHEEL_MAX_TORQUE);
    printf("ADCS HYBRID: Error=[%.6f,%.6f,%.6f] (mag=%.6f, %.1f deg)\n",
           attitude_error[0], attitude_error[1], attitude_error[2], error_magnitude, angle_error * 57.2958);
    printf("ADCS HYBRID: Rates=[%.6f,%.6f,%.6f] (mag=%.6f rad/s)\n", 
           context_42->wn[0], context_42->wn[1], context_42->wn[2], rate_magnitude);
    printf("ADCS HYBRID: Sun=[%.3f,%.3f,%.3f] Target=[%.3f,%.3f,%.3f] Dot=%.3f\n",
           sun_body[0], sun_body[1], sun_body[2], target_body[0], target_body[1], target_body[2], sun_dot_target);
    printf("ADCS PROGRESS: Pointing error %.1f deg (target: 0 deg)\n", 
           acos(fmax(-1.0, fmin(1.0, sun_dot_target))) * 57.2958);
    #endif
}

// Main ADCS Controller Update
static void adcs_controller_update(adcs_sim_state_t* state, const simulith_42_context_t* context_42, double current_time) 
{
    double dt = 0.0;
    double required_dt = 1.0 / ADCS_CONTROLLER_UPDATE_RATE_HZ;
    double nadir_body[3];
    double nadir_inertial[3] = { -context_42->pos_n[0], -context_42->pos_n[1], -context_42->pos_n[2] };
    double tgt_body[3];
    double zero_dipole[3] = {0.0, 0.0, 0.0};
    double zero_torques[4] = {0.0, 0.0, 0.0, 0.0};

    if (!state->controller_active) {
        return;
    }

    if (state->last_control_time > 0.0) 
    {
        dt = current_time - state->last_control_time;
        if (dt < 0.0) 
        {
            // Time went backwards or invalid — resync timer but do not run controller
            state->last_control_time = current_time;
            return;
        }
    } else 
    {
        // First tick: initialize timer but do NOT force immediate control execution.
        // This avoids running the controller when the simulator time is paused.
        state->last_control_time = current_time;
        return;
    }

    // Only run controller at specified rate
    if (dt < required_dt) {
        return;
    }

    // Update control time
    state->last_control_time = current_time;
    
    #ifdef ADCS_CFG_DEBUG
    printf("ADCS CONTROLLER: Running mode %d at time %.3f (dt=%.3f)\n", 
           state->current_mode, current_time, dt);
    #endif
    
    switch (state->current_mode) {
        case 0: // Disabled
            simulith_42_send_wheel_command(0, zero_torques, 0x00);
            simulith_42_send_mtb_command(0, zero_dipole, 0x00);
            #ifdef ADCS_CFG_DEBUG
            printf("ADCS CONTROLLER: Disabled mode - zero commands sent\n");
            #endif
            break;
            
        case 1: // B-dot detumble only
            #ifdef ADCS_CFG_DEBUG
            printf("ADCS CONTROLLER: B-dot detumble mode\n");
            #endif
            adcs_bdot_controller(state, context_42);
            /* reuse zero_torques declared at function scope */
            simulith_42_send_wheel_command(0, zero_torques, 0x00);
            break;
            
        case 2: // Hybrid sun pointing with momentum management
            #ifdef ADCS_CFG_DEBUG
            printf("ADCS CONTROLLER: Hybrid sun pointing mode\n");
            #endif
            adcs_hybrid_sun_pointing_controller(state, context_42, dt);
            break;

        case 3: // Nadir pointing - point +X toward nadir (assume nadir is -position vector)
            #ifdef ADCS_CFG_DEBUG
            printf("ADCS CONTROLLER: Nadir pointing mode\n");
            #endif
            rotate_inertial_to_body_safe(context_42->qn, nadir_inertial, nadir_body);
            adcs_point_vector_controller(state, context_42, nadir_body, dt, "NADIR");
            break;

        case 4: // Target-track - inertial target follows g_inertial_target (rotated into body)
            #ifdef ADCS_CFG_DEBUG    
            printf("ADCS CONTROLLER: Target-track mode\n");
            #endif
            rotate_inertial_to_body_safe(context_42->qn, g_inertial_target, tgt_body);
            adcs_point_vector_controller(state, context_42, tgt_body, dt, "TRACK");
            break;

        case 5: // Inertial pointing - keep body +X aligned to a fixed inertial direction
            #ifdef ADCS_CFG_DEBUG    
            printf("ADCS CONTROLLER: Inertial pointing mode\n");
            #endif
            rotate_inertial_to_body_safe(context_42->qn, g_inertial_target, tgt_body);
            adcs_point_vector_controller(state, context_42, tgt_body, dt, "INERTIAL");
            break;
            
        default:
            printf("ADCS CONTROLLER: Mode %d not implemented\n", state->current_mode);
            break;
    }
}

static void send_housekeeping(adcs_sim_state_t* state)
{
    if (!state) return;
    uint8_t response[ADCS_DEVICE_HK_SIZE];
    uint8_t *ptr = response;

    /* Header */
    ptr[0] = ADCS_DEVICE_HDR_0;
    ptr[1] = ADCS_DEVICE_HDR_1;
    ptr += 2;

    /* DeviceCounter (uint16 big-endian) */
    ptr[0] = (uint8_t)((state->hk.DeviceCounter >> 8) & 0xFF);
    ptr[1] = (uint8_t)(state->hk.DeviceCounter & 0xFF);
    ptr += 2;

    /* Target (uint16 big-endian) */
    ptr[0] = (uint8_t)((state->hk.Target >> 8) & 0xFF);
    ptr[1] = (uint8_t)(state->hk.Target & 0xFF);
    ptr += 2;

    /* Mode (uint8) */
    ptr[0] = state->hk.Mode;
    ptr += 1;

    /* GpsSeconds, GpsSubseconds (uint32 big-endian) */
    uint32_t u32;
    u32 = state->hk.GpsSeconds;
    ptr[0] = (uint8_t)((u32 >> 24) & 0xFF); 
    ptr[1] = (uint8_t)((u32 >> 16) & 0xFF); 
    ptr[2] = (uint8_t)((u32 >> 8) & 0xFF); 
    ptr[3] = (uint8_t)(u32 & 0xFF); 
    ptr += 4;
    u32 = state->hk.GpsSubseconds;
    ptr[0] = (uint8_t)((u32 >> 24) & 0xFF); 
    ptr[1] = (uint8_t)((u32 >> 16) & 0xFF); 
    ptr[2] = (uint8_t)((u32 >> 8) & 0xFF); 
    ptr[3] = (uint8_t)(u32 & 0xFF); 
    ptr += 4;
    
    for (int i = 0; i < 3; i++)
    {
        uint32_t u;
        memcpy(&u, &state->hk.GpsPosition[i], sizeof(u));
        ptr[0] = (uint8_t)((u >> 24) & 0xFF); 
        ptr[1] = (uint8_t)((u >> 16) & 0xFF); 
        ptr[2] = (uint8_t)((u >> 8) & 0xFF); 
        ptr[3] = (uint8_t)(u & 0xFF); 
        ptr += 4;
    }
    for (int i = 0; i < 3; i++)
    {
        uint32_t u;
        memcpy(&u, &state->hk.Velocity[i], sizeof(u));
        ptr[0] = (uint8_t)((u >> 24) & 0xFF); 
        ptr[1] = (uint8_t)((u >> 16) & 0xFF); 
        ptr[2] = (uint8_t)((u >> 8) & 0xFF); 
        ptr[3] = (uint8_t)(u & 0xFF); 
        ptr += 4;
    }

    /* Attitude source */
    ptr[0] = (uint8_t)state->hk.AttitudeSource;
    ptr += 1;

    for (int i = 0; i < 3; i++)
    {
        uint32_t u;
        memcpy(&u, &state->hk.AngRate[i], sizeof(u));
        ptr[0] = (uint8_t)((u >> 24) & 0xFF); 
        ptr[1] = (uint8_t)((u >> 16) & 0xFF); 
        ptr[2] = (uint8_t)((u >> 8) & 0xFF); 
        ptr[3] = (uint8_t)(u & 0xFF); 
        ptr += 4;
    }

    for (int i = 0; i < 4; i++)
    {
        uint32_t u;
        memcpy(&u, &state->hk.Quaternion[i], sizeof(u));
        ptr[0] = (uint8_t)((u >> 24) & 0xFF); 
        ptr[1] = (uint8_t)((u >> 16) & 0xFF); 
        ptr[2] = (uint8_t)((u >> 8) & 0xFF); 
        ptr[3] = (uint8_t)(u & 0xFF); 
        ptr += 4;
    }

    /* Eclipse */
    ptr[0] = (uint8_t)state->hk.Eclipse;
    ptr += 1;

    for (int i = 0; i < 3; i++)
    {
        uint32_t u;
        memcpy(&u, &state->hk.SunVectorBody[i], sizeof(u));
        ptr[0] = (uint8_t)((u >> 24) & 0xFF); 
        ptr[1] = (uint8_t)((u >> 16) & 0xFF); 
        ptr[2] = (uint8_t)((u >> 8) & 0xFF); 
        ptr[3] = (uint8_t)(u & 0xFF); 
        ptr += 4;
    }

    /* Trailer */
    ptr[0] = (uint8_t)ADCS_DEVICE_TRAILER_0;
    ptr[1] = (uint8_t)ADCS_DEVICE_TRAILER_1;

    #ifdef ADCS_CFG_DEBUG
    printf("ADCS SIM: send_housekeeping raw[%zu]: ", sizeof(response));
    for (size_t i = 0; i < sizeof(response); ++i) printf("%02X ", response[i]);
    printf("\n");
    printf("ADCS SIM: sunX=%.3f sunY=%.3f sunZ=%.3f eclipse=%d mode=%d target=%d\n",
           state->hk.SunVectorBody[0], state->hk.SunVectorBody[1], state->hk.SunVectorBody[2],
           state->hk.Eclipse, state->hk.Mode, state->hk.Target);
    #endif

    simulith_transport_send((transport_port_t*)&g_uart_port, response, (size_t)sizeof(response));
}

static void send_adcs_data(adcs_sim_state_t* state)
{
    if (!state) return;
    uint8_t response[10];
    response[0] = ADCS_DEVICE_HDR_0;
    response[1] = ADCS_DEVICE_HDR_1;
    response[2] = (uint8_t)((state->data.Chan1 >> 8) & 0xFF);
    response[3] = (uint8_t)(state->data.Chan1 & 0xFF);
    response[4] = (uint8_t)((state->data.Chan2 >> 8) & 0xFF);
    response[5] = (uint8_t)(state->data.Chan2 & 0xFF);
    response[6] = (uint8_t)((state->data.Chan3 >> 8) & 0xFF);
    response[7] = (uint8_t)(state->data.Chan3 & 0xFF);
    response[8] = ADCS_DEVICE_TRAILER_0;
    response[9] = ADCS_DEVICE_TRAILER_1;
    simulith_transport_send((transport_port_t*)&g_uart_port, response, (size_t)sizeof(response));
}

static void handle_command(adcs_sim_state_t* state, const uint8_t* data, size_t length)
{
    if (!state || !data || length < ADCS_DEVICE_CMD_SIZE) 
    {  // Check for minimum command size
        printf("ADCS SIM: Invalid command parameters: state=%p, data=%p, length=%zu\n", 
               (void*)state, (const void*)data, length);
        return;
    }
    
    uint16_t header  = ((uint16_t) data[0] << 8) | data[1];
    uint16_t cmd_id  = ((uint16_t) data[2] << 8) | data[3];
    uint16_t payload = ((uint16_t) data[4] << 8) | data[5];
    uint16_t trailer = ((uint16_t) data[6] << 8) | data[7];

    // Validate header
    if (header != ADCS_DEVICE_HDR) 
    {
        printf("ADCS SIM: Invalid command header (0x%04X)\n", header);
        return;
    }

    // Validate trailer
    if (trailer != ADCS_DEVICE_TRAILER) 
    {
        printf("ADCS SIM: Invalid command trailer (0x%04X)\n", trailer);
        return;
    }

    // Echo command back
    #ifdef ADCS_CFG_DEBUG
    printf("ADCS SIM: handle_command: Echo command back to UART: ID=%d, Payload=0x%08X\n", cmd_id, payload);
    #endif
    simulith_transport_send((transport_port_t*)&g_uart_port, data, length);

    // Process command
    switch (cmd_id)
    {
        case ADCS_DEVICE_NOOP_CMD:
            #ifdef ADCS_CFG_DEBUG
            printf("ADCS SIM: Processing NOOP command\n");
            #endif
            // Just echo the command back, which was already done
            break;

        case ADCS_DEVICE_REQ_HK_CMD:
            #ifdef ADCS_CFG_DEBUG
            printf("ADCS SIM: Processing GET_HK command\n");
            #endif
            send_housekeeping(state);
            break;

        case ADCS_DEVICE_GET_CSS_CMD:
        case ADCS_DEVICE_GET_FSS_CMD:
        case ADCS_DEVICE_GET_GPS_CMD:
        case ADCS_DEVICE_GET_IMU_CMD:
        case ADCS_DEVICE_GET_MAG_CMD:
        case ADCS_DEVICE_GET_MTB_CMD:
        case ADCS_DEVICE_GET_RW_CMD:
        case ADCS_DEVICE_GET_ST_CMD:
            #ifdef ADCS_CFG_DEBUG
            printf("ADCS SIM: Processing GET_DATA command ID=%u\n", cmd_id);
            #endif
            /* For now, all sensor frames use the three-channel payload. */
            send_adcs_data(state);
            break;

        case ADCS_DEVICE_SET_MODE_CMD:
            #ifdef ADCS_CFG_DEBUG
            printf("ADCS SIM: Processing SET_MODE command with payload %u\n", payload);
            #endif
            /* Store mode in hk.Mode and update controller */
            state->hk.Mode = (uint8_t)(payload & 0xFF);
            state->current_mode = state->hk.Mode;
            
            // Activate/deactivate controller based on mode
            if (state->current_mode == 0) {
                state->controller_active = 0;
                #ifdef ADCS_CFG_DEBUG
                printf("ADCS CONTROLLER: Deactivated (mode 0)\n");
                #endif
            } else {
                state->controller_active = 1;
                // Reset controller state for new mode
                for (int i = 0; i < 3; i++) {
                    state->prev_attitude_error[i] = 0.0;
                }
                #ifdef ADCS_CFG_DEBUG
                printf("ADCS CONTROLLER: Activated mode %d\n", state->current_mode);
                #endif
            }
            break;

        case ADCS_DEVICE_SET_TARGET_CMD:
            #ifdef ADCS_CFG_DEBUG
            printf("ADCS SIM: Processing SET_TARGET command with payload %u\n", payload);
            #endif
            /* Store target id in hk.Target */
            state->hk.Target = payload & 0xFFFF;
            /* Simple mapping: payload 1 = +X inertial, 2 = -X inertial, 3 = custom (keeps previous)
               We set global inertial target in inertial frame; it will be rotated to body each tick */
            if ((payload & 0xFFFF) == 1) {
                g_inertial_target[0] = 1.0; g_inertial_target[1] = 0.0; g_inertial_target[2] = 0.0;
            } else if ((payload & 0xFFFF) == 2) {
                g_inertial_target[0] = -1.0; g_inertial_target[1] = 0.0; g_inertial_target[2] = 0.0;
            } else if ((payload & 0xFFFF) == 3) {
                /* leave as-is for manual setting via CLI */
            }
            break;

        default:
            printf("ADCS SIM: Unknown command ID: %d\n", cmd_id);
            break;
    }

    // Increment command counter
    state->hk.DeviceCounter++;
}

static void adcs_sim_on_tick(uint64_t tick_time_ns, const simulith_42_context_t* context_42)
{
    int bytes;
    uint8_t data[256];

    if (!g_state) return;
    
    // Convert nanoseconds to seconds
    double current_time = (double)tick_time_ns / 1e9;
    
    // Run ADCS controller if active
    adcs_controller_update(g_state, context_42, current_time);
    
    // Update adcs data at the specified rate
    if (current_time - g_state->last_update_time >= (1.0 / ADCS_SIM_UPDATE_RATE_HZ)) 
    {
        // If 42 context is available, populate channels and HK with data from context
        if (context_42 && context_42->valid) {
            // Populate sensor channels with Sun Vector Body (SVB) scaled into uint16 range
            g_state->data.Chan1 = (uint16_t)((context_42->sun_vector_body[0] * 10000.0) + 32768.0);
            g_state->data.Chan2 = (uint16_t)((context_42->sun_vector_body[1] * 10000.0) + 32768.0);
            g_state->data.Chan3 = (uint16_t)((context_42->sun_vector_body[2] * 10000.0) + 32768.0);

            /* Copy 42 context fields into HK so telemetry reflects simulator state */
            /* Time: use 42 dynamic time (absolute time) rather than local sim time */
            g_state->hk.GpsSeconds = (uint32_t) context_42->dyn_time;
            /* Use fractional part of dyn_time for subseconds (scaled to uint32 range) */
            double frac = context_42->dyn_time - (double)g_state->hk.GpsSeconds;
            if (frac < 0.0) frac = 0.0;
            g_state->hk.GpsSubseconds = (uint32_t)(frac * 1e9); /* nanosecond-resolution in uint32 */

            /* Position/velocity (cast from double to float) */
            for (int i = 0; i < 3; ++i) {
                g_state->hk.GpsPosition[i] = (float) context_42->pos_n[i];
                g_state->hk.Velocity[i]    = (float) context_42->vel_n[i];
            }

            /* Attitude quaternion and angular rates */
            for (int i = 0; i < 4; ++i) g_state->hk.Quaternion[i] = (i < 4) ? (float) context_42->qn[i] : 0.0f;
            for (int i = 0; i < 3; ++i) g_state->hk.AngRate[i]   = (float) context_42->wn[i];

            /* Eclipse flag and sun vector */
            g_state->hk.Eclipse = (uint8_t)(context_42->eclipse ? 1 : 0);
            for (int i = 0; i < 3; ++i) g_state->hk.SunVectorBody[i] = (float) context_42->sun_vector_body[i];

            /* Mark attitude source as 1 (from 42) */
            g_state->hk.AttitudeSource = 1;
        } else {
            /* Fallback: simple counter-derived channels if no 42 context available */
            g_state->data.Chan1 = (uint16_t)(g_state->hk.DeviceCounter * 1);
            g_state->data.Chan2 = (uint16_t)(g_state->hk.DeviceCounter * 2);
            g_state->data.Chan3 = (uint16_t)(g_state->hk.DeviceCounter * 3);
        }

        g_state->last_update_time = current_time;
    }

    // Process UART
    bytes = simulith_transport_available((transport_port_t*)&g_uart_port);
    if (bytes > 0)
    {
    // Read UART
    bytes = simulith_transport_receive((transport_port_t*)&g_uart_port, data, sizeof(data));

        #ifdef ADCS_CFG_DEBUG
        printf("ADCS SIM: Received %d bytes from UART\n", bytes);
        for(int i = 0; i < bytes; i++) 
        {
            printf("%02X ", data[i]);
        }
        printf("\n");
        #endif

    // Process the command (cast bytes to size_t)
    handle_command(g_state, data, (size_t)bytes);
    }
}

int adcs_sim_init(adcs_sim_state_t* state)
{
    if (!state) return ADCS_SIM_ERROR;

    // Initialize state
    memset(state, 0, sizeof(adcs_sim_state_t));

    // Set global state pointer
    g_state = state;

    // Initialize UART port struct for Simulith (server/bind)
    memset(&g_uart_port, 0, sizeof(g_uart_port));
    snprintf(g_uart_port.name, sizeof(g_uart_port.name), "adcs_sim_uart%d", ADCS_CFG_HANDLE);
    snprintf(g_uart_port.address, sizeof(g_uart_port.address), "ipc:///tmp/simulith_pub:%d", SIMULITH_UART_BASE_PORT + ADCS_CFG_HANDLE);
    g_uart_port.is_server = 1; // Always server/bind for the simulator

    int uart_result = simulith_transport_init((transport_port_t*)&g_uart_port);
    if (uart_result < 0) 
    {
        printf("ADCS SIM: Failed to initialize Simulith UART server\n");
        return ADCS_SIM_ERROR;
    }

    // Initialize default values
    state->hk.DeviceCounter = 0;
    state->hk.Mode = 0;
    state->hk.GpsSeconds = 0;
    state->hk.GpsSubseconds = 0;
    for (int i = 0; i < 3; i++) { state->hk.GpsPosition[i] = 0.0f; state->hk.Velocity[i] = 0.0f; state->hk.AngRate[i] = 0.0f; state->hk.SunVectorBody[i] = 0.0f; }
    for (int i = 0; i < 4; i++) { state->hk.Quaternion[i] = 0.0f; }
    state->hk.AttitudeSource = 0;
    state->hk.Eclipse = 0;

    state->data.Chan1 = 0;
    state->data.Chan2 = 0;
    state->data.Chan3 = 0;
    state->last_update_time = 0.0;
    
    // Initialize ADCS controller state
    state->last_control_time = 0.0;
    for (int i = 0; i < 3; i++) {
        state->prev_attitude_error[i] = 0.0;
    }
    state->current_mode = 0;          // Start in disabled mode
    state->controller_active = 0;     // Controller inactive initially

    printf("ADCS SIM: Initialized successfully as %s\n", g_uart_port.name);
    return ADCS_SIM_SUCCESS;
}

void adcs_sim_cleanup(adcs_sim_state_t* state)
{
    if (!state) return;

    g_state = NULL;  // Clear global state pointer
    simulith_transport_close((transport_port_t*)&g_uart_port);
}

// Component interface implementation
static int adcs_sim_component_init(component_state_t** state)
{
    adcs_sim_state_t* adcs_state = malloc(sizeof(adcs_sim_state_t));
    if (!adcs_state) {
        return COMPONENT_ERROR;
    }
    
    int result = adcs_sim_init(adcs_state);
    if (result != ADCS_SIM_SUCCESS) {
        free(adcs_state);
        return COMPONENT_ERROR;
    }
    
    *state = (component_state_t*)adcs_state;
    return COMPONENT_SUCCESS;
}

static void adcs_sim_component_tick(component_state_t* state, uint64_t tick_time_ns, const simulith_42_context_t* context_42)
{
    if (!state) return;
    
    adcs_sim_state_t* adcs_state = (adcs_sim_state_t*)state;
    
    // Set global state for the tick callback
    adcs_sim_state_t* old_state = g_state;
    g_state = adcs_state;
    
    // Call the original tick function with 42 context
    adcs_sim_on_tick(tick_time_ns, context_42);
    
    // Restore previous state
    g_state = old_state;
}

static void adcs_sim_component_cleanup(component_state_t* state)
{
    if (!state) return;
    
    adcs_sim_state_t* adcs_state = (adcs_sim_state_t*)state;
    adcs_sim_cleanup(adcs_state);
    free(adcs_state);
}

static const component_interface_t adcs_sim_interface = {
    .name = "adcs_sim",
    .description = "Adcs component simulation with UART interface",
    .init = adcs_sim_component_init,
    .tick = adcs_sim_component_tick,
    .cleanup = adcs_sim_component_cleanup
};

// Component registration function - exported for dynamic loading
REGISTER_COMPONENT(adcs_sim)
{
    return &adcs_sim_interface;
}

// Export the registration function with a standard name for dynamic loading
__attribute__((visibility("default")))
const component_interface_t* get_component_interface(void)
{
    return &adcs_sim_interface;
}
