/*******************************************************************************
** File: demo_cli.c
**
** Purpose:
**   This checkout can be run without cFS and is used to quickly develop and
**   test functions required for a specific component.
**
*******************************************************************************/

/*
** Include Files
*/
#include "demo_cli.h"

/*
** Global Variables
*/
uart_info_t            DemoUart;
DEMO_Device_HK_tlm_t   DemoHK;
DEMO_Device_Data_tlm_t DemoData;

/*
** Component Functions
*/
void DEMO_print_help(void)
{
    printf(PROMPT "command [args]\n"
                  "---------------------------------------------------------------------\n"
                  "help                               - Display help                    \n"
                  "exit                               - Exit app                        \n"
                  "noop                               - No operation command to device  \n"
                  "  n                                - ^                               \n"
                  "hk                                 - Request device housekeeping     \n"
                  "  h                                - ^                               \n"
                  "demo                               - Request demo data               \n"
                  "  s                                - ^                               \n"
                  "cfg #                              - Send configuration #            \n"
                  "  c #                              - ^                               \n"
                  "\n");
}

int DEMO_get_command(const char *str)
{
    int  status = CMD_UNKNOWN;
    char lcmd[MAX_INPUT_TOKEN_SIZE + 1];
    strncpy(lcmd, str, MAX_INPUT_TOKEN_SIZE);

    /* Convert command to lower case */
    DEMO_to_lower(lcmd);

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
    else if (strcmp(lcmd, "demo") == 0)
    {
        status = CMD_DEMO;
    }
    else if (strcmp(lcmd, "s") == 0)
    {
        status = CMD_DEMO;
    }
    else if (strcmp(lcmd, "cfg") == 0)
    {
        status = CMD_CFG;
    }
    else if (strcmp(lcmd, "c") == 0)
    {
        status = CMD_CFG;
    }
    return status;
}

int DEMO_process_command(int cc, int num_tokens, char tokens[MAX_INPUT_TOKENS][MAX_INPUT_TOKEN_SIZE])
{
    int32_t  status      = OS_SUCCESS;
    int32_t  exit_status = OS_SUCCESS;
    uint16_t config;

    /* Process command */
    switch (cc)
    {
        case CMD_HELP:
            DEMO_print_help();
            break;

        case CMD_EXIT:
            exit_status = OS_ERROR;
            break;

        case CMD_NOOP:
            if (DEMO_check_number_arguments(num_tokens, 0) == OS_SUCCESS)
            {
                status = DEMO_CommandDevice(&DemoUart, DEMO_DEVICE_NOOP_CMD, 0);
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
            if (DEMO_check_number_arguments(num_tokens, 0) == OS_SUCCESS)
            {
                status = DEMO_RequestHK(&DemoUart, &DemoHK);
                if (status == OS_SUCCESS)
                {
                    OS_printf("DEMO_RequestHK command success\n");
                }
                else
                {
                    OS_printf("DEMO_RequestHK command failed!\n");
                }
            }
            break;

        case CMD_DEMO:
            if (DEMO_check_number_arguments(num_tokens, 0) == OS_SUCCESS)
            {
                status = DEMO_RequestData(&DemoUart, &DemoData);
                if (status == OS_SUCCESS)
                {
                    OS_printf("DEMO_RequestData command success\n");
                }
                else
                {
                    OS_printf("DEMO_RequestData command failed!\n");
                }
            }
            break;

        case CMD_CFG:
            if (DEMO_check_number_arguments(num_tokens, 1) == OS_SUCCESS)
            {
                config = (uint16_t)atoi(tokens[0]);
                status = DEMO_CommandDevice(&DemoUart, DEMO_DEVICE_CFG_CMD, config);
                if (status == OS_SUCCESS)
                {
                    OS_printf("Configuration command success with value %u\n", config);
                }
                else
                {
                    OS_printf("Configuration command failed!\n");
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
    int     run_status = OS_SUCCESS; /* signed to avoid conversion warnings */

    /* Initialize UART */
    DemoUart.deviceString = DEMO_CFG_STRING;
    DemoUart.handle = DEMO_CFG_HANDLE;
    DemoUart.isOpen = PORT_CLOSED;
    DemoUart.baud = DEMO_CFG_BAUDRATE_HZ;
    DemoUart.access_option = uart_access_flag_RDWR;

    OS_printf("Delay for UART initialization...\n");
    sleep(3);

    status = uart_init_port(&DemoUart);
    if (status == OS_SUCCESS)
    {
        printf("UART device %s configured with baudrate %d \n", DemoUart.deviceString, DemoUart.baud);
    }
    else
    {
        printf("UART device %s failed to initialize! \n", DemoUart.deviceString);
        run_status = OS_ERROR;
    }

    /* Main loop */
    DEMO_print_help();
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
                cmd = DEMO_get_command(token_ptr);
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
            run_status = DEMO_process_command(cmd, num_input_tokens, input_tokens);
        }
    }

    // Close the device
    if (DemoUart.isOpen == PORT_OPEN)
    {
        uart_close_port(&DemoUart);
        DemoUart.isOpen = PORT_CLOSED;
    }

    return status;
}

/*
** Generic Functions
*/
int DEMO_check_number_arguments(int actual, int expected)
{
    int status = OS_SUCCESS;
    if (actual != expected)
    {
        status = OS_ERROR;
        OS_printf("Invalid command format, type 'help' for more info\n");
    }
    return status;
}

void DEMO_to_lower(char *str)
{
    char *ptr = str;
    while (*ptr)
    {
        *ptr = (char) tolower((unsigned char)*ptr);
        ptr++;
    }
    return;
}
