# Commissioning

## Objective

In this scenario, you will perform the initial on orbit checkout of a Design Reference Mission spacecraft.
As the flight controller, your job is to make first contact, verify the health and status of the spacecraft's core systems, and transition it from the Do No Harm configuration toward an operational state.

## Prerequisites

Before you begin, ensure you have:

* Completed [Getting Started](../manual/handbook/getting-started.md)
* Confirmed that SHIRE is installed and able to run
* Read the [DRM Concept of Operations](../drm/concept-of-operations.md)
* Reviewed [System Architecture](../manual/core-concepts/architecture.md)
* Have the default DRM `sat-1` configuration

Use a freshly build DRM when possible.
A previous run can leave component state, YAMCS history, and stored files that make the results harder to interpret.

## Overview

The DRM spacecraft has just been deployed from its launch vehicle.
It boots into the implemented "Do No Harm" configuration.
In this state, ADCS and Demo are disabled, the EPS switches are off, the Radio is enabled in Receive mode, and basic telemetry is being stored by Data Storage.

Your task is to walk through the commissioning checklist and bring the available systems into operation.

You will follow these phases:

* Startup
* First contact
* Health assessment
* Subsystem checkout
* Download data

## Startup

Open a terminal and navigate to the SHIRE repository.
Build and launch the DRM:

```bash
make
make start
```

`make start` runs Compose in the foreground.
Leave this terminal open so you can watch service output and simulation time.

> **Capture 1:** Capture the terminal with all six services visible and running.
> Save it as `01_services_running.png`.

Wait until the initialization of the vehicle is complete and a "Do No Harm" default start state has been reached.


