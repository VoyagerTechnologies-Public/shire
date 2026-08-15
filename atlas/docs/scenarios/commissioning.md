# Commissioning

> **Scenario status:** The DRM boots into the Do No Harm configuration through implemented application defaults, SC startup behavior, LC tables, and RTS tables.
> This walkthrough uses current commands, links, and component procedures.
> The repository does not yet provide one end to end commissioning procedure or one spacecraft mode parameter.

## What you will learn

This scenario introduces the full lab from an operator's perspective.
You will learn how to:

* recognize the implemented Do No Harm boot configuration
* distinguish the direct debug path from the simulated radio path
* open a ground pass with an onboard stored command sequence
* verify basic cFE and subsystem command handling
* compare ADCS telemetry with 42 truth
* prepare a Data Storage file and request a CFDP downlink
* retain enough context to explain the result later

## Prerequisites

Before you begin:

* complete [Getting Started](../manual/handbook/getting-started.md)
* use the default DRM `sat-1` configuration unless you have reviewed another selection
* read the [DRM Concept of Operations](../drm/concept-of-operations.md)
* review the command and telemetry paths in [System Architecture](../manual/core-concepts/architecture.md)
* choose a directory outside the repository for run notes and exported evidence

Start with a newly launched lab when possible.
A prior run can leave component state, YAMCS history, and stored files that make the observations harder to interpret.

## Understand the starting state

The spacecraft does not receive one command named Do No Harm.
The configuration emerges from these startup actions:

| Startup behavior | Expected observation |
| --- | --- |
| cFE loads the selected applications | Startup events appear for cFS and component applications. |
| ADCS, Demo, and Radio initialize with their devices disabled | Their initial housekeeping reports disabled before later automatic actions affect the Radio. |
| The EPS simulator initializes all eight switches off | EPS switch telemetry reports `OFF`. |
| SC starts RTS 1 after a power on reset | SC events or housekeeping show the automatic sequence. |
| RTS 1 enables DS, TO_LAB, and LC | Data collection, direct debug telemetry, and limit checking become active. |
| RTS 1 enables RTS 1 through 15 and starts RTS 3 | RTS 3 sends one ES NOOP as an initialization check. |
| LC action point 5 starts RTS 5 | RTS 5 enables the Radio and configures Receive mode. |

The final item is important for new operators.
Receive mode accepts the simulated command link but does not transmit radio telemetry.
The direct TO_LAB debug telemetry remains available inside the lab.

## Phase 1 Start the lab

From the repository root, build and launch SHIRE:

```bash
make
make start
```

