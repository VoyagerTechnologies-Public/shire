#ifndef _ADCS_DEVICE_H_
#define _ADCS_DEVICE_H_

/*
** Required header files.
*/
#include "device_cfg.h"
#ifndef CFE_TABLE_BUILD
#include "hwlib.h"
#endif

/*
** Type definitions
*/
#define ADCS_DEVICE_HDR_0 0xAD
#define ADCS_DEVICE_HDR_1 0xC5
#define ADCS_DEVICE_HDR   ((ADCS_DEVICE_HDR_0 << 8) | ADCS_DEVICE_HDR_1)

/* Command codes as described in the component README */
#define ADCS_DEVICE_NOOP_CMD         0x00 /* No operation */
#define ADCS_DEVICE_REQ_HK_CMD       0x01 /* Get housekeeping */
#define ADCS_DEVICE_SET_MODE_CMD     0x02 /* Set mode */
#define ADCS_DEVICE_SET_TARGET_CMD   0x03 /* Set target */
#define ADCS_DEVICE_GET_CSS_CMD      0x0A /* Get CSS data (10) */
#define ADCS_DEVICE_GET_FSS_CMD      0x0B /* Get FSS data (11) */
#define ADCS_DEVICE_GET_GPS_CMD      0x0C /* Get GPS data (12) */
#define ADCS_DEVICE_GET_IMU_CMD      0x0D /* Get IMU data (13) */
#define ADCS_DEVICE_GET_MAG_CMD      0x0E /* Get MAG data (14) */
#define ADCS_DEVICE_GET_MTB_CMD      0x0F /* Get MTB data (15) */
#define ADCS_DEVICE_GET_RW_CMD       0x10 /* Get RW data (16) */
#define ADCS_DEVICE_GET_ST_CMD       0x11 /* Get ST data (17) */
#define ADCS_DEVICE_OVERRIDE_MTB_CMD 0x14 /* Override MTB (20) */
#define ADCS_DEVICE_OVERRIDE_RW_CMD  0x15 /* Override RW (21) */
#define ADCS_DEVICE_SET_GAINS_CMD    0x16 /* Set control-law gains (22) */
#define ADCS_DEVICE_SET_TARGET_VECTOR_CMD 0x17 /* Set inertial target vector (23) */

#define ADCS_DEVICE_TRAILER_0 0x5C
#define ADCS_DEVICE_TRAILER_1 0xDA
#define ADCS_DEVICE_TRAILER   ((ADCS_DEVICE_TRAILER_0 << 8) | ADCS_DEVICE_TRAILER_1)

#define ADCS_DEVICE_CMD_SIZE    8
#define ADCS_DEVICE_HDR_TRL_LEN 4

/* Runtime-tunable control-law gains, pushed from the cFE app's boot-loaded
 * gains table (comp/adcs/src/adcs_tbl.c) down to the device over UART via
 * ADCS_DEVICE_SET_GAINS_CMD. Wire format is big-endian floats, in this
 * field order, framed the same way as other device commands (header, cmd
 * id, payload, trailer) -- just with a larger payload than the generic
 * uint16 one ADCS_CommandDevice() sends.
 */
typedef struct
{
    float SunPointKp;
    float SunPointKd;
    float WheelMaxTorqueNm;
    float MtbMaxDipoleAm2;
    float DetumbleGainBase;
    float DetumbleGainHigh;
    float RotisserieRateRadS;

} __attribute__((packed)) ADCS_Device_GainsCmd_t;
#define ADCS_DEVICE_GAINS_PAYLOAD_LEN sizeof(ADCS_Device_GainsCmd_t)
#define ADCS_DEVICE_GAINS_CMD_SIZE    (ADCS_DEVICE_HDR_TRL_LEN + 2 + ADCS_DEVICE_GAINS_PAYLOAD_LEN)

/* Arbitrary inertial target direction, pushed down over UART via
 * ADCS_DEVICE_SET_TARGET_VECTOR_CMD. Need not be pre-normalized by the
 * sender -- the device normalizes on receipt (see adcs_sim.c), matching
 * the existing invariant that state->inertial_target is always a unit
 * vector. Wire format matches ADCS_Device_GainsCmd_t: big-endian floats,
 * same frame shape, larger payload than the generic uint16 one.
 */
