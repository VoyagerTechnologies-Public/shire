#ifndef _EPS_MSGIDS_H_
#define _EPS_MSGIDS_H_

/*
** CCSDS V1 Command Message IDs (MID) must be 0x18xx
*/
#define EPS_CMD_MID 0x18D0

/*
** This MID is for commands telling the app to publish its telemetry message
*/
#define EPS_REQ_HK_MID 0x18D1

/*
** CCSDS V1 Telemetry Message IDs must be 0x08xx
*/
#define EPS_HK_TLM_MID     0x08D0
#define EPS_DEVICE_TLM_MID 0x08D1

#endif /* _EPS_MSGIDS_H_ */
