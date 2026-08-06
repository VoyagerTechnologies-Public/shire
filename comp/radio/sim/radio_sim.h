#ifndef RADIO_SIM_H
#define RADIO_SIM_H

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>

#include "radio_device.h"
#include "simulith.h"
#include "simulith_component.h"
#include "simulith_42_context.h"
#include "simulith_42_commands.h"

// Configuration parameters
#define RADIO_SIM_UPDATE_RATE_HZ 10

// Status codes
#define RADIO_SIM_SUCCESS 0
#define RADIO_SIM_ERROR  1

// Radio modes
#define RADIO_SIM_MODE_SLEEP  0
#define RADIO_SIM_MODE_TX     1
#define RADIO_SIM_MODE_RX     2
#define RADIO_SIM_MODE_DUPLEX 3

// Buffer management
#define RADIO_SIM_RX_BUFFER_SIZE RADIO_CFG_BUFFER_SIZE
#define RADIO_SIM_TX_BUFFER_SIZE RADIO_CFG_BUFFER_SIZE
#define RADIO_SIM_INTERRUPT_THRESHOLD RADIO_CFG_INTERRUPT_THRESHOLD

// Radio simulator state
typedef struct 
{
    // Communication handles
    void* time_handle;
    
    // Device state
    uint8_t interrupt_asserted;
    RADIO_Device_HK_tlm_t hk;
    RADIO_Device_Config_t config;
    
    // Tick counter for rate limiting GPIO reads
    uint32_t tick_counter;
    
    // Data buffers
    uint8_t rx_buffer[RADIO_SIM_RX_BUFFER_SIZE];
    uint8_t tx_buffer[RADIO_SIM_TX_BUFFER_SIZE];
    uint32_t rx_buffer_head;
    uint32_t rx_buffer_tail;
    uint32_t tx_buffer_head;
    uint32_t tx_buffer_tail;
    
    // Statistics
    uint32_t bytes_received;
    uint32_t bytes_sent;
    
    // UDP ground interface
    int udp_rx_socket;
    int udp_tx_socket;
    struct sockaddr_in ground_rx_addr;
    struct sockaddr_in ground_tx_addr;
    pthread_t udp_thread;
    uint8_t udp_thread_running;
    
    // Timing
    double last_update_time;
    
    // Thread synchronization
    pthread_mutex_t buffer_mutex;
} radio_sim_state_t;

// Function declarations
static void* udp_ground_thread(void* arg);
static void radio_sim_update_interrupt(radio_sim_state_t* state);
static uint32_t radio_sim_get_rx_buffer_count(radio_sim_state_t* state);
static int radio_sim_write_to_rx_buffer(radio_sim_state_t* state, const uint8_t* data, uint32_t length);
static void radio_sim_send_response(radio_sim_state_t* state, const uint8_t* data, uint32_t length);
static void radio_sim_send_housekeeping(radio_sim_state_t* state);
static void radio_sim_handle_spi_command(radio_sim_state_t* state, const uint8_t* data, size_t length);
static void radio_sim_on_tick(uint64_t tick_time_ns, const simulith_42_context_t* context_42);
int radio_sim_init(radio_sim_state_t* state);
void radio_sim_cleanup(radio_sim_state_t* state);

#endif /* RADIO_SIM_H */ 