Open the [42 dynamics display](http://localhost:5801/vnc_auto.html).
Open the [YAMCS ground interface](http://localhost:8090).

Confirm that all six services in the full lab remain running.
Confirm that simulation time is advancing in the Compose output.
Wait for the automatic RTS activity to settle before sending commands.

In YAMCS, open the SHIRE instance and select **Links**.
Inspect `debug-in`, `debug-out`, `radio-in`, `radio-out`, `sim-backdoor`, and `truth42-in`.
The receive only Radio will not provide a continuous radio downlink at this point, so use the debug path to inspect startup telemetry.

### Do No Harm checkpoint

Confirm the observable startup state before commissioning changes it.
Record:

* DS and LC application state
* SC events for RTS 1 and RTS 3
* ADCS and Demo device state
* all eight EPS switch states
* Radio device state and Receive mode after RTS 5
* advancing 42 truth and simulation time

Do not infer the spacecraft state from one parameter because there is no top level mode parameter today.
The collection above is the current proof of the boot configuration.

## Phase 2 Establish first contact

Send `/SC/SC_COMMANDS/SC_START_RTS` with `RTSID` set to 6.
RTS 6 configures the Radio for Duplex mode, waits 480 SC wakeups, and returns the Radio to Receive mode.
The checked in table describes that wait as eight minutes at the expected scheduler rate.

Watch SC events, Radio housekeeping, and YAMCS link data counts.
Confirm that the Radio reports Duplex mode during the pass and that `radio-in` begins receiving telemetry.

Both `debug-out` and `radio-out` consume the `tc_realtime` command stream in the checked in YAMCS configuration.
When both command links are enabled, one YAMCS command can reach cFS through both paths.
Use command history and counters with that duplication in mind.

When RTS 6 completes, the Radio returns to Receive mode and radio telemetry stops.
The direct debug telemetry path remains available.

## Phase 3 Verify cFE health

Record the current `/CFE_ES/CFE_ES_HKPACKET/CMDCOUNTER` and command error count.
Send `/CFE_ES/CFE_ES_COMMANDS/CFE_ES_NOOP`.
Confirm that the command counter increases and the error count does not increase.

RTS 3 already sends one ES NOOP during startup.
The two enabled command paths can also deliver the operator NOOP more than once.
Judge the result from the observed increment and command history instead of expecting one absolute counter value.

If the counter does not move, check the YAMCS processor, command link state, FSW events, and container logs before proceeding.

## Phase 4 Check out subsystems

### EPS

Open `Procedures / Stacks / EpsComponent.ycs`.
Run from the first step.
The procedure resets EPS counters, requests housekeeping, sends a NOOP, and verifies that the command count increments.

Confirm that every step succeeds.
Retain the EPS switch state before changing any load because the all off state is part of the Do No Harm baseline.

### ADCS

Open `Procedures / Stacks / AdcsComponent.ycs`.
Run from the first step.

The procedure:

* enables the ADCS device
* resets counters and verifies a NOOP
* displays current parameters
* sets the mode to `SUNSAFE`
* checks that `SUN_X` is above 0.98
* checks that `SUN_Y` and `SUN_Z` remain between negative 0.02 and positive 0.02

Confirm that every assertion succeeds.
After ADCS settles, compare the component telemetry with 42.
The B1 body frame X axis in 42 should align with the yellow Sun vector.

### Demo instrument

Open `Procedures / Stacks / DemoComponent.ycs`.
Run from the first step.

The procedure enables Demo, resets its counters, sends a NOOP, and verifies that the command count increments.
After it succeeds, send `/DEMO/DEMO_CONFIG_CC` with `DEVICE_CONFIG` set to 10.
Wait for a new Demo telemetry packet and confirm that `DEVICE_CONFIG` reports 10.

## Phase 5 Prepare and download data

If RTS 6 is still running, stop it with `/SC/SC_COMMANDS/SC_STOP_RTS` and set `RTSID` to 6.
Stopping it prevents the delayed second command from returning the Radio to Receive mode during the transfer.

Send `/RADIO/RADIO_CONFIG_CC` with `MODE` set to `DUPLEX`.
Review the receive and transmit settings shown by YAMCS before sending.
Class 2 CFDP requires traffic in both directions, so keep the Radio in Duplex mode during the transfer.

Close the current Data Storage file set with `/DS/DS_COMMANDS/DS_CLOSE_ALL`.
Request the `/d` directory listing with `/FM/FM_COMMANDS/FM_GET_DIR_PKT` and set `DIRECTORY` to `/d`.
Wait for `/FM/FM_DIRLISTPKT`.

Choose a completed file from the returned listing.
Do not assume that a filename from an earlier run exists.

Send `/CF/CF_COMMANDS/CF_TX_FILE` with `SRCFILENAME` set to `/d/<returned-file>` and `DSTFILENAME` set to `<returned-file>`.
Review the default Class 2, channel, destination, and preservation values before sending.
Watch **File transfer** and confirm that YAMCS records completion.

If integrity is part of the run criteria, export the received file and compare its hash with the source file hash.
A transfer completion record alone does not establish file content equality.

## Expected results

| Phase | Expected result |
| --- | --- |
| Startup | All six services remain running and simulation time advances. |
| Do No Harm | The distributed startup observations match the implemented configuration. |
| First contact | SC reports RTS 6 activity, the Radio enters Duplex mode, and radio telemetry reaches YAMCS. |
| cFE health | The ES command counter increases without a new command error. |
| EPS checkout | `EpsComponent.ycs` passes and the initial switch state is recorded. |
| ADCS checkout | `AdcsComponent.ycs` passes and the measured Sun vector meets its limits. |
| Demo checkout | `DemoComponent.ycs` passes and the requested configuration appears in telemetry. |
| Data preparation | FM returns a completed file selected from the actual `/d` listing. |
| File transfer | YAMCS reports CFDP completion and the received file is available. |

## Troubleshooting cues

| Symptom | First checks |
| --- | --- |
| Debug telemetry is absent | Check `debug-in`, TO_LAB events, the YAMCS processor, and UDP port 1235. |
| Radio telemetry is absent before first contact | This is expected in Receive mode, so start RTS 6 and confirm Duplex mode. |
| One command increments a counter twice | Check whether both `debug-out` and `radio-out` sent the same `tc_realtime` command. |
| A component stack stops | Preserve the failed assertion, confirm fresh housekeeping, and inspect the component and simulator logs. |
| `/d` has no suitable file | Confirm DS is active, allow telemetry to accumulate, close the file set again, and request a fresh listing. |
| CFDP does not complete | Confirm Duplex mode, both radio links, CF and YAMCS CFDP status, and the exact returned source filename. |

See the [FAQ](../manual/handbook/faq.md) for service and link diagnostics.

## Evidence to retain

Save the active configuration, repository revision, startup state, YAMCS link state, component stack results, command history, FM listing, CFDP result, and relevant service logs.
Record source and received file hashes when file integrity matters.

A useful screenshot set contains the Do No Harm checkpoint, Duplex radio state, successful subsystem procedures, 42 Sun alignment, FM listing, and completed CFDP transfer.

This walkthrough remains a manual composition of implemented commands and procedures.
Do not report it as one automated commissioning test until a reviewed procedure contains the full assertions, failure handling, and cleanup.

## Finish the run

The walkthrough intentionally leaves ADCS and Demo enabled as part of the transition toward nominal operations.
Continue with [Nominal Operations](nominal-operations.md) if that is your goal.

For a clean repeat, save the evidence, stop the lab with `make stop`, and start a new run.
Confirm the Do No Harm checkpoint again because YAMCS history and stored ground data can persist across lab restarts.

***
Last reviewed: 14 August 2026
