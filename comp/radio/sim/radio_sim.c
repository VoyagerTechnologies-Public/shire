#include "radio_sim.h"

/* Radio owns all SPI/GPIO/UDP resources and mutable signal state in its
 * component instance. PREPARE advances tick-driven interrupt behavior and
 * EXECUTE services complete, nonblocking device transactions. It has no 42
 * actuator output, so ACTUATE is intentionally omitted. */

#define RADIO_INTERRUPT_UPDATE_PERIOD_NS \
    (1000000000ULL / RADIO_SIM_UPDATE_RATE_HZ)

typedef enum
{
    RADIO_REQUEST_ERROR = -1,
    RADIO_REQUEST_SUCCESS = 0,
    RADIO_REQUEST_REJECTED = 1
} radio_request_result_t;

static void radio_sim_update_interrupt(radio_sim_state_t* state);
static uint32_t radio_sim_get_rx_buffer_count(radio_sim_state_t* state);
static int radio_sim_write_to_rx_buffer(radio_sim_state_t* state,
                                        const uint8_t* data, uint32_t length);

static bool radio_sim_udp_thread_is_running(radio_sim_state_t* state)
{
    bool running;

    pthread_mutex_lock(&state->buffer_mutex);
    running = state->udp_thread_running != 0;
    pthread_mutex_unlock(&state->buffer_mutex);

    return running;
}

/*
** UDP Ground Thread - handles communication with ground software
*/
static void* udp_ground_thread(void* arg)
{
    radio_sim_state_t* state = (radio_sim_state_t*)arg;
    fd_set read_fds;
    struct timeval timeout;
    uint8_t buffer[8192];
    ssize_t bytes_received;
    struct sockaddr_in from_addr;
    socklen_t from_len;
    
    printf("UDP ground thread started\n");
    
    while (radio_sim_udp_thread_is_running(state))
    {
        FD_ZERO(&read_fds);
        FD_SET(state->udp_rx_socket, &read_fds);
        
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms timeout
        
        int select_result = select(state->udp_rx_socket + 1, &read_fds, NULL, NULL, &timeout);
        
        if (select_result > 0 && FD_ISSET(state->udp_rx_socket, &read_fds))
        {
            from_len = sizeof(from_addr);
            bytes_received = recvfrom(state->udp_rx_socket, buffer, sizeof(buffer), 0,
                                     (struct sockaddr*)&from_addr, &from_len);
            
            if (bytes_received > 0)
            {
                #ifdef RADIO_CFG_DEBUG
                printf("Received %zu bytes from ground station:\n  ", bytes_received);
                for (ssize_t i = 0; i < bytes_received && i < 10; i++) {
                    printf("0x%02X ", buffer[i]);
                }
                printf("\n");
                #endif
                
                // Write to RX buffer if radio is powered and in RX or DUPLEX mode
                pthread_mutex_lock(&state->buffer_mutex);
                if (state->power_gpio.value &&
                    (state->config.Mode == RADIO_MODE_RX || state->config.Mode == RADIO_MODE_DUPLEX))
                {
                    radio_sim_write_to_rx_buffer(state, buffer, (uint32_t)bytes_received);
                    state->bytes_received += (uint32_t)bytes_received;
                    radio_sim_update_interrupt(state);
                }
                pthread_mutex_unlock(&state->buffer_mutex);
            }
        }
        else if (select_result < 0 && errno != EINTR)
        {
            printf("UDP select error: %s\n", strerror(errno));
            break;
        }
    }
    
    printf("UDP ground thread exiting\n");
    return NULL;
}

/*
** Update interrupt GPIO based on buffer status
*/
static void radio_sim_update_interrupt(radio_sim_state_t* state)
{
    uint32_t rx_count = radio_sim_get_rx_buffer_count(state);
    uint8_t should_assert = (rx_count >= RADIO_SIM_INTERRUPT_THRESHOLD) ? 1 : 0;
    
    if (state->interrupt_asserted != should_assert)
    {
        state->interrupt_asserted = should_assert;
        state->interrupt_gpio.value = should_assert;
       
        if (should_assert)
        {
            printf("Interrupt asserted - RX buffer has %u bytes\n", rx_count);
        }
        else
        {
            printf("Interrupt cleared - RX buffer has %u bytes\n", rx_count);
        }
    }
}

/*
** Get number of bytes in RX buffer
*/
static uint32_t radio_sim_get_rx_buffer_count(radio_sim_state_t* state)
{
    if (state->rx_buffer_head >= state->rx_buffer_tail)
    {
        return state->rx_buffer_head - state->rx_buffer_tail;
    }
    else
    {
        return (RADIO_SIM_RX_BUFFER_SIZE - state->rx_buffer_tail) + state->rx_buffer_head;
    }
}

