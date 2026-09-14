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
- 0x0001 DEMO_SET_CONFIG: payload `uint16` new config value; emits HK.
- 0x0002 DEMO_RAND_HK: payload optional 1 enable / 0 disable (default enable); randomizes HK counter; emits HK.
- 0x0003 DEMO_RAND_DATA: payload optional 1 enable / 0 disable (default enable); randomizes data; emits data.

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
   batch for the current tick. The director commits the combined batch to 42
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

### Tick and transaction timing

Each 10 ms simulation tick is a synchronized interval:

```text
PREPARE  component on_tick updates autonomous state from 42 truth
EXECUTE  FSW sends request -> service processes it -> FSW receives response
ACTUATE  component publishes its final actuator outputs
COMMIT   director applies the combined actuator command batch to 42
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