typedef struct
{
    float X;
    float Y;
    float Z;

} __attribute__((packed)) ADCS_Device_TargetVectorCmd_t;
#define ADCS_DEVICE_TARGET_VECTOR_PAYLOAD_LEN sizeof(ADCS_Device_TargetVectorCmd_t)
#define ADCS_DEVICE_TARGET_VECTOR_CMD_SIZE    (ADCS_DEVICE_HDR_TRL_LEN + 2 + ADCS_DEVICE_TARGET_VECTOR_PAYLOAD_LEN)

/* ADCS device housekeeping telemetry definition (in-memory representation)
 * Wire format (big-endian) is defined in the README and implemented by
 * serialization/parsing helpers in adcs_device.c.
 */
typedef struct
{
    uint16_t DeviceCounter;       /* command counter */
    uint16_t Target;              /* last set target (serialized in device HK frame) */
    uint8_t  Mode;                /* operation mode */
    uint32_t GpsSeconds;          /* GPS seconds */
    uint32_t GpsSubseconds;       /* GPS subseconds */
    float    GpsPosition[3];      /* float32[3] */
    float    Velocity[3];         /* float32[3] */
    uint8_t  AttitudeSource;      /* attitude source */
    float    AngRate[3];          /* estimated angular rate */
    float    Quaternion[4];       /* estimated quaternion */
    uint8_t  Eclipse;             /* eclipse flag */
    float    SunVectorBody[3];    /* estimated sun vector in body frame */
    float    PointVectorBody[3];  /* normalized body-frame vector adcs_point_vector_controller()
                                    * is currently driving toward +X (NADIR/TRACK/INERTIAL modes
                                    * 3/4/5 only; stale from the last such mode otherwise) */

} __attribute__((packed)) ADCS_Device_HK_tlm_t;
#define ADCS_DEVICE_HK_LNGTH sizeof(ADCS_Device_HK_tlm_t)
#define ADCS_DEVICE_HK_SIZE  (ADCS_DEVICE_HK_LNGTH + ADCS_DEVICE_HDR_TRL_LEN)

/*
** ADCS device data telemetry definition
*/
typedef struct
{
    uint16_t Chan1;
    uint16_t Chan2;
    uint16_t Chan3;

} __attribute__((packed)) ADCS_Device_Data_tlm_t;
#define ADCS_DEVICE_DATA_LNGTH sizeof(ADCS_Device_Data_tlm_t)
#define ADCS_DEVICE_DATA_SIZE  ADCS_DEVICE_DATA_LNGTH + ADCS_DEVICE_HDR_TRL_LEN

/*
** Prototypes
*/
#ifndef CFE_TABLE_BUILD
int32_t ADCS_ReadData(uart_info_t *device, uint8_t *read_data, uint8_t data_length);
int32_t ADCS_CommandDevice(uart_info_t *device, uint16_t cmd, uint16_t payload);
int32_t ADCS_RequestHK(uart_info_t *device, ADCS_Device_HK_tlm_t *data);
int32_t ADCS_RequestData(uart_info_t *device, ADCS_Device_Data_tlm_t *data, uint16_t data_cmd);
int32_t ADCS_SendGainsCmd(uart_info_t *device, const ADCS_Device_GainsCmd_t *gains);
int32_t ADCS_SendTargetVectorCmd(uart_info_t *device, const ADCS_Device_TargetVectorCmd_t *target);
#endif
void ADCS_PrintHK(const ADCS_Device_HK_tlm_t *hk);

/* Helpers for testing/processing raw frames */
int32_t ADCS_HandleRequestHK(const uint8_t *read_data, ADCS_Device_HK_tlm_t *data);
int32_t ADCS_HandleRequestData(const uint8_t *read_data, ADCS_Device_Data_tlm_t *data);
int32_t ADCS_ParseHK(const uint8_t *read_data, ADCS_Device_HK_tlm_t *data);

#endif /* _ADCS_DEVICE_H_ */
