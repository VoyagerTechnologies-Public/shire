# SHIRE Component Demo
This repository is a demonstration component for the SHIRE environment, showcasing the standard patterns for component development.

## Overview
Command line interface (CLI), flight software (FSW), ground software (GSW), and simulation (SIM) directories are included in this repository.

The demo component is a UART device that is speak when spoken to.
Each command that is successfully interpreted is echoed back.
If additional telemetry is to be generated, it will follow.
The specific command format is as follows:
* uint16, header, 0xC0FF
* uint16, command
  * (0) No operation
  * (1) Get housekeeping
  * (2) Get data
  * (3) Set configuration
* uint16, payload
  * Unused except for set configuration command
* uint16, trailer, 0xFEFE

Response formats:
* Housekeeping
  * uint16, header, 0xC0FF 
  * uint16, command counter
  * uint16, configuration
  * uint16, trailer, 0xFEFE
* Data
  * uint16, header, 0xC0FF
  * uint16, data channel 1
  * uint16, data channel 2
  * uint16, data channel 3
  * uint16, trailer, 0xFEFE

## Command Line Interface
The CLI can be configured to connect to either the hardware or simulation.
This enables direct checkouts these without interference.

## Flight Software
The core Flight System (cFS) flight software application receives commands from the software bus.
Two message IDs exist for commands:
* 0x18FA - Commands
  * (0) No operation
  * (1) Reset counters
  * (2) Enable
  * (3) Disable
  * (4) Set configuration
* 0x18FB - Requests
  * (0) Request housekeeping
  * (1) Request data point

Two message IDs exist for telemetry:
* 0x08FA - Application Housekeeping
* 0x08FB - Component Telemetry

## Ground Software
The XTCE file provided details the CCSDS Space Packet Protocol format used for commanding and telemetry.

## Simulation
The simulation available is built as a library that is loaded by the shire-director for use.
This maintains the state of the simulation and enables communication to the FSW via simulith.
Similar to the CLI, the `make cfg` call at the top level shire-lab is required prior to building.

Demo component simulator backdoor commands:
- 0x0001 DEMO_SET_CONFIG: payload `uint16` new config value, encoded big-endian.
- 0x0002 DEMO_RAND_HK: payload optional 1 enable / 0 disable (default enable), randomizes HK on ready model updates.
- 0x0003 DEMO_RAND_DATA: payload optional 1 enable / 0 disable (default enable), randomizes data on ready model updates.

These commands change private model state and do not emit unsolicited device responses.
Use a subsequent real device request to observe the effect once the device is ready.
Power-cycle reset clears these DEMO test overrides.

### Component simulator lifecycle

The demo simulator is the reference pattern for `component_interface_t`. The
director obtains this interface from the component's `get_component_interface()`
export and owns the callback sequence:

1. `create` allocates one private component state and opens its simulator-side
   device transports.
2. `on_tick` optionally runs exactly once during PREPARE. It advances
   autonomous, time-dependent model behavior using the current immutable 42
   truth snapshot. A strictly request-driven component should set it to `NULL`.
3. `wait_for_service` blocks on the component transports and the Director's
   interrupt descriptor. It observes readiness but never consumes a request.
4. `service` runs only after work is ready and handles the complete available
   protocol transaction without waiting for a future simulation tick.
5. `actuate` runs once after EXECUTE and publishes the component's final output
   batch for the current tick, after queued backdoors and power transitions. The director commits the combined batch to 42
   only after every component returns. Components without actuators set it to
   `NULL`; the demo implements a documented no-op for mold visibility.
6. `destroy` closes transports and releases the state created by `create` after
   the director has stopped all component callbacks.
7. `backdoor`, when provided, handles simulation-only control and fault
   injection independently of the flight device protocol.

Every interface declares `SIMULITH_COMPONENT_API_VERSION` and
`sizeof(component_interface_t)`. The director rejects a library with a different
version or size before calling any function pointer. Increment the API version
whenever an existing field changes type, position, or meaning. A clean rebuild
of every component is required after such a change.

