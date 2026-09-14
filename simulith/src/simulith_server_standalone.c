#include "simulith.h"
#include <errno.h>
#include <math.h>

static int parse_seconds(const char *text, int allow_zero, uint64_t *nanoseconds)
{
    char *end = NULL;
    errno = 0;
    double seconds = strtod(text, &end);
    const double maximum_seconds = (double)UINT64_MAX / 1000000000.0;
    if (errno != 0 || !end || *end != '\0' || !isfinite(seconds) ||
        seconds < 0.0 || (!allow_zero && seconds <= 0.0) ||
        seconds > maximum_seconds)
        return -1;
    *nanoseconds = (uint64_t)(seconds * 1000000000.0);
    return 0;
}

static void usage(const char *program)
{
    printf("Usage: %s [clients] [--speed FACTOR|max] [--duration SECONDS] [--warmup SECONDS] [--metrics-json PATH]\n",
           program);
}

static int parse_positive_int(const char *text, int *value)
{
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed <= 0 || parsed > 32)
        return -1;
    *value = (int)parsed;
    return 0;
}

static int parse_speed(const char *text, double *speed)
{
    if (strcmp(text, "max") == 0)
    {
        *speed = 0.0;
        return 0;
    }
    char *end = NULL;
    errno = 0;
    double parsed = strtod(text, &end);
    if (errno != 0 || !end || *end != '\0' || !isfinite(parsed) || parsed <= 0.0)
        return -1;
    *speed = parsed;
    return 0;
}

int main(int argc, char *argv[]) 
{
    int num_clients = 1;
    double speed = 1.0;
    uint64_t duration_ns = 0;
    uint64_t warmup_ns = 0;
    const char *metrics_path = NULL;
    int positional_seen = 0;

    const char *env_speed = getenv("SIMULITH_SPEED");
    const char *env_duration = getenv("SIMULITH_DURATION");
    const char *env_metrics = getenv("SIMULITH_METRICS_JSON");
    const char *env_warmup = getenv("SIMULITH_WARMUP");
    if (env_speed && parse_speed(env_speed, &speed) != 0)
    {
        fprintf(stderr, "Invalid SIMULITH_SPEED: %s\n", env_speed);
        return 1;
    }
    if (env_duration && env_duration[0] != '\0')
    {
        if (parse_seconds(env_duration, 0, &duration_ns) != 0)
        {
            fprintf(stderr, "Invalid SIMULITH_DURATION: %s\n", env_duration);
            return 1;
        }
    }
    if (env_metrics && env_metrics[0] != '\0')
        metrics_path = env_metrics;
    if (env_warmup && env_warmup[0] != '\0')
    {
        if (parse_seconds(env_warmup, 1, &warmup_ns) != 0)
        {
            fprintf(stderr, "Invalid SIMULITH_WARMUP: %s\n", env_warmup);
            return 1;
        }
    }

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc)
        {
            if (parse_speed(argv[++i], &speed) != 0)
            {
                fprintf(stderr, "Invalid speed: %s\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc)
        {
            if (parse_seconds(argv[++i], 0, &duration_ns) != 0)
            {
                fprintf(stderr, "Invalid duration: %s\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--metrics-json") == 0 && i + 1 < argc)
        {
            metrics_path = argv[++i];
        }
        else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc)
        {
            if (parse_seconds(argv[++i], 1, &warmup_ns) != 0)
            {
                fprintf(stderr, "Invalid warmup: %s\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        else if (argv[i][0] != '-' && !positional_seen)
        {
            if (parse_positive_int(argv[i], &num_clients) != 0)
            {
                fprintf(stderr, "Error: Number of clients must be between 1 and 32\n");
                usage(argv[0]);
                return 1;
            }
            positional_seen = 1;
        }
        else
        {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }
    
    printf("Starting Simulith Server with %d client(s)...\n", num_clients);
    if (simulith_server_configure(speed, duration_ns, metrics_path) != 0 ||
        simulith_server_configure_warmup(warmup_ns) != 0 ||
        simulith_server_init(LOCAL_PUB_ADDR, LOCAL_REP_ADDR, num_clients, INTERVAL_NS) != 0)
        return 1;
    simulith_server_run();
    simulith_server_shutdown();
    return 0;
}
