# Simulations

SHIRE's simulation layer consists of the Simulith Server, the Director process with its loaded component simulator libraries, and the separate 42 dynamics process.

## Simulith Server

The Server owns simulation time.
Its current tick interval is 10 ms.
For every tick it:

1. Publishes the sequence and current simulated time for PREPARE
2. Waits for every client registered for PREPARE
3. Publishes EXECUTE and waits for registered FSW and Director completion
4. Publishes COMMIT and waits for the Director to commit component outputs
5. Delays as needed for the requested speed
6. Advances simulated time by one interval.

The DRM expects two clients: FSW and the Director.
The focused CLI environment expects one Director client.
From the attached Server console, use `p` to pause or resume, `speed 25` to
request an exact rate, `speed max` for unbounded execution, and `+` or `-` to
double or halve the current requested rate.
The Server reports simulated time, attempted speed, and achieved speed every 10
seconds of simulated time regardless of the active synchronization transport.
The interactive controls accept attempted rates from 1/64x through 1024x, but
host performance determines the achieved rate.
A request above the complete stack's measured unbounded throughput cannot be
paced consistently.
Use `make perf` to verify that the host has sufficient margin for the desired
rate.
To request 25x from startup instead of attaching to the console, run
`SIMULITH_SPEED=25 make start`.

### Ground-commanded pause/play/speed

