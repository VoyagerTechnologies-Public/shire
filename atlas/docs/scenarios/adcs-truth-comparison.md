# ADCS Truth Comparison

> **Scenario status:** The `adcs-truth-verification` scenario and the `AdcsTruthComparison.ycs` stack automatically check that a settled ADCS Sun vector agrees with live 42 truth within a tolerance of 0.02 per component.
> Run it with `make scenario SCENARIO=adcs-truth-verification`.
> The `approx` comparison in that stack cannot be evaluated by the YAMCS web UI, so the numeric agreement check runs only through `make scenario`.
> The manual procedure below adds visual context and repeatability observations, and its sample count and acceptance limit are still open.

## Objective

Enable ADCS, command `SUNSAFE`, and compare the component Sun vector with the corresponding 42 truth and visual attitude.
Separate command acceptance, control settling, component telemetry, truth data, and visual context into distinct observations.

## What the stacks check

`AdcsComponent.ycs` displays:

* `/ADCS/SUN_X`
* `/ADCS/SUN_Y`
* `/ADCS/SUN_Z`
* `/SIM_42_TRUTH/SVB_1`
* `/SIM_42_TRUTH/SVB_2`
* `/SIM_42_TRUTH/SVB_3`

It commands `SUNSAFE` and then waits up to 120 seconds for `SUN_X` to rise above 0.95 while `SUN_Y` and `SUN_Z` stay between negative 0.05 and positive 0.05.
It then asserts that `CMD_COUNT` is at least 3, `CMD_ERR_COUNT` is 0, and `ANGRATE_X`, `ANGRATE_Y`, and `ANGRATE_Z` are each between negative 0.1 and positive 0.1.
Those checks validate the component vector and rate limits and do not compare the vector with truth.

`AdcsTruthComparison.ycs` enables ADCS, commands `SUNSAFE`, and waits up to 120 seconds for `SUN_X` above 0.98 with `SUN_Y` and `SUN_Z` within plus or minus 0.02.
It then checks that `SUN_X`, `SUN_Y`, and `SUN_Z` each match `SVB_1`, `SVB_2`, and `SVB_3` with an `approx` condition and a tolerance of 0.02.
The `approx` condition is an extension of the headless commander that the YAMCS web UI cannot evaluate.
If you run the stack by hand in the UI, that final step does not work, so use `make scenario SCENARIO=adcs-truth-verification` for the numeric agreement result.
The scenario runs on the nominal baseline orbit for 180 seconds with the stack starting at time zero.

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

The procedure enables ADCS, resets counters, verifies a NOOP, displays component and truth parameters, commands `SUNSAFE`, and checks the component Sun vector, command counters, and angular rates.
Confirm each existing assertion passes before relying on a truth comparison.
Then run `make scenario SCENARIO=adcs-truth-verification` to check numeric agreement with truth.

## Phase 2 Wait for a stable observation

Do not choose a result from the first packet after the mode command.
The stacks poll until their vector limits are met or the 120 second timeout expires, and they do not require a number of consecutive samples.
For a manual observation, wait until the component vector remains within the `SUNSAFE` limits for a reviewed number of consecutive fresh samples.

Record the start and end simulation times of this settling interval.
The current repository does not define a universal settling duration, so a manual run must not invent one after the result.

## Phase 3 Compare telemetry with truth

Capture ADCS `SUN_X`, `SUN_Y`, and `SUN_Z` with `SVB_1`, `SVB_2`, and `SVB_3` at aligned simulation times.
Record the raw values before rounding.

The automated scenario compares each component with its truth counterpart using a tolerance of 0.02 at one settled live sample.
That check does not compute a vector error norm or an error over several samples.
Calculate any other error metric only after the coordinate frames, sign convention, units, normalization, and sample timing are reviewed in the simulator and ground definitions.
Do not assert field by field equality for other parameters merely because both sets contain three values.

Use the 42 display as supporting context.
After settling, the B1 body frame X axis should visually align with the yellow Sun vector.
The numeric records carry the acceptance decision, and the automated scenario provides it for the single settled sample.

## Phase 4 Repeatability sample

Collect several aligned samples without changing the mode.
Record the maximum observed component limit deviation and the observed difference from truth for each component.

If simulation time pauses, telemetry becomes stale, or samples cannot be aligned, stop the measurement interval and mark it invalid.
Do not fill missing values from unrelated timestamps.

## Expected results

| Checkpoint | Expected result |
| --- | --- |
| Connectivity | ADCS telemetry and 42 truth are fresh while simulation time advances. |
| Commanding | ADCS accepts enable, NOOP, and `SUNSAFE` commands without a new error. |
| Component limits | `AdcsComponent.ycs` passes its `SUN_X`, `SUN_Y`, `SUN_Z`, command counter, and angular rate assertions. |
| Visual context | The B1 body frame X axis aligns with the yellow Sun vector after settling. |
| Truth comparison | `make scenario SCENARIO=adcs-truth-verification` passes with each component within 0.02 of truth, and aligned raw samples are retained. |
| Repeatability | The observed differences remain within a limit chosen before the measured run. |

The automated scenario checks numeric agreement at a settled sample.
The broader manual repeatability criterion still requires an approved sample count and limit.

## Troubleshooting

If component limits fail, confirm ADCS is enabled, `SUNSAFE` was accepted, simulation time is moving, and 42 is producing truth.
Inspect ADCS and Director logs before repeating the mode command.

If component and truth values appear inconsistent, first check timestamps and coordinate definitions.
A sign or frame mismatch is not automatically an ADCS control failure.

## Cleanup and evidence

Record the final ADCS mode and enabled state.
Leave `SUNSAFE` active only when the next scenario expects that state.
Otherwise follow a reviewed transition or restart the simulated lab for a clean baseline.

Retain the YAMCS procedure result, the scenario output when it was run, aligned parameter export, tolerance source, sample interval, 42 screenshot, active configuration, and relevant logs.

## Remaining work

The scenario provides a timeout, a final numeric comparison, and a single settled sample.
It does not yet provide fresh data checks beyond the polling timeout, consecutive sample criteria, a vector error metric over several samples, or a defined final mode.
Add another ADCS mode only after its target, expected 42 behavior, and acceptance limits are documented.

***
Last reviewed: 20261027
