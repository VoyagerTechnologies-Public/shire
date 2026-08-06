#ifndef _DEMO_DEVICE_H_
#define _DEMO_DEVICE_H_

/*
** Required header files.
*/
#include "device_cfg.h"
#include "hwlib.h"

/*
** Type definitions
*/
#define DEMO_DEVICE_HDR_0 0xC0
#define DEMO_DEVICE_HDR_1 0xFF
#define DEMO_DEVICE_HDR   ((DEMO_DEVICE_HDR_0 << 8) | DEMO_DEVICE_HDR_1)

#define DEMO_DEVICE_NOOP_CMD     0x00
#define DEMO_DEVICE_REQ_HK_CMD   0x01
#define DEMO_DEVICE_REQ_DATA_CMD 0x02
#define DEMO_DEVICE_CFG_CMD      0x03

#define DEMO_DEVICE_TRAILER_0 0xFE
#define DEMO_DEVICE_TRAILER_1 0xFE
#define DEMO_DEVICE_TRAILER   ((DEMO_DEVICE_TRAILER_0 << 8) | DEMO_DEVICE_TRAILER_1)

#define DEMO_DEVICE_CMD_SIZE    8
#define DEMO_DEVICE_HDR_TRL_LEN 4

/*
** DEMO device housekeeping telemetry definition
*/
typedef struct
{
    uint16_t DeviceCounter;
    uint16_t DeviceConfig;

} __attribute__((packed)) DEMO_Device_HK_tlm_t;
#define DEMO_DEVICE_HK_LNGTH sizeof(DEMO_Device_HK_tlm_t)
#define DEMO_DEVICE_HK_SIZE  DEMO_DEVICE_HK_LNGTH + DEMO_DEVICE_HDR_TRL_LEN

/*
** DEMO device data telemetry definition
*/
typedef struct
{
    uint16_t Chan1;
    uint16_t Chan2;
    uint16_t Chan3;

} __attribute__((packed)) DEMO_Device_Data_tlm_t;
#define DEMO_DEVICE_DATA_LNGTH sizeof(DEMO_Device_Data_tlm_t)
#define DEMO_DEVICE_DATA_SIZE  DEMO_DEVICE_DATA_LNGTH + DEMO_DEVICE_HDR_TRL_LEN

/*
** Prototypes
*/
int32_t DEMO_ReadData(uart_info_t *device, uint8_t *read_data, uint8_t data_length);
int32_t DEMO_CommandDevice(uart_info_t *device, uint16_t cmd, uint16_t payload);
int32_t DEMO_RequestHK(uart_info_t *device, DEMO_Device_HK_tlm_t *data);
int32_t DEMO_RequestData(uart_info_t *device, DEMO_Device_Data_tlm_t *data);

#endif /* _DEMO_DEVICE_H_ */
