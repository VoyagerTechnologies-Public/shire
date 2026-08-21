#ifndef _RADIO_EVENTS_H_
#define _RADIO_EVENTS_H_

/* Standard app event IDs */
#define RADIO_RESERVED_EID        0
#define RADIO_STARTUP_INF_EID     1
#define RADIO_LEN_ERR_EID         2
#define RADIO_PIPE_ERR_EID        3
#define RADIO_SUB_CMD_ERR_EID     4
#define RADIO_SUB_REQ_HK_ERR_EID  5
#define RADIO_PROCESS_CMD_ERR_EID 6

/* Standard command event IDs */
#define RADIO_CMD_ERR_EID         10
#define RADIO_CMD_NOOP_INF_EID    11
#define RADIO_CMD_RESET_INF_EID   12
#define RADIO_ENABLE_INF_EID      13
#define RADIO_ENABLE_ERR_EID      14
#define RADIO_DISABLE_INF_EID     15
#define RADIO_DISABLE_ERR_EID     16

/* Device specific command event IDs */
#define RADIO_CMD_CONFIG_EN_ERR_EID  20
#define RADIO_CMD_CONFIG_VAL_ERR_EID 21
#define RADIO_CMD_CONFIG_INF_EID     22
#define RADIO_CMD_CONFIG_DEV_ERR_EID 23

/* Hardware protocol event IDs */
#define RADIO_UART_INIT_ERR_EID  30
#define RADIO_UART_CLOSE_ERR_EID 31

/* Standard telemetry event IDs */
#define RADIO_DEVICE_TLM_ERR_EID 40
#define RADIO_REQ_HK_ERR_EID     41

/* Device specific telemetry event IDs */
#define RADIO_REQ_DATA_ERR_EID        42
#define RADIO_REQ_DATA_STATUS_ERR_EID 43

#endif /* _RADIO_EVENTS_H_ */