/*
** Write data to RX buffer (circular buffer)
*/
static int radio_sim_write_to_rx_buffer(radio_sim_state_t* state, const uint8_t* data, uint32_t length)
{
    uint32_t available_space = RADIO_SIM_RX_BUFFER_SIZE - radio_sim_get_rx_buffer_count(state) - 1;
    
    if (length > available_space)
    {
        printf("RX buffer overflow - dropping %u bytes\n", (unsigned)(length - available_space));
        length = available_space;
    }
    
    for (uint32_t i = 0; i < length; i++)
    {
        state->rx_buffer[state->rx_buffer_head] = data[i];
        state->rx_buffer_head = (state->rx_buffer_head + 1) % RADIO_SIM_RX_BUFFER_SIZE;
    }
    
    return (int)length;
}

/*
** Read data from RX buffer
*/
static int radio_sim_read_from_rx_buffer(radio_sim_state_t* state, uint8_t* data, uint32_t max_length)
{
    uint32_t available = radio_sim_get_rx_buffer_count(state);
    uint32_t to_read = (max_length < available) ? max_length : available;
    
    for (uint32_t i = 0; i < to_read; i++)
    {
        data[i] = state->rx_buffer[state->rx_buffer_tail];
        state->rx_buffer_tail = (state->rx_buffer_tail + 1) % RADIO_SIM_RX_BUFFER_SIZE;
    }
    
    return (int)to_read;
}

/*
** Send response via SPI
*/
static int radio_sim_send_response(radio_sim_state_t* state, const uint8_t* data, uint32_t length)
{
    #ifdef RADIO_CFG_DEBUG
    printf("Sending SPI response: length=%d, first 4 bytes: 0x%02X 0x%02X 0x%02X 0x%02X\n", 
        length, data[0], data[1], data[2], data[3]);
    #endif
    return simulith_transport_send(&state->spi_device, data, (size_t)length) ==
        (int)length ? SIMULITH_TRANSPORT_SUCCESS : SIMULITH_TRANSPORT_ERROR;
}

/*
** Send housekeeping response
*/
static int radio_sim_send_housekeeping(radio_sim_state_t* state)
{
    uint8_t response[RADIO_DEVICE_HK_SIZE];

    pthread_mutex_lock(&state->buffer_mutex);
    
    #ifdef RADIO_CFG_DEBUG
    printf("Building HK response: powered_on=%d\n", state->power_gpio.value);
    #endif

    // Build housekeeping response
    response[0] = RADIO_DEVICE_HDR;
    response[1] = (uint8_t)((state->hk.CommandCounter >> 8) & 0xFF);
    response[2] = (uint8_t)(state->hk.CommandCounter & 0xFF);
    response[3] = state->hk.Mode;
    response[4] = state->hk.GroundLock;
    response[5] = state->hk.RxSpeedSetting;
    response[6] = state->hk.RxWavelengthSetting;
    response[7] = state->hk.TxSpeedSetting;
    response[8] = state->hk.TxWavelengthSetting;
    
    #ifdef RADIO_CFG_DEBUG
    printf("HK header: 0x%02X, counter: %d\n", response[0], state->hk.CommandCounter);
    #endif
    
    // Bytes in RX buffer (4 bytes)
    uint32_t rx_count = radio_sim_get_rx_buffer_count(state);
    response[9] = (uint8_t)((rx_count >> 24) & 0xFF);
    response[10] = (uint8_t)((rx_count >> 16) & 0xFF);
    response[11] = (uint8_t)((rx_count >> 8) & 0xFF);
    response[12] = (uint8_t)(rx_count & 0xFF);
    
    // Bytes received (4 bytes)
    response[13] = (uint8_t)((state->hk.BytesReceived >> 24) & 0xFF);
    response[14] = (uint8_t)((state->hk.BytesReceived >> 16) & 0xFF);
    response[15] = (uint8_t)((state->hk.BytesReceived >> 8) & 0xFF);
    response[16] = (uint8_t)(state->hk.BytesReceived & 0xFF);
    
    // Bytes sent (4 bytes)
    response[17] = (uint8_t)((state->hk.BytesSent >> 24) & 0xFF);
    response[18] = (uint8_t)((state->hk.BytesSent >> 16) & 0xFF);
    response[19] = (uint8_t)((state->hk.BytesSent >> 8) & 0xFF);
    response[20] = (uint8_t)(state->hk.BytesSent & 0xFF);
    
    // Trailer
    response[21] = RADIO_DEVICE_TRAILER;

    pthread_mutex_unlock(&state->buffer_mutex);
    
    #ifdef RADIO_CFG_DEBUG
    printf("HK response built, size=%ld, trailer at [21]: 0x%02X\n", 
           RADIO_DEVICE_HK_SIZE, response[21]);
    #endif
    
    return radio_sim_send_response(state, response, RADIO_DEVICE_HK_SIZE);
}

