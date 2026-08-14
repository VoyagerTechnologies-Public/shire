/*
** File: adcs_cli.h
** Minimal header for ADCS CLI
*/
#ifndef _ADCS_CLI_H_
#define _ADCS_CLI_H_

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
#include "adcs_device.h"

#if TGTNAME == cpu1
#include "simulith_transport.h"
#endif

/*
** Standard Defines
*/
#define PROMPT               "adcs> "
#define MAX_INPUT_BUF        512
#define MAX_INPUT_TOKENS     64
#define MAX_INPUT_TOKEN_SIZE 50

/*
** Command Defines
*/
#define CMD_UNKNOWN -1
#define CMD_HELP     0
#define CMD_EXIT     1
#define CMD_NOOP     2
#define CMD_HK       3
#define CMD_ADCS     4
#define CMD_SET_MODE 5
#define CMD_SET_TARGET 6
#define CMD_GET_CSS  7

/*
** Prototypes
*/
void ADCS_print_help(void);
int  ADCS_get_command(const char *str);
int  ADCS_process_command(int cc, int num_tokens, char tokens[MAX_INPUT_TOKENS][MAX_INPUT_TOKEN_SIZE]);
int  main(int argc, char *argv[]);
int  ADCS_check_number_arguments(int actual, int expected);
void ADCS_to_lower(char *str);

#endif /* _ADCS_CLI_H_ */
