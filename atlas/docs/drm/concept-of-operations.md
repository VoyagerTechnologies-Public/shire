# DRM Concept of Operations

> **Scope:** This page describes the intended operational model for the DRM.
> The checked in applications, tables, procedures, and simulators implement portions of it, but not every autonomous transition, quantitative threshold, failure response, or mission phase below has executed verification evidence.
> The current startup configuration boots into the Do No Harm configuration through application defaults, SC, LC, and RTS tables.
> SHIRE does not currently include a top level mission mode manager or one telemetry parameter that identifies the spacecraft wide mode.
> Treat unreferenced operational behavior as a design objective until it is linked from the [V&V plan](verification-and-validation.md).

## System Overview

The Design Reference Mission (DRM) represents a small satellite in Low Earth Orbit (LEO).
The DRM includes command sequences, scenario definitions, and documentation intended to exercise the interactions between these components during mission phases.

The subsystem descriptions below state the intended mission role.
They do not claim that every listed failure response or autonomous behavior is implemented.

Mission subsystems:

* Attitude Determination and Control System (ADCS)
    * Purpose: Provides attitude sensing and control to point the vehicle or payload for mission objectives such as Sun pointing, nadir observations, or stable inertial pointing.
    * Key interfaces: Publishes telemetry from sensors and commands actuators based on modes and setpoint commands received.
    * Typical commands: Mode changes, attitude set points, calibration and sensor resets.
    * Failure modes & mitigations: Loss of fine pointing (fall back to SUNSAFE), stuck wheel (command wheel saturation or stop), sensor dropouts (switch to coarse sensors or safe mode).
* CryptoLib
    * Purpose: Provides mission cryptographic services for authenticated encryption when enabled by the mission profile.
    * Key interfaces: Offers crypto services to other cFS apps such as sign/verify, encrypt/decrypt, key management, and security event logging.
    * Typical commands: Apply or process security, enable/disable/configure crypto services, perform self tests.
    * Failure modes & mitigations: Key corruption or service failure, change security parameter index in use and resolve.
* Demonstration Instrument
    * Purpose: A representative payload used to exercise data collection, onboard storage, and retrieval workflows such as generating files that FM and DS manage and CFDP transfers.
    * Key interfaces: Configuration commands, enable/disable, and science telemetry.
    * Typical commands: Enable instrument, configure measurement parameters, and disable instrument.
    * Failure modes & mitigations: Invalid data range reported, restart payload.
* Electrical Power System (EPS)
    * Purpose: Manages power generation from solar arrays, storage from batteries, and power distribution via switched services.
    * Key interfaces: Bus voltages, battery state of charge, solar panel outputs, load states, and accepts switch circuit on/off commands.
    * Typical commands: Change power modes, enable/disable loads, perform battery/state of charge resets, and run power health tests.
    * Failure modes & mitigations: Low bus voltage (shed nonessential loads, enter safe mode), battery over temperature (reduce charging).
* Radio
    * Purpose: Enables a radio frequency (RF) space link including duplex and half duplex modes and direct data transfers.
    * Key interfaces: Radio state and mode telemetry, and spacelink interface to the ground.
    * Typical commands: configure radio mode and set link parameters.
    * Failure modes & mitigations: link outage (retry on next pass or extend pass with RTS), packet loss (retransmit via CFDP), and misconfiguration (reset the radio or issue the pass sequence again).

## Mission Modes

The DRM concept uses distinct mission modes to define the overall vehicle configuration and operational state.
Mode transitions are commanded by ground operators or triggered autonomously based on fault conditions.

``` mermaid
stateDiagram-v2
    dnh: Do No Harm
    [*] --> dnh :Startup
    dnh --> Safe :Ground Command
    Safe --> Science:Ground Command
    Science --> Safe: Fault
```
### Do No Harm Mode

**Purpose:** Conservative startup state that minimizes spacecraft activity and power consumption immediately following deployment or during critical fault conditions.

**Configuration:**
* ADCS, EPS loads, Payload, and Radio transmit disabled
* Spacecraft is passively tumbling
* Core cFS applications active: CFE, SCH, TO_LAB, CI_LAB, EVS, SB
* Data Storage (DS) logging event messages and critical telemetry to file
* Limit Checker (LC) monitoring telemetry thresholds
* Stored Command (SC) ready to execute time tagged sequences
* Radio in Receive only mode, listening for ground commands
* RTS 1 (basic initialization) executes automatically on boot

