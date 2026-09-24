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
** Path to the persisted time-fallback file (see ADCS_LoadTimeFromFile /
** ADCS_SaveTimeToFile below).
*/
#define ADCS_TIME_FILE "/cf/adcs_time.dat"

/*
** Added (times BootOffsetCount + 1) to the saved time on every boot-time
** load, so repeated boots off the same stale file (no real GPS save in
** between) keep producing distinct, monotonically increasing timestamps
** instead of colliding on one static value.
*/
#define ADCS_TIME_FILE_BOOT_OFFSET_SEC 60u

/*
** How many HK cycles (1 Hz) between periodic saves of the current synced
** time to ADCS_TIME_FILE. Decimated rather than saved every cycle to avoid
** needless flash wear.
*/
#define ADCS_TIME_FILE_SAVE_PERIOD_CYCLES 10u

/*
** On-disk layout of ADCS_TIME_FILE. Deliberately just the raw struct (no
** CFE_FS_Header) -- this is an internal scratch value, not a downlinked
** archive.
*/
typedef struct
{
    uint32 Seconds;         /* CFE_TIME_SysTime_t.Seconds at last real save */
    uint32 Subseconds;      /* CFE_TIME_SysTime_t.Subseconds at last real save */
    uint32 BootOffsetCount; /* Stale boots since last real save; 0 right after a save */
} ADCS_TimeFileData_t;

/*
** Submit the device's GPS time (from the latest device HK sample) to cFE
** TIME via CFE_TIME_ExternalGPS, when it has advanced since the last
** submission. No-op while the device is disabled. On success, periodically
** persists the synced time to ADCS_TIME_FILE (see ADCS_SaveTimeToFile).
*/
void ADCS_ProcessGpsTime(void);

/*
** Load a fallback time from ADCS_TIME_FILE, applying the boot offset
** described above, and arm it for resubmission (see
** ADCS_ResubmitTimeFallback). Called once from ADCS_AppInit(),
** unconditionally. A missing file is expected on a spacecraft's
** first-ever boot (before any real GPS save has happened) and is not an
** error; the clock is simply left untouched in that case, as it is today.
*/
void ADCS_LoadTimeFromFile(void);

/*
** Re-submit the file-loaded fallback time to cFE TIME via
** CFE_TIME_ExternalGPS every HK cycle, until real GPS data takes over
** (ADCS_AppData.GpsTimeSynced). A single one-shot submission at boot
** isn't enough: cFE TIME's virtual tone generator (no physical
** CFE_PLATFORM_TIME_CFG_SIGNAL, CFE_PLATFORM_TIME_CFG_VIRTUAL instead)
** recomputes AtToneSTCF from internal MET on every tick, so an external
** time source has to keep re-asserting itself every cycle to keep
** "winning" -- exactly like ADCS_ProcessGpsTime() already does for real
** GPS. Called from ADCS_ReportHousekeeping() regardless of device state,
** since the fallback must work even with the GPS device never enabled.
*/
void ADCS_ResubmitTimeFallback(void);

/*
** Write the current synced time to ADCS_TIME_FILE, resetting
** BootOffsetCount to 0. Called from ADCS_ProcessGpsTime()'s success path,
** decimated to once every ADCS_TIME_FILE_SAVE_PERIOD_CYCLES calls.
*/
void ADCS_SaveTimeToFile(void);

#endif /* _ADCS_TIME_H_ */