`create` and `destroy` are required and successful creation must return non-null
private state. `on_tick` and `actuate` return `COMPONENT_SUCCESS` or
`COMPONENT_ERROR`; an error withholds the associated synchronization completion.
The director reports the component and sequence instead of committing partial
state. A component with `service` must also provide `wait_for_service`; either
callback may report an error, which is latched and prevents COMMIT.

All mutable simulator data and transport handles belong in the component state.
Avoid file-scope mutable state: it prevents multiple instances and obscures
ownership between callbacks.

### EPS power callbacks

DEMO is a power consumer, with `simulith_power_runtime_t power` in its private state.
EPS owns the topology and exposes it through the provider callback `power_supply`.
DEMO leaves `power_supply` null and registers all four consumer callbacks in `demo_sim_interface`.
The Director matches the resolved load name `demo_sim` to that interface.
EPS service and backdoors queue supply changes for the Director to apply, rather than invoking these callbacks from a service worker.
`demo_power_set` receives effective physical supply, which may differ from an EPS requested state under a stuck-switch fault.
The callback bodies and their source comments are in [demo_sim.c](sim/demo_sim.c).

| Callback | Responsibility in DEMO |
| --- | --- |
| `demo_power_configure` | Copy the resolved electrical settings using `simulith_power_configure`. The Director calls it at startup and every COMMIT, so later calls must preserve the device configuration, supply state, boot deadline and counters. |
| `demo_power_set` | Apply effective supply OFF/ON at startup or COMMIT. `simulith_power_set` returns an edge flag. Call `demo_power_reset` on a real edge, and return `COMPONENT_SUCCESS` for valid power operations. Repeated ON/OFF is a no-op. |
| `demo_power_reset` | Clear buffered UART bytes, device housekeeping and channel data, restore the deterministic PRNG seed, disable DEMO random overrides, and rebase sampling to `ns`. Keep the UART open and preserve the complete power runtime. |
| `demo_power_snapshot` | Call `simulith_power_snapshot` with physical mode index `0`. Report readiness, history and scaled watts without transport I/O or model changes. This supplies EPS accounting rather than Yamcs telemetry. |

All of these callbacks run while device-service workers are quiescent.
The `ns` arguments are simulated nanoseconds, not wall-clock time.
A callback reports `COMPONENT_ERROR` only for invalid arguments or a simulator failure.
An off or booting device is a valid modeled state.

The shared helpers in `simulith/include/simulith_power.h` divide the mechanics:

- `simulith_power_configure` copies settings and enters managed mode only on first configuration.
- `simulith_power_set` returns `1` for a supply edge or `0` for an unchanged state, sets an ON edge's boot deadline, and increments its cycle count.
- `simulith_power_ready` queries whether supply, the physical enable gate, and boot timing permit device behavior.
- `simulith_power_record` counts one successful or rejected device transaction and updates the last successful response time.
- `simulith_power_snapshot` reports boot demand before readiness, mode demand afterward, and zero demand while off or physically disabled.

Do not return `simulith_power_set` directly from the component callback.
Its edge flag is not a `COMPONENT_*` status, and the helper does not perform the device-specific reset.
Do not clear the entire `power` structure in `demo_power_reset`.
That would lose the EPS mapping, supply state, boot deadline, and diagnostic history.
The device's own `hk.DeviceCounter` resets separately from the power runtime's monotonic request and cycle counters.
A direct `power_reset` call does not turn the supply on or restart the boot interval.
EPS `RESET` also queues supply transitions to restore the startup rails, including an explicit reset for loads that are already off.

`power.enabled` is a physical gate, such as Radio's GPIO enable.
DEMO leaves this gate enabled and does not derive it from the FSW application's `DEVICE_ENABLED` flag.
Stopping FSW polling does not remove DEMO's electrical load while its EPS supply remains on.
`DeviceConfig` is a protocol configuration value, not a power-mode index.
DEMO always selects mode `0` for electrical demand.

