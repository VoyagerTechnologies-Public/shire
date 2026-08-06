#ifndef _ADCS_MSGIDS_H_
#define _ADCS_MSGIDS_H_

/*
** CCSDS V1 Command Message IDs (MID) must be 0x18xx
*/
#define ADCS_CMD_MID 0x18D4

/*
** This MID is for commands telling the app to publish its telemetry message
*/
#define ADCS_REQ_HK_MID 0x18D5

/*
** CCSDS V1 Telemetry Message IDs must be 0x08xx
*/
#define ADCS_HK_TLM_MID     0x08D4
#define ADCS_CSS_TLM_MID    0x08D5
#define ADCS_FSS_TLM_MID    0x08D6
#define ADCS_GPS_TLM_MID    0x08D7
#define ADCS_IMU_TLM_MID    0x08D8
#define ADCS_MAG_TLM_MID    0x08D9
#define ADCS_MTB_TLM_MID    0x08DA
#define ADCS_RW_TLM_MID     0x08DB
#define ADCS_ST_TLM_MID     0x08DC

#endif /* _ADCS_MSGIDS_H_ */
