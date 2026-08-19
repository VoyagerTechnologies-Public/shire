# DRM Concept of Operations

> **Status:** This page distinguishes the current DRM implementation from its intended mission operations.
> The default `sat-1` configuration implements a distributed Do No Harm startup, focused component control, two command and telemetry paths, stored sequences, two LC responses, telemetry storage, and CFDP services.
> Safe and payload operations are operational configurations rather than software modes in the current repository.
> SHIRE does not provide a spacecraft mode manager, one spacecraft mode parameter, automatic transitions among those configurations, or executed verification evidence for every mission phase.

## Current operational baseline

The Design Reference Mission represents a small spacecraft in Low Earth Orbit.
42 calculates orbit and vehicle dynamics while the SHIRE director and server coordinate the component simulators, cFS, and simulation time.
The default `sat-1` selection includes ADCS, Demo, EPS, and Radio components.

The current implementation provides the following operational building blocks.

| Area | Implemented behavior | Current boundary |
| --- | --- | --- |
| ADCS | cFS enable, disable, mode, target, and sensor requests with six simulator control modes against 42 | No current LC action or mode manager selects an ADCS recovery mode automatically. |
| Demo | cFS control of a representative device whose three channels normally encode the 42 body Sun vector | Demo publishes telemetry and does not create standalone science data files. |
| EPS | Simulated solar input, battery energy, and eight switched loads with cFS switch control and device telemetry | The current model has no named power modes, component power connections, automatic load shedding, state of charge telemetry, or battery protection response. |
| Radio | Command and telemetry packet exchange using `SLEEP`, `TX`, `RX`, and `DUPLEX` configurations | The model has no RF propagation, ground visibility, signal strength, or geometry driven link availability. |
| CryptoLib | Flight Radio security processing with reciprocal processing by the standalone ground service | The checked in configuration is a development example rather than an operational key management or security deployment. |
| Flight data | Selected DS packet storage under `/d`, FM file operations, and CF with YAMCS CFDP services | DS does not record every telemetry packet and no payload processing pipeline is implemented. |
| Ground operations | YAMCS commanding, telemetry, archives, procedures, displays, timelines, and CFDP services | The default timeline contains parameter bands but no scheduled activities. |

## Operational configurations

The mission concept uses three recognizable spacecraft configurations.
Only Do No Harm is assembled automatically by the current flight configuration.
Safe and payload operations require reviewed operator commands and procedures.

``` mermaid
stateDiagram-v2
    state "Do No Harm" as DNH
    state "Safe configuration" as SAFE
    state "Payload operations" as PAYLOAD

    [*] --> DNH: Implemented power on startup
    DNH --> SAFE: Ground command
    SAFE --> PAYLOAD: Ground command
    PAYLOAD --> SAFE: Ground command / anomaly

    note right of DNH
        Distributed observed state
    end note
    note right of SAFE
        Conceptual configuration
    end note
    note right of PAYLOAD
        Conceptual configuration
    end note
```

Only the startup transition is implemented automatically.
The remaining arrows describe the intended operator flow rather than current flight software transitions.

### Do No Harm

Do No Harm is the conservative startup configuration used while the operator establishes the current state and prepares commissioning.
It is not a named cFS mode or one command.

The default `sat-1` power on path is assembled as follows.

| Step | Current implementation |
| --- | --- |
| Application load | The CPU1 startup script loads CryptoLib, IO Lib, CF, DS, FM, LC, SC, SCH, ADCS, Demo, EPS, Radio, CI_LAB, and TO_LAB. |
| Initial component state | The ADCS, Demo, and Radio cFS apps initialize their device state as disabled, the ADCS simulator starts in `PASSIVE`, and the EPS simulator starts all eight switches off. |
| Initial dynamics | The checked in 42 spacecraft starts with body rates of 5, negative 5, and 10 degrees per second while ADCS control is inactive. |
| Automatic sequence | SC is configured to start RTS 1 after a power on reset and not after a processor reset. |
| Core services | RTS 1 enables DS, enables TO_LAB output to `shire-gsw`, sets LC active, enables RTS numbers 1 through 15, and starts RTS 3. |
| Initialization check | RTS 3 sends one cFE Executive Services NOOP. |
| Radio receive state | LC action point 5 observes that the Radio cFS device state is not enabled and starts RTS 5 after one failure. |
| Radio configuration | RTS 5 enables the Radio cFS device, configures it for `RX`, and resets the statistics for LC action point 5. |
| Telemetry storage | DS writes its configured cFE and cFS packets plus Demo housekeeping and device telemetry, EPS housekeeping, and Radio housekeeping to time named `sv` files under `/d`. |

The Radio simulator internally initializes its configuration as `DUPLEX` before the cFS Radio app is enabled.
RTS 5 establishes the settled flight reported state as enabled and `RX`.
Use Radio cFS housekeeping after RTS 5 when confirming the operational configuration.

