#ifndef SIMULITH_POWER_H
#define SIMULITH_POWER_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* The reference DRM has three consumers. Stable IDs are DEMO=0, ADCS=1,
 * RADIO=2, including spacecraft which omit one of them. */
#define SIMULITH_POWER_SWITCHES 8U
#define SIMULITH_POWER_LOADS 3U
#define SIMULITH_POWER_MODES 6U
#define SIMULITH_POWER_TRANSITIONS 256U

typedef struct {
    char component[32];
    uint8_t switch_id;
    double mode_w[SIMULITH_POWER_MODES];
    double boot_w;
    double boot_delay_s;
    double scale;
} simulith_power_load_config_t;

typedef struct {
    uint8_t managed;
    uint8_t supply_on;
    uint8_t enabled;
    uint64_t boot_ready_ns;
    uint64_t cycles;
    uint64_t successful_requests;
    uint64_t rejected_requests;
    uint64_t last_response_ns;
    simulith_power_load_config_t config;
} simulith_power_runtime_t;

typedef struct {
    uint8_t supplied;
    uint8_t ready;
    uint8_t mode;
    uint64_t cycles;
    uint64_t successful_requests;
    uint64_t rejected_requests;
    uint64_t last_response_ns;
    uint64_t rf_received;
    uint64_t rf_sent;
    double watts;
} simulith_power_snapshot_t;

typedef struct {
    uint8_t switch_id;
    uint8_t on;
    uint8_t reset;
} simulith_power_transition_t;

typedef struct {
    double voltage[SIMULITH_POWER_SWITCHES];
    uint8_t startup[SIMULITH_POWER_SWITCHES];
    uint8_t requested[SIMULITH_POWER_SWITCHES];
    uint8_t effective[SIMULITH_POWER_SWITCHES];
    uint8_t fault[SIMULITH_POWER_SWITCHES];
    simulith_power_load_config_t loads[SIMULITH_POWER_LOADS];
    simulith_power_snapshot_t snapshots[SIMULITH_POWER_LOADS];
    simulith_power_transition_t transitions[SIMULITH_POWER_TRANSITIONS];
    size_t transition_count;
    uint8_t configured;
    uint8_t overflow;
} simulith_power_supply_t;

/* Query whether device behavior may run at simulated time ns. Unmapped models
 * stay usable; mapped models require supply, their physical enable gate, and
 * the completed boot delay. This query does not advance time or perform I/O. */
static inline int simulith_power_ready(const simulith_power_runtime_t *p, uint64_t ns)
{
    return !p->managed || (p->supply_on && p->enabled && ns >= p->boot_ready_ns);
}

/* Copy resolved load settings. Only first configuration enters managed mode
 * and initializes its supply/gate; later updates preserve runtime history. */
static inline void simulith_power_configure(simulith_power_runtime_t *p,
                                            const simulith_power_load_config_t *config)
{
    p->config = *config;
    if (p->managed) return;
    p->managed = 1;
    p->supply_on = 0;
    p->enabled = 1;
}

/* Return 1 for a real supply edge, 0 for an unchanged state. An ON edge starts
 * boot at ns and increments cycles. The caller resets its own volatile model
 * on an edge and translates this flag to the COMPONENT_* callback status. */
static inline int simulith_power_set(simulith_power_runtime_t *p, int on, uint64_t ns)
{
    if ((int)p->supply_on == on) return 0;
    p->supply_on = (uint8_t)on;
    if (on) {
        uint64_t delay = (uint64_t)(p->config.boot_delay_s * 1e9);
        p->boot_ready_ns = delay > UINT64_MAX - ns ? UINT64_MAX : ns + delay;
        p->cycles++;
    }
    return 1;
}

/* Record one serviced device transaction, not one callback or response frame.
 * A modeled off/boot rejection counts as rejected. Only successful responses
 * update last_response_ns. Keep this history across volatile device resets. */
static inline void simulith_power_record(simulith_power_runtime_t *p, int success, uint64_t ns)
{
    if (success) { p->successful_requests++; p->last_response_ns = ns; }
    else p->rejected_requests++;
}

/* Produce an internal accounting snapshot without changing runtime state.
 * Mode is the component's physical mode index, not a generic device config.
 * A managed, supplied and enabled load uses scaled boot/mode watts; all other
 * loads report zero modeled demand. Unmanaged loads remain ready but unaccounted. */
static inline void simulith_power_snapshot(const simulith_power_runtime_t *p,
                                            uint8_t mode, uint64_t ns,
                                            simulith_power_snapshot_t *out)
{
    memset(out, 0, sizeof(*out));
    out->supplied = !p->managed || p->supply_on;
    out->ready = (uint8_t)simulith_power_ready(p, ns);
    out->mode = mode;
    out->cycles = p->cycles;
    out->successful_requests = p->successful_requests;
    out->rejected_requests = p->rejected_requests;
    out->last_response_ns = p->last_response_ns;
    if (p->managed && p->supply_on && p->enabled)
        out->watts = p->config.scale * (out->ready ?
            p->config.mode_w[mode < SIMULITH_POWER_MODES ? mode : 0] : p->config.boot_w);
}

#endif
