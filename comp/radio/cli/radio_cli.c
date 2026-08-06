/*******************************************************************************
** File: radio_cli.c
**
** Purpose:
**   This checkout can be run without cFS and is used to quickly develop and
**   test functions required for a specific component.
**
*******************************************************************************/

/*
** Include Files
*/
#include "radio_cli.h"

/*
** Global Variables
*/
spi_info_t               RadioSpi;
gpio_info_t              RadioPowerGpio;
gpio_info_t              RadioInterruptGpio;
RADIO_Device_HK_tlm_t    RadioHK;
RADIO_Device_Config_t    RadioConfig;

/*
** Component Functions
*/
void RADIO_print_help(void)
{
    printf(PROMPT "command [args]\n"
                  "---------------------------------------------------------------------\n"
                  "help                               - Display help                    \n"
                  "exit                               - Exit app                        \n"
                  "noop                               - No operation command to device  \n"
                  "  n                                - ^                               \n"
                  "hk                                 - Request device housekeeping     \n"
                  "  h                                - ^                               \n"
                  "cfg mode rx_speed rx_wave tx_speed tx_wave - Set configuration      \n"
                  "  c mode rx_speed rx_wave tx_speed tx_wave - ^                      \n"
                  "send \"data\"                       - Send data string               \n"
                  "  s \"data\"                         - ^                               \n"
                  "receive [max_bytes]                - Receive data (default 64)      \n"
                  "  r [max_bytes]                    - ^                               \n"
                  "power_on                           - Turn on radio power            \n"
                  "  pon                              - ^                               \n"
                  "power_off                          - Turn off radio power           \n"
                  "  poff                             - ^                               \n"
                  "\n");
}

int RADIO_get_command(const char *str)
{
    int  status = CMD_UNKNOWN;
    char lcmd[MAX_INPUT_TOKEN_SIZE + 1];
    strncpy(lcmd, str, MAX_INPUT_TOKEN_SIZE);

    /* Convert command to lower case */
    RADIO_to_lower(lcmd);

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
    else if (strcmp(lcmd, "cfg") == 0)
    {
        status = CMD_CFG;
    }
    else if (strcmp(lcmd, "c") == 0)
    {
        status = CMD_CFG;
    }
    else if (strcmp(lcmd, "send") == 0)
    {
        status = CMD_SEND;
    }
    else if (strcmp(lcmd, "s") == 0)
    {
        status = CMD_SEND;
    }
    else if (strcmp(lcmd, "receive") == 0)
    {
        status = CMD_RECEIVE;
    }
    else if (strcmp(lcmd, "r") == 0)
    {
        status = CMD_RECEIVE;
    }
    else if (strcmp(lcmd, "power_on") == 0)
    {
        status = CMD_POWER_ON;
    }
    else if (strcmp(lcmd, "pon") == 0)
    {
        status = CMD_POWER_ON;
    }
    else if (strcmp(lcmd, "power_off") == 0)
    {
        status = CMD_POWER_OFF;
    }
    else if (strcmp(lcmd, "poff") == 0)
    {
        status = CMD_POWER_OFF;
    }
    return status;
}

