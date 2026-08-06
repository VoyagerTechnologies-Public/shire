#ifndef _RADIO_DEVICE_H_
#define _RADIO_DEVICE_H_

/*
** Required header files.
*/
#include "device_cfg.h"
#include "hwlib.h"
#include "libspi.h"
#include "libgpio.h"

/*
** Type definitions
*/
#define RADIO_DEVICE_HDR    0xAA
#define RADIO_DEVICE_TRAILER 0x11

#define RADIO_DEVICE_NOOP_CMD        0x00
#define RADIO_DEVICE_REQ_HK_CMD      0x01
#define RADIO_DEVICE_SET_CFG_CMD     0x02
#define RADIO_DEVICE_RECEIVE_CMD     0x03
#define RADIO_DEVICE_SEND_CMD        0x04

/* Radio modes */
#define RADIO_MODE_SLEEP  0
#define RADIO_MODE_TX     1
#define RADIO_MODE_RX     2
#define RADIO_MODE_DUPLEX 3

/* Device command payload sizes */
#define RADIO_MAX_PAYLOAD_SIZE 1024
#define RADIO_CFG_PAYLOAD_SIZE 5
#define RADIO_RECEIVE_PAYLOAD_SIZE 1

/*
** RADIO device housekeeping telemetry definition
*/
typedef struct
{
    uint16_t CommandCounter;        /* Number of commands accepted */
    uint8_t  Mode;                  /* 0 Sleep, 1 TX, 2 RX, 3 Duplex */
    uint8_t  GroundLock;           /* Ground lock status */
    uint8_t  RxSpeedSetting;       /* RX speed setting (TBD) */
    uint8_t  RxWavelengthSetting;  /* RX wavelength setting (TBD) */
    uint8_t  TxSpeedSetting;       /* TX speed setting (TBD) */
    uint8_t  TxWavelengthSetting;  /* TX wavelength setting (TBD) */
    uint32_t BytesInRxBuffer;      /* Bytes in received buffer */
    uint32_t BytesReceived;        /* Total bytes received */
    uint32_t BytesSent;            /* Total bytes sent */

} __attribute__((packed)) RADIO_Device_HK_tlm_t;
#define RADIO_DEVICE_HK_LNGTH sizeof(RADIO_Device_HK_tlm_t)
#define RADIO_DEVICE_HK_SIZE  RADIO_DEVICE_HK_LNGTH + 2 /* Header + Trailer */

/*
** RADIO device configuration structure
*/
typedef struct
{
    uint8_t Mode;                  /* 0 Sleep, 1 TX, 2 RX, 3 Duplex */
    uint8_t RxSpeedSetting;        /* RX speed setting (TBD) */
    uint8_t RxWavelengthSetting;   /* RX wavelength setting (TBD) */
    uint8_t TxSpeedSetting;        /* TX speed setting (TBD) */
    uint8_t TxWavelengthSetting;   /* TX wavelength setting (TBD) */

} __attribute__((packed)) RADIO_Device_Config_t;

/*
** RADIO device command structure
*/
typedef struct
{
    uint16_t Header;               /* 0xABAC */
    uint8_t  Command;              /* Command code */
    uint8_t  PayloadLength;        /* Payload length in bytes */
    uint8_t  Payload[RADIO_MAX_PAYLOAD_SIZE];  /* Variable payload */
    uint16_t Trailer;              /* 0xADAB */

} __attribute__((packed)) RADIO_Device_Command_t;

/*
** Prototypes
*/
int32_t RADIO_InitDevice(spi_info_t *spi_device, gpio_info_t *power_gpio, gpio_info_t *interrupt_gpio);
int32_t RADIO_CommandDevice(spi_info_t *device, uint8_t cmd, uint16_t payload_len, uint8_t *payload);
int32_t RADIO_RequestHK(spi_info_t *device, RADIO_Device_HK_tlm_t *data);
int32_t RADIO_SetConfiguration(spi_info_t *device, RADIO_Device_Config_t *config);
int32_t RADIO_SendData(spi_info_t *device, uint8_t *data, uint16_t data_length);
int32_t RADIO_ReceiveData(spi_info_t *device, uint8_t *data, uint16_t max_length, uint16_t *actual_length);
int32_t RADIO_CheckInterrupt(gpio_info_t *interrupt_gpio, uint8_t *interrupt_status);
int32_t RADIO_PowerOn(gpio_info_t *power_gpio);
int32_t RADIO_PowerOff(gpio_info_t *power_gpio);

#endif /* _RADIO_DEVICE_H_ */
