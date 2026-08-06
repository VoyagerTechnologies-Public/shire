/*******************************************************************************
** File: eps_cli.c
**
** Purpose:
**   This checkout can be run without cFS and is used to quickly develop and
**   test functions required for a specific component.
**
*******************************************************************************/

/*
** Include Files
*/
#include "eps_cli.h"

/*
** Global Variables
*/
i2c_bus_info_t           EpsI2c;
EPS_Device_HK_tlm_t      EpsHK;

/*
** Component Functions
*/
void EPS_print_help(void)
{
    printf(PROMPT "command [args]\n"
                  "---------------------------------------------------------------------\n"
                  "help                               - Display help                    \n"
                  "exit                               - Exit app                        \n"
                  "noop                               - No operation command to device  \n"
                  "hk                                 - Request device housekeeping     \n"
                  "  h                                                                  \n"
                  "switch_on #                        - Set switch # ON                 \n"
                  "  on #                                                               \n"
                  "switch_off #                       - Set switch # OFF                \n"
                  "  off #                                                              \n"
                  "\n");
}

int EPS_get_command(const char *str)
{
    int  status = CMD_UNKNOWN;
    char lcmd[MAX_INPUT_TOKEN_SIZE + 1];
    strncpy(lcmd, str, MAX_INPUT_TOKEN_SIZE);

    /* Convert command to lower case */
    EPS_to_lower(lcmd);

    if (strcmp(lcmd, "help") == 0)
        status = CMD_HELP;
    else if (strcmp(lcmd, "exit") == 0)
        status = CMD_EXIT;
    else if (strcmp(lcmd, "noop") == 0)
        status = CMD_NOOP;
    else if (strcmp(lcmd, "hk") == 0)
        status = CMD_HK;
    else if (strcmp(lcmd, "h") == 0)
        status = CMD_HK;
    else if (strcmp(lcmd, "switch_on") == 0)
        status = CMD_SWITCH_ON;
    else if (strcmp(lcmd, "on") == 0)
        status = CMD_SWITCH_ON;
    else if (strcmp(lcmd, "switch_off") == 0)
        status = CMD_SWITCH_OFF;
    else if (strcmp(lcmd, "off") == 0)
        status = CMD_SWITCH_OFF;
    return status;
}

int EPS_process_command(int cc, int num_tokens, char tokens[MAX_INPUT_TOKENS][MAX_INPUT_TOKEN_SIZE])
{
    int32_t  status      = OS_SUCCESS;
    int32_t  exit_status = OS_SUCCESS;
    uint8_t switch_num;

    /* Process command */
    switch (cc)
    {
        case CMD_HELP:
            EPS_print_help();
            break;

        case CMD_EXIT:
            exit_status = OS_ERROR;
            break;

        case CMD_NOOP:
            if (EPS_check_number_arguments(num_tokens, 0) == OS_SUCCESS)
            {
                status = EPS_CommandDevice(&EpsI2c, EPS_CMD_NOOP, 0);
                if (status == OS_SUCCESS)
                    OS_printf("NOOP command success\n");
                else
                    OS_printf("NOOP command failed with error %d!\n", status);
            }
            break;

        case CMD_HK:
            if (EPS_check_number_arguments(num_tokens, 0) == OS_SUCCESS)
            {
                status = EPS_RequestHK(&EpsI2c, &EpsHK);
                if (status != OS_SUCCESS)
                    OS_printf("EPS_RequestHK command failed!\n");
            }
            break;

        case CMD_SWITCH_ON:
            if (EPS_check_number_arguments(num_tokens, 1) == OS_SUCCESS)
            {
                switch_num = (uint8_t) atoi(tokens[0]);
                status = EPS_SetSwitch(&EpsI2c, switch_num, true);
                if (status == OS_SUCCESS)
                    OS_printf("Switch %d ON command success\n", switch_num);
                else
                    OS_printf("Switch %d ON command failed!\n", switch_num);
            }
            break;

        case CMD_SWITCH_OFF:
            if (EPS_check_number_arguments(num_tokens, 1) == OS_SUCCESS)
            {
                switch_num = (uint8_t) atoi(tokens[0]);
                status = EPS_SetSwitch(&EpsI2c, switch_num, false);
                if (status == OS_SUCCESS)
                    OS_printf("Switch %d OFF command success\n", switch_num);
                else
                    OS_printf("Switch %d OFF command failed!\n", switch_num);
            }
            break;

        default:
            OS_printf("Invalid command format, type 'help' for more info\n");
            break;
    }
    return exit_status;
}

int main(int argc, char *argv[])
{
    int     status = OS_SUCCESS;
    char    input_buf[MAX_INPUT_BUF];
    char    input_tokens[MAX_INPUT_TOKENS][MAX_INPUT_TOKEN_SIZE];
    int     num_input_tokens;
    int     cmd;
    char   *token_ptr;
    int     run_status = OS_SUCCESS;

    /* Initialize I2C */
    status = EPS_InitDevice(&EpsI2c);
    if (status == OS_SUCCESS)
    {
        printf("I2C device initialized: handle=%d, addr=0x%02X, speed=%d\n", EpsI2c.handle, EpsI2c.addr, EpsI2c.speed);
        EpsI2c.isOpen = I2C_OPEN;
    }
    else
    {
        printf("I2C device failed to initialize!\n");
        run_status = OS_ERROR;
    }

    /* Main loop */
    EPS_print_help();
    while (run_status == OS_SUCCESS)
    {
        num_input_tokens = -1;
        cmd              = CMD_UNKNOWN;

        /* Read user input */
        printf(PROMPT);
        if (fgets(input_buf, MAX_INPUT_BUF, stdin) == NULL)
        {
            /* EOF or error on stdin - exit the loop */
            OS_printf("End of input or read error, exiting...\n");
            run_status = OS_ERROR;
            break;
        }

        /* Tokenize line buffer */
        token_ptr = strtok(input_buf, " \t\n");
        while ((num_input_tokens < MAX_INPUT_TOKENS) && (token_ptr != NULL))
        {
            if (num_input_tokens == -1)
            {
                /* First token is command */
                cmd = EPS_get_command(token_ptr);
            }
            else
            {
                strncpy(input_tokens[num_input_tokens], token_ptr, MAX_INPUT_TOKEN_SIZE);
            }
            token_ptr = strtok(NULL, " \t\n");
            num_input_tokens++;
        }

        /* Process command if valid */
        if (num_input_tokens >= 0)
        {
            /* Process command */
            run_status = EPS_process_command(cmd, num_input_tokens, input_tokens);
        }
    }

    // Close the device
    if (EpsI2c.isOpen == I2C_OPEN)
    {
        i2c_master_close(&EpsI2c);
        EpsI2c.isOpen = I2C_CLOSED;
    }

    return status;
}

/*
** Generic Functions
*/
int EPS_check_number_arguments(int actual, int expected)
{
    int status = OS_SUCCESS;
    if (actual != expected)
    {
        status = OS_ERROR;
        OS_printf("Invalid command format, type 'help' for more info\n");
    }
    return status;
}

void EPS_to_lower(char *str)
{
    char *ptr = str;
    while (*ptr)
    {
        *ptr = (char) tolower((unsigned char)*ptr);
        ptr++;
    }
    return;
}