/*
** Handle SPI command
*/
/* Forward prototype for component registration export */
const component_interface_t* get_radio_sim_component_interface(void);
const component_interface_t* get_component_interface(void);

static radio_request_result_t radio_sim_handle_spi_command(
    radio_sim_state_t* state, const uint8_t* data, size_t length)
{
    if (!state || !data)
    {
        printf("Invalid SPI command parameters\n");
        return RADIO_REQUEST_ERROR;
    }

    #ifdef RADIO_CFG_DEBUG
    printf("SPI Handler: Received %zu bytes, powered_on=%d\n", length, state->power_gpio.value);
    printf("SPI Data: ");
    for (size_t i = 0; i < length && i < 10; i++) 
    {
        printf("0x%02X ", data[i]);
    }
    printf("\n");
    #endif

    if (length < 5)  // Minimum: header(1) + cmd(1) + len(2) + trailer(1)
    {
        printf("Invalid SPI command length: %zu\n", length);
        return RADIO_REQUEST_REJECTED;
    }
    
    // Check if radio is powered on - if not, drop all commands silently
    if (!state->power_gpio.value)
    {
        #ifdef RADIO_CFG_DEBUG
        printf("Radio not powered - dropping SPI command\n");
        #endif
        return RADIO_REQUEST_REJECTED;
    }
    
    // Parse command
    uint8_t command = data[1];
    uint16_t payload_len = ((uint16_t)data[2] << 8) | data[3];
    
    // Validate header
    if (data[0] != RADIO_DEVICE_HDR)
    {
        printf("Invalid command header: 0x%02X\n", data[0]);
        return RADIO_REQUEST_REJECTED;
    }

    /* Validate that the provided buffer contains the full command (header+cmd+len(2)+payload+trailer) */
    size_t expected_len = 5 + payload_len; /* header(1) + cmd(1) + len(2) + payload + trailer(1) */
    if (length != expected_len)
    {
        printf("Command length mismatch (have %zu, need %zu)\n", length, expected_len);
        return RADIO_REQUEST_REJECTED;
    }

    // Validate trailer
    uint8_t trailer = data[expected_len - 1];
    if (trailer != RADIO_DEVICE_TRAILER)
    {
        printf("Invalid command trailer: 0x%02X\n", trailer);
        return RADIO_REQUEST_REJECTED;
    }

    /* Handle commands */
    switch (command)
    {
        case RADIO_DEVICE_NOOP_CMD:
            // No operation
            state->hk.CommandCounter++; // Increment command counter
            break;

        case RADIO_DEVICE_REQ_HK_CMD:
            state->hk.CommandCounter++; // Increment command counter
            pthread_mutex_lock(&state->buffer_mutex);
            state->hk.BytesInRxBuffer = radio_sim_get_rx_buffer_count(state);
            state->hk.BytesReceived = state->bytes_received;
            state->hk.BytesSent = state->bytes_sent;
            pthread_mutex_unlock(&state->buffer_mutex);
            if (radio_sim_send_housekeeping(state) != SIMULITH_TRANSPORT_SUCCESS)
                return RADIO_REQUEST_ERROR;
            break;

        case RADIO_DEVICE_SET_CFG_CMD:
            if (payload_len == RADIO_CFG_PAYLOAD_SIZE)
            {
                pthread_mutex_lock(&state->buffer_mutex);
                state->config.Mode = data[4];
                state->config.RxSpeedSetting = data[5];
                state->config.RxWavelengthSetting = data[6];
                state->config.TxSpeedSetting = data[7];
                state->config.TxWavelengthSetting = data[8];

                /* Update housekeeping mirrors */
                state->hk.Mode = state->config.Mode;
                state->hk.RxSpeedSetting = state->config.RxSpeedSetting;
                state->hk.RxWavelengthSetting = state->config.RxWavelengthSetting;
                state->hk.TxSpeedSetting = state->config.TxSpeedSetting;
                state->hk.TxWavelengthSetting = state->config.TxWavelengthSetting;
                
                #ifdef RADIO_CFG_DEBUG
                printf("Radio sim: SET_CFG_CMD - Mode=%d, RxSpeed=%d, RxWavelength=%d, TxSpeed=%d, TxWavelength=%d\n",
                       state->config.Mode, state->config.RxSpeedSetting, state->config.RxWavelengthSetting,
                       state->config.TxSpeedSetting, state->config.TxWavelengthSetting);
                #endif

                state->hk.CommandCounter++; // Increment command counter
                pthread_mutex_unlock(&state->buffer_mutex);
            }
            else
            {
                printf("Radio sim: SET_CFG_CMD with invalid payload_len=%d\n", payload_len);
                return RADIO_REQUEST_REJECTED;
            }
            break;

        case RADIO_DEVICE_RECEIVE_CMD:
            if (payload_len == 2)
            {
                uint16_t requested = (uint16_t)(((uint16_t)data[4] << 8) | data[5]);
                uint32_t available;
                uint32_t to_send;

                pthread_mutex_lock(&state->buffer_mutex);
                available = radio_sim_get_rx_buffer_count(state);
                to_send = (available > requested) ? requested : available;

                uint8_t tx_buf[4096];
                if ((size_t)requested > sizeof(tx_buf) - 4U)
                {
                    pthread_mutex_unlock(&state->buffer_mutex);
                    printf("Radio sim: RECEIVE_CMD request too large: %u\n", requested);
                    return RADIO_REQUEST_REJECTED;
                }
                memset(tx_buf, 0, sizeof(tx_buf));

                /* Frame: header, length(2), payload..., trailer */
                tx_buf[0] = RADIO_DEVICE_HDR;
                tx_buf[1] = (uint8_t)((to_send >> 8) & 0xFF);
                tx_buf[2] = (uint8_t)(to_send & 0xFF);

                if (to_send > 0)
                {
                    int read = radio_sim_read_from_rx_buffer(state, &tx_buf[3], to_send);
                    if ((uint32_t)read != to_send)
                    {
                        to_send = (uint32_t)read;
                        tx_buf[1] = (uint8_t)((to_send >> 8) & 0xFF);
                        tx_buf[2] = (uint8_t)(to_send & 0xFF);
                    }
                    state->hk.CommandCounter++; // Increment command counter
                }

                tx_buf[3 + to_send] = RADIO_DEVICE_TRAILER;

                /* Debug print: show reply buffer and length */
                #ifdef RADIO_CFG_DEBUG
                uint32_t requested_plus_hdr_trl = requested + 4; // header(1) + len(2) + trailer(1)
                printf("radio_sim: RECEIVE_CMD reply: requested=%u, to_send=%u, send_len=%u\n", requested, to_send, requested);
                printf("radio_sim: reply buffer: ");
                for (uint32_t i = 0; i < requested_plus_hdr_trl && i < 32; ++i) {
                    printf("%02X ", tx_buf[i]);
                }
                printf("\n");
                #endif

                /* Send the full frame: header(1)+len(2)+payload+trailer(1) */
                int send_status = radio_sim_send_response(
                    state, tx_buf, (uint32_t)requested + 4U); /* Pad to the requested SPI read length. */

                state->hk.BytesSent += (uint32_t)to_send;
                pthread_mutex_unlock(&state->buffer_mutex);
                if (send_status != SIMULITH_TRANSPORT_SUCCESS)
                    return RADIO_REQUEST_ERROR;
            }
            else
            {
                printf("Radio sim: RECEIVE_CMD with invalid payload_len=%d\n", payload_len);
                return RADIO_REQUEST_REJECTED;
            }
            break;

        case RADIO_DEVICE_SEND_CMD:
            if (payload_len > 0)
            {
                uint8_t mode;

                /* Count bytes received from host */
                pthread_mutex_lock(&state->buffer_mutex);
                state->hk.BytesReceived += (uint32_t)payload_len;
                mode = state->config.Mode;
                pthread_mutex_unlock(&state->buffer_mutex);

                /* Forward to ground if in TX/DUPLEX */
                if (mode == RADIO_MODE_TX || mode == RADIO_MODE_DUPLEX)
                {
                    pthread_mutex_lock(&state->buffer_mutex);
                    state->hk.CommandCounter++;
                    pthread_mutex_unlock(&state->buffer_mutex);
                    /* Payload begins at data[4] (protocol: header[0], cmd[1], len_hi[2], len_lo[3], payload[4..]) */
                    ssize_t sent = sendto(state->udp_tx_socket, &data[4], payload_len, 0,
                                         (struct sockaddr*)&state->ground_tx_addr, (socklen_t)sizeof(state->ground_tx_addr));
                    if (sent > 0)
                    {
                        pthread_mutex_lock(&state->buffer_mutex);
                        state->bytes_sent += (uint32_t)sent;
                        state->hk.BytesSent += (uint32_t)sent;
                        pthread_mutex_unlock(&state->buffer_mutex);
                    }
                    else
                    {
                        printf("Failed to send to ground station: %s\n", strerror(errno));
                        return RADIO_REQUEST_ERROR;
                    }
                }
                #ifdef RADIO_CFG_DEBUG
                else
                {
                    printf("Radio not in TX mode - dropping data\n");
                }
                #endif
            }
            else
            {
                return RADIO_REQUEST_REJECTED;
            }
            break;

        default:
            printf("Unknown command: 0x%02X\n", command);
            return RADIO_REQUEST_REJECTED;
    }
    return RADIO_REQUEST_SUCCESS;
}

