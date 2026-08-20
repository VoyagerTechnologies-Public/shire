# Nominal Operations

> **Scenario status:** The repository does not yet contain one complete YAMCS procedure for this scenario.
> This page defines a reviewable operator flow assembled from current cFS applications, DRM tables, and component procedures.
> It is not an executed acceptance record.

## What you will learn

This scenario models a routine payload data pass after commissioning.
It helps a new operator connect spacecraft health, pass timing, payload activity, onboard files, and downlink completion into one operational flow.

You will learn how to:

* perform a short prepass health review
* use RTS 6 to create a simulated Duplex radio window
* separate payload generation from file storage and file transfer
* compare ADCS observations with 42 truth
* choose an actual closed file for CFDP downlink
* close a pass in a known Receive state

## Prerequisites

Complete the [Commissioning](commissioning.md) walkthrough first.
At minimum, verify the cFE command path and run the ADCS, Demo, EPS, and Radio component procedures successfully.

Before starting this flow:

* record the current simulation time and YAMCS link state
* confirm that SC, DS, FM, CF, ADCS, Demo, EPS, and Radio housekeeping is fresh
* define the acceptable command error counts and ADCS pointing limits for this run
* decide which Demo configuration represents the planned payload activity
* decide whether file hash equality is required

These decisions turn a demonstration into a repeatable test.
Do not choose limits after seeing the result.

## Implemented building blocks

* RTS 6 places the Radio in Duplex mode, waits 480 scheduler wakeups, then returns it to Receive mode.
* YAMCS provides direct debug and simulated radio command and telemetry links.
* `AdcsComponent.ycs`, `DemoComponent.ycs`, `EpsComponent.ycs`, and `RadioComponent.ycs` provide focused subsystem checks.
* DS records selected telemetry to files.
* FM lists and manages flight files.
* CF and YAMCS provide CFDP services.
* 42 truth supports comparison between the simulated environment and spacecraft observations.

These pieces work independently today.
The missing item is one reviewed procedure that coordinates them and asserts the complete pass result.

## Operator flow

### Phase 1 Prepare the pass

1. Start the default DRM and wait for Do No Harm startup activity to settle.
2. Inspect YAMCS links, cFS events, simulation time, and service health.
3. Record cFE and component command counts and error counts.
4. Confirm DS is active and determine whether an existing open file set should be retained.
5. Run any component checkout required by the run plan.

The purpose of this phase is to distinguish a preexisting problem from a failure introduced during the pass.
Stop here if required telemetry is stale, error counts exceed the chosen limits, or the simulator is not advancing.

### Phase 2 Open the radio window

Send `/SC/SC_COMMANDS/SC_START_RTS` with `RTSID` set to 6.
Confirm SC accepts the command.
Confirm the Radio enters Duplex mode and `radio-in` begins receiving telemetry.

Treat the 480 wakeup duration as a configured simulation interval.
Host load and scheduler behavior can affect wall clock observation, so use SC and Radio state rather than a desktop timer alone.

YAMCS prefers `radio-out` and falls back to `debug-out` when the preferred interface is unavailable.
Confirm the selected interface when the transport path matters to the activity.

### Phase 3 Assess attitude and power

Confirm the intended ADCS mode for the activity.
Compare ADCS telemetry with the corresponding 42 truth values and visual attitude.
Apply the pointing limits chosen before the run.

Review EPS switch, voltage, current, and battery related telemetry before enabling additional activity.
The current repository includes focused EPS switch procedures, but it does not define a mission power budget for this complete scenario.
Do not claim a power positive result without reviewed thresholds and duration criteria.

### Phase 4 Operate the Demo payload

Enable Demo if it is not already enabled.
Send the selected configuration and confirm the value returns in fresh telemetry.
Observe the Demo channels for the duration defined by the run plan.

Record when payload activity starts and stops.
That time range helps connect Demo telemetry to the DS files selected later.

### Phase 5 Prepare a stored file

Send `/DS/DS_COMMANDS/DS_CLOSE_ALL` to close the current file set.
Wait for DS to accept the command.
Request an FM directory packet for `/d`.

Select a completed file from the returned listing.
Record its exact path, size, and timestamp where available.
Do not reuse a filename copied from an earlier run.

### Phase 6 Downlink the file

Confirm the Radio still reports Duplex mode before starting CFDP.
Send `/CF/CF_COMMANDS/CF_TX_FILE` using the selected source path and reviewed destination values.
Watch the CFDP transfer in YAMCS until it completes or reaches the run timeout.

Record the transaction result and received filename.
Compare source and destination hashes when file integrity is an acceptance criterion.

If the radio window closes during the transfer, retain the partial transaction state.
Do not call the pass successful merely because the transfer was started.

### Phase 7 Close the pass

Stop RTS 6 if it is still executing, or allow it to complete.
Confirm the Radio returns to Receive mode.
Confirm that radio telemetry stops while direct debug telemetry remains available.

Review new command errors, application errors, events, and incomplete CFDP transactions.
Record the final spacecraft configuration so the next operator knows what remains enabled.

## Expected results

| Phase | Expected result |
| --- | --- |
| Preparation | Required services and telemetry are healthy enough to begin under predefined limits. |
| Pass opening | RTS 6 executes, the Radio enters Duplex mode, and radio telemetry arrives. |
| Attitude and power | Observations meet the limits chosen before the run. |
| Payload | Demo accepts the requested state and configuration and produces fresh data. |
| File preparation | DS closes its file set and FM returns an actual completed file. |
| Downlink | CFDP reports completion and the destination file is available. |
| Pass close | The Radio returns to Receive mode and final state is recorded. |

## Stop conditions

Pause the flow and preserve evidence when:

* a required telemetry packet becomes stale
* a command error count increases unexpectedly
* ADCS or EPS leaves the defined limits
* Radio mode differs from the planned pass state
* CFDP reports a terminal failure
* simulation time stops or a required service exits

The current page does not define automatic safing actions for those conditions.
Follow a reviewed recovery procedure when one exists.

## Evidence and screenshots

Retain the standard scenario evidence packet plus the pass start and stop observations, selected file metadata, CFDP transaction record, and any hashes.

Useful screenshots include:

* the prepass health summary
* Radio Duplex state with active `radio-in` traffic
* ADCS telemetry beside 42 truth
* Demo state and configuration during payload activity
* the FM listing with the selected file
* completed CFDP transaction and received file
* final Radio Receive state

## Work needed for an executable scenario

A complete YAMCS procedure should add explicit assertions for link state, command counters, radio mode, ADCS pointing tolerance, payload state, DS closure, FM response, CFDP completion, and destination file integrity.
It should also define timeouts, stop conditions, cleanup behavior, and the final expected component state.

Until those controls and retained results exist, this page demonstrates available mechanisms rather than verified nominal mission performance.
The [Recommended Scenarios](recommended.md) page separates near term documentation work from features that need implementation.

***
Last reviewed: 14 August 2026
