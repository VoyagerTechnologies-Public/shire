/*
 * Simulith 42 Socket Client Header
 * 
 * Interface for communicating with 42 via socket IPC.
 * This maintains clear license separation between SHIRE (AGPLv3)
 * and 42 (NOSA) by using network communication instead of linking.
 */

#ifndef SIMULITH_42_SOCKET_CLIENT_H
#define SIMULITH_42_SOCKET_CLIENT_H

#include "simulith_42_context.h"
#include "simulith_42_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize connection to 42 simulator
 * 
 * @param hostname Hostname/IP of 42 server (NULL for default "shire-42")
 * @param port Port number (0 for default 5556)
 * @return 0 on success, -1 on failure
 */
int simulith_42_init(const char *hostname, int port);

/**
 * Request current state from 42
 * Reads latest spacecraft state via socket IPC
 * 
 * @param context Output buffer for 42 state
 * @return 0 on success, -1 on failure
 */
int simulith_42_request_state(simulith_42_context_t *context);

/**
 * Check if connected to 42
 * 
 * @return 1 if connected, 0 if not
 */
int simulith_42_is_connected(void);

/**
 * Send batch of commands to 42 in a single message
 * More efficient than sending individual commands
 * 
 * @param commands Array of commands to send
 * @param count Number of commands in array
 * @return 0 on success, -1 on failure
 */
int simulith_42_send_command_batch(const simulith_42_command_t *commands, int count);

/**
 * Send empty command message to 42
 * Required in TXRX mode on every tick when there are no commands
 * 
 * @return 0 on success, -1 on failure
 */
int simulith_42_send_empty_commands(void);

/**
 * Cleanup and close connection to 42
 */
void simulith_42_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // SIMULITH_42_SOCKET_CLIENT_H