/*
** Main tick function called by simulith
*/
static int radio_sim_component_on_tick(component_state_t* component_state,
                                       uint64_t tick_time_ns,
                                       const simulith_42_context_t* context_42)
{
    (void)context_42;
    radio_sim_state_t *state = (radio_sim_state_t *)component_state;
    if (!state) return COMPONENT_ERROR;
    
    // Increment tick counter for rate limiting
    state->tick_counter++;
    
    // Update at specified rate
    if (tick_time_ns >= state->next_interrupt_update_ns)
    {
        // Update interrupt status
        pthread_mutex_lock(&state->buffer_mutex);
        radio_sim_update_interrupt(state);
        pthread_mutex_unlock(&state->buffer_mutex);

        uint64_t periods =
            ((tick_time_ns - state->next_interrupt_update_ns) /
             RADIO_INTERRUPT_UPDATE_PERIOD_NS) + 1U;
        if (periods > (UINT64_MAX - state->next_interrupt_update_ns) /
                      RADIO_INTERRUPT_UPDATE_PERIOD_NS)
            state->next_interrupt_update_ns = UINT64_MAX;
        else
            state->next_interrupt_update_ns +=
                periods * RADIO_INTERRUPT_UPDATE_PERIOD_NS;
    }
    return COMPONENT_SUCCESS;
}

