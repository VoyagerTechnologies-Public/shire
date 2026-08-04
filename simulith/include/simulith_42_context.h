#ifndef SIMULITH_42_CONTEXT_H
#define SIMULITH_42_CONTEXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 42 Context Structure - Essential spacecraft data for simulators
typedef struct {
    // Time information
    double sim_time;          // Current simulation time [sec]
    double dyn_time;          // Absolute dynamic time [sec since J2000]
    
    // Spacecraft attitude (quaternion: scalar part last convention)
    double qn[4];            // Attitude quaternion of spacecraft body relative to inertial frame
    
    // Angular rates
    double wn[3];            // Angular velocity [rad/sec] in inertial frame
    
    // Position and velocity
    double pos_n[3];         // Position [m] in inertial frame
    double vel_n[3];         // Velocity [m/s] in inertial frame
    
    // Relative position and velocity (wrt reference orbit)
    double pos_r[3];         // Position [m] relative to reference orbit
    double vel_r[3];         // Velocity [m/s] relative to reference orbit
    
    // Environmental vectors
    double sun_vector_body[3];  // Sun-pointing unit vector in body frame
    double mag_field_body[3];   // Magnetic field [Tesla] in body frame
    double sun_vector_inertial[3]; // Sun-pointing unit vector in inertial frame
    double mag_field_inertial[3];  // Magnetic field [Tesla] in inertial frame
    double hvb[3]; // Angular Momentum Vector in Body Frame (Nms)
    
    // Spacecraft properties
    double mass;             // Total spacecraft mass [kg]
    double cm[3];           // Center of mass [m] in body frame
    double inertia[3][3];   // Inertia matrix [kg*m^2] about CM in body frame
    
    // Environmental conditions
    int eclipse;            // Eclipse flag (0=sunlit, 1=eclipse)
    double atmo_density;    // Atmospheric density [kg/m^3]
    
    // Spacecraft status
    int spacecraft_id;      // Spacecraft ID (index in SC array)
    int exists;            // Spacecraft exists flag
    char label[40];        // Spacecraft label/name
    
    // Validity flag
    int valid;             // 1 if data is valid, 0 otherwise
} simulith_42_context_t;

#ifdef __cplusplus
}
#endif

#endif // SIMULITH_42_CONTEXT_H
