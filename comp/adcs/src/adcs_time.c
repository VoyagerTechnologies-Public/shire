#include "adcs_app.h"
#include "adcs_time.h"

/*
** Push the device's GPS time to cFE TIME as the spacecraft clock's external
** time source. Called once per HK cycle from ADCS_ReportHousekeeping(),
** immediately after a successful device HK read.
*/
void ADCS_ProcessGpsTime(void)
{
    if (ADCS_AppData.HkTelemetryPkt.DeviceEnabled != ADCS_DEVICE_ENABLED)
    {
        return;
    }

    uint32 GpsSeconds = ADCS_AppData.HkTelemetryPkt.DeviceHK.GpsSeconds;

    /*
    ** Only submit a time update when GPS seconds has actually advanced.
    ** Every HK cycle would otherwise call CFE_TIME_ExternalGPS with an
    ** identical or sub-second-jittered value, needlessly stressing the
    ** tight CFE_PLATFORM_TIME_MAX_DELTA acceptance window once the clock
    ** has been set.
    */
    if (ADCS_AppData.GpsTimeSynced && GpsSeconds == ADCS_AppData.LastGpsSecondsSubmitted)
    {
        return;
    }

    CFE_TIME_SysTime_t NewTime;
    NewTime.Seconds = GpsSeconds + ADCS_GPS_TO_MISSION_EPOCH_OFFSET_SEC;
    /* DeviceHK.GpsSubseconds is nanosecond-resolution; cFE only provides a
       microsecond-to-subseconds conversion. */
    NewTime.Subseconds = CFE_TIME_Micro2SubSecs(ADCS_AppData.HkTelemetryPkt.DeviceHK.GpsSubseconds / 1000);

    /* NewLeaps is inert while CFE_MISSION_TIME_CFG_DEFAULT_UTC is false. */
    CFE_TIME_ExternalGPS(NewTime, 0);

    /* Report only the first submission as an event -- this runs once per
    ** advancing GPS second for as long as the device stays enabled, and an
    ** EVS event on every one of those would flood the event log with
    ** routine, expected traffic instead of a meaningful state change. */
    if (!ADCS_AppData.GpsTimeSynced)
    {
        CFE_EVS_SendEvent(ADCS_GPS_TIME_SYNC_INF_EID, CFE_EVS_EventType_INFORMATION,
                          "ADCS: GPS time sync submitted to cFE TIME (GpsSeconds=%u)", (unsigned int)GpsSeconds);

        /* Real data has now taken over from the file fallback, if any was
           active -- ADCS_ResubmitTimeFallback() also stops resubmitting it
           as of this same GpsTimeSynced flip. */
        ADCS_AppData.HkTelemetryPkt.TimeFileFallbackActive = false;
    }

    ADCS_AppData.LastGpsSecondsSubmitted = GpsSeconds;
    ADCS_AppData.GpsTimeSynced           = true;

    ADCS_SaveTimeToFile();
}

/*
** Re-submit the file-loaded fallback time to cFE TIME every HK cycle
** until real GPS data takes over. See adcs_time.h for why a single
** one-shot submission at boot isn't enough. Called unconditionally from
** ADCS_ReportHousekeeping(), regardless of device state.
*/
void ADCS_ResubmitTimeFallback(void)
{
    if (!ADCS_AppData.TimeFileFallbackAvailable || ADCS_AppData.GpsTimeSynced)
    {
        return;
    }

    CFE_TIME_ExternalGPS(ADCS_AppData.TimeFileFallbackTime, 0);
}

