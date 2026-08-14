# DRM Concept of Operations

## System Overview

The Design Reference Mission (DRM) example provided with SHIRE is a small satellite deployed in Low Earth Orbit (LEO).
The smallsat platform has become the standard for technology demonstrations in the space industry and enables scaling to constellations of vehicles.
The DRM includes command sequences, scenario definitions, and documentation intended to exercise the interactions between these components during mission phases.

Mission subsystems:

* Attitude Determination and Control System (ADCS)
    * Purpose: Provides attitude sensing and control to point the vehicle or payload for mission objectives such as sun‑pointing, nadir observations, or stable inertial pointing.
    * Key interfaces: Publishes telemetry from sensors and commands actuators based on modes and setpoint commands received.
    * Typical commands: Mode changes, attitude set points, calibration and sensor resets.
    * Failure modes & mitigations: Loss of fine pointing (fall back to SUNSAFE), stuck wheel (command wheel saturation or stop), sensor dropouts (switch to coarse sensors or safe mode).
* CryptoLib
    * Purpose: Provides mission cryptographic services for authenticated encryption when enabled by the mission profile.
    * Key interfaces: Offers crypto services to other cFS apps such as sign/verify, encrypt/decrypt, key management, and security event logging.
    * Typical commands: Apply or process security, enable/disable/configure crypto services, perform self-tests.
    * Failure modes & mitigations: Key corruption or service failure, change security parameter index in use and resolve.
* Demonstration Instrument
    * Purpose: A representative payload used to exercise data collection, on‑board storage, and retrieval workflows (e.g., generate files that are managed by FM/DS and transferred via CFDP).
    * Key interfaces: Configuration commands, enable/disable, and science telemetry.
    * Typical commands: Enable instrument, configure measurement parameters, and disable instrument.
    * Failure modes & mitigations: Invalid data range reported, restart payload.
* Electrical Power System (EPS)
    * Purpose: Manages power generation from solar arrays, storage from batteries, and power distribution via switched services.
    * Key interfaces: Bus voltages, battery state of charge, solar panel outputs, load states, and accepts switch circuit on/off commands.
    * Typical commands: Change power modes, enable/disable loads, perform battery/state-of-charge resets, and run power health tests.
    * Failure modes & mitigations: Low bus voltage (shed non‑essential loads, enter safe mode), battery over temperature (reduce charging).
* Radio
    * Purpose: Enables radio frequency (RF) spacelink including duplex/half‑duplex modes and point‑to‑point data transfers.
    * Key interfaces: Radio state and mode telemetry, and spacelink interface to the ground.
    * Typical commands: configure radio mode and set link parameters.
    * Failure modes & mitigations: link outage (retry on next pass or extend pass with RTS), packet loss (retransmit via CFDP), and misconfiguration (reset radio or re‑issue pass sequence).

## Mission Modes

The DRM spacecraft operates in distinct mission modes that define the overall vehicle configuration and operational state. 
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

**Purpose:** Conservative boot-up state that minimizes spacecraft activity and power consumption immediately following deployment or during critical fault conditions.

**Configuration:**
* All component subsystems disabled (ADCS, EPS loads, Payload, Radio transmit)
* Spacecraft is passively tumbling
* Core cFS applications active: CFE, SCH, TO_LAB, CI_LAB, EVS, SB
* Data Storage (DS) logging event messages and critical telemetry to file
* Limit Checker (LC) monitoring telemetry thresholds
* Stored Command (SC) ready to execute time-tagged sequences
* Radio in RECEIVE-only mode, listening for ground commands
* RTS 1 (basic initialization) executes automatically on boot

**Entry Conditions:**
* Spacecraft separation from launch vehicle (automatic)
* Commanded entry from any other mode
* Critical fault requiring system-wide reset

**Exit Conditions:**
* Ground command to transition to Safe Mode or Science Mode
* Execution of commissioning RTS sequences

**Rationale:** This mode ensures the spacecraft cannot inadvertently harm itself through uncontrolled actuator commands, power drain, or thermal issues while awaiting ground contact and operator assessment.

### Safe Mode

**Purpose:** Stable operational state that maintains spacecraft health and power-positive configuration while enabling ground communications.

**Configuration:**
* ADCS enabled in SUNSAFE mode (sun-pointing for maximum solar array illumination)
* EPS managing power with essential loads only:
  - ADCS actuators enabled
  - Radio in DUPLEX mode during ground passes, else RECEIVE-only mode
  - Payload disabled
* Autonomous attitude control maintaining sun-pointing
* All core cFS applications active and responsive
* Radio capable of commanding and telemetry downlink
* DS, LC, SC apps actively monitoring and logging

