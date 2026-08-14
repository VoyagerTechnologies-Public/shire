#include "simulith_time.h"
#include "simulith.h"
#include <stdlib.h>
#include <time.h>
#include <zmq.h>

// Time provider structure
typedef struct {
    void* context;       // ZMQ context
    void* sub_socket;    // ZMQ SUB socket for tick messages
    uint64_t tick_count; // Number of ticks received
    double tick_interval;// Time between ticks in seconds
} simulith_time_provider_t;

void* simulith_time_init(void) 
{
    simulith_time_provider_t* provider = malloc(sizeof(simulith_time_provider_t));
    if (!provider) return NULL;
    
    // Initialize ZMQ context and socket
    provider->context = zmq_ctx_new();
    if (!provider->context) 
    {
        free(provider);
        return NULL;
    }
    
    provider->sub_socket = zmq_socket(provider->context, ZMQ_SUB);
    if (!provider->sub_socket) 
    {
        zmq_ctx_destroy(provider->context);
        free(provider);
        return NULL;
    }
    
    // Connect to the Simulith PUB socket
    if (zmq_connect(provider->sub_socket, LOCAL_PUB_ADDR) != 0) 
    {
        zmq_close(provider->sub_socket);
        zmq_ctx_destroy(provider->context);
        free(provider);
        return NULL;
    }
    
    // Subscribe to tick messages
    zmq_setsockopt(provider->sub_socket, ZMQ_SUBSCRIBE, "", 0);
    
    provider->tick_count = 0;
    provider->tick_interval = INTERVAL_NS / 1e9; // Convert ns to seconds
    
    return provider;
}

double simulith_time_get(void* handle) 
{
    if (!handle) return 0.0;
    
    simulith_time_provider_t* provider = (simulith_time_provider_t*)handle;
    /* Promote tick_count to double before multiplication to avoid integer->double conversion warnings */
    return (double)provider->tick_count * provider->tick_interval;
}

int simulith_time_wait_for_next_tick(void* handle) 
{
    if (!handle) return -1;
    
    simulith_time_provider_t* provider = (simulith_time_provider_t*)handle;
    
    // Wait for next tick message
    uint64_t tick_time;
    int result = zmq_recv(provider->sub_socket, &tick_time, sizeof(tick_time), 0);
    if (result < 0) return -1;
    
    provider->tick_count++;
    return 0;
}

void simulith_time_cleanup(void* handle) 
{
    if (!handle) return;
    
    simulith_time_provider_t* provider = (simulith_time_provider_t*)handle;
    
    if (provider->sub_socket) 
    {
        zmq_close(provider->sub_socket);
    }
    
    if (provider->context) 
    {
        zmq_ctx_destroy(provider->context);
    }
    
    free(provider);
} 