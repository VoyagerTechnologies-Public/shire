#ifndef RADIO_SIM_H
#define RADIO_SIM_H

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>

#include "radio_device.h"
#include "radio_link.h"
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

// Backdoor command IDs (target name "radio_sim"). Payload integers are big-endian.
//   SET_LINK_MODE:           u8 mode (radio_link_mode_t: 0 auto, 1 always, 2 never)
//   SET_DELAY:               u8 direction (0 up, 1 down, 2 both) + u32 delay [ms]
//   SET_DROP_RATE:           u8 direction + u32 drop probability [ppm, <= 1000000]
//   SET_IMPAIRMENTS_ENABLED: u8 0/1 (bypass delay + loss, keeping configured values)
//   SET_STATION_ENABLED:     u8 station index (0xFF = all) + u8 0/1
//   SET_SEED:                u32 loss PRNG seed
//   CORRUPT_PACKETS:         u8 direction, u16 count, u16 byte offset: flip one bit at that offset in each of the
//                            next count packets that cross the link (count 0 cancels)
//   REPLAY_PACKET:           u8 direction (0 or 1): re-inject the last packet that crossed the link, bypassing
//                            visibility, delay and loss
//   RESET_LINK:              no payload; restore configured link defaults
#define RADIO_BD_SET_LINK_MODE           1U
#define RADIO_BD_SET_DELAY               2U
#define RADIO_BD_SET_DROP_RATE           3U
#define RADIO_BD_SET_IMPAIRMENTS_ENABLED 4U
#define RADIO_BD_SET_STATION_ENABLED     5U
#define RADIO_BD_SET_SEED                6U
#define RADIO_BD_RESET_LINK              7U
#define RADIO_BD_CORRUPT_PACKETS         8U
#define RADIO_BD_REPLAY_PACKET           9U

#define RADIO_BD_ALL_STATIONS 0xFFU

// Buffer management
#define RADIO_SIM_RX_BUFFER_SIZE RADIO_CFG_BUFFER_SIZE
#define RADIO_SIM_TX_BUFFER_SIZE RADIO_CFG_BUFFER_SIZE
#define RADIO_SIM_INTERRUPT_THRESHOLD RADIO_CFG_INTERRUPT_THRESHOLD

// Radio simulator state
typedef struct 
{
    // Communication resources are owned by this model instance.
    transport_port_t spi_device;
    simulith_power_runtime_t power;
    uint64_t power_time_ns;
    uint64_t rf_received_total;
    uint64_t rf_sent_total;
    transport_port_t power_gpio_device;
    transport_port_t interrupt_gpio_device;
    simulith_gpio_state_t power_gpio;
    simulith_gpio_state_t interrupt_gpio;
    
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
    
    // RF link model (visibility, delay, loss); guarded by buffer_mutex
    radio_link_t link;
    uint32_t backdoor_accepted;
    uint32_t backdoor_rejected;

    // Timing
    uint64_t next_interrupt_update_ns;
    
    // Thread synchronization
    pthread_mutex_t buffer_mutex;
} radio_sim_state_t;

// Public simulator lifecycle
int radio_sim_init(radio_sim_state_t* state);
void radio_sim_cleanup(radio_sim_state_t* state);

#endif /* RADIO_SIM_H */
