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

    ADCS_AppData.LastGpsSecondsSubmitted = GpsSeconds;
    ADCS_AppData.GpsTimeSynced           = true;

    CFE_EVS_SendEvent(ADCS_GPS_TIME_SYNC_INF_EID, CFE_EVS_EventType_INFORMATION,
                      "ADCS: GPS time sync submitted to cFE TIME (GpsSeconds=%u)", (unsigned int)GpsSeconds);
}
