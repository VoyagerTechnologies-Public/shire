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

The screenshots below show one completed run.
Simulation times, counters, filenames, and packet totals will differ in your run.

## Startup

Open a terminal and navigate to the SHIRE repository.
Build and launch the DRM:

```bash
make
make start
```

![Compose output showing SHIRE services starting](../assets/scenarios/commissioning/01-services-startup.png)

`make start` runs Compose in the foreground.
Leave this terminal open so you can watch service output and simulation time.

Wait until the initialization of the vehicle is complete and a "Do No Harm" default start state has been reached.

Open the [42 dynamics environment](http://localhost:5801/vnc_auto.html).
Click `Connect` and confirm that simulation time is advancing and the spacecraft is visible.

![Initial 42 display showing the spacecraft and advancing simulation time](../assets/scenarios/commissioning/02-42-initial-spacecraft.png)

Open the [YAMCS ground software](http://localhost:8090).
Select the SHIRE instance.
Confirm that packets are being received and their generation times are updating.

![YAMCS Home page showing current SHIRE packet flow](../assets/scenarios/commissioning/03-yamcs-packet-flow.png)

Select **Links** on the left side.
Confirm that `debug-in`, `debug-out`, `radio-in`, `radio-out`, `sim-backdoor`, and `truth42-in` are present.

YAMCS uses `radio-out` as the preferred command interface.
If that interface is unavailable, YAMCS falls back to `debug-out`.

![YAMCS Links view showing the six configured links](../assets/scenarios/commissioning/04-yamcs-startup-links.png)

Wait for the startup relative time sequences to complete before sending commands.
This includes RTS 5, which enables the Radio and configures it to receive ground commands.

![Compose output showing the Do No Harm startup events](../assets/scenarios/commissioning/05-do-no-harm-startup-events.png)

## First contact

Use the search bar to find commands and telemetry quickly.
Open `/SC/SC_COMMANDS/SC_START_RTS`.
Set `RTSID` to `6`, set `PADDING` to `0`, then send the command.

![YAMCS command form configured to start RTS 6](../assets/scenarios/commissioning/06-start-pass-rts-command.png)

RTS 6 is the Start Pass sequence.
It places the Radio in Duplex mode for 480 seconds or eight minutes, then returns the Radio to Receive mode.
The checked in table describes this as an eight minute pass at the expected one hertz scheduler rate.
The wall time can differ when the simulation is not running at real time.

Confirm that Command History reports the command as accepted.
Watch the flight software output or YAMCS Events for the RTS start event.

Open Radio housekeeping.
Confirm `RADIO_DEVICE_Mode` is `3`, the receive settings are `1` and `2`, and the transmit settings are `3` and `4`.
Mode `3` is Duplex mode.
Return to **Links** and confirm that `radio-in` is receiving telemetry.

![YAMCS Links view showing radio telemetry and command activity](../assets/scenarios/commissioning/07-radio-contact-links.png)

You have now commanded and received telemetry through the simulated radio path.

When RTS 6 completes, the Radio returns to Receive mode and `radio-in` stops receiving telemetry.
The `radio-out` uplink can still deliver commands in Receive mode.
Telemetry from `debug-in` continues, so you can finish the exercise after the pass ends.

## Health assessment

Open `/CFE_ES/CFE_ES_HKPACKET` and record the current `CMDCOUNTER` and `ERRCOUNTER`.
Send `/CFE_ES/CFE_ES_COMMANDS/CFE_ES_NOOP`.
This NOOP is the basic command path check for cFE Executive Services.

![Flight software output showing the cFE ES NOOP event](../assets/scenarios/commissioning/08-cfe-es-noop-event.png)

Wait for fresh housekeeping.
Confirm that `CMDCOUNTER` increases by exactly one and `ERRCOUNTER` does not increase.
Do not expect `CMDCOUNTER` to begin at zero because RTS 3 sends an ES NOOP during startup.

![Fresh cFE ES housekeeping showing the command and error counters](../assets/scenarios/commissioning/09-cfe-es-health-telemetry.png)

## Subsystem checkout

### ADCS

Open `Procedures / Stacks / AdcsComponent.ycs`.
The stack initially shows the command and verification steps that it will execute.

![ADCS procedure stack ready to run](../assets/scenarios/commissioning/10-adcs-procedure-ready.png)

Select the first step, then select **Run all from selected step**.

The procedure:

* Enables the ADCS device
* Resets its counters
* Sends an application NOOP
* Confirms that the command count increments
* Displays current parameters
* Sets the mode to `SUNSAFE`
* Verifies Sun pointing over the procedure interval

The spacecraft may need time to rotate and settle within the required margins.
The flight software output shows the device enable, counter reset, NOOP, and mode commands while the stack runs.

![Flight software output showing ADCS procedure command events](../assets/scenarios/commissioning/11-adcs-command-events.png)

Confirm that every procedure step succeeds.

![Completed ADCS stack with successful Sun vector assertions](../assets/scenarios/commissioning/12-adcs-checkout-complete.png)

Return to 42.
The B1 body frame positive X axis should align with the yellow Sun vector.

![42 display showing the spacecraft aligned with the Sun vector](../assets/scenarios/commissioning/13-42-sun-safe-alignment.png)

### Demo instrument

Open `Procedures / Stacks / DemoComponent.ycs`.
Select the first step, then select **Run all from selected step**.

The procedure:

* Enables the Demo device
* Resets its counters
* Sends an application NOOP
* Confirms that the command count increments

Confirm that every procedure step succeeds.

![Demo procedure stack showing its command and counter checks](../assets/scenarios/commissioning/14-demo-checkout-complete.png)

The flight software output should show the Demo enable, reset, and NOOP events.

![Flight software output showing Demo procedure command events](../assets/scenarios/commissioning/15-demo-command-events.png)

Open `/DEMO/DEMO_CONFIG_CC`.
Send the command with `DEVICE_CONFIG` set to `10`.

Open `/DEMO/DEVICE_CONFIG` in a separate parameter view.
Wait for a fresh telemetry packet, which can take about ten seconds.
Confirm that the value updates to `10`.

![Fresh Demo telemetry showing DEVICE_CONFIG set to 10](../assets/scenarios/commissioning/16-demo-configuration-telemetry.png)

## Download data

If RTS 6 is still executing, open `/SC/SC_COMMANDS/SC_STOP_RTS`.
Set `RTSID` to `6`, set `PADDING` to `0`, then send the command.

Stopping RTS 6 prevents it from returning the Radio to Receive mode during the file transfer.

Open `/RADIO/RADIO_CONFIG_CC`.
Set `MODE` to `Duplex Mode`, `RX_SPEED` to `1`, `RX_WAVE` to `2`, `TX_SPEED` to `3`, and `TX_WAVE` to `4`.
Send the command.

![YAMCS Radio configuration command set to Duplex mode](../assets/scenarios/commissioning/17-radio-duplex-command.png)

Confirm that fresh Radio housekeeping reports mode `3`.
The Radio must remain in Duplex mode because reliable Class 2 CFDP transfers require traffic in both directions.

![Fresh Radio housekeeping showing Duplex mode and link settings](../assets/scenarios/commissioning/18-radio-duplex-telemetry.png)

Close the current Data Storage file set with `/DS/DS_COMMANDS/DS_CLOSE_ALL`.
This makes the files available for transfer.

Open `/FM/FM_COMMANDS/FM_GET_DIR_PKT`.
Set `DIRECTORY` to `/d`, `DIRLISTOFFSET` to `0`, and `GETSIZETIMEMODE` to `1`.
Send the command.

![Flight software output showing the Data Storage close and File Manager directory commands](../assets/scenarios/commissioning/19-data-download-events.png)

Wait for fresh `/FM/FM_DIRLISTPKT` telemetry.
Choose a `.ds` file with a nonzero size from the returned listing.
Prefer an earlier file rather than the newest entry.
Copy the exact filename shown in telemetry.

![File Manager directory telemetry showing stored data filenames and sizes](../assets/scenarios/commissioning/20-fm-directory-listing.png)

Open `/CF/CF_COMMANDS/CF_TX_FILE`.
Set `CFDP_CLASS` to `CLASS 2 - WITH FEEDBACK`, `KEEP_FILE` to `KEEP`, `CHANNEL` to `CHANNEL 0`, and `PRIORITY` to `0`.
Confirm that `DESTINATION_ENTITY_ID` is `23`, its configured default.
Set `SOURCE_FILENAME` to `/d/<selected_file>` and `DESTINATION_FILENAME` to `<selected_file>` using the exact filename returned by FM.
Treat the values in the screenshot as an example only.
The selected filename and resulting source and destination paths can change on every run.
Send the command.
Destination entity `23` is the YAMCS ground system entity in the checked in configuration.

![YAMCS CF transmit-file command configured for a Class 2 download](../assets/scenarios/commissioning/21-cfdp-transmit-file-command.png)

Open **File transfer**.
Watch the transfer and confirm that its state reaches completion.
Confirm that the received filename matches the file selected from the FM listing.

![YAMCS File Transfer view showing the completed CFDP download](../assets/scenarios/commissioning/22-cfdp-transfer-complete.png)

## Review

Commissioning of the available DRM systems is complete.

You have:

* Confirmed the implemented Do No Harm startup state
* Established command and telemetry through the simulated radio path
* Verified basic cFE command handling
* Enabled and checked the ADCS and Demo systems
* Commanded Sun safe pointing and confirmed the result in 42
* Closed, selected, and transferred a stored data file

This walkthrough leaves ADCS and Demo enabled and the Radio in Duplex mode for continued work.
Nominal operations remains a planned scenario.

This page composes existing commands and component procedures into a manual walkthrough.
It is not yet one automated commissioning procedure with complete assertions and failure handling.

For a clean repeat, save the evidence, run `make stop`, then start a new DRM.

***
Last reviewed: 20260820