static int radio_sim_component_wait_for_service(component_state_t* component_state,
                                                int interrupt_fd)
{
    radio_sim_state_t* state = (radio_sim_state_t*)component_state;
    if (!state) return COMPONENT_ERROR;
    transport_port_t* ports[] = {
        &state->power_gpio_device,
        &state->interrupt_gpio_device,
        &state->spi_device,
    };
    return simulith_transport_wait_for_request(ports, 3U, interrupt_fd);
}

static int radio_sim_component_service(component_state_t* component_state,
                                       uint64_t tick_time_ns,
                                       const simulith_42_context_t* context_42)
{
    (void)tick_time_ns;
    (void)context_42;
    radio_sim_state_t* state = (radio_sim_state_t*)component_state;
    int work = COMPONENT_IDLE;
    uint64_t transaction_id;
    if (!state) return COMPONENT_ERROR;

    // Service GPIO requests for power and interrupt pins every tick
    // (Process GPIO before SPI so a power-on write takes effect before any SPI command this tick)
    uint8_t gpio_rx_buf[8];
    int gpio_bytes = simulith_transport_receive_request(&state->power_gpio_device,
                                                        gpio_rx_buf, sizeof(gpio_rx_buf),
                                                        &transaction_id);
    if (gpio_bytes > 0) 
    {
        work = COMPONENT_WORK;
        int request_status = SIMULITH_TRANSPORT_ERROR;
        // Simple protocol: [cmd, pin, value]
        //   cmd: 0=read, 1=write
        if (gpio_bytes >= 2) {
            uint8_t cmd = gpio_rx_buf[0];
            uint8_t pin = gpio_rx_buf[1];
            if (pin == state->power_gpio.pin)
            {
                if (cmd == 0 && gpio_bytes == 2)
                {   // read
                    uint8_t resp[3] = {0, pin, (uint8_t)state->power_gpio.value};
                    if (simulith_transport_send(&state->power_gpio_device,
                                                resp, (size_t)sizeof(resp)) ==
                        (int)sizeof(resp))
                        request_status = SIMULITH_TRANSPORT_SUCCESS;
                } 
                else if (cmd == 1 && gpio_bytes == 3 && gpio_rx_buf[2] <= 1U)
                {   // write
                    uint8_t value = gpio_rx_buf[2];
                    pthread_mutex_lock(&state->buffer_mutex);
                    if (value != state->power_gpio.value)
                    {
                        state->power_gpio.value = value;
                        #ifdef RADIO_CFG_DEBUG
                        printf("Radio power %s (via GPIO write)\n", value ? "ON" : "OFF");
                        #endif
                        if (!value) 
                        {
                            state->rx_buffer_head = 0;
                            state->rx_buffer_tail = 0;
                            state->tx_buffer_head = 0;
                            state->tx_buffer_tail = 0;
                            state->interrupt_asserted = 0;
                            state->interrupt_gpio.value = 0;
                            memset(&state->config, 0, sizeof(state->config));
                            state->hk.Mode = RADIO_MODE_SLEEP;
                        }
                    }
                    pthread_mutex_unlock(&state->buffer_mutex);
                    request_status = SIMULITH_TRANSPORT_SUCCESS;
                }
            }
        }
        if (simulith_transport_complete_request(&state->power_gpio_device,
                                                transaction_id,
                                                request_status) !=
            SIMULITH_TRANSPORT_SUCCESS)
            return COMPONENT_ERROR;
    }
    else if (gpio_bytes < 0) return COMPONENT_ERROR;

    // Interrupt GPIO: check for incoming requests (read/write)
    gpio_bytes = simulith_transport_receive_request(&state->interrupt_gpio_device,
                                                    gpio_rx_buf, sizeof(gpio_rx_buf),
                                                    &transaction_id);
    if (gpio_bytes > 0) {
        work = COMPONENT_WORK;
        int request_status = SIMULITH_TRANSPORT_ERROR;
        if (gpio_bytes >= 2) 
        {
            uint8_t cmd = gpio_rx_buf[0];
            uint8_t pin = gpio_rx_buf[1];
            if (pin == state->interrupt_gpio.pin)
            {
                if (cmd == 0 && gpio_bytes == 2)
                {   // read
                    pthread_mutex_lock(&state->buffer_mutex);
                    uint8_t value = (uint8_t)state->interrupt_gpio.value;
                    pthread_mutex_unlock(&state->buffer_mutex);
                    uint8_t resp[3] = {0, pin, value};
                    if (simulith_transport_send(&state->interrupt_gpio_device,
                                                resp, (size_t)sizeof(resp)) ==
                        (int)sizeof(resp))
                        request_status = SIMULITH_TRANSPORT_SUCCESS;
                } 
                else if (cmd == 1 && gpio_bytes == 3 && gpio_rx_buf[2] <= 1U)
                {   // write
                    uint8_t value = gpio_rx_buf[2];
                    pthread_mutex_lock(&state->buffer_mutex);
                    state->interrupt_gpio.value = value;
                    pthread_mutex_unlock(&state->buffer_mutex);
                    #ifdef RADIO_CFG_DEBUG
                    printf("Interrupt GPIO set to %d (via GPIO write)\n", value);
                    #endif
                    request_status = SIMULITH_TRANSPORT_SUCCESS;
                }
            }
        }
        if (simulith_transport_complete_request(&state->interrupt_gpio_device,
                                                transaction_id,
                                                request_status) !=
            SIMULITH_TRANSPORT_SUCCESS)
            return COMPONENT_ERROR;
    }
    else if (gpio_bytes < 0) return COMPONENT_ERROR;

    // Poll for SPI requests using Simulith transport (after GPIO so power state is current)
    uint8_t spi_rx_buf[4096];
    int spi_bytes = simulith_transport_receive_request(&state->spi_device, spi_rx_buf,
                                                       sizeof(spi_rx_buf), &transaction_id);
    if (spi_bytes > 0)
    {
        work = COMPONENT_WORK;
        int request_status = SIMULITH_TRANSPORT_ERROR;
        if (state->power_gpio.value)
        {
            request_status = radio_sim_handle_spi_command(
                state, spi_rx_buf, (size_t)spi_bytes);
        }
        else
        {
            printf("Radio powered off - dropping %d bytes from SPI\n", spi_bytes);
        }
        if (simulith_transport_complete_request(&state->spi_device, transaction_id,
                                                request_status == RADIO_REQUEST_SUCCESS ?
                                                    SIMULITH_TRANSPORT_SUCCESS :
                                                    SIMULITH_TRANSPORT_ERROR) !=
            SIMULITH_TRANSPORT_SUCCESS)
            return COMPONENT_ERROR;
        if (request_status == RADIO_REQUEST_ERROR)
            return COMPONENT_ERROR;
    }
    else if (spi_bytes < 0) return COMPONENT_ERROR;
    return work;
}

