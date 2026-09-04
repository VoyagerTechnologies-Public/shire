#include "simulith_director.h"

int main(int argc, char *argv[])
{
    printf("Simulith Director starting...\n");

    int parse_result = parse_args(argc, argv, &g_director_config);
    if (parse_result < 0)
        return 0;
    if (parse_result > 0)
        return 1;

    if (load_components(&g_director_config) != 0)
        return 1;

    if (initialize_components(&g_director_config) != 0)
    {
        cleanup_components(&g_director_config);
        return 1;
    }

    if (initialize_42(&g_director_config) != 0)
    {
        fprintf(stderr, "Warning: 42 initialization failed; continuing without it\n");
        g_director_config.enable_42 = 0;
        g_director_config.fortytwo_initialized = 0;
    }

    initialize_telemetry();

    sleep(1);
    if (simulith_client_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, "shire-director", INTERVAL_NS) != 0)
    {
        cleanup_components(&g_director_config);
        return 1;
    }

    if (simulith_client_handshake() != 0)
    {
        simulith_client_shutdown();
        cleanup_components(&g_director_config);
        return 1;
    }

    simulith_client_run_loop(on_tick);
    simulith_client_shutdown();
    cleanup_components(&g_director_config);
    return 0;
}
