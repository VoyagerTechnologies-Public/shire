# EPS Load Switching

> **Scenario status:** Draft simulated scenario using the current EPS CLI model, cFS application, and YAMCS procedures.
> The switch exercise is not a power budget, battery endurance, or hardware safety validation.

## Objective

Verify EPS command handling and all eight simulated switch states through an all off, all on, and restored all off sequence.
Observe voltage and current as model outputs without assigning mission acceptance limits that the repository does not define.

## Safety boundary

Run this draft only against the Simulith EPS model.
Do not run an all on sequence against physical hardware without an approved load map, current limits, power source limits, thermal constraints, and emergency stop method.

The startup EPS simulator state sets every switch to `OFF` with zero switch voltage and current.
The existing procedures command switch numbers 0 through 7.

## Prerequisites

* start the default `sat-1` or `flatsat` full lab
* wait for Do No Harm startup activity to settle
* confirm EPS housekeeping is fresh
* confirm the direct debug command and telemetry links are available
* ensure no other procedure is commanding EPS

Use the direct debug path so the exercise does not depend on a radio pass.

## Phase 1 Verify EPS health

Open `Procedures / Stacks / EpsComponent.ycs`.
Run the complete procedure.

It resets EPS counters, requests housekeeping, sends an EPS NOOP, requests housekeeping again, and verifies the command count.
Stop if any step fails.

Record the initial state of switches 0 through 7.
A fresh simulator should report every switch `OFF`.
If the state differs, preserve it and either explain the prior activity or restart the lab before continuing.

## Phase 2 Establish all off

Open `Procedures / Stacks / EpaAllOff.ycs`.
The filename currently uses `Epa`, although the commands target EPS.

Review the eight switch commands and final assertion before running.
Run the complete procedure and confirm that every switch reports `OFF` in fresh housekeeping.
Record switch voltage and current as baseline observations.

## Phase 3 Command all on

Open `Procedures / Stacks / EpsAllOn.ycs`.
Run the complete procedure.

The procedure commands switch numbers 0 through 7 on, requests EPS housekeeping, and verifies that all eight states are `ON`.
Confirm that each assertion passes.
Record the resulting switch voltage and current values with simulation time.

Do not interpret a nonzero model value as proof that a real load is powered.
The result shows the configured simulator response to the command.

## Phase 4 Restore all off

Run `EpaAllOff.ycs` again.
Confirm every switch returns to `OFF`.
Confirm fresh telemetry reports the restored state and record final voltage and current values.

The all off result is the required cleanup state for this scenario.
If the stack stops early, command and verify each remaining switch individually or restart the simulated lab.

## Expected results

| Checkpoint | Expected result |
| --- | --- |
| EPS health | `EpsComponent.ycs` completes without a failed assertion. |
| Initial state | Switches 0 through 7 report `OFF` on a fresh simulator. |
| All off baseline | `EpaAllOff.ycs` verifies every switch is `OFF`. |
| All on | `EpsAllOn.ycs` verifies every switch is `ON`. |
| Cleanup | The second all off run verifies every switch is `OFF`. |
| Errors | No unexpected EPS command or device error is introduced. |

## Troubleshooting

If one switch differs from the commanded state, request fresh EPS housekeeping before repeating a command.
Inspect the EPS application event, command error count, simulator connection, switch number, and procedure result.

If all telemetry is stale, diagnose the EPS simulator and cFS scheduling path before continuing.
Do not repeat the all on procedure blindly because duplicate commands can hide the first failure.

## Evidence and next automation

Retain the three procedure results, switch state snapshots, EPS counters, voltage and current observations, timestamps, active configuration, and EPS logs.
A useful screenshot set shows all off, all on, and restored all off with the same parameter layout.

A combined procedure should add a simulator only guard, fresh telemetry checks, timeouts, a cleanup branch, and explicit command error assertions.
Mission power limits should remain out of scope until they are defined and traced to the EPS model.

***
Last reviewed: 14 August 2026
