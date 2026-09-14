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
        fprintf(stderr, "42 initialization failed; refusing to run an open-loop simulation\n");
        cleanup_components(&g_director_config);
        return 1;
    }

    if (initialize_telemetry() != 0)
    {
        cleanup_components(&g_director_config);
        return 1;
    }

    if (initialize_scenario(&g_director_config) != 0)
    {
        fprintf(stderr, "Scenario initialization failed\n");
        cleanup_components(&g_director_config);
        return 1;
    }

    sleep(1);
    if (simulith_client_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, "shire-director", INTERVAL_NS) != 0)
    {
        cleanup_components(&g_director_config);
        return 1;
    }

    if (simulith_client_configure_phases(SIMULITH_PHASE_MASK_PREPARE |
                                         SIMULITH_PHASE_MASK_EXECUTE |
                                         SIMULITH_PHASE_MASK_COMMIT) != 0)
    {
        simulith_client_shutdown();
        cleanup_components(&g_director_config);
        return 1;
    }

    if (simulith_client_handshake() != 0)
    {
        simulith_client_shutdown();
        cleanup_components(&g_director_config);
        return 1;
    }

    simulith_client_run_phased_loop(director_prepare_tick,
                                    director_execute_tick,
                                    director_commit_tick);
    director_write_terminal_metrics();
    simulith_client_shutdown();
    cleanup_components(&g_director_config);
    return 0;
}
