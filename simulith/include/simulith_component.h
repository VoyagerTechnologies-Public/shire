#ifndef SIMULITH_COMPONENT_H
#define SIMULITH_COMPONENT_H

#include <stddef.h>
#include <stdint.h>
#include "simulith_power.h"
#include "simulith_42_context.h"
#include "simulith_42_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Lifecycle and service return codes. COMPONENT_SUCCESS and COMPONENT_IDLE
 * intentionally share zero: they apply to different callbacks. A modeled
 * device rejection is successfully serviced work, not COMPONENT_ERROR; the
 * device protocol carries that rejection back to its caller. */
#define COMPONENT_SUCCESS 0
#define COMPONENT_ERROR -1
#define COMPONENT_IDLE 0
#define COMPONENT_WORK 1

/*
 * Component plug-in ABI contract.
 *
 * Increment the version whenever an existing field changes meaning, type, or
 * position. The size permits future versions to append fields while allowing
 * the director to reject an interface it cannot safely call. Every component
 * library must be rebuilt when this version changes.
 */
#define SIMULITH_COMPONENT_API_VERSION 4U
#define SIMULITH_COMPONENT_ENTRY_POINT "get_component_interface"

// Forward declaration of component state
typedef struct component_state component_state_t;

/*
 * Runtime contract implemented by every dynamically loaded component simulator.
 *
 * For each synchronized simulation tick the director calls on_tick() exactly
 * once during PREPARE, waits for service readiness while FSW executes, calls
 * service() for ready device work, and calls actuate() exactly once after
 * EXECUTE. The director does not begin the next
 * PREPARE until every component has finished actuate() and the resulting 42
 * command batch has been committed. Simulated time is fixed throughout this
 * sequence.
 *
 * Only actuate() may enqueue 42 commands. The director applies completed EPS
 * transactions and simulator backdoors, then power transitions, before this
 * callback. PREPARE, service, backdoor, and power callbacks may update private
 * pending state but must not enqueue outputs. The director rejects an early
 * or carried-over command instead of silently discarding it.
 *
 * The Simulith client validates time before invoking the director: time is
 * identical across phases of one sequence and increases by the configured
 * fixed timestep between consecutive sequences. Components may rely on that
 * invariant. Restarting or rewinding a scenario requires destroying and
 * recreating component state.
 *
 * on_tick() and actuate() are optional. A request-driven component with no
 * autonomous time behavior may leave on_tick NULL. A component that does not
 * drive 42 actuators may leave actuate NULL.
 */
typedef struct {
    uint32_t api_version;       // Must equal SIMULITH_COMPONENT_API_VERSION
    uint32_t struct_size;       // Must equal sizeof(component_interface_t)
    const char* name;           // Component name (must be unique)
    const char* description;    // Component description

    // Lifecycle functions
    /* Allocate and initialize private state. Required. */
    int (*create)(component_state_t** state);

    /* Observe one new simulation tick and advance autonomous internal state.
     * The supplied 42 truth snapshot is immutable for this tick. Do not poll
     * device transports here: scheduled FSW has not started this tick. Return
     * COMPONENT_SUCCESS or COMPONENT_ERROR; failure withholds PREPARE. */
    int (*on_tick)(component_state_t* state, uint64_t tick_time_ns,
                   const simulith_42_context_t* context_42);

    /* Block until at least one device request can be serviced or the director
     * interrupts EXECUTE through interrupt_fd. This callback observes transport
     * readiness only: it must not receive, mutate component state, or complete a
     * transaction. Return COMPONENT_WORK for ready work, COMPONENT_IDLE for a
     * director interruption, or COMPONENT_ERROR for a wait failure. Required
     * when service is present; leave both callbacks NULL for components without
     * a flight-facing device transport. */
    int (*wait_for_service)(component_state_t* state, int interrupt_fd);

    /* EXECUTE-phase device service after wait_for_service reports readiness.
     * Process currently ready requests without waiting and return WORK, IDLE,
     * or ERROR. The current tick time and
     * immutable 42 snapshot permit lazy sensing only when a request needs it.
     * Protocol rejection is WORK; ERROR is reserved for simulator/transport
     * failure. A synchronous request, including its immediate response and
     * completion acknowledgement, must finish in the current tick. Optional. */
    int (*service)(component_state_t* state, uint64_t tick_time_ns,
                   const simulith_42_context_t* context_42);

    /* Publish this tick's complete actuator output set. This runs after FSW
     * and all current-tick device transactions and power transitions have
     * finished, but before the director commits the shared command batch to
     * 42. Emit only this component's owned actuator indices. A power cut must
     * explicitly overwrite its previous physical output, even while off.
     * Never wait or poll.
     * Return COMPONENT_SUCCESS or COMPONENT_ERROR; failure withholds COMMIT. */
    int (*actuate)(component_state_t* state, uint64_t tick_time_ns,
                   const simulith_42_context_t* context_42);

    /* Close resources and release state allocated by create(). Required. The
     * director quiesces all callbacks before calling this exactly once. */
    void (*destroy)(component_state_t* state);

    /* Optional simulation-only control/fault-injection path. */
    void (*backdoor)(component_state_t* state, uint16_t cmd_id, const uint8_t* payload, uint16_t payload_len);

    /* Supply access and consumer callbacks run only with service workers
     * quiescent. power_set must preserve transport resources and reset model
     * state on an edge. A modeled outage is never COMPONENT_ERROR. */

    /* Provider only (EPS): return its owned, persistent topology/transition
     * structure. The director resolves loads by interface name and fills the
     * consumer snapshots. Current topology supports one provider. Consumers
     * leave this NULL; a mapped consumer must implement all four callbacks below. */
    simulith_power_supply_t *(*power_supply)(component_state_t *state);

    /* Copy resolved settings at startup and every COMMIT. Reconfiguration must
     * preserve device state, current supply, boot deadline and diagnostic history.
     * Return COMPONENT_SUCCESS/ERROR, not a power-helper edge flag. */
    int (*power_configure)(component_state_t *state, const simulith_power_load_config_t *config);

    /* Apply effective supply state (on = 0/1), with time in simulated ns.
     * Reset volatile state only on an edge. An ON edge begins boot; repeated
     * ON does not reboot. Keep endpoints open to service modeled rejections. */
    int (*power_set)(component_state_t *state, int on, uint64_t tick_time_ns);

    /* Explicitly reset volatile device state even without a supply edge.
     * Preserve component resources and power-runtime history. A reset alone
     * does not change supply or start boot; EPS RESET also queues supply edges. */
    int (*power_reset)(component_state_t *state, uint64_t tick_time_ns);

    /* Query readiness, mode, counters and watts at startup, PREPARE, and after
     * COMMIT transitions. Do not advance the model or publish outputs here.
     * This internal snapshot is for power accounting, not a telemetry stream. */
    int (*power_snapshot)(component_state_t *state, uint64_t tick_time_ns,
                          simulith_power_snapshot_t *snapshot);
} component_interface_t;

// Component registration function type
typedef const component_interface_t* (*get_component_interface_fn)(void);

// Macro to help implement component registration
#define REGISTER_COMPONENT(name) \
    const component_interface_t* get_##name##_component_interface(void)

#ifdef __cplusplus
}
#endif

#endif // SIMULITH_COMPONENT_H