The Server also accepts the same pause, resume, and speed-change requests as
ground commands, over a dedicated backdoor UDP link (target name
`shire_server`, port 50061) separate from the Director's component backdoor
described in [Fault injection](#fault-injection).
YAMCS drives it with three commands, defined in `shire_server.xtce` and routed
over the `tc_server_backdoor` stream:

* `/SHIRE_SERVER/BACKDOOR_SERVER_PAUSE` and `/SHIRE_SERVER/BACKDOOR_SERVER_PLAY` pause and resume the simulation clock.
  Both are idempotent.
* `/SHIRE_SERVER/BACKDOOR_SERVER_SET_SPEED` sets the attempted rate from its `SPEED` argument, a 64-bit float using the same big-endian convention as the Demo component's backdoor arguments.
  `0` requests the unbounded "max" rate, matching the CLI's `speed max`.
  Values are validated the same way as the CLI's `speed <factor>` command: they must be finite and fall within 1/64x through 1024x, or the request is ignored and the current speed is left unchanged.

The Server reports its state on a separate telemetry link (port 50043) as
`/SHIRE_SERVER/PAUSED`, `/SHIRE_SERVER/SPEED`, and `/SHIRE_SERVER/SIM_TIME_NS`.
`CheckoutTest.ycs` exercises all three commands.
See [Checkout stack](ground-software.md#checkout-stack).

## Simulith Director

The Director scans its `components` directory for shared objects.
A loadable simulator exports `get_component_interface`, which returns the lifecycle defined in `simulith/include/simulith_component.h`:

* `create` allocates and initializes private component state
* `on_tick`, when present, advances autonomous state once using the current
  simulation time and immutable 42 truth snapshot
* `wait_for_service` blocks on flight-device request readiness plus a Director
  interrupt without consuming requests
* `service` handles ready flight-device protocol requests during the FSW
  execution phase without waiting for future simulation time
* `actuate`, when present, publishes the current tick's final actuator outputs
  before the director commits the combined command batch to 42
* `destroy` releases component state after all callbacks are quiescent
* `backdoor`, when implemented, accepts commands used only for tests

Each interface also declares `SIMULITH_COMPONENT_API_VERSION` and its compiled
`sizeof(component_interface_t)`.
The Director rejects a library whose version,
size, required lifecycle callbacks, or component name does not match the active
contract.
An API change therefore requires a clean rebuild of every component.
An old shared object is never called through a guessed layout.

Mutable model data, transport endpoints, GPIO state, timing deadlines, and
deterministic random-generator state belong to the allocated component state.
The current ADCS, Demo, EPS, and Radio simulators follow this ownership rule.

### Tick loop at a glance

Every synchronized tick follows the same closed-loop cycle:

```mermaid
%%{init: {"themeCSS": ".nodeLabel, .nodeLabel p { color: #000000 !important; }"}}%%
flowchart LR
    PREPARE["PREPARE<br/>Read 42 truth<br/>Run on_tick"]
    EXECUTE["EXECUTE<br/>Run exactly one SCH slot<br/>Service device requests"]
    COMMIT["COMMIT<br/>Quiesce service<br/>Run actuate<br/>Commit commands to 42"]

    PREPARE -->|Prepared| EXECUTE
    EXECUTE -->|All FSW work complete| COMMIT
    COMMIT -->|Advance time 10 ms and repeat| PREPARE

    classDef prepare fill:#eaf4ff,stroke:#3b82b6,color:#000000,stroke-width:2px
    classDef execute fill:#eef8e9,stroke:#4f8a3d,color:#000000,stroke-width:2px
    classDef commit fill:#fff4df,stroke:#c47b16,color:#000000,stroke-width:2px
    class PREPARE prepare
    class EXECUTE execute
    class COMMIT commit
```

The Server advances the clock only after every required completion for the
current phase has arrived.

### Detailed tick sequence

The callbacks occupy distinct synchronization boundaries.
The following sequence shows one complete tick and how the Server's phases map
to the component-facing API:

```mermaid
sequenceDiagram
    autonumber
    participant Server as Simulith Server
    participant Director
    participant Component as Component callbacks in Director
    participant FSW as cFS FSW and SCH
    participant FortyTwo as 42 dynamics

    rect rgb(235, 245, 255)
        Note over Server,FortyTwo: PREPARE: observe truth and advance autonomous state
        Server->>Director: Publish sequence N and simulated time
        Director->>FortyTwo: Request current truth state
        FortyTwo-->>Director: Return immutable truth snapshot
        loop Each component in deterministic order
            Director->>Component: on_tick(time, truth)
            Component-->>Director: Autonomous state updated
        end
        Director-->>Server: PREPARE complete
    end

    rect rgb(242, 249, 238)
        Note over Server,FortyTwo: EXECUTE: run one FSW slot and complete device transactions
        Server->>Director: Open service window for sequence N
        Director-->>Server: EXECUTE release accepted
        Server->>FSW: Release sequence N
        FSW->>FSW: SCH executes exactly one slot
        loop For each ready flight-device request
            FSW->>Component: Send request through simulation HWLib
            Component->>Component: wait_for_service returns ready
            Component->>Component: service handles ready work
            Component-->>FSW: Return response and completion acknowledgment
        end
        FSW->>FSW: Finish scheduled and transitively published work
        FSW-->>Server: EXECUTE complete
    end

    rect rgb(255, 247, 230)
        Note over Server,FortyTwo: COMMIT: freeze service and publish final actuator outputs
        Server->>Director: Publish COMMIT for sequence N
        Director->>Component: Interrupt readiness waits
        Director->>Director: Wait for active service callbacks
        loop Each component in deterministic order
            Director->>Component: actuate(time, truth)
            Component-->>Director: Queue final actuator outputs
        end
        Director->>FortyTwo: Commit complete command batch
        FortyTwo-->>Director: Commands accepted
        Director-->>Server: COMMIT complete
    end

    par After COMMIT completes
        Server->>Server: Pace and advance time by 10 ms
    and
        FortyTwo->>FortyTwo: Advance dynamics using committed commands
    end
    Note over Server,FortyTwo: PREPARE N+1 cannot complete until the resulting truth is available
```

The same flow expressed as component responsibilities is:

1. During PREPARE, the Director calls `on_tick` once for each component in a
   deterministic order.
2. During EXECUTE, one worker per device-serving component blocks in
   `wait_for_service` and calls `service` only for ready work.
3. During COMMIT, the Director prevents new service calls, waits for active
   calls to return, and calls `actuate` once for each component in a
   deterministic order.
4. The Director commits the resulting command batch to 42 before completing
   the tick.

`on_tick` is for autonomous state evolution and may be omitted by a strictly
request-driven component.
The current 42 truth snapshot is also available to `service`, which permits a
sensor to calculate data only when FSW requests it.
`actuate` is for components that drive 42 and may be omitted by components with
no actuator output.
Keeping it separate prevents an early or partial command publication while FSW
is still changing the component's commanded state.

The Director creates one service worker thread per loaded component that
provides the paired readiness/service callbacks.
Only device service runs concurrently across components.
The small `on_tick` and `actuate` callbacks run on the Director thread to avoid
worker wake and barrier overhead while retaining deterministic ordering.
The build script copies only simulators selected for the active spacecraft into the Director image.
At runtime the Director loads each library with `dlopen` and owns its component state.
The loaded simulators are not separate processes or Simulith Server clients.

`service` handles the currently ready request set without waiting and returns
`COMPONENT_WORK`, `COMPONENT_IDLE`, or `COMPONENT_ERROR`.
A modeled protocol rejection is completed through the device protocol and
returns `COMPONENT_WORK` because the simulator itself did not fail.
A synchronous transaction must complete within the current tick.
For a modeled operation that spans several ticks, the device accepts or rejects
the request immediately, stores operation state, advances that state in
`on_tick`, and exposes progress through later status requests.
Holding a synchronous request open until a future tick would deadlock the
closed-loop barrier.

## 42 integration

42 runs as a separate container.
The Director exchanges state and actuator commands with it through a Unix domain socket in the shared Simulith volume.
The context exposed to component simulators includes simulation and dynamics time, position, velocity, attitude quaternion, body rates, sun and magnetic field vectors, eclipse state, mass properties, and atmospheric density.

Examples in the current component models include:

* ADCS uses 42 attitude, rate, sun, and magnetic field state and queues magnetorquer and reaction wheel commands back to 42.
* EPS derives simulated solar generation from the 42 sun vector and eclipse flag.
* Demo maps the body frame sun vector into three representative data channels unless random data injection is enabled.

The Director also publishes a 42 truth packet to YAMCS on UDP 50042 every 100 Director ticks.

## Simulated device transport

HWLIB's simulation drivers communicate with the simulator libraries loaded inside the Director over ZeroMQ IPC endpoints in the shared `/tmp` volume.
Endpoint families are reserved for UART, I2C, SPI, and GPIO.
Each endpoint is derived from the configured device handle, bus, address, chip select, or pin.
All endpoints in one process share a single ZeroMQ context.
Blocking exact receives use monotonic deadlines and support partial responses,
timeouts, rejection, and shutdown interruption without application-side polling.

For example, Demo uses a UART endpoint based on `SIMULITH_UART_BASE_PORT + DEMO_CFG_HANDLE`.
The simulator binds the endpoint, while the FSW side UART driver connects to it.
The same Demo device packet format is used by the simulator, the cFS application, and the developer CLI.

## Synchronized completion

FSW participates in EXECUTE once per 10 ms tick.
SCH releases exactly one slot and the PSP records registration-to-dispatch and
dispatch-to-return latency for each participant.
When an active participant publishes a Software Bus command to a task-activation
pipe, the cFE SB and PSP integration registers a child completion for that
destination subscriber.
The tick therefore cannot finish merely because CI_LAB or another producer
returned while work it published remains queued.

This same-tick completion rule applies to task-activation pipes.
A pipe used only with `CFE_SB_POLL` is instead a durable input queue: delivery remains
lossless, but the queued data is consumed under the application's next
scheduled activation.
For example, Radio may enqueue several CFDP PDUs on a CF
channel pipe during one tick, while CF intentionally drains that pipe on its
later CF wakeup.
Such queued data does not hold the originating tick open.
The scheduled activation that processes it is itself synchronized.

The Server rejects duplicate, stale, future, wrong-phase, and unknown
completions.
Its watchdog names the client holding a phase, while the PSP watchdog identifies
the schedule entry, message ID, and participant state holding EXECUTE.
No synchronized mode batches ticks, queues future ticks, or skips SCH slots.

## Performance regression runs

See [Verify synchronized simulation performance](../how-to/performance.md) for
the workload, acceptance checks, and report location.
From the repository root, run:

```sh
make perf
```

`make perf` runs a full-output reference, paced 1x, 25x, and 50x control-mode
trials, and three unbounded trials.
Each unbounded trial must exceed 50x while the control-loop, command, device,
truth, and trace checks pass.
Use `make perf-smoke` for a short diagnostic or
`make perf-compare BASELINE=/path/to/accepted-report.json` to compare with a
reviewed report.

The standalone Server also accepts `--speed <factor|max>`, `--duration`,
`--warmup`, and `--metrics-json` while retaining the positional client count and
interactive controls.

## Fault injection

The Director listens for backdoor datagrams on UDP 50060 and dispatches a valid packet to the named component's `backdoor` callback.
Backdoor behavior is specific to each component.
Demo currently supports three backdoor commands, all routed over the `tc_backdoor` stream: `/DEMO/BACKDOOR_DEMO_SET_CONFIG` writes `DEVICE_CONFIG` directly, bypassing normal command processing (see `DemoFDIR.ycs` and the [FDIR scenario](../../scenarios/fdir.md) for the fault-injection pattern this enables), and `/DEMO/BACKDOOR_DEMO_RAND_HK` / `/DEMO/BACKDOOR_DEMO_RAND_DATA` toggle randomized housekeeping or channel data.
This is a component-level backdoor into the Director, a different link from the Simulith Server's own pause/play/speed backdoor described above.
A backdoor is a test interface, not a flight interface, and should only be used in isolated simulation runs.

## Tests

Simulation tests are present in `simulith/test/` and `comp/<name>/test-sim/`.
Run the selected component simulator tests and combined coverage from the repository root:

```bash
make test-sim
```

`make test-sim` builds the Simulith executables needed by the component tests, but it does not run the separate Simulith core test suite.
Run that suite from the repository root with `make test-simulith` (or directly
with `make -C simulith test`).
Both simulation suites produce line, branch, and MC/DC HTML reports and a
separate Codecov-compatible LCOV trace.
A passing unit test does not by itself verify a complete mission scenario.
Record the configuration, revision, command, and result for any formal verification claim.

***
Last reviewed: 20260914