**Entry Conditions:**
* Spacecraft separation from launch vehicle (automatic)
* Commanded entry from any other mode
* Critical fault requiring system wide reset

**Exit Conditions:**
* Ground command to transition to Safe Mode or Science Mode
* Execution of commissioning RTS sequences

**Rationale:** This mode ensures the spacecraft cannot inadvertently harm itself through uncontrolled actuator commands, power drain, or thermal issues while awaiting ground contact and operator assessment.

#### Current boot implementation

The checked in DRM establishes the Do No Harm configuration through several cooperating mechanisms.
It does not rely on a single command that sets a named mode.

| Do No Harm behavior | Current implementation |
| --- | --- |
| Flight applications start | The cFE startup script launches the selected cFS applications. |
| Component activity remains conservative | ADCS, Demo, and Radio devices initialize disabled while the EPS simulator initializes all eight switches off. |
| Startup actions run automatically | SC is configured to start RTS 1 after a power on reset. |
| Data and monitoring services become active | RTS 1 enables DS, the TO_LAB debug output, and LC. |
| Stored commands become available | RTS 1 enables RTS 1 through 15 and starts initialization RTS 3. |
| The radio listens without transmitting | Active LC action point 5 detects the disabled Radio and starts RTS 5, which enables the Radio and configures Receive mode. |

This distributed configuration is the implemented Do No Harm boot path.
Operators must verify its observable parts through application housekeeping, events, SC execution state, LC state, EPS switch state, and Radio mode.
The absence of one mode parameter means that a future mission mode manager would improve commanding and observability without changing the present boot intent.

### Safe Mode

**Purpose:** Stable operational state that maintains spacecraft health and power positive configuration while enabling ground communications.

**Configuration:**
* ADCS enabled in SUNSAFE mode (Sun pointing for maximum solar array illumination)
* EPS managing power with essential loads only:
  * ADCS actuators enabled
  * Radio in DUPLEX mode during ground passes, else Receive only mode
  * Payload disabled
* Autonomous attitude control maintaining Sun pointing
* All core cFS applications active and responsive
* Radio capable of commanding and telemetry downlink
* DS, LC, SC apps actively monitoring and logging

**Entry Conditions:**
* Commanded transition from Do No Harm Mode during commissioning
* Autonomous entry from Science Mode upon detection of:
  * Bus voltage < 24V
  * Battery SOC < 30%
  * ADCS loss of fine pointing (automatic SUNSAFE transition)
  * Payload fault or anomaly
* Commanded entry from ground during anomaly response

**Exit Conditions:**
* Power margins restored (Bus voltage > 26V, Battery SOC > 50%)
* ADCS operational and stable
* Ground command to transition to Science Mode
* All fault conditions cleared

**Rationale:** Safe Mode prioritizes spacecraft survival and ground communication.
The Sun pointing attitude ensures maximum power generation while the configuration provides sufficient capability for diagnosis and recovery operations.

### Science Mode

**Purpose:** Nominal operational state for mission science objectives with payload operations and targeted attitude control.

**Configuration:**
* ADCS in TRACK mode with nadir pointing attitude for Earth observation
* EPS distributing power to all operational loads:
  * ADCS maintaining targeted attitude
  * Payload (Demonstration Instrument) enabled and collecting data
  * Radio in scheduled DUPLEX mode for passes
  * All nonessential loads available per power budget
* Payload actively acquiring science data and generating data products
* Automated sequences (RTS) executing planned activities
* Data files being generated, stored, and queued for downlink
* LC monitoring for contingency conditions with automated responses
* SC executing time tagged commanding for scheduled operations

**Entry Conditions:**
* Commanded transition from Safe Mode
* Spacecraft commissioning complete
* Power margins adequate (Bus voltage > 26V, Battery SOC > 60%)
* ADCS operational with fine pointing capability
* Payload checkout complete
* Ground approval obtained

**Exit Conditions:**
* Power margins degraded below threshold
* ADCS fault or loss of attitude control
* Payload anomaly requiring investigation
* Commanded transition to Safe Mode for maintenance or troubleshooting
* Autonomous fault detection triggering mode change

**Rationale:** Science Mode represents the mission's primary operational state, maximizing science data collection while maintaining sufficient margins for safe operations.
The nadir pointing attitude enables Earth observation payloads while the full system configuration supports mission objectives.

## Mission Phases