The settled observable Do No Harm checkpoint is:

* ADCS device disabled with the simulated controller in `PASSIVE`
* Demo device disabled
* all eight EPS switches off
* Radio device enabled in `RX`
* DS and LC active
* TO_LAB debug telemetry enabled
* RTS 1 and RTS 3 complete
* 42 and Simulith time advancing

This checkpoint is the current evidence of Do No Harm because no single telemetry parameter represents it.
The selected spacecraft changes which component applications are retained in the generated startup script, so operators must adjust the checkpoint when using `flatsat` or `sat-2`.

### Safe configuration

Safe is an intended operator assembled configuration for maintaining observability and conservative activity after the startup checkpoint.
The current repository does not include a `SAFE` command, a Safe RTS, an autonomous Safe transition, or one complete Safe procedure.

A reviewed Safe configuration would normally include:

* ADCS enabled in `SUNSAFE` when the run plan requires active Sun pointing
* Demo disabled
* EPS switches set to a reviewed essential load pattern
* Radio in `RX` outside a planned pass and `DUPLEX` during the pass
* DS, LC, and SC active
* fresh command, event, power, attitude, and link telemetry

`AdcsComponent.ycs` and `CheckoutTest.ycs` currently demonstrate enabling ADCS and selecting `SUNSAFE`.
They do not establish a spacecraft wide Safe state.
The current LC tables do not contain active bus voltage, battery, pointing, or payload watchpoints that transition the spacecraft to Safe.

### Payload operations configuration

Payload operations are the intended configuration for exercising the Demo component, attitude behavior, telemetry storage, and file downlink after commissioning.
This is not a current software mode.

A reviewed payload operation can combine:

* an ADCS mode chosen for the objective from `SUNSAFE`, `NADIR`, `TARGET`, or `INERTIAL`
* Demo enabled with a recorded configuration value
* EPS switch settings and limits defined by the run plan
* DS recording selected Demo and spacecraft telemetry
* Radio in `DUPLEX` for the bounded transfer interval
* FM file selection followed by CF and YAMCS CFDP transfer

The Demo component generates housekeeping and three channel device telemetry.
DS records those packets into a shared telemetry archive along with other selected packets.
The resulting `.ds` file is therefore not a Demo only science product.

No current sequence checks power margin, payload readiness, or pointing performance before entering this configuration.
No current YAMCS timeline schedules payload activities or pass commands.
Use the [Nominal Operations](../scenarios/nominal-operations.md) scenario as a manual composition of the available mechanisms.

## Current onboard automation

SC supports 15 RTS slots in the current configuration.
Enabling the range does not mean that all 15 contain DRM operational behavior.

| RTS | Current contents |
| --- | --- |
| 1 | SHIRE power on sequence that enables DS, TO_LAB, LC, and the RTS group before starting RTS 3. |
| 2 | Upstream SC example sequence containing three SC NOOP commands. |
| 3 | SHIRE initialization sequence containing one cFE Executive Services NOOP. |
| 4 | Upstream SC example sequence containing three SC NOOP commands. |
| 5 | Radio enable sequence that selects `RX` and resets LC action point 5 statistics. |
| 6 | Pass sequence that selects `DUPLEX`, waits 480 SC wakeups, and returns the Radio to `RX`. |
| 7 through 10 | Empty mission table stubs. |
| 11 | Demo response that disables Demo, waits 10 SC wakeups, and enables it again. |
| 12 through 15 | Empty mission table stubs. |

RTS 6 describes its wait as eight minutes at the expected one hertz SC wakeup rate.
It does not consult 42 ground station visibility or the YAMCS timeline.

The current LC tables contain two active action points.

| Action point | Watch condition | Response |
| --- | --- | --- |
| 5 | Radio `DeviceEnabled` is not 1 | Start RTS 5 after one failure. |
| 11 | Demo is enabled and Demo channel 1 is below 256 | Start RTS 11 after one failure. |

LC does not currently implement voltage, battery state of charge, attitude, thermal, or general Safe responses.
The Mission Requirements page records these behaviors as candidate needs until an approved source, acceptance criterion, and verification approach exist.

## Command and telemetry operations

The DRM provides two operator facing command and telemetry paths.

| Path | Current use |
| --- | --- |
| Direct debug | YAMCS commands reach CI_LAB directly and TO_LAB returns telemetry without using Radio or CryptoLib. |
| Representative radio | Commands pass through the standalone CryptoLib service, the Radio simulator, and the Radio cFS app while telemetry returns through the reciprocal path. |

The debug path is a development convenience and is not evidence that the representative radio path worked.
In `RX`, the Radio path can deliver commands but does not return radio telemetry.
In `DUPLEX`, commands and telemetry can flow in both directions.

