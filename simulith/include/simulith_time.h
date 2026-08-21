#ifndef SIMULITH_TIME_H
#define SIMULITH_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the time provider
 * @return Handle to time provider, NULL on failure
 */
void* simulith_time_init(void);

/**
 * @brief Get current simulation time
 * @param handle Time provider handle
 * @return Current time in seconds
 */
double simulith_time_get(void* handle);

/**
 * @brief Wait for next time tick
 * @param handle Time provider handle
 * @return 0 on success, -1 on failure
 */
int simulith_time_wait_for_next_tick(void* handle);

/**
 * @brief Cleanup time provider
 * @param handle Time provider handle
 */
void simulith_time_cleanup(void* handle);

#ifdef __cplusplus
}
#endif

#endif /* SIMULITH_TIME_H */ 