1. Launch & Deployment
    * Immediately after separation the vehicle is placed into the conservative, power saving **Do No Harm** mode described above.
    * No component subsystems are enabled and the spacecraft is tumbling.
    * The data storage (DS) application is logging all event messages and telemetry produced to file.
    * The limit checker (LC) and stored command (SC) applications perform any fault detection and correction.
    * The basic initialization is done in cFS via the startup relative time sequence (RTS) number 1.
    * The spacecraft awaits ground contact for commissioning.
2. Commissioning
    * Establish first contact from the ground by sending the Start RTS 6 (Start Pass) command.
    * Verify command/telemetry link and basic responsiveness with a NOOP command and other health checks.
    * Bring core subsystems online using reviewed procedures.
    * Perform basic functional tests (file listings via FM, file transfers via CFDP, storage management via DS).
    * Perform any anomaly investigations as necessary.
3. Nominal Operations
    * Regular science/payload activities, attitude maintenance, periodic housekeeping, and scheduled data downlinks.
    * Modifications to limits and configuration settings as requested by subsystems after review.
    * Routine use of RTS onboard the vehicle and YAMCS procedure stacks to automate passes.
4. Contingency and Anomaly Response
    * Detection via EVS/event messages and telemetry limits.
    * Fault isolation using cFS app diagnostics, log review, and replayed telemetry in SHIRE.
    * Recovery actions: safe mode recovery procedures, selective subsystem restarts, and uplinked software patches where applicable.
5. Decommissioning / End of Life
    * Final data retrieval, graceful shutdown of mission services, and transition to a passive or disposal state according to mission rules.

## A Day in the Life (Nominal Operations)

Typical operations day for the DRM in SHIRE focuses on monitoring, scheduled commanding, and data handling.
Depending on the current data rates one or more passes per day are expected, assuming the ground station is available for scheduling.
Assuming one pass the following is to be performed:

* Confirm current state of health, begin anomaly investigation if contingency.
* Perform any uploads or configuration changes.
* Delete any files that the data pipeline confirmed successful receipt of.
* Get the latest data file set from onboard the vehicle.
* Download data files until the end of the pass.
* Once pass is complete confirm data pipeline has begun processing.

It is typical for data to be backlogged on the vehicle.
Data may be requested to be skipped based on an external phenomenon, such as a high priority event the payload may have detected.

## Operational Constraints and Simulation Fidelity

The SHIRE DRM models command, telemetry, flight software, component interfaces, and vehicle dynamics with these limitations:

* Timing and pass windows are simulated.
  Real orbital dynamics are being calculated and used by the simulators, but are not leveraged for pass durations.
* Hardware effects are modeled at a software level.
  Thermal gradients, fine star tracker jitter, and radiation effects are not represented by the current DRM components.
* Resource contention and container level resource caps (CPU, memory) are dependent on the host and may affect simulated timing when running on limited machines.
* Network latency and Docker host performance can change CFDP transfer characteristics compared to in flight performance.

Recommended operator practices to mitigate these constraints:

* Use the scenario stacks and step controls in YAMCS to repeat timing sensitive steps.
* When validating fault responses, inject a defined failure through the available scenario controls rather than relying on an uncontrolled condition.
* Monitor host resources with `docker stats` while running heavy scenarios and consider increasing container resources for more representative testing.

## Roles and Responsibilities

This section clarifies who does what in the DRM.

* Flight Controllers (Mission Operator)
    * Primary responsibility for commanding and monitoring the DRM during commissioning and nominal operations.
    * Executes RTS sequences, performs health checks, and coordinates data downlinks.
* Subsystem Leads (ADCS, Payload, Radio)
    * Expert for a specific subsystem. 
    * Runs detailed checkouts, interprets subsystem telemetry, and advises the Flight Controller on configuration changes.
* Simulation and Test Engineer
    * Maintains the SHIRE scenario stacks, container images, and scenario scripts used by the DRM.
    * Ensures YAMCS, CFDP, and the simulated hardware stacks are current and reproducible.
* Software Engineer (cFS & Apps)
    * Develops and maintains cFS applications and ensures their integration with the simulation environment.
    * Produces and validates command/telemetry definitions and application level test cases used.

## Notes and Next Steps

See the [mission requirements](./mission-requirements.md) for a detailed list of DRM requirements or jump into the [commissioning scenario](../scenarios/commissioning.md) for an implementation example.

***
Last reviewed: 14 August 2026