Both YAMCS command output links currently consume `tc_realtime` when enabled.
One operator command can therefore reach cFS through both paths and increment counters twice.
Select and record the intended path before interpreting command history or counters.

YAMCS also sends simulation backdoor packets to the Director and receives 42 truth telemetry from the Director.
Backdoor commands are simulation controls and must not be treated as spacecraft commands.

## Mission phase support

The mission phases remain useful organizing concepts, but their implementation depth differs.

| Mission phase | Operational intent | Current repository support |
| --- | --- | --- |
| Launch and deployment | Conservative configuration after separation | 42 begins directly in the configured LEO state and the flight stack performs the distributed Do No Harm startup, but launch and separation are not simulated events. |
| Commissioning | Radio path establishment with core and component checkout | The manual Commissioning scenario composes RTS 6, cFE checks, component procedures, FM, DS, and CFDP, but no single end to end commissioning procedure exists. |
| Nominal operations | Attitude, payload, storage, and downlink activities | The manual Nominal Operations scenario uses current building blocks, but no scheduled timeline items or complete nominal procedure exist. |
| Contingency response | Detection, isolation, recovery, and evidence retention | cFE events, LC action points 5 and 11, component tests, backdoors, and focused procedures are available, but there is no general safing manager or complete anomaly response procedure. |
| Decommissioning | Final data retrieval and a reviewed passive configuration | The concept and proposed requirements describe this phase, but no decommissioning sequence or procedure is implemented. |

## Representative operator cycle

The repository does not schedule one or more ground contacts per day.
The default YAMCS timeline has no scheduled items and the Radio does not derive availability from 42 ground station geometry.

A current manual operator cycle is:

1. Start the DRM and verify the distributed Do No Harm checkpoint.
2. Review service health, YAMCS links, fresh telemetry, events, and command error counts.
3. Start RTS 6 and confirm the Radio enters `DUPLEX` through the intended command path.
4. Run the required component checks and configure ADCS, Demo, and EPS for the activity.
5. Record Demo activity while DS collects the selected telemetry packets.
6. Close the DS file set and select an actual completed file from an FM listing.
7. Transfer the selected file with CFDP while the Radio remains `DUPLEX`.
8. Confirm the Radio returns to `RX`, review errors and incomplete work, and record the final component state.

The current implementation does not automatically prioritize files, delete successfully received files, process science products, or manage an onboard backlog.
Those actions require a reviewed operator plan or future automation.

## Operational constraints and simulation fidelity

The DRM is a software development and integration environment with these limits:

* 42 calculates orbital and vehicle dynamics from the checked in configuration, including the initial body rates, but many environmental torque sources are disabled.
* The current ADCS simulator uses 42 truth and sends modeled wheel and magnetic torquer commands, but this does not establish physical sensor or actuator performance.
* The EPS simulator uses a simple solar, battery energy, and fixed load model, and its switch states do not power other simulated components on or off.
* The Radio and CryptoLib path exchanges packets but does not model RF propagation, antenna geometry, contact visibility, bit errors, or measured throughput.
* RTS 6 creates a fixed pass interval instead of using the two ground stations listed in the 42 configuration.
* DS records only the packets named in its filter table.
* Docker resource limits, host load, and scheduler behavior can affect wall clock timing and CFDP performance.
* The included YAMCS configuration and CryptoLib setup are development defaults rather than hardened operational deployments.
* The current DRM runs on the host CPU1 target and does not establish behavior on a flight processor or physical hardware.

Use these practices when operating or testing the DRM:

* Define acceptance limits, timeouts, path selection, and stop conditions before starting a run.
* Use fresh telemetry and application events to confirm state rather than relying on one display or command acknowledgment.
* Use named simulator backdoor controls only when the test identifies the injected condition and expected response.
* Monitor `docker stats` and simulation time when timing matters.
* Retain the active configuration, repository revision, procedure revision, command history, telemetry, logs, and result with each evidence record.

## Roles and responsibilities

* Mission operator
    * Commands and monitors the DRM, chooses the intended link path, applies stop conditions, and records the final state.
* Subsystem lead
    * Defines component limits, reviews commands, interprets telemetry, and approves subsystem recovery actions.
* Simulation and test engineer
    * Maintains simulator behavior, configuration, test controls, expected results, and reproducibility evidence.
* Flight and ground software engineer
    * Maintains cFS applications, tables, YAMCS definitions, procedures, and automated tests.

## Related material

The [Mission Requirements](mission-requirements.md) page defines the proposed traceable baseline and records candidate needs that are not ready to become requirements.
The [Verification and Validation](verification-and-validation.md) page describes current evidence entry points and remaining verification work.
Begin hands on review with the [Commissioning](../scenarios/commissioning.md) scenario, then continue with [Nominal Operations](../scenarios/nominal-operations.md).

***
Last reviewed: 20260819
