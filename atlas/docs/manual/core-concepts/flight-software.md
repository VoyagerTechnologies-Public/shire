# Flight Software

SHIRE uses NASA's core Flight System (cFS) as its DRM flight software framework.
The current simulation target runs cFS on the SHIRE PSP/OSAL configuration inside the `shire-fsw` container.
An alternate flight software framework would need its own build and runtime integration, device interfaces, and command and telemetry paths.
It needs a Simulith timing client only when it participates in the synchronized simulation.
The repository does not currently include another integrated flight software framework.

## cFS architecture layers

cFS separates mission behavior from the operating system and hardware that run it.
Each lower layer provides a stable interface to the layer above it so applications can move between supported targets with less platform specific code.
The diagram shows responsibility layers rather than every function call.

```mermaid
flowchart TB
    TOOLS["Development tools<br/>Ground systems"]
    APPS["Application layer<br/>cFS apps<br/>SHIRE component apps"]
    CFE["core Flight Executive<br/>ES, EVS, SB<br/>TIME, TBL"]
    ABSTRACTION["Platform abstraction<br/>OSAL, PSP"]
    PLATFORM["RTOS and boot<br/>Linux, BSP, drivers"]

    TOOLS <--> APPS
    APPS --> CFE --> ABSTRACTION --> PLATFORM
```

| Layer | Responsibility in SHIRE |
| --- | --- |
| Development tools and ground systems | Tools build, configure, test, command, monitor, and operate the flight software, with YAMCS serving as the DRM ground system. |
| Application | Reusable cFS applications provide common mission functions while SHIRE component applications provide spacecraft specific behavior. |
| core Flight Executive | Executive Services, Event Services, Software Bus, Time Services, and Table Services provide the portable application runtime. |
| Platform abstraction | OSAL presents a common operating system API while the PSP handles startup, timebase, memory, watchdog, and platform services. |
| RTOS and boot | The operating system, board support, boot environment, C library, and device drivers support the abstraction layer, with CPU1 currently using the SHIRE Linux configuration. |

### Where SHIRE hardware interfaces fit

HWLIB is a hardware interface library inside the PSP source tree rather than a separate standard cFS architecture layer.
Component applications call shared device code, which uses HWLIB interfaces for UART, I2C, SPI, GPIO, sockets, memory, CAN, and torquers.

```mermaid
flowchart TB
    APP["Component<br/>application"]
    SHARED["Shared device<br/>protocol"]
    HWLIB["HWLIB<br/>interface"]
    SIM["CPU1<br/>Simulith transport"]
    HARDWARE["Target driver<br/>Physical device"]

    APP --> SHARED --> HWLIB
    HWLIB --> SIM
    HWLIB -.-> HARDWARE
```

The enabled CPU1 SHIRE PSP compiles the simulation implementations from `cfs/psp/fsw/hwlib/src/sim/` and links them with Simulith and ZeroMQ.
Linux HWLIB implementations also exist under `cfs/psp/fsw/hwlib/src/linux/`, but the disabled CPU2 cFS target does not currently assemble them into a validated board build.
Moving to physical hardware therefore requires target specific PSP, OSAL, board support, driver, configuration, and deployment work.

### Simulation and flight target boundary

Simulith is a dependency of the `amd64-shire` simulation target, not of the flight applications or the default cFS target.
The Shire toolchain explicitly selects the Shire PSP/OSAL, the Software Bus completion observer, and the Simulith-backed SCH clock and completion barrier.
Those selections are absent from physical-target toolchains.

Without the Shire selection, cFE compiles out the Software Bus observer call sites and SCH uses the portable clock backend based on cFE TIME and an OSAL timer.
The scheduler application and component applications contain no direct Simulith calls.
A physical target supplies its normal PSP, OSAL, and HWLIB drivers and neither links nor runs Simulith.

The disabled `armv7l-linux` target already selects the portable `pc-linux` PSP and POSIX OSAL path and does not opt into either Shire extension.
It remains disabled because the complete board image and physical drivers have not yet been validated, not because Simulith is required.

## Current mission content

The baseline CPU1 startup script is `cfg/shire_defs/cpu1_cfe_es_startup.scr`.
It declares:

| Type | Included content |
| --- | --- |
| Libraries | CryptoLib and IO_LIB |
| Reusable cFS applications | CF, DS, FM, LC, SC, SCH, CI_LAB, and TO_LAB |
| SHIRE component applications | ADCS, Demo, EPS, and Radio |

During configuration, the orchestrator copies the mission definitions to `build/<mission>/<spacecraft>/shire_defs/` and removes ADCS, Demo, EPS, or Radio startup entries that are not selected by that spacecraft.
Other reusable applications remain in the startup script.

`cfg/shire_defs/targets.cmake` defines the build targets and application set.
Mission tables under `cfg/shire_defs/tables/` configure scheduler messages, subscriptions, data storage, limit checking, stored commands, radio routing, and CFDP behavior.

## Command and telemetry flow

Component applications receive CCSDS and cFS messages on the Software Bus.
They translate application commands into device operations through HWLIB and publish housekeeping or component telemetry back to the Software Bus.
CI_LAB and TO_LAB provide the direct debug link to YAMCS.
The Radio component provides the simulated radio path.

Reusable applications add mission services:

* **CF** provides CFDP file transfer behavior.
* **DS** records selected packets to files.
* **FM** exposes file system operations.
* **LC** evaluates watchpoints and can initiate configured actions.
* **SC** executes absolute and relative command sequences.
* **SCH** produces scheduled wakeups and commands.

## Synchronized tick processing

The SHIRE PSP participates only in the EXECUTE phase of each simulation tick.
SCH releases exactly one schedule slot for that tick.
The PSP registers the scheduled messages that require completion and considers a
participant complete only after its consuming task has returned to the Software
Bus receive boundary.
It records registration-to-dispatch and dispatch-to-return latency separately so
scheduler delay can be distinguished from application and device execution.

Completion accounting also follows commands produced by scheduled work.
When an active participant publishes a command packet, cFE Software Bus routing
registers one child participant for every destination subscriber.
The parent task can therefore finish without allowing simulation time to advance
past command processing that is still queued downstream.
Telemetry publication and ordinary asynchronous task traffic do not create
these child records.

The PSP rejects duplicate, stale, and future completion records.
Missing work withholds EXECUTE completion.
Watchdog output identifies the sequence, schedule entry, message ID, and
claimed or running state that is holding EXECUTE.
The next SCH slot is not released until the Server completes PREPARE, EXECUTE,
and COMMIT for the current tick.

Flight applications do not contain simulation-only behavior branches.
Their shared device functions perform complete transactions through target-neutral
HWLIB interfaces.
The simulation implementations use blocking deadline-based transport, while a
physical target supplies its own implementation of the same interface.

## Build and test

From the repository root:

```bash
make fsw
make test-fsw
```

`make fsw` builds the configured cFS target and its runtime image.
`make test-fsw` performs a clean test build, runs CTest, collects LCOV data, and generates an HTML coverage report under the configured FSW build tree.

The presence of CPU2 and ARM toolchain definitions demonstrates cross build scaffolding, but successful deployment to a board depends on the selected target, BSP, drivers, and hardware specific validation.
See [Development Board](../how-to/development-board.md) for the currently documented board workflow.

***
Last reviewed: 20260913