int RADIO_process_command(int cc, int num_tokens, char tokens[MAX_INPUT_TOKENS][MAX_INPUT_TOKEN_SIZE])
{
    int32_t  status      = OS_SUCCESS;
    int32_t  exit_status = OS_SUCCESS;
    uint8_t  interrupt_status;
    uint8_t  send_data[RADIO_MAX_PAYLOAD_SIZE];
    uint8_t  recv_data[RADIO_MAX_PAYLOAD_SIZE];
    uint16_t  actual_length;
    uint16_t max_length;
    uint32_t i;

    /* Process command */
    switch (cc)
    {
        case CMD_HELP:
            RADIO_print_help();
            break;

        case CMD_EXIT:
            exit_status = OS_ERROR;
            break;

        case CMD_NOOP:
            if (RADIO_check_number_arguments(num_tokens, 0) == OS_SUCCESS)
            {
                status = RADIO_CommandDevice(&RadioSpi, RADIO_DEVICE_NOOP_CMD, 0, NULL);
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
            if (RADIO_check_number_arguments(num_tokens, 0) == OS_SUCCESS)
            {
                status = RADIO_RequestHK(&RadioSpi, &RadioHK);
                if (status == OS_SUCCESS)
                {
                    OS_printf("RADIO_RequestHK command success\n");
                    OS_printf("  Command Counter:    %d\n", RadioHK.CommandCounter);
                    OS_printf("  Mode:               %d\n", RadioHK.Mode);
                    OS_printf("  Ground Lock:        %d\n", RadioHK.GroundLock);
                    OS_printf("  RX Speed:           %d\n", RadioHK.RxSpeedSetting);
                    OS_printf("  RX Wavelength:      %d\n", RadioHK.RxWavelengthSetting);
                    OS_printf("  TX Speed:           %d\n", RadioHK.TxSpeedSetting);
                    OS_printf("  TX Wavelength:      %d\n", RadioHK.TxWavelengthSetting);
                    OS_printf("  Bytes in RX Buffer: %d\n", RadioHK.BytesInRxBuffer);
                    OS_printf("  Bytes Received:     %d\n", RadioHK.BytesReceived);
                    OS_printf("  Bytes Sent:         %d\n", RadioHK.BytesSent);
                }
                else
                {
                    OS_printf("RADIO_RequestHK command failed!\n");
                }
            }
            break;

        case CMD_CFG:
            if (RADIO_check_number_arguments(num_tokens, 5) == OS_SUCCESS)
            {
                RadioConfig.Mode = (uint8_t) atoi(tokens[0]);
                RadioConfig.RxSpeedSetting = (uint8_t) atoi(tokens[1]);
                RadioConfig.RxWavelengthSetting = (uint8_t) atoi(tokens[2]);
                RadioConfig.TxSpeedSetting = (uint8_t) atoi(tokens[3]);
                RadioConfig.TxWavelengthSetting = (uint8_t) atoi(tokens[4]);

                status = RADIO_SetConfiguration(&RadioSpi, &RadioConfig);
                if (status == OS_SUCCESS)
                {
                    OS_printf("Configuration command success\n");
                    OS_printf("  Mode: %d, RX: %d/%d, TX: %d/%d\n", 
                              RadioConfig.Mode, RadioConfig.RxSpeedSetting, RadioConfig.RxWavelengthSetting,
                              RadioConfig.TxSpeedSetting, RadioConfig.TxWavelengthSetting);
                }
                else
                {
                    OS_printf("Configuration command failed!\n");
                }
            }
            break;

        case CMD_SEND:
            if (RADIO_check_number_arguments(num_tokens, 1) == OS_SUCCESS)
            {
                /* Copy string data to send buffer */
                strncpy((char*)send_data, tokens[0], RADIO_MAX_PAYLOAD_SIZE-1);
                send_data[RADIO_MAX_PAYLOAD_SIZE-1] = '\0';

                status = RADIO_SendData(&RadioSpi, send_data, (uint16_t)strlen((char*)send_data));
                if (status == OS_SUCCESS)
                {
                    OS_printf("Send data command success: \"%s\" (%d bytes)\n", send_data, (int)strlen((char*)send_data));
                }
                else
                {
                    OS_printf("Send data command failed!\n");
                }
            }
            break;

        case CMD_RECEIVE:
            max_length = 64; /* Default */
            if (num_tokens == 1)
            {
                max_length = (uint16_t) atoi(tokens[0]);
                if (max_length > RADIO_MAX_PAYLOAD_SIZE)
                {
                    max_length = RADIO_MAX_PAYLOAD_SIZE;
                }
            }
            else if (num_tokens > 1)
            {
                OS_printf("Invalid command format, type 'help' for more info\n");
                break;
            }

            status = RADIO_ReceiveData(&RadioSpi, recv_data, max_length, &actual_length);
            if (status == OS_SUCCESS)
            {
                OS_printf("Receive data command success: %d bytes received\n", actual_length);
                if (actual_length > 0)
                {
                    OS_printf("Data: ");
                    for (i = 0; i < actual_length; i++)
                    {
                        if (recv_data[i] >= 32 && recv_data[i] <= 126) /* Printable */
                        {
                            OS_printf("%c", recv_data[i]);
                        }
                        else
                        {
                            OS_printf("\\x%02x", recv_data[i]);
                        }
                    }
                    OS_printf("\n");
                }
            }
            else
            {
                OS_printf("Receive data command failed!\n");
            }
            break;

        case CMD_POWER_ON:
            if (RADIO_check_number_arguments(num_tokens, 0) == OS_SUCCESS)
            {
                status = RADIO_PowerOn(&RadioPowerGpio);
                if (status == OS_SUCCESS)
                {
                    OS_printf("Radio power ON\n");
                }
                else
                {
                    OS_printf("Failed to turn on radio power!\n");
                }
            }
            break;

        case CMD_POWER_OFF:
            if (RADIO_check_number_arguments(num_tokens, 0) == OS_SUCCESS)
            {
                status = RADIO_PowerOff(&RadioPowerGpio);
                if (status == OS_SUCCESS)
                {
                    OS_printf("Radio power OFF\n");
                }
                else
                {
                    OS_printf("Failed to turn off radio power!\n");
                }
            }
            break;

        default:
            OS_printf("Invalid command format, type 'help' for more info\n");
            break;
    }

    /* Check interrupt status after each command */
    if (RADIO_CheckInterrupt(&RadioInterruptGpio, &interrupt_status) == OS_SUCCESS)
    {
        if (interrupt_status)
        {
            OS_printf("*** INTERRUPT ACTIVE ***\n");
        }
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

    /* Initialize SPI device */
    RadioSpi.bus = RADIO_CFG_SPI_BUS;
    RadioSpi.cs = RADIO_CFG_SPI_CS;
    RadioSpi.isOpen = SPI_DEVICE_CLOSED;

    /* Initialize GPIO devices */
    RadioPowerGpio.pin = RADIO_CFG_GPIO_POWER_PIN;
    RadioPowerGpio.direction = GPIO_OUTPUT;
    RadioPowerGpio.isOpen = GPIO_CLOSED;

    RadioInterruptGpio.pin = RADIO_CFG_GPIO_INTERRUPT_PIN;
    RadioInterruptGpio.direction = GPIO_INPUT;

    OS_printf("Delay for device initialization...\n");
    sleep(3);

    /* Initialize radio device */
    status = RADIO_InitDevice(&RadioSpi, &RadioPowerGpio, &RadioInterruptGpio);
    if (status == OS_SUCCESS)
    {
        printf("Radio device initialized (SPI Bus %d, CS %d, Power GPIO %d, Interrupt GPIO %d)\n", 
               RadioSpi.bus, RadioSpi.cs, RadioPowerGpio.pin, RadioInterruptGpio.pin);
        
        /* Power on the radio */
        status = RADIO_PowerOn(&RadioPowerGpio);
        if (status == OS_SUCCESS)
        {
            printf("Radio powered on\n");
        }
        else
        {
            printf("Failed to power on radio\n");
            run_status = OS_ERROR;
        }
    }
    else
    {
        printf("Radio device failed to initialize!\n");
        run_status = OS_ERROR;
    }

    /* Main loop */
    RADIO_print_help();
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
                cmd = RADIO_get_command(token_ptr);
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
            run_status = RADIO_process_command(cmd, num_input_tokens, input_tokens);
        }
    }

    // Close the devices
    if (RadioSpi.isOpen == SPI_DEVICE_OPEN)
    {
        spi_close_device(&RadioSpi);
        RadioSpi.isOpen = SPI_DEVICE_CLOSED;
    }
    
    if (RadioPowerGpio.isOpen == GPIO_OPEN)
    {
        RADIO_PowerOff(&RadioPowerGpio);
        RadioPowerGpio.isOpen = GPIO_CLOSED;
    }
    
    if (RadioInterruptGpio.isOpen == GPIO_OPEN)
    {
        gpio_close(&RadioInterruptGpio);
        RadioInterruptGpio.isOpen = GPIO_CLOSED;
    }

    return status;
}

/*
** Generic Functions
*/
int RADIO_check_number_arguments(int actual, int expected)
{
    int status = OS_SUCCESS;
    if (actual != expected)
    {
        status = OS_ERROR;
        OS_printf("Invalid command format, type 'help' for more info\n");
    }
    return status;
}

void RADIO_to_lower(char *str)
{
    char *ptr = str;
    while (*ptr)
    {
        *ptr = (char) tolower((unsigned char)*ptr);
        ptr++;
    }
    return;
}
