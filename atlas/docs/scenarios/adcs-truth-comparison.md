# ADCS Truth Comparison

> **Scenario status:** Draft simulated scenario built around `AdcsComponent.ycs` and current 42 truth telemetry.
> The existing procedure displays truth values but does not assert numeric agreement between ADCS telemetry and 42.

## Objective

Enable ADCS, command `SUNSAFE`, and compare the component Sun vector with the corresponding 42 truth and visual attitude.
Separate command acceptance, control settling, component telemetry, truth data, and visual context into distinct observations.

## Parameters in the current procedure

`AdcsComponent.ycs` displays:

* `/ADCS/SUN_X`
* `/ADCS/SUN_Y`
* `/ADCS/SUN_Z`
* `/SIM_42_TRUTH/SVB_1`
* `/SIM_42_TRUTH/SVB_2`
* `/SIM_42_TRUTH/SVB_3`

It commands `SUNSAFE` and asserts that `SUN_X` is above 0.98.
It also asserts that `SUN_Y` and `SUN_Z` remain between negative 0.02 and positive 0.02.
Those checks validate the component vector limits but do not calculate error against the truth vector.

## Prerequisites

* use `sat-1`, which includes ADCS
* start the DRM and confirm simulation time advances
* confirm `truth42-in`, the debug links, and ADCS telemetry are fresh
* open the 42 dynamics display
* ensure no other procedure is changing ADCS mode or target

Record the initial ADCS enabled state, mode, Sun vector, truth vector, and simulation time.

## Phase 1 Run the current checkout

Open `Procedures / Stacks / AdcsComponent.ycs`.
Review the command and assertion sequence.
Run from the first step.

The procedure enables ADCS, resets counters, verifies a NOOP, displays component and truth parameters, commands `SUNSAFE`, and checks the component Sun vector.
Confirm each existing assertion passes before adding a separate truth comparison.

## Phase 2 Wait for a stable observation

Do not choose a result from the first packet after the mode command.
Wait until the component vector remains within the existing `SUNSAFE` limits for a reviewed number of consecutive fresh samples.

Record the start and end simulation times of this settling interval.
The current repository does not define a universal settling duration, so the draft must not invent one after the run.

## Phase 3 Compare telemetry with truth

Capture ADCS `SUN_X`, `SUN_Y`, and `SUN_Z` with `SVB_1`, `SVB_2`, and `SVB_3` at aligned simulation times.
Record the raw values before rounding.

The draft procedure should calculate a vector error only after the coordinate frames, sign convention, units, normalization, and sample timing are reviewed in the simulator and ground definitions.
Do not assert field by field equality merely because both sets contain three values.

Use the 42 display as supporting context.
After settling, the B1 body frame X axis should visually align with the yellow Sun vector.
The numeric records should carry the acceptance decision once the comparison method is defined.

## Phase 4 Repeatability sample

Collect several aligned samples without changing the mode.
Record the maximum observed component limit deviation and the planned truth comparison metric.

If simulation time pauses, telemetry becomes stale, or samples cannot be aligned, stop the measurement interval and mark it invalid.
Do not fill missing values from unrelated timestamps.

## Expected results

| Checkpoint | Expected result |
| --- | --- |
| Connectivity | ADCS telemetry and 42 truth are fresh while simulation time advances. |
| Commanding | ADCS accepts enable, NOOP, and `SUNSAFE` commands without a new error. |
| Component limits | `AdcsComponent.ycs` passes its `SUN_X`, `SUN_Y`, and `SUN_Z` assertions. |
| Visual context | The B1 body frame X axis aligns with the yellow Sun vector after settling. |
| Truth comparison | Aligned raw samples are retained for a reviewed frame and error calculation. |
| Repeatability | The defined metric remains within a limit chosen before the measured run. |

The truth comparison and repeatability rows remain draft criteria until the frame mapping, calculation, tolerance, and sample count are approved.

## Troubleshooting

If component limits fail, confirm ADCS is enabled, `SUNSAFE` was accepted, simulation time is moving, and 42 is producing truth.
Inspect ADCS and Director logs before repeating the mode command.

If component and truth values appear inconsistent, first check timestamps and coordinate definitions.
A sign or frame mismatch is not automatically an ADCS control failure.

## Cleanup and evidence

Record the final ADCS mode and enabled state.
Leave `SUNSAFE` active only when the next scenario expects that state.
Otherwise follow a reviewed transition or restart the simulated lab for a clean baseline.

Retain the YAMCS procedure result, aligned parameter export, chosen comparison formula, tolerance source, sample interval, 42 screenshot, active configuration, and relevant logs.

## Automation needed

A complete procedure needs fresh data checks, time alignment, explicit frame conversion, a numeric error calculation, consecutive sample criteria, a timeout, and a defined final mode.
Add another ADCS mode only after its target, expected 42 behavior, and acceptance limits are documented.

***
Last reviewed: 14 August 2026
