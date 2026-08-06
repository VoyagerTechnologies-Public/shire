#ifndef _EPS_DEVICE_H_
#define _EPS_DEVICE_H_

/*
** Required header files.
*/
#include <stdbool.h>
#include "device_cfg.h"
#include "hwlib.h"
#include "libi2c.h"

/*
** EPS Command definitions (as per README)
*/
#define EPS_CMD_NOOP        0x00
#define EPS_CMD_GET_HK      0x01
#define EPS_CMD_SWITCH_OFF  0x02
#define EPS_CMD_SWITCH_ON   0x03

/*
** EPS Switch definitions
*/
#define EPS_NUM_SWITCHES    8
#define EPS_SWITCH_OFF      0
#define EPS_SWITCH_ON       1

/*
** EPS Command packet structure
*/
typedef struct
{
    uint8_t i2c_addr;    /* I2C address (0x1E) */
    uint8_t command;     /* Command code */
    uint8_t payload;     /* Payload (switch number for switch commands) */
    uint8_t crc;         /* CRC-8-CCITT */
} __attribute__((packed)) EPS_Command_t;
#define EPS_COMMAND_SIZE sizeof(EPS_Command_t)

/*
** EPS switch telemetry structure
*/
typedef struct
{
    uint8_t state;       /* 0=off, 1=on, else=fault */
    uint8_t voltage;     /* 32V / 255 = 0.125V per count */
    uint8_t current;     /* 10A / 255 = 0.0392A per count */
} __attribute__((packed)) EPS_Switch_tlm_t;

/*
** EPS housekeeping telemetry definition (as per README)
*/
typedef struct
{
    uint8_t battery_voltage;      /* 32V / 255 = 0.12549V per count */
    uint8_t battery_temperature;  /* 250C / 255 = 0.9804C per count */
    uint8_t solar_voltage;        /* 32V / 255 = 0.125V per count */
    uint8_t solar_temperature;    /* 250C / 255 = 0.9804C per count */
    EPS_Switch_tlm_t switches[EPS_NUM_SWITCHES];  /* 8 switches */
    uint8_t crc;                  /* CRC-8-CCITT */
} __attribute__((packed)) EPS_Device_HK_tlm_t;
#define EPS_DEVICE_HK_LNGTH sizeof(EPS_Device_HK_tlm_t)

/*
** CRC-8-CCITT Functions
*/
uint8_t EPS_Calculate_CRC8(const uint8_t *data, size_t length);
bool EPS_Verify_CRC8(const uint8_t *data, size_t length, uint8_t expected_crc);

/*
** Device Interface Functions
*/
int32_t EPS_InitDevice(i2c_bus_info_t *device);
int32_t EPS_CommandDevice(i2c_bus_info_t *device, uint8_t cmd, uint8_t payload);
int32_t EPS_RequestHK(i2c_bus_info_t *device, EPS_Device_HK_tlm_t *data);
int32_t EPS_SetSwitch(i2c_bus_info_t *device, uint8_t switch_num, bool state);

#endif /* _EPS_DEVICE_H_ */