Open the [42 dynamics environment](http://localhost:5801/vnc_auto.html).
Click `Connect` and confirm that simulation time is advancing and the spacecraft is visible.

> **Capture 2:** Capture the initial 42 display with the spacecraft and advancing simulation time visible.
> Save it as `02_42_initial_state.png`.

Open the [YAMCS ground software](http://localhost:8090).
Select the SHIRE instance.
Confirm that packets are being received and their generation times are updating.

> **Capture 3:** Capture the SHIRE instance with incoming packets and current generation times visible.
> Save it as `03_yamcs_packet_flow.png`.

Select **Links** on the left side.
Confirm that `debug-in`, `debug-out`, `radio-in`, `radio-out`, `sim-backdoor`, and `truth42-in` are present.

YAMCS uses `radio-out` as the preferred command interface.
If that interface is unavailable, YAMCS falls back to `debug-out`.

> **Capture 4:** Capture the Links view with all six links and their current status visible.
> Save it as `04_startup_links.png`.

Wait for the startup relative time sequences to complete before sending commands.
This includes RTS 5, which enables the Radio and configures it to receive ground commands.

> **Capture 5:** Compose terminal output example to compare against.
> Save it as `05_do_no_harm_state.png`.

## First contact

Use the search bar to find commands and telemetry quickly.
Open `/SC/SC_COMMANDS/SC_START_RTS`.
Set `RTSID` to `6`, set `PADDING` to `0`, then send the command.

RTS 6 is the Start Pass sequence.
It places the Radio in Duplex mode for 480 seconds or eight minutes, then returns the Radio to Receive mode.
The checked in table describes this as an eight minute pass at the expected one hertz scheduler rate.
The wall time can differ when the simulation is not running at real time.

Confirm that Command History reports the command as accepted.
Watch the flight software output or YAMCS Events for the RTS start event.

> **Capture 6:** Capture the accepted RTS 6 command and its arguments in Command History.
> Save it as `06_start_pass_command.png`.

Open Radio housekeeping.
Confirm `RADIO_DEVICE_Mode` is `3`, the receive settings are `1` and `2`, and the transmit settings are `3` and `4`.
Mode `3` is Duplex mode.
Return to **Links** and confirm that `radio-in` is receiving telemetry.

> **Capture 7:** Capture the Links view with `radio-in` receiving data and `radio-out` showing command activity.
> Save it as `07_radio_contact.png`.

You have now commanded and received telemetry through the simulated radio path.

When RTS 6 completes, the Radio returns to Receive mode and `radio-in` stops receiving telemetry.
The `radio-out` uplink can still deliver commands in Receive mode.
Telemetry from `debug-in` continues, so you can finish the exercise after the pass ends.

## Health assessment

Open `/CFE_ES/CFE_ES_HKPACKET` and record the current `CMDCOUNTER` and `ERRCOUNTER`.
Send `/CFE_ES/CFE_ES_COMMANDS/CFE_ES_NOOP`.
This NOOP is the basic command path check for cFE Executive Services.

Wait for fresh housekeeping.
Confirm that `CMDCOUNTER` increases by exactly one and `ERRCOUNTER` does not increase.
Do not expect `CMDCOUNTER` to begin at zero because RTS 3 sends an ES NOOP during startup.

> **Capture 8:** Capture the accepted ES NOOP and fresh housekeeping showing the command counter increment without a new error.
> Save it as `08_cfe_health_check.png`.

## Subsystem checkout

### ADCS

Open `Procedures / Stacks / AdcsComponent.ycs`.
Select the first step, then select **Run all from selected step**.

The procedure:

* enables the ADCS device
* resets its counters
* sends an application NOOP
* confirms that the command count increments
* displays current parameters
* sets the mode to `SUNSAFE`
* verifies Sun pointing over the procedure interval

The spacecraft may need time to rotate and settle within the required margins.
Confirm that every procedure step succeeds.

> **Capture 9:** Capture the completed ADCS stack with the successful Sun vector assertions visible.
> Save it as `09_adcs_checkout.png`.

Return to 42.
The B1 body frame positive X axis should align with the yellow Sun vector.

> **Capture 10:** Capture the 42 display with the B1 positive X axis aligned with the yellow Sun vector.
> Save it as `10_adcs_sun_alignment.png`.

### Demo instrument

Open `Procedures / Stacks / DemoComponent.ycs`.
Select the first step, then select **Run all from selected step**.

The procedure:

* enables the Demo device
* resets its counters
* sends an application NOOP
* confirms that the command count increments

Confirm that every procedure step succeeds.

> **Capture 11:** Capture the completed Demo stack with all steps successful.
> Save it as `11_demo_checkout.png`.

Open `/DEMO/DEMO_CONFIG_CC`.
Send the command with `DEVICE_CONFIG` set to `10`.

Open `/DEMO/DEVICE_CONFIG` in a separate parameter view.
Wait for a fresh telemetry packet, which can take about ten seconds.
Confirm that the value updates to `10`.

> **Capture 12:** Capture fresh Demo telemetry showing `DEVICE_CONFIG` set to `10`.
> Save it as `12_demo_configuration.png`.

## Download data

If RTS 6 is still executing, open `/SC/SC_COMMANDS/SC_STOP_RTS`.
Set `RTSID` to `6`, set `PADDING` to `0`, then send the command.

Stopping RTS 6 prevents it from returning the Radio to Receive mode during the file transfer.

Open `/RADIO/RADIO_CONFIG_CC`.
Set `MODE` to `Duplex Mode`, `RX_SPEED` to `1`, `RX_WAVE` to `2`, `TX_SPEED` to `3`, and `TX_WAVE` to `4`.
Send the command.

Confirm that fresh Radio housekeeping reports mode `3`.
The Radio must remain in Duplex mode because reliable Class 2 CFDP transfers require traffic in both directions.

> **Capture 13:** Capture the accepted Radio configuration command and fresh housekeeping showing Duplex mode.
> Save it as `13_radio_duplex.png`.

Close the current Data Storage file set with `/DS/DS_COMMANDS/DS_CLOSE_ALL`.
This makes the files available for transfer.

Open `/FM/FM_COMMANDS/FM_GET_DIR_PKT`.
Set `DIRECTORY` to `/d`, `DIRLISTOFFSET` to `0`, and `GETSIZETIMEMODE` to `1`.
Send the command.

Wait for fresh `/FM/FM_DIRLISTPKT` telemetry.
Choose a `.ds` file with a nonzero size from the returned listing.
Prefer an earlier file rather than the newest entry.
Copy the exact filename shown in telemetry.

> **Capture 14:** Capture the directory packet with the selected filename, size, and time visible.
> Save it as `14_fm_directory_listing.png`.

Open `/CF/CF_COMMANDS/CF_TX_FILE`.
Set `CLASS` to `CLASS 2 - WITH FEEDBACK`, `KEEP` to `KEEP`, `CHAN_NUM` to `CHAN 0`, `PRIORITY` to `0`, and `DEST_ID` to `23`.
Set `SRCFILENAME` to `/d/<selected_file>` and `DSTFILENAME` to `<selected_file>` using the exact filename returned by FM.
Send the command.
Destination entity `23` is the YAMCS ground system entity in the checked in configuration.

Open **File transfer**.
Watch the transfer and confirm that its state reaches completion.
Confirm that the received filename matches the file selected from the FM listing.

> **Capture 15:** Capture the completed File Transfer entry with the filename, source, destination, and completion state visible.
> Save it as `15_cfdp_complete.png`.

If file integrity is part of your run criteria, export the received file and compare its hash with the source file.
A completed transfer record alone does not prove that the file contents match.

## Review

Commissioning of the available DRM systems is complete.

You have:

* confirmed the implemented Do No Harm startup state
* established command and telemetry through the simulated radio path
* verified basic cFE command handling
* enabled and checked the ADCS and Demo systems
* commanded Sun safe pointing and confirmed the result in 42
* closed, selected, and transferred a stored data file

This walkthrough leaves ADCS and Demo enabled and the Radio in Duplex mode for continued work.
Nominal operations remains a planned scenario.

This page composes existing commands and component procedures into a manual walkthrough.
It is not yet one automated commissioning procedure with complete assertions and failure handling.

For a clean repeat, save the evidence, run `make stop`, then start a new DRM.

***
Last reviewed: 20 August 2026