/*
** Load a fallback time from ADCS_TIME_FILE, applying the boot offset, and
** arm it for resubmission by ADCS_ResubmitTimeFallback(). Called once
** from ADCS_AppInit(). See adcs_time.h for the boot-offset rationale.
*/
void ADCS_LoadTimeFromFile(void)
{
    osal_id_t            FileHandle;
    int32                status;
    ADCS_TimeFileData_t  FileData;

    status = OS_OpenCreate(&FileHandle, ADCS_TIME_FILE, OS_FILE_FLAG_NONE, OS_READ_ONLY);
    if (status != OS_SUCCESS)
    {
        /* Expected on a first-ever boot, before any real GPS save has
           happened -- not an error, just nothing to fall back to yet. */
        CFE_EVS_SendEvent(ADCS_TIME_FILE_LOAD_INF_EID, CFE_EVS_EventType_INFORMATION,
                          "ADCS: No time-fallback file at %s, clock left unset", ADCS_TIME_FILE);
        return;
    }

    status = OS_read(FileHandle, &FileData, sizeof(FileData));
    OS_close(FileHandle);
    if (status != sizeof(FileData))
    {
        CFE_EVS_SendEvent(ADCS_TIME_FILE_LOAD_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ADCS: Time-fallback file %s unreadable/corrupt, RC=%d", ADCS_TIME_FILE,
                          (int)status);
        return;
    }

    uint32 BootOffsetCount = FileData.BootOffsetCount + 1;

    CFE_TIME_SysTime_t FallbackTime;
    FallbackTime.Seconds    = FileData.Seconds + (BootOffsetCount * ADCS_TIME_FILE_BOOT_OFFSET_SEC);
    FallbackTime.Subseconds = FileData.Subseconds;

    ADCS_AppData.TimeFileFallbackTime      = FallbackTime;
    ADCS_AppData.TimeFileFallbackAvailable = true;
    ADCS_AppData.HkTelemetryPkt.TimeFileFallbackActive  = true;
    ADCS_AppData.HkTelemetryPkt.TimeFileBootOffsetCount = BootOffsetCount;

    CFE_EVS_SendEvent(ADCS_TIME_FILE_LOAD_INF_EID, CFE_EVS_EventType_INFORMATION,
                      "ADCS: Time-fallback applied from %s (Seconds=%u, BootOffsetCount=%u)", ADCS_TIME_FILE,
                      (unsigned int)FallbackTime.Seconds, (unsigned int)BootOffsetCount);

    /* Persist the advanced BootOffsetCount so a second stale boot (no real
       save in between) keeps advancing the offset rather than repeating
       the same value. */
    FileData.BootOffsetCount = BootOffsetCount;
    status = OS_OpenCreate(&FileHandle, ADCS_TIME_FILE, OS_FILE_FLAG_CREATE | OS_FILE_FLAG_TRUNCATE, OS_READ_WRITE);
    if (status != OS_SUCCESS)
    {
        CFE_EVS_SendEvent(ADCS_TIME_FILE_SAVE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ADCS: Error re-opening %s to persist boot offset, RC=%d", ADCS_TIME_FILE, (int)status);
        return;
    }

    status = OS_write(FileHandle, &FileData, sizeof(FileData));
    OS_close(FileHandle);
    if (status != sizeof(FileData))
    {
        CFE_EVS_SendEvent(ADCS_TIME_FILE_SAVE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ADCS: Error persisting boot offset to %s, RC=%d", ADCS_TIME_FILE, (int)status);
    }
}

/*
** Write the current synced time to ADCS_TIME_FILE, resetting
** BootOffsetCount to 0. Decimated to once every
** ADCS_TIME_FILE_SAVE_PERIOD_CYCLES calls (this is called once per HK
** cycle, from ADCS_ProcessGpsTime()'s success path).
*/
void ADCS_SaveTimeToFile(void)
{
    ADCS_AppData.TimeFileSaveCounter++;
    if (ADCS_AppData.TimeFileSaveCounter < ADCS_TIME_FILE_SAVE_PERIOD_CYCLES)
    {
        return;
    }
    ADCS_AppData.TimeFileSaveCounter = 0;

    CFE_TIME_SysTime_t CurrentTime = CFE_TIME_GetTime();

    ADCS_TimeFileData_t FileData;
    FileData.Seconds         = CurrentTime.Seconds;
    FileData.Subseconds      = CurrentTime.Subseconds;
    FileData.BootOffsetCount = 0;

    osal_id_t FileHandle;
    int32     status = OS_OpenCreate(&FileHandle, ADCS_TIME_FILE, OS_FILE_FLAG_CREATE | OS_FILE_FLAG_TRUNCATE,
                                     OS_READ_WRITE);
    if (status != OS_SUCCESS)
    {
        CFE_EVS_SendEvent(ADCS_TIME_FILE_SAVE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ADCS: Error opening %s to save time, RC=%d", ADCS_TIME_FILE, (int)status);
        return;
    }

    status = OS_write(FileHandle, &FileData, sizeof(FileData));
    OS_close(FileHandle);
    if (status != sizeof(FileData))
    {
        CFE_EVS_SendEvent(ADCS_TIME_FILE_SAVE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "ADCS: Error saving time to %s, RC=%d", ADCS_TIME_FILE, (int)status);
        return;
    }

    ADCS_AppData.HkTelemetryPkt.TimeFileSaveCount++;
}