**Entry Conditions:**
* Commanded transition from Do No Harm Mode during commissioning
* Autonomous entry from Science Mode upon detection of:
  - Bus voltage < 24V
  - Battery SOC < 30%
  - ADCS loss of fine pointing (automatic SUNSAFE transition)
  - Payload fault or anomaly
* Commanded entry from ground during anomaly response

**Exit Conditions:**
* Power margins restored (Bus voltage > 26V, Battery SOC > 50%)
* ADCS operational and stable
* Ground command to transition to Science Mode
* All fault conditions cleared

**Rationale:** Safe Mode prioritizes spacecraft survival and ground communication. 
The sun-pointing attitude ensures maximum power generation while the configuration provides sufficient capability for diagnosis and recovery operations.

### Science Mode

**Purpose:** Nominal operational state for mission science objectives with payload operations and targeted attitude control.

**Configuration:**
* ADCS in TRACK mode with nadir-pointing attitude for Earth observation
* EPS distributing power to all operational loads:
  - ADCS maintaining targeted attitude
  - Payload (Demonstration Instrument) enabled and collecting data
  - Radio in scheduled DUPLEX mode for passes
  - All non-essential loads available per power budget
* Payload actively acquiring science data and generating data products
* Automated sequences (RTS) executing planned activities
* Data files being generated, stored, and queued for downlink
* LC monitoring for off-nominal conditions with automated responses
* SC executing time-tagged commanding for scheduled operations

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
The nadir-pointing attitude enables Earth observation payloads while the full system configuration supports mission objectives.

## Mission Phases

1. Launch & Deployment
    * Immediately after separation the vehicle is placed into a conservative, power‑saving "safe mode." 
    * No component subsystems are enabled and the spacecraft is tumbling.
    * The data storage (DS) application is logging all event messages and telemetry produced to file.
    * The limit checker (LC) and stored command (SC) applications perform any fault detection and correction.
    * The basic initialization is done in cFS via the startup relative time sequence (RTS) number 1.
    * The spacecraft awaits ground contact for commissioning.
2. Commissioning
    * Establish first contact from the ground by sending the Start RTS 6 (Start Pass) command.
    * Verify command/telemetry link and basic responsiveness with a NOOP command and other health checks.
    * Bring core subsystems online (ADCS, power management, payload) following verified checklists.
    * Perform basic functional tests (file listings via FM, file transfers via CFDP, storage management via DS).
    * Perform any anomaly investigations as necessary.
3. Nominal Operations
    * Regular science/payload activities, attitude maintenance, periodic housekeeping, and scheduled data downlinks.
    * Modifications to limits and configuration settings as requested by subsystems after review.
    * Routine use of RTS on-board the vehicle and YAMCS procedure stacks to automate passes.
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

* Confirm current state of health, begin anomaly investigation if off-nominal.
* Perform any uploads or configuration changes.
* Delete any files that the data pipeline confirmed successful receipt of.
* Get the latest data file set from on-board the vehicle.
* Download data files until the end of the pass.
* Once pass is complete confirm data pipeline has begun processing.

It is typical for data to be backlogged on the vehicle.
Data may be requested to be skipped based on an external phenomenon, such as a high‑priority event the payload may have detected.

## Operational Constraints and Simulation Fidelity

The SHIRE DRM scenario provides high fidelity in command/telemetry flows and flight software behavior, but the operator should be aware of limitations:

* Timing and pass windows are simulated. Real orbital dynamics are being calculated and used by the simulators, but are not leveraged for pass durations.
* Hardware effects and failures are modeled at a software level; some low-level hardware dynamics (thermal gradients, fine star‑tracker jitter, radiation effects) are either simplified or omitted depending on the scenario.
* Resource contention and container-level resource caps (CPU, memory) are host-dependent and may affect simulated timing when running on limited machines.
* Network latency and Docker host performance can change CFDP transfer characteristics compared to in-flight performance.

Recommended operator practices to mitigate these constraints:

* Use the scenario stacks and stepwise execution controls in YAMCS to reproduce timing-sensitive steps deterministically.
* When validating fault responses, explicitly inject failures through the scenario controls rather than relying on emergent, hard-to-reproduce conditions.
* Monitor host resources (docker stats) while running heavy scenarios and consider increasing container resources for high‑fidelity testing.

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
    * Ensures YAMCS, CFDP, and the simulated hardware stacks are up-to-date and reproducible.
* Software Engineer (cFS & Apps)
    * Develops and maintains cFS applications and ensures their integration with the simulation environment.
    * Produces and validates command/telemetry definitions and app-level test cases used.

## Notes and Next Steps

See the [mission requirements](./mission-requirements.md) for a detailed list of DRM requirements or jump into the [commissioning scenario](../scenarios/commissioning.md) for an implementation example.

---
Last updated: 20251218
