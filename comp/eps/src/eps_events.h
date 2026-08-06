#ifndef _EPS_EVENTS_H_
#define _EPS_EVENTS_H_

/* Standard app event IDs */
#define EPS_RESERVED_EID        0
#define EPS_STARTUP_INF_EID     1
#define EPS_LEN_ERR_EID         2
#define EPS_PIPE_ERR_EID        3
#define EPS_SUB_CMD_ERR_EID     4
#define EPS_SUB_REQ_HK_ERR_EID  5
#define EPS_PROCESS_CMD_ERR_EID 6

/* Standard command event IDs */
#define EPS_CMD_ERR_EID         10
#define EPS_CMD_NOOP_INF_EID    11
#define EPS_CMD_RESET_INF_EID   12

/* Device specific command event IDs */
#define EPS_CMD_SWITCH_ON_INF_EID  13
#define EPS_CMD_SWITCH_ON_ERR_EID  14
#define EPS_CMD_SWITCH_OFF_INF_EID 15
#define EPS_CMD_SWITCH_OFF_ERR_EID 16

/* Hardware protocol event IDs */
#define EPS_I2C_INIT_ERR_EID  30
#define EPS_I2C_CLOSE_ERR_EID 31

/* Standard telemetry event IDs */
#define EPS_DEVICE_TLM_ERR_EID 40
#define EPS_REQ_HK_ERR_EID     41

/* Device specific telemetry event IDs */
#define EPS_REQ_DATA_ERR_EID        42
#define EPS_REQ_DATA_STATUS_ERR_EID 43

#endif /* _EPS_EVENTS_H_ */
