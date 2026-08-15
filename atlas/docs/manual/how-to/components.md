# Components

A SHIRE component keeps the artifacts needed to develop, exercise, integrate, and operate one spacecraft subsystem under `comp/<name>/`.

## Current reference components

| Component | Simulated interface | Current role |
| --- | --- | --- |
| ADCS | UART | Models an integrated attitude determination and control unit that consumes 42 truth state and can command simulated magnetorquers and reaction wheels. |
| Demo | UART | Provides a minimal reference payload and source for the component mold that echoes commands and produces representative housekeeping and three channel data. |
| EPS | I2C | Models solar generation, battery state, and eight switched loads using solar input derived from 42 sun and eclipse state. |
| Radio | SPI, GPIO, and UDP | Models device commanding, buffered uplink/downlink, radio modes, and the ground radio path. |

CryptoLib is also under `comp/`, but it is integrated as an external library and standalone security processing service rather than following the same reference component mold.

The active spacecraft decides which component simulators are built and loaded and which component applications remain in the generated CPU1 startup script.
The current `cfg/shire_defs/targets.cmake` still compiles all four reference component applications into the cFS build.
See [Configuration](configuration.md#change-the-active-target).

## Standard layout

| Path | Responsibility |
| --- | --- |
| `src/` | cFS application source and message handling |
| `shared/` | Device protocol, framing, generated configuration, and code reusable by the app, CLI, or simulator |
| `cli/` | Focused developer checkout program and build rules |
| `sim/` | Director loadable component simulator |
| `test-fsw/` | cFS application and shared code unit/coverage tests |
| `test-sim/` | Simulator lifecycle, protocol, state, and dynamics tests |
| `gsw/` | XTCE command/telemetry definitions, displays, and YAMCS procedures |
| `support/` | Device configuration defaults/template and CLI container definition |
| `mission_inc/` | Mission scoped identifiers such as performance IDs |
| `platform_inc/` | Platform scoped identifiers such as cFS message IDs |

Not every component is required to implement every optional artifact, but the build expects a valid cFS CMake target, unique message/device identifiers, and a simulator interface when the component is selected for simulation.

## Device path

```mermaid
flowchart LR
    CMD[cFS command] --> APP[Component cFS app]
    APP --> SHARED[Shared device protocol]
    SHARED --> HWLIB[HWLIB interface]
    HWLIB <--> SIM[Component simulator]
    SIM --> DYNAMICS[42 context or commands]
    APP --> TLM[cFS telemetry]
```

In simulation, HWLIB connects to a ZeroMQ IPC endpoint and the simulator binds the matching endpoint.
On a physical target, a target specific HWLIB implementation performs the device I/O.
The device protocol above HWLIB should remain consistent, but transport timing and hardware behavior must be tested separately.

## Focused CLI workflow

Select the CLI component in `build/active.yaml`, then run:

```bash
make cfg
make cli
make cli-start
```

The CLI environment starts 42, one Simulith Server, one Director, the selected component simulator, and the selected CLI container.
It does not start cFS, YAMCS, or CryptoLib.

## Full integration workflow

After focused protocol and simulator checks:

```bash
make cfg
make
make start
```

The YAMCS build copies every component's XTCE, displays, and procedures into its build context.
The cFS build finds component applications through `CFS_APP_PATH=../comp`, while `cfg/shire_defs/targets.cmake` and the generated startup script control what is built and started.

## Tests

```bash
make test-sim
make test-fsw
```

`make test-sim` discovers component simulator tests from the spacecraft configuration in `build/build.yaml` and writes combined simulator coverage under `build/sim-coverage/`.
Run `make cfg` first so that snapshot matches `build/active.yaml`.
When the snapshot is absent, the Simulith Makefile uses its fallback component list.
`make test-fsw` runs the cFS/app test build and writes coverage in the configured FSW build tree.

When changing a component, keep its device protocol, cFS messages, XTCE, procedure arguments, simulator, CLI, configuration template, and tests synchronized.
