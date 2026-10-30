#ifndef _ADCS_GAINS_LIMITS_H_
#define _ADCS_GAINS_LIMITS_H_

/*
** Physical/behavioral ceilings for ADCS_GainsTbl_t fields, used by
** ADCS_ValidateGainsTbl() (adcs_tbl.c). These mirror the compiled-in
** hardware ceilings in comp/adcs/sim/adcs_sim.h (ADCS_WHEEL_MAX_TORQUE,
** ADCS_MTB_MAX_DIPOLE) -- duplicated rather than shared, since the flight
** app and the simulated device are independently built targets that
** shouldn't depend on each other's internals; keep these in sync with
** adcs_sim.h's values if the actuator spec ever changes.
*/
#define ADCS_GAINS_WHEEL_MAX_TORQUE_CEILING 0.005f  /* N*m, matches ADCS_WHEEL_MAX_TORQUE */
#define ADCS_GAINS_MTB_MAX_DIPOLE_CEILING   1.42f   /* A*m^2, matches ADCS_MTB_MAX_DIPOLE */

/*
** "Mild" rotisserie rate ceiling (issue #8): a full rotation in at most
** ~5 minutes (2*pi / 300s =~ 0.021 rad/s), rounded down slightly.
*/
#define ADCS_GAINS_ROTISSERIE_RATE_CEILING 0.02f /* rad/s */

#endif /* _ADCS_GAINS_LIMITS_H_ */