### Use readiness without blocking a tick

`demo_sim_component_on_tick` checks `simulith_power_ready` before evolving autonomous data.
`wait_for_service` continues waiting on the open UART during an outage.
`service` must still receive and complete an unavailable device request so the flight caller can return.
It checks readiness before `handle_command`, which prevents even the usual command echo from leaking out while off or booting.
After receiving a request and its transaction ID, the relevant service pattern is:

```c
demo_command_result_t result =
    simulith_power_ready(&state->power, tick_time_ns) ?
    handle_command(state, data, (size_t)bytes) : DEMO_COMMAND_REJECTED;
simulith_power_record(&state->power, result == DEMO_COMMAND_SUCCESS,
                       tick_time_ns);
int completion = result == DEMO_COMMAND_SUCCESS ?
    SIMULITH_TRANSPORT_SUCCESS : SIMULITH_TRANSPORT_ERROR;
if (simulith_transport_complete_request(&state->uart_port, transaction_id,
                                        completion) != SIMULITH_TRANSPORT_SUCCESS)
    return COMPONENT_ERROR;
return result == DEMO_COMMAND_ERROR ? COMPONENT_ERROR : COMPONENT_WORK;
```

A power rejection emits no device payload and completes the transport transaction with a modeled error.
The callback returns `COMPONENT_WORK`, because handling the rejection succeeded.
Never sleep until boot completes, leave a transaction outstanding, close the UART on a power cut, or report the outage as `COMPONENT_ERROR`.
The next tick cannot begin until the current synchronous flight call finishes.

DEMO has no physical actuators.
A component that drives 42 must additionally clear pending outputs on a power edge or reset and publish zero for its owned actuator indices in `actuate` while off or resetting.
Keep 42 command publication out of `power_set`, `power_reset`, and `service`.
The common 42 command API does not determine which device is powered.

### Configure and exercise DEMO power

Add an EPS override to the selected spacecraft or scenario YAML, then run `make cfg` and rebuild the runtime images:

```yaml
eps:
  loads:
    demo:
      switch: 0
      mode_power_w: [0.8, 0.8, 0.8, 0.8, 0.8, 0.8]
      boot_power_w: 1.0
      boot_delay_s: 2.0
      power_scale: 1.0
```

The reference DRM maps DEMO to the 3.3 V rail `0` by default.
The six mode values are required by the shared configuration format even though DEMO only uses the first.
These watts are illustrative configuration values rather than hardware measurements.
An unmapped DEMO stays unmanaged and responsive, allowing its standalone CLI to work without EPS.
Unmanaged models contribute no configured demand to EPS.

For this example, an ON edge at simulated time 1 s starts a boot deadline at 3 s and consumes 1 W during boot.
Another ON at 2 s preserves that deadline and the cycle count.
Requests before 3 s receive no device payload and complete as failures.
At 3 s the device is ready and consumes 0.8 W.
An OFF edge clears the device's volatile state, rejects subsequent requests, and reports zero watts.
A later ON starts a new boot interval and restores defaults.
The power runtime's diagnostic history remains intact throughout.

At startup and COMMIT the Director applies mapping and transition callbacks in a deterministic order.
Normal EPS flight transactions finish before boundary backdoors, queued transitions are preserved in order, and `actuate` sees the updated power state in that same boundary.
The next EXECUTE sees the resulting readiness.
An OFF/ON pair within one boundary still resets the device.

Run `make test-sim COMP_TESTS=demo` for the focused simulator checks.
`test_powered_off_and_booting_requests_complete_without_payload` exercises unavailable request handling.
`test_eps_power_cycle_boot_and_mode_demand` covers boot demand, repeated ON, explicit reset, restored defaults, and preserved cycle history.
The EPS Yamcs stacks demonstrate integration using real flight commands and fresh FSW transactions.
An increasing FSW device-error counter during an outage and new successful transactions after recovery provide observable evidence.
Cached device data alone does not prove responsiveness.
These internal snapshots do not add a simulator telemetry stream or require console observation.

