/*******************************************************************************
** File: eps_cli.h
**
** Purpose:
**   This is the header file for the EPS checkout.
**
*******************************************************************************/
#ifndef _EPS_CLI_H_
#define _EPS_CLI_H_

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
#include <fcntl.h>
#include <time.h>

#include "hwlib.h"
#include "device_cfg.h"
#include "eps_device.h"

#if TGTNAME == cpu1
#include "simulith_transport.h"
#endif

/*
** Standard Defines
*/
#define PROMPT               "eps> "
#define MAX_INPUT_BUF        512
#define MAX_INPUT_TOKENS     64
#define MAX_INPUT_TOKEN_SIZE 50
#define TELEM_BUF_LEN        8

/*
** Command Defines
*/
#define CMD_UNKNOWN   -1
#define CMD_HELP      0
#define CMD_EXIT      1
#define CMD_NOOP      2
#define CMD_HK        3
#define CMD_SWITCH_ON 4
#define CMD_SWITCH_OFF 5

/*
** Prototypes
*/
void EPS_print_help(void);
int  EPS_get_command(const char *str);
int  EPS_process_command(int cc, int num_tokens, char tokens[MAX_INPUT_TOKENS][MAX_INPUT_TOKEN_SIZE]);
int  main(int argc, char *argv[]);
int  EPS_check_number_arguments(int actual, int expected);
void EPS_to_lower(char *str);

#endif /* _EPS_CLI_H_ */
