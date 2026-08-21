# Recommended Scenarios

The next scenarios should deepen the learning path while turning more of the current repository into repeatable evidence.
The recommendations below separate documentation work from new implementation so readers can see what is possible now.

## Drafted scenarios

| Scenario | Value and next work |
| --- | --- |
| [Data Lifecycle](data-lifecycle.md) | Connect Demo data, DS files, FM selection, CFDP delivery, and file integrity with file selection support, completion checks, and hash evidence. |
| [Debug and Radio Path Comparison](link-path-comparison.md) | Explain preferred and fallback routing with reviewed link setup, one named command per path, packet evidence, and restoration. |
| [EPS Load Switching](eps-load-switching.md) | Show commanded loads and telemetry response by wrapping the existing EPS stacks with initial state, timing, and cleanup assertions. |
| [ADCS Truth Comparison](adcs-truth-comparison.md) | Connect component observations with 42 through mode, settling, pointing tolerance, truth comparison, and timeout assertions. |
| [Component Hardware Development](component-hardware-development.md) | Carry a molded component from CLI and cFS simulation through focused board CLI and board cFS hardware gates. |

The Do No Harm checks remain part of [Commissioning](commissioning.md) rather than becoming a separate scenario.
The first four drafts mainly need procedure composition, reviewed acceptance criteria, and retained run results.
The hardware development draft also identifies target work that is not implemented today.

## Scenarios ready for procedure work

### Data Lifecycle

The [draft](data-lifecycle.md) generates identifiable Demo activity, closes the DS file set, lists `/d` through FM, transfers one returned file with CFDP, and separates completion from hash evidence.

### Debug and Radio Path Comparison

The [draft](link-path-comparison.md) runs the same harmless command through each path while enabling only the intended command output.
It restores the expected link configuration and prepares users for Security Incident Response without injecting a failure.

### EPS Load Switching

The [draft](eps-load-switching.md) begins with the simulator's all off state, runs the existing health and switch procedures, and restores all switches off.
It deliberately avoids unreviewed power and hardware claims.

### ADCS Truth Comparison

The [draft](adcs-truth-comparison.md) extends `AdcsComponent.ycs` with aligned component and 42 observations.
It identifies the frame mapping, numeric calculation, tolerance, timing, and sample count that still need review.

### Component Hardware Development

The [draft](component-hardware-development.md) starts with the component mold, then moves through CLI with Simulith, cFS with Simulith, board CLI with hardware, simulator reconciliation, and board cFS with hardware.
It uses gates so hardware discoveries return to the shared protocol, simulator, tests, cFS application, and ground definitions.

### Component Checkout Course

Create a short learning path that runs `AdcsComponent.ycs`, `DemoComponent.ycs`, `EpsComponent.ycs`, and `RadioComponent.ycs` one at a time.
Explain each reset, NOOP, housekeeping request, command count, and assertion.

This scenario would help contributors validate one component before running an integrated mission flow.
It should also state which procedures change persistent component state.

### Simulation Time Control

Demonstrate pause, resume, faster rate, and slower rate through the Simulith Server console.
Use 42 truth and one component telemetry timestamp to show what changes while the simulation is paused.

The scenario should distinguish simulation time from host wall clock time.
It would make timing related procedure failures easier for new users to diagnose.

### Spacecraft Configuration Comparison

Build `flatsat`, `sat-1`, and `sat-2` one at a time and record the generated component set.
Verify that the selected simulators and cFS startup entries match the generated configuration.

The current checked in selections are:

| Spacecraft | Components |
| --- | --- |
| `flatsat` | Demo, EPS, Radio |
| `sat-1` | ADCS, Demo, EPS, Radio |
| `sat-2` | ADCS, EPS, Radio |

This would teach configuration generation and prevent users from treating every component compiled by CMake as active on every spacecraft.

## Scenarios that need stronger coordination

### Automated Commissioning

Combine the manual commissioning phases into one controlled procedure.
Add checkpoints that allow an operator to stop after Do No Harm verification, first contact, each subsystem checkout, and file transfer.

This should reuse the Do No Harm checkpoint already documented in Commissioning and the Data Lifecycle scenario.

### Automated Nominal Pass

Turn the current Nominal Operations flow into one procedure with prepass criteria, pass opening, payload activity, file transfer, pass closure, and final state assertions.
Define behavior for a short pass, an incomplete transfer, and an unexpected command error.

### Lost Contact and Pass Recovery

Begin from a verified Duplex pass, interrupt the representative radio path through an approved control, and show the expected loss of telemetry.
Restore the path and demonstrate recovery without relying on the direct debug path as proof.

This needs reviewed link controls, transaction handling, timeouts, and a decision about whether an interrupted CFDP transfer resumes or restarts.

### Repeatability Run

Run one short procedure several times from the same generated configuration.
Compare simulation timestamps, parameter limits, events, and results.

This scenario needs a defined reset boundary because YAMCS data and flight files can persist while application state resets on a new lab launch.

## Scenarios that need new implementation

| Scenario | Missing capability |
| --- | --- |
| ADCS fault and recovery | Reviewed simulator controls for sensor, actuator, or pointing faults plus LC and recovery logic. |
| EPS low power response | A controlled low power condition, mission thresholds, load shedding response, and recovery criteria. |
| Radio outage beyond link control | A flight relevant fault model and a bounded onboard or operator recovery sequence. |
| Security Incident Response | Safe integration level controls for defined CryptoLib failures plus sanitized evidence handling. |
| Safe mode transition | A spacecraft mode owner, transition commands, mode telemetry, entry criteria, and exit criteria. |
| Hardware checkout | Validated hardware adapters, safety constraints, calibration steps, and hardware specific acceptance limits. |
| Multi spacecraft interaction | Mission configuration, addressing, ground routing, and scenario coordination for more than one active vehicle. |

Do not represent these as runnable mission capabilities merely because the Concept of Operations describes the desired behavior.
Each needs an implementation, unit and integration tests, an operator procedure, cleanup, and retained execution evidence.

## Definition of done for a new scenario

A scenario is ready to publish as executable when it has:

* a named objective and learning outcome
* a supported mission and spacecraft configuration
* explicit prerequisites and initial state
* one controlled action or condition at a time
* observable expected results with numeric limits where appropriate
* timeouts and stop conditions
* cleanup and a defined final state
* an evidence checklist with sanitization guidance
* a reviewed YAMCS procedure or equivalent automation
* a retained successful result tied to repository and submodule revisions
* a requirement mapping when the result supports verification

Start procedure implementation with Data Lifecycle because it connects the largest number of current operational building blocks.

***
Last reviewed: 14 August 2026