/*
** Initialize radio simulator
*/
int radio_sim_init(radio_sim_state_t* state)
{
    if (!state) return RADIO_SIM_ERROR;
    
    // Initialize state
    memset(state, 0, sizeof(radio_sim_state_t));
    
    // Initialize mutex
    if (pthread_mutex_init(&state->buffer_mutex, NULL) != 0)
    {
        printf("Failed to initialize buffer mutex\n");
        return RADIO_SIM_ERROR;
    }
    
    // Initialize SPI device (server/bind)
    snprintf(state->spi_device.name, sizeof(state->spi_device.name), "radio_sim_spi");
    snprintf(state->spi_device.address, sizeof(state->spi_device.address), "ipc:///tmp/simulith_pub:%d",
         SIMULITH_SPI_BASE_PORT + (RADIO_CFG_SPI_BUS * 8) + RADIO_CFG_SPI_CS);
    state->spi_device.is_server = 1;
    // No bus_id/cs_id fields in transport_port_t
    if (simulith_transport_init(&state->spi_device) != SIMULITH_TRANSPORT_SUCCESS)
    {
    printf("Failed to initialize Simulith SPI transport\n");
    pthread_mutex_destroy(&state->buffer_mutex);
    return RADIO_SIM_ERROR;
    }
    
    // Initialize power GPIO
    snprintf(state->power_gpio_device.name, sizeof(state->power_gpio_device.name), "radio_sim_power_gpio");
    snprintf(state->power_gpio_device.address, sizeof(state->power_gpio_device.address), "ipc:///tmp/simulith_pub:%d",
             SIMULITH_GPIO_BASE_PORT + RADIO_CFG_GPIO_POWER_PIN);
    state->power_gpio_device.is_server = 1;
    state->power_gpio.pin = RADIO_CFG_GPIO_POWER_PIN;
    state->power_gpio.direction = GPIO_INPUT;  // Radio reads power state
    state->power_gpio.value = 1;
    if (simulith_transport_init(&state->power_gpio_device) < 0)
    {
        printf("Failed to initialize Simulith power GPIO server\n");
        simulith_transport_close(&state->spi_device);
        pthread_mutex_destroy(&state->buffer_mutex);
        return RADIO_SIM_ERROR;
    }
    
    // Initialize interrupt GPIO
    snprintf(state->interrupt_gpio_device.name, sizeof(state->interrupt_gpio_device.name), "radio_sim_interrupt_gpio");
    snprintf(state->interrupt_gpio_device.address, sizeof(state->interrupt_gpio_device.address), "ipc:///tmp/simulith_pub:%d",
             SIMULITH_GPIO_BASE_PORT + RADIO_CFG_GPIO_INTERRUPT_PIN);
    state->interrupt_gpio_device.is_server = 1;
    state->interrupt_gpio.pin = RADIO_CFG_GPIO_INTERRUPT_PIN;
    state->interrupt_gpio.direction = GPIO_OUTPUT;  // Radio controls interrupt signal
    state->interrupt_gpio.value = 0;
    if (simulith_transport_init(&state->interrupt_gpio_device) < 0)
    {
        printf("Failed to initialize Simulith interrupt GPIO server\n");
        simulith_transport_close(&state->power_gpio_device);
        simulith_transport_close(&state->spi_device);
        pthread_mutex_destroy(&state->buffer_mutex);
        return RADIO_SIM_ERROR;
    }
    
    // Initialize UDP sockets for ground communication
    state->udp_rx_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (state->udp_rx_socket < 0)
    {
        printf("Failed to create UDP RX socket: %s\n", strerror(errno));
        simulith_transport_close(&state->interrupt_gpio_device);
        simulith_transport_close(&state->power_gpio_device);
        simulith_transport_close(&state->spi_device);
        pthread_mutex_destroy(&state->buffer_mutex);
        return RADIO_SIM_ERROR;
    }
    
    state->udp_tx_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (state->udp_tx_socket < 0)
    {
        printf("Failed to create UDP TX socket: %s\n", strerror(errno));
        close(state->udp_rx_socket);
        simulith_transport_close(&state->interrupt_gpio_device);
        simulith_transport_close(&state->power_gpio_device);
        simulith_transport_close(&state->spi_device);
        pthread_mutex_destroy(&state->buffer_mutex);
        return RADIO_SIM_ERROR;
    }

    // Configure UDP addresses
    memset(&state->ground_rx_addr, 0, sizeof(state->ground_rx_addr));
    state->ground_rx_addr.sin_family = AF_INET;
    state->ground_rx_addr.sin_addr.s_addr = INADDR_ANY;
    state->ground_rx_addr.sin_port = htons(RADIO_CFG_UDP_GROUND_RX_PORT);

    memset(&state->ground_tx_addr, 0, sizeof(state->ground_tx_addr));
    state->ground_tx_addr.sin_family = AF_INET;
    // Hostname resolution for ground station.
    // RADIO_GROUND_HOST env var (dotted-decimal) skips DNS — useful in test environments
    // where "shire-cryptolib" doesn't resolve and gethostbyname() would block for seconds.
    const char *ground_host_env = getenv("RADIO_GROUND_HOST");
    if (ground_host_env &&
        inet_pton(AF_INET, ground_host_env, &state->ground_tx_addr.sin_addr) == 1)
    {
        /* env var provided a valid dotted-decimal IP — skip DNS */
    }
    else
    {
        struct hostent *ground_host = gethostbyname("shire-cryptolib");
        if (ground_host && ground_host->h_addrtype == AF_INET && ground_host->h_addr_list[0]) {
            memcpy(&state->ground_tx_addr.sin_addr, ground_host->h_addr_list[0], (size_t)ground_host->h_length);
        } else {
            printf("Failed to resolve ground station hostname 'shire-cryptolib', using INADDR_ANY\n");
            state->ground_tx_addr.sin_addr.s_addr = INADDR_ANY;
        }
    }
    state->ground_tx_addr.sin_port = htons(RADIO_CFG_UDP_GROUND_TX_PORT);

    // Bind RX socket
    if (bind(state->udp_rx_socket, (struct sockaddr*)&state->ground_rx_addr, sizeof(state->ground_rx_addr)) < 0)
    {
        printf("Failed to bind UDP RX socket: %s\n", strerror(errno));
        close(state->udp_tx_socket);
        close(state->udp_rx_socket);
        simulith_transport_close(&state->interrupt_gpio_device);
        simulith_transport_close(&state->power_gpio_device);
        simulith_transport_close(&state->spi_device);
        pthread_mutex_destroy(&state->buffer_mutex);
        return RADIO_SIM_ERROR;
    }
    
    // Initialize default values
    state->hk.CommandCounter = 0;
    state->hk.Mode = RADIO_MODE_DUPLEX;
    state->hk.GroundLock = 0;

    state->config.Mode = RADIO_MODE_DUPLEX;
    
    state->interrupt_asserted = 0;
    state->next_interrupt_update_ns = RADIO_INTERRUPT_UPDATE_PERIOD_NS;
    
    // Start UDP ground thread
    state->udp_thread_running = 1;
    if (pthread_create(&state->udp_thread, NULL, udp_ground_thread, state) != 0)
    {
        printf("Failed to create UDP ground thread\n");
        close(state->udp_tx_socket);
        close(state->udp_rx_socket);
        simulith_transport_close(&state->interrupt_gpio_device);
        simulith_transport_close(&state->power_gpio_device);
        simulith_transport_close(&state->spi_device);
        pthread_mutex_destroy(&state->buffer_mutex);
        return RADIO_SIM_ERROR;
    }
    
    printf("Radio simulator initialized successfully\n");
    printf("  SPI: %s\n", state->spi_device.address);
    printf("  Power GPIO: %s\n", state->power_gpio_device.address);
    printf("  Interrupt GPIO: %s\n", state->interrupt_gpio_device.address);
    printf("  UDP RX: port %d\n", RADIO_CFG_UDP_GROUND_RX_PORT);
    printf("  UDP TX: port %d\n", RADIO_CFG_UDP_GROUND_TX_PORT);
    printf("Waiting for commands...\n");
    
    return RADIO_SIM_SUCCESS;
}

