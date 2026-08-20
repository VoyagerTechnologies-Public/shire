# Debug and Radio Path Comparison

> **Scenario status:** Draft manual scenario using the current YAMCS link controls and command paths.
> YAMCS prefers `radio-out` and falls back to `debug-out` when the preferred interface is unavailable.

## Objective

Send the same harmless command once through the direct debug path and once through the representative Radio and CryptoLib path.
Compare the route, flight observation, return telemetry, and timing.

## Understand the paths

| Path | Command route | Telemetry route |
| --- | --- | --- |
| Debug | YAMCS `debug-out` to cFS on UDP 1234 | TO_LAB to YAMCS `debug-in` on UDP 1235 |
| Radio | YAMCS `radio-out` to CryptoLib on UDP 12345, then Radio on UDP 12343 | Radio on UDP 12344 to CryptoLib, then YAMCS `radio-in` on UDP 12346 |

The debug path is a direct lab interface.
The radio path exercises more of the representative communications chain.
Neither path is proven by a green status indicator alone.

## Prerequisites

* complete the first contact phase in [Commissioning](commissioning.md)
* wait for RTS 5 to enable the Radio in Receive mode
* choose one harmless command with an observable counter or event
* record the initial link state and flight counter
* ensure no other operator or procedure is commanding the same instance

An ES NOOP is a suitable starting command.
Read the current ES command counter and error count before each trial.

## Trial 1 Debug path

1. Open the YAMCS **Links** page.
2. Enable `debug-out` and `debug-in`.
3. Disable `radio-out` for the measurement.
4. Record the link state and data counts.
5. Send `/CFE_ES/CFE_ES_COMMANDS/CFE_ES_NOOP`.
6. Confirm one new command observation through the ES counter, command history, and FSW event.
7. Confirm fresh telemetry reaches `debug-in`.
8. Record the command time and first confirming telemetry time.

`radio-in` can remain visible, but its packets are not evidence for this command trial.
Disabling `radio-out` forces YAMCS to select the debug fallback for this measurement.

## Trial 2 Radio path

1. Disable `debug-out`.
2. Enable `radio-out` and `radio-in`.
3. Start RTS 6 through the Radio path so the Radio enters Duplex mode.
4. Confirm `radio-in` begins receiving telemetry.
5. Record a fresh ES counter and error baseline.
6. Send the same ES NOOP.
7. Confirm one new command observation through the ES counter, command history, and FSW event.
8. Confirm the resulting telemetry reaches `radio-in`.
9. Record the command time and first confirming telemetry time.

Starting RTS 6 is itself a command and must be separated from the NOOP when interpreting counters.
Use timestamps and command names rather than subtracting from the original startup count.

## Compare the trials

| Observation | Debug trial | Radio trial |
| --- | --- | --- |
| Enabled command output | `debug-out` only | `radio-out` only |
| Flight command accepted | Record event and counter change | Record event and counter change |
| Return telemetry link | `debug-in` | `radio-in` during Duplex mode |
| New command errors | Record value | Record value |
| Observed response time | Record value | Record value |
| Relevant service logs | cFS and YAMCS | YAMCS, CryptoLib, Radio, and cFS |

The observed timing is diagnostic data from one host run.
It is not a communications performance requirement unless a reviewed limit is chosen before execution.

## Expected results

Each trial should show one intended command in YAMCS history and one corresponding accepted command observation in cFS.
The debug trial should return telemetry through `debug-in`.
The radio trial should return telemetry through `radio-in` while the Radio is in Duplex mode.
Neither trial should create a new command error.

If the flight counter changes more than expected, stop and check for another command source or procedure.
If the radio command succeeds but no radio telemetry returns, confirm RTS 6, Radio mode, CryptoLib service state, and `radio-in` data counts.

## Restore the lab

Reenable the link configuration expected by the next scenario.
Allow RTS 6 to complete or stop it and return the Radio to Receive mode.
Record the restored link state.

The checked in configuration enables both command interfaces and restores the preferred radio path with debug fallback after a restart.

## Evidence and automation

Retain before and after link screenshots, command history, ES counters, events, Radio mode, link data counts, timing observations, and service logs.

An automated version needs reviewed link control, a unique command marker, assertions for the selected interface, and restoration even when a trial fails.
It should run before the [Security Incident Response](security-incident-response.md) scenario so normal radio behavior is established first.

***
Last reviewed: 14 August 2026
