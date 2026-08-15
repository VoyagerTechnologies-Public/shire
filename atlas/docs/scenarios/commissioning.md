# Commissioning

> **Scenario status:** This walkthrough uses commands, tables, links, and component procedures present in the current repository.
> The launch and Do No Harm context is an operator narrative.
> SHIRE does not currently implement a top level mission mode manager that tracks a spacecraft wide Do No Harm state.

## Objective

In this scenario, you will perform a simulated initial checkout of the Design Reference Mission (DRM) spacecraft in orbit.
As the flight controller, your job is to make first contact, verify the health and status of the spacecraft's core systems, and begin transitioning it from its **Do No Harm** configuration after launch toward nominal operations.

## Prerequisites

Before you begin, please ensure you have:

* Successfully completed [Getting Started](../manual/handbook/getting-started.md).
* Confirmed SHIRE environment is installed and able to run.
* Read the [DRM Concept of Operations](../drm/concept-of-operations.md) to understand the goals and configuration of the spacecraft you'll be controlling.
* Reviewed the [System Architecture](../manual/core-concepts/architecture.md) page to understand how the components interact.

## Overview

The exercise assumes that the DRM spacecraft has just been deployed from its launch vehicle and is in the **Do No Harm** configuration defined by the DRM concept of operations.
In the implemented startup path, SC automatically starts RTS 1.
RTS 1 enables DS, TO_LAB, and LC, enables RTS 1 through 15, and starts initialization RTS 3.
LC action point 5 can start RTS 5 to enable the Radio and place it in Receive mode.
Your task is to walk through the commissioning checklist to bring it to full functionality.

We will follow these phases:

* Startup: launch SHIRE and verify execution.
* First contact: establish the simulated space link.
* Health assessment: verify basic command handling.
* Subsystem checkout: enable and configure ADCS and Demo.
* Download data: inspect stored files and request a transfer.

## Startup

From the repository root, build and launch SHIRE:

```bash
make
make start
```

Open the [42 dynamics display](http://localhost:5801/vnc_auto.html).
Open the [YAMCS ground interface](http://localhost:8090).

Confirm that simulation time is advancing in the Compose output.
Wait for the startup RTS events to complete before commanding.
In YAMCS, open the SHIRE instance and select **Links**.
Confirm that `debug-in`, `debug-out`, `radio-in`, `radio-out`, `sim-backdoor`, and `truth42-in` show the expected state for the running lab.


## First Contact

Send `/SC/SC_COMMANDS/SC_START_RTS` with `RTSID` set to 6.
RTS 6 places the Radio in Duplex mode, waits 480 SC wakeups, and returns it to Receive mode.
You should see the command and RTS events in the FSW output.

Confirm that `radio-in` is receiving telemetry.
Both `debug-out` and `radio-out` consume the `tc_realtime` command stream in the checked in YAMCS configuration, so the command can be emitted by both links when both are enabled.

When RTS 6 completes, radio telemetry stops because Receive mode does not transmit to the ground.
The debug telemetry path remains available.

## Health Assessment

Test the command link with `/CFE_ES/CFE_ES_COMMANDS/CFE_ES_NOOP`, the "hello world" of the cFS flight software.


Ensure `/CFE_ES/CFE_ES_HKPACKET/CMDCOUNTER` increments.
RTS 3 also sends an ES NOOP during initialization, so a fresh run will normally show 2 after this command.


## Subsystem Checkout

### ADCS

Initialize ADCS and confirm health with `Procedures / Stacks / AdcsComponent.ycs`.

* Enables ADCS application.
* Resets counters, sends a NOOP, and checks that the command count increments.
* Displays current parameters.
* Sets mode to SUNSAFE.
* Checks that `SUN_X` is above 0.98 and that `SUN_Y` and `SUN_Z` are between negative 0.02 and positive 0.02.

Select the first step, then click the **Run all from selected step** button.


Confirm that every stack step succeeds.
The B1 body frame X axis shown in 42 should align with the yellow sun vector after ADCS settles.


### Demo Instrument

Initialize the demonstration instrument and confirm health with `Procedures / Stacks / DemoComponent.ycs`.

* Enables DEMO application.
* Confirms commanding by resetting counters and then doing an application NOOP.
* Verifies command count increments.

Select the first step, then click the **Run all from selected step** button.


Confirm successful execution.



Send `/DEMO/DEMO_CONFIG_CC` with `DEVICE_CONFIG` set to 10.
Wait for the next Demo telemetry packet and confirm the reported configuration value.


## Download Data

Stop RTS 6 with `/SC/SC_COMMANDS/SC_STOP_RTS` and `RTSID` set to 6.


Send `/RADIO/RADIO_CONFIG_CC` with `MODE` set to `DUPLEX`.
Class 2 CFDP needs traffic in both directions, so keep the Radio in Duplex mode during the transfer.



Close the current Data Storage file set with `/DS/DS_COMMANDS/DS_CLOSE_ALL`.


Request the `/d` directory listing with `/FM/FM_COMMANDS/FM_GET_DIR_PKT` and `DIRECTORY` set to `/d`.


Wait for the response in `/FM/FM_DIRLISTPKT`.


Choose a completed file from the listing.
Use the actual returned filename rather than assuming an example file exists.
Send `/CF/CF_COMMANDS/CF_TX_FILE` with `SRCFILENAME` set to `/d/<returned-file>` and `DSTFILENAME` set to `<returned-file>`.
Review the default Class 2, channel, destination, and preservation values before sending the command.


Watch the file downlink and confirm its receipt under **File transfer**.




## Review

The simulated commissioning walkthrough is complete.

You have:

* Observed command and telemetry traffic on the configured links.
* Verified basic cFE command handling.
* Enabled and configured the ADCS and demonstration payload.
* Completed the documented commissioning exercise from its Do No Harm narrative premise.

Next steps include performing [nominal operations](./nominal-operations.md) each day.

***
Last reviewed: 14 August 2026