The current topology has one EPS provider, eight rails, three stable consumer slots, and six mode values per slot.
Adding another component type requires updating the topology definitions, configuration catalog and selectors alongside its callback implementations.
See [the Atlas power callback guide](../../atlas/docs/manual/how-to/components.md#power-aware-component-simulators) for the complete provider contract and integration rules.

### Tick and transaction timing

Each 10 ms simulation tick is a synchronized interval:

```text
PREPARE  power snapshots -> component on_tick from 42 truth
EXECUTE  FSW request -> service readiness check -> response/completion
COMMIT   quiesce workers -> backdoors -> power changes/snapshots
         -> component actuate -> commit combined commands to 42
```

Simulith validates that time is unchanged across phases of one sequence and
increases by the configured fixed timestep between consecutive sequences.
Component code may rely on forward-only time and does not need its own rewind
handling. A restart or future rewind must destroy and recreate component state
explicitly.

Simulated time remains fixed throughout EXECUTE. The demo UART therefore models
a complete request, processing, response, and completion acknowledgement as an
atomic operation at 10 ms resolution. This does not bypass the flight
transaction: the normal synchronous flight device call blocks until the
simulator has processed the request and sent its response.

`wait_for_service` sleeps until a transport is readable or COMMIT interrupts
the worker. It does not receive data, so an interrupted request remains queued.
`service` performs only ready work and must never wait for a future tick. One
request may produce multiple immediate response
frames before its completion is acknowledged. It returns:

- `COMPONENT_WORK` after successfully servicing available work.
- `COMPONENT_IDLE` if readiness changed before the request was consumed.
- `COMPONENT_ERROR` only when the simulator or its transport fails.

The callback also receives the current tick time and immutable 42 truth snapshot
so a request-driven sensor can sample lazily. The demo intentionally uses
`on_tick` because its example channels model an autonomous 10 Hz sampling cadence;
a component without such continuous behavior can omit `on_tick` entirely.

The director waits for readiness again while EXECUTE remains active. Completion must only
be acknowledged after validation, modeled state changes, and response delivery.
A malformed, unsupported, or state-invalid device command is a protocol-level
rejection: complete that flight transaction with the modeled error and return
`COMPONENT_WORK`. Do not report a correctly modeled rejection as a simulator
failure.

A synchronous transaction must not wait for a future tick. The server cannot
advance to that tick until scheduled FSW has returned, while FSW cannot return
until the device responds. Such a design would deadlock the closed-loop barrier.
If a real operation needs to span multiple 10 ms steps, `service` records the
operation and immediately returns the protocol's accepted/busy response.
`on_tick` then progresses the internal operation once per tick, and later flight
status requests observe busy, complete, or failed. Do not hold a synchronous bus
transaction open while waiting for a future tick.

Keep autonomous physical evolution in `on_tick`, protocol handling in `service`,
and 42 output publication in `actuate`. Calling transport receive/send functions
from `on_tick` reintroduces ordering dependence because scheduled FSW has not yet
executed for that tick.

For regression repeatability, time gates use absolute deadlines in integer
simulated nanoseconds and pseudo-random behavior uses PRNG state owned and seeded
by each component instance. Advancing the deadline by the model period avoids
cadence drift when that period is not an exact multiple of the global tick.
Device commands must match the documented wire size exactly; truncated or
overlong requests receive a failed completion rather than being partially
interpreted.

Simulator tests keep the production callbacks unchanged. They invoke `on_tick`,
`wait_for_service`, `service`, and `actuate` as separate phases and include a framed
`simulith_transport_request()` from a flight-side thread. That request must
remain blocked through PREPARE, receive all response bytes, and return only after
EXECUTE sends its completion record.
