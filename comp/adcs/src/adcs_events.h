#ifndef _ADCS_EVENTS_H_
#define _ADCS_EVENTS_H_

/* Standard app event IDs */
#define ADCS_RESERVED_EID        0
#define ADCS_STARTUP_INF_EID     1
#define ADCS_LEN_ERR_EID         2
#define ADCS_PIPE_ERR_EID        3
#define ADCS_SUB_CMD_ERR_EID     4
#define ADCS_SUB_REQ_HK_ERR_EID  5
#define ADCS_PROCESS_CMD_ERR_EID 6

/* Standard command event IDs */
#define ADCS_CMD_ERR_EID          10
#define ADCS_CMD_NOOP_INF_EID     11
#define ADCS_CMD_RESET_INF_EID    12
#define ADCS_ENABLE_INF_EID       13
#define ADCS_ENABLE_ERR_EID       14
#define ADCS_DISABLE_INF_EID      15
#define ADCS_DISABLE_ERR_EID      16
#define ADCS_CMD_DISABLED_ERR_EID 17

/* Device specific command event IDs */
#define ADCS_SET_MODE_INF_EID    20
#define ADCS_SET_MODE_ERR_EID    21
#define ADCS_SET_TARGET_INF_EID  22
#define ADCS_SET_TARGET_ERR_EID  23
#define ADCS_SET_TARGET_VECTOR_INF_EID 24
#define ADCS_SET_TARGET_VECTOR_ERR_EID 25

/* Hardware protocol event IDs */
#define ADCS_UART_INIT_ERR_EID  30
#define ADCS_UART_CLOSE_ERR_EID 31

/* Standard telemetry event IDs */
#define ADCS_DEVICE_TLM_ERR_EID 40
#define ADCS_REQ_HK_ERR_EID     41

/* Device specific telemetry event IDs */
#define ADCS_REQ_DATA_ERR_EID        42
#define ADCS_REQ_DATA_STATUS_ERR_EID 43

/* Table and time synchronization event IDs */
#define ADCS_GPS_TIME_SYNC_INF_EID 50
#define ADCS_TBL_REGISTER_ERR_EID  51
#define ADCS_TBL_LOAD_ERR_EID      52
#define ADCS_TBL_MANAGE_ERR_EID    53
#define ADCS_TBL_GETADDR_ERR_EID   54
#define ADCS_TBL_VALIDATE_ERR_EID  55
#define ADCS_SEND_GAINS_INF_EID    56
#define ADCS_SEND_GAINS_ERR_EID    57

#endif /* _ADCS_EVENTS_H_ */
