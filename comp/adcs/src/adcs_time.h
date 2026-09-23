#ifndef _ADCS_TIME_H_
#define _ADCS_TIME_H_

/*
** Seconds from the cFE mission epoch (1980-01-01T00:00:00, see
** CFE_MISSION_TIME_EPOCH_* in cfe_mission_cfg.h) to the J2000 epoch that
** simulith's dyn_time is referenced to (simulith_42_context_t.dyn_time,
** "seconds since J2000").
**
** 42/Include/42defines.h defines GPS_EPOCH = -630763200.0 as the offset
** from J2000 to the true GPS epoch (1980-01-06). The cFE mission epoch is
** 5 days earlier than that (1980-01-01), so:
**   ADCS_GPS_TO_MISSION_EPOCH_OFFSET_SEC = 630763200 + 5*86400 = 631195200
** i.e. SecondsSinceMissionEpoch = dyn_time + ADCS_GPS_TO_MISSION_EPOCH_OFFSET_SEC
*/
#define ADCS_GPS_TO_MISSION_EPOCH_OFFSET_SEC 631195200u

/*
** Submit the device's GPS time (from the latest device HK sample) to cFE
** TIME via CFE_TIME_ExternalGPS, when it has advanced since the last
** submission. No-op while the device is disabled.
*/
void ADCS_ProcessGpsTime(void);

#endif /* _ADCS_TIME_H_ */