/*
** Cleanup radio simulator
*/
void radio_sim_cleanup(radio_sim_state_t* state)
{
    if (!state) return;
    
    // Stop UDP thread. The flag is shared with the UDP worker.
    pthread_mutex_lock(&state->buffer_mutex);
    bool join_udp_thread = state->udp_thread_running != 0;
    state->udp_thread_running = 0;
    pthread_mutex_unlock(&state->buffer_mutex);
    if (join_udp_thread)
    {
        pthread_join(state->udp_thread, NULL);
    }
    
    // Cleanup resources
    if (state->udp_rx_socket >= 0)
    {
        close(state->udp_rx_socket);
        state->udp_rx_socket = -1;
    }
    
    if (state->udp_tx_socket >= 0)
    {
        close(state->udp_tx_socket);
        state->udp_tx_socket = -1;
    }
    
    simulith_transport_close(&state->interrupt_gpio_device);
    simulith_transport_close(&state->power_gpio_device);
    simulith_transport_close(&state->spi_device);
    
    pthread_mutex_destroy(&state->buffer_mutex);
}

static int radio_sim_component_create(component_state_t** state)
{
    if (!state) return COMPONENT_ERROR;
    *state = NULL;
    radio_sim_state_t* radio_state = malloc(sizeof(radio_sim_state_t));
    if (!radio_state) {
        return COMPONENT_ERROR;
    }
    
    int result = radio_sim_init(radio_state);
    if (result != RADIO_SIM_SUCCESS) {
        free(radio_state);
        return COMPONENT_ERROR;
    }
    
    *state = (component_state_t*)radio_state;
    return COMPONENT_SUCCESS;
}

static void radio_sim_component_destroy(component_state_t* state)
{
    if (!state) return;
    
    radio_sim_state_t* radio_state = (radio_sim_state_t*)state;
    radio_sim_cleanup(radio_state);
    free(radio_state);
}

static const component_interface_t radio_sim_interface = {
    .api_version = SIMULITH_COMPONENT_API_VERSION,
    .struct_size = sizeof(component_interface_t),
    .name = "radio_sim",
    .description = "Radio simulation component with SPI, GPIO, and UDP ground interface",
    .create = radio_sim_component_create,
    .on_tick = radio_sim_component_on_tick,
    .wait_for_service = radio_sim_component_wait_for_service,
    .service = radio_sim_component_service,
    .actuate = NULL,
    .destroy = radio_sim_component_destroy,
    .backdoor = NULL
};

// Component registration function - exported for dynamic loading
REGISTER_COMPONENT(radio_sim)
{
    return &radio_sim_interface;
}

// Export the registration function with a standard name for dynamic loading
__attribute__((visibility("default")))
const component_interface_t* get_component_interface(void)
{
    return &radio_sim_interface;
}
