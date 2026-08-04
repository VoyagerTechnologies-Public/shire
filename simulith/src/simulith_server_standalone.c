#include "simulith.h"

int main(int argc, char *argv[]) 
{
    int num_clients = 1; // default value
    
    // Check if number of clients argument is provided
    if (argc > 1) {
        num_clients = atoi(argv[1]);
        if (num_clients <= 0) {
            printf("Error: Number of clients must be a positive integer\n");
            printf("Usage: %s [num_clients]\n", argv[0]);
            return 1;
        }
    }
    
    printf("Starting Simulith Server with %d client(s)...\n", num_clients);
    simulith_server_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, num_clients, INTERVAL_NS);
    simulith_server_run();
    simulith_server_shutdown();
    return 0;
}
