/*******************************************************************************
** File: radio_cli.h
**
** Purpose:
**   This is the header file for the RADIO checkout.
**
*******************************************************************************/
#ifndef _RADIO_CHECKOUT_H_
#define _RADIO_CHECKOUT_H_

/*
** Includes
*/
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>

#include "hwlib.h"
#include "libgpio.h"
#include "libspi.h"
#include "device_cfg.h"
#include "radio_device.h"

#if TGTNAME == cpu1
#include "simulith_transport.h"
#endif

/*
** Standard Defines
*/
#define PROMPT               "radio> "
#define MAX_INPUT_BUF        512
#define MAX_INPUT_TOKENS     64
#define MAX_INPUT_TOKEN_SIZE 50
#define TELEM_BUF_LEN        8

/*
** Command Defines
*/
#define CMD_UNKNOWN  -1
#define CMD_HELP     0
#define CMD_EXIT     1
#define CMD_NOOP     2
#define CMD_HK       3
#define CMD_CFG      4
#define CMD_SEND     5
#define CMD_RECEIVE  6
#define CMD_POWER_ON 7
#define CMD_POWER_OFF 8

/*
** Prototypes
*/
void RADIO_print_help(void);
int  RADIO_get_command(const char *str);
int  RADIO_process_command(int cc, int num_tokens, char tokens[MAX_INPUT_TOKENS][MAX_INPUT_TOKEN_SIZE]);
int  main(int argc, char *argv[]);
int  RADIO_check_number_arguments(int actual, int expected);
void RADIO_to_lower(char *str);

#endif /* _RADIO_CHECKOUT_H_ */
