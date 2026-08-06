#ifndef _RADIO_MSGIDS_H_
#define _RADIO_MSGIDS_H_

/*
** CCSDS V1 Command Message IDs (MID) must be 0x18xx
*/
#define RADIO_CMD_MID 0x18D2

/*
** This MID is for commands telling the app to publish its telemetry message
*/
#define RADIO_REQ_HK_MID 0x18D3

/*
** CCSDS V1 Telemetry Message IDs must be 0x08xx
*/
#define RADIO_HK_TLM_MID     0x08D2

#endif /* _RADIO_MSGIDS_H_ */
