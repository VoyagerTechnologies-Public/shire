/*******************************************************************************
** File: adcs_cli.c
**
** Purpose:
**   This checkout can be run without cFS and is used to quickly develop and
**   test functions required for a specific component.
**
*******************************************************************************/

/*
** Include Files
*/
#include "adcs_cli.h"

/*
** Global Variables
*/
uart_info_t            AdcsUart;
ADCS_Device_HK_tlm_t   AdcsHK;
ADCS_Device_Data_tlm_t AdcsData;

/*
** Component Functions
*/
void ADCS_print_help(void)
{
    printf(PROMPT "command [args]\n"
                  "---------------------------------------------------------------------\n"
                  "help                               - Display help                    \n"
                  "exit                               - Exit app                        \n"
                  "noop                               - No operation command to device  \n"
                  "  n                                - ^                               \n"
                  "hk                                 - Request device housekeeping     \n"
                  "  h                                - ^                               \n"
                  "adcs                               - Request adcs (CSS) data         \n"
                  "  s                                - ^                               \n"
                  "mode #                             - Set ADCS mode (0..5)           \n"
                  "  m #                              - ^                               \n"
                  "target #                           - Set target id/value            \n"
                  "  t #                              - ^                               \n"
                  "css                                - Request CSS sensor data        \n"
                  "  css                              - ^                               \n"
                  "\n");
}

int ADCS_get_command(const char *str)
{
    int  status = CMD_UNKNOWN;
    char lcmd[MAX_INPUT_TOKEN_SIZE + 1];
    strncpy(lcmd, str, MAX_INPUT_TOKEN_SIZE);

    /* Convert command to lower case */
    ADCS_to_lower(lcmd);

    if (strcmp(lcmd, "help") == 0)
    {
        status = CMD_HELP;
    }
    else if (strcmp(lcmd, "exit") == 0)
    {
        status = CMD_EXIT;
    }
    else if (strcmp(lcmd, "noop") == 0)
    {
        status = CMD_NOOP;
    }
    else if (strcmp(lcmd, "n") == 0)
    {
        status = CMD_NOOP;
    }
    else if (strcmp(lcmd, "hk") == 0)
    {
        status = CMD_HK;
    }
    else if (strcmp(lcmd, "h") == 0)
    {
        status = CMD_HK;
    }
    else if (strcmp(lcmd, "adcs") == 0)
    {
        status = CMD_ADCS;
    }
    else if (strcmp(lcmd, "s") == 0)
    {
        status = CMD_ADCS;
    }
    else if (strcmp(lcmd, "mode") == 0)
    {
        status = CMD_SET_MODE;
    }
    else if (strcmp(lcmd, "m") == 0)
    {
        status = CMD_SET_MODE;
    }
    else if (strcmp(lcmd, "target") == 0)
    {
        status = CMD_SET_TARGET;
    }
    else if (strcmp(lcmd, "t") == 0)
    {
        status = CMD_SET_TARGET;
    }
    else if (strcmp(lcmd, "css") == 0)
    {
        status = CMD_GET_CSS;
    }
    /* 'cfg' command removed - configuration now uses set mode/target */
    return status;
}

