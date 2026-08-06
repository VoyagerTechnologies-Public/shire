#ifndef _DEMO_EVENTS_H_
#define _DEMO_EVENTS_H_

/* Standard app event IDs */
#define DEMO_RESERVED_EID        0
#define DEMO_STARTUP_INF_EID     1
#define DEMO_LEN_ERR_EID         2
#define DEMO_PIPE_ERR_EID        3
#define DEMO_SUB_CMD_ERR_EID     4
#define DEMO_SUB_REQ_HK_ERR_EID  5
#define DEMO_PROCESS_CMD_ERR_EID 6

/* Standard command event IDs */
#define DEMO_CMD_ERR_EID         10
#define DEMO_CMD_NOOP_INF_EID    11
#define DEMO_CMD_RESET_INF_EID   12
#define DEMO_ENABLE_INF_EID      13
#define DEMO_ENABLE_ERR_EID      14
#define DEMO_DISABLE_INF_EID     15
#define DEMO_DISABLE_ERR_EID     16

/* Device specific command event IDs */
#define DEMO_CMD_CONFIG_EN_ERR_EID  20
#define DEMO_CMD_CONFIG_VAL_ERR_EID 21
#define DEMO_CMD_CONFIG_INF_EID     22
#define DEMO_CMD_CONFIG_DEV_ERR_EID 23

/* Hardware protocol event IDs */
#define DEMO_UART_INIT_ERR_EID  30
#define DEMO_UART_CLOSE_ERR_EID 31

/* Standard telemetry event IDs */
#define DEMO_DEVICE_TLM_ERR_EID 40
#define DEMO_REQ_HK_ERR_EID     41

/* Device specific telemetry event IDs */
#define DEMO_REQ_DATA_ERR_EID        42
#define DEMO_REQ_DATA_STATUS_ERR_EID 43

#endif /* _DEMO_EVENTS_H_ */