int ADCS_process_command(int cc, int num_tokens, char tokens[MAX_INPUT_TOKENS][MAX_INPUT_TOKEN_SIZE])
{
    int32_t  status      = OS_SUCCESS;
    int32_t  exit_status = OS_SUCCESS;

    /* Process command */
    switch (cc)
    {
        case CMD_HELP:
            ADCS_print_help();
            break;

        case CMD_EXIT:
            exit_status = OS_ERROR;
            break;

        case CMD_NOOP:
            if (ADCS_check_number_arguments(num_tokens, 0) == OS_SUCCESS)
            {
                status = ADCS_CommandDevice(&AdcsUart, ADCS_DEVICE_NOOP_CMD, 0);
                if (status == OS_SUCCESS)
                {
                    OS_printf("NOOP command success\n");
                }
                else
                {
                    OS_printf("NOOP command failed with error %d!\n", status);
                }
            }
            break;

        case CMD_HK:
            if (ADCS_check_number_arguments(num_tokens, 0) == OS_SUCCESS)
            {
                status = ADCS_RequestHK(&AdcsUart, &AdcsHK);
                if (status == OS_SUCCESS)
                {
                    OS_printf("ADCS_RequestHK command success\n");
                    ADCS_PrintHK(&AdcsHK);
                }
                else
                {
                    OS_printf("ADCS_RequestHK command failed!\n");
                }
            }
            break;

        case CMD_ADCS:
            if (ADCS_check_number_arguments(num_tokens, 0) == OS_SUCCESS)
            {
                status = ADCS_RequestData(&AdcsUart, &AdcsData, ADCS_DEVICE_GET_CSS_CMD);
                if (status == OS_SUCCESS)
                {
                    OS_printf("ADCS_RequestData (CSS) command success\n");
                }
                else
                {
                    OS_printf("ADCS_RequestData command failed!\n");
                }
            }
            break;

        case CMD_SET_MODE:
            if (ADCS_check_number_arguments(num_tokens, 1) == OS_SUCCESS)
            {
                uint16_t mode = (uint16_t)atoi(tokens[0]);
                status = ADCS_CommandDevice(&AdcsUart, ADCS_DEVICE_SET_MODE_CMD, mode);
                if (status == OS_SUCCESS)
                {
                    OS_printf("Set mode command success (mode=%u)\n", mode);
                }
                else
                {
                    OS_printf("Set mode command failed!\n");
                }
            }
            break;

        case CMD_SET_TARGET:
            if (ADCS_check_number_arguments(num_tokens, 1) == OS_SUCCESS)
            {
                uint16_t target = (uint16_t)atoi(tokens[0]);
                status = ADCS_CommandDevice(&AdcsUart, ADCS_DEVICE_SET_TARGET_CMD, target);
                if (status == OS_SUCCESS)
                {
                    OS_printf("Set target command success (target=%u)\n", target);
                }
                else
                {
                    OS_printf("Set target command failed!\n");
                }
            }
            break;

        case CMD_GET_CSS:
            if (ADCS_check_number_arguments(num_tokens, 0) == OS_SUCCESS)
            {
                status = ADCS_RequestData(&AdcsUart, &AdcsData, ADCS_DEVICE_GET_CSS_CMD);
                if (status == OS_SUCCESS)
                {
                    OS_printf("CSS data request success\n");
                }
                else
                {
                    OS_printf("CSS data request failed!\n");
                }
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

    /* Initialize UART */
    AdcsUart.deviceString = ADCS_CFG_STRING;
    AdcsUart.handle = ADCS_CFG_HANDLE;
    AdcsUart.isOpen = PORT_CLOSED;
    AdcsUart.baud = ADCS_CFG_BAUDRATE_HZ;
    AdcsUart.access_option = uart_access_flag_RDWR;

    OS_printf("Delay for UART initialization...\n");
    sleep(3);

    status = uart_init_port(&AdcsUart);
    if (status == OS_SUCCESS)
    {
        printf("UART device %s configured with baudrate %d \n", AdcsUart.deviceString, AdcsUart.baud);
    }
    else
    {
        printf("UART device %s failed to initialize! \n", AdcsUart.deviceString);
        run_status = OS_ERROR;
    }

    /* Main loop */
    ADCS_print_help();
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
                cmd = ADCS_get_command(token_ptr);
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
            run_status = ADCS_process_command(cmd, num_input_tokens, input_tokens);
        }
    }

    // Close the device
    if (AdcsUart.isOpen == PORT_OPEN)
    {
        uart_close_port(&AdcsUart);
        AdcsUart.isOpen = PORT_CLOSED;
    }

    return status;
}

/*
** Generic Functions
*/
int ADCS_check_number_arguments(int actual, int expected)
{
    int status = OS_SUCCESS;
    if (actual != expected)
    {
        status = OS_ERROR;
        OS_printf("Invalid command format, type 'help' for more info\n");
    }
    return status;
}

void ADCS_to_lower(char *str)
{
    char *ptr = str;
    while (*ptr)
    {
        *ptr = (char) tolower((unsigned char)*ptr);
        ptr++;
    }
    return;
}
