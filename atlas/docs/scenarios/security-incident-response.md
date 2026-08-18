# Security Incident Response

> **Scenario status:** No executable procedure covers this complete flow in the current repository.
> This page is a planning guide grounded in the implemented radio pipeline and Radio unit tests.
> It must not be reported as an operational security validation.

## What you will learn

This proposed scenario teaches a disciplined way to evaluate failure handling in the simulated secured radio path.
It focuses on baseline, one controlled condition, observable detection, bounded containment, restoration, and sanitized evidence.

A new user should leave able to explain:

* where CryptoLib participates in the command and telemetry path
* why the direct debug path is different from the representative radio path
* which observation proves a frame was rejected or processing failed
* how to restore the known good baseline
* which evidence is safe to retain and publish

## Current implementation

* The Radio cFS application initializes CryptoLib.
* The Radio processes received telecommand transfer frames and applies security to telemetry frames sent on the radio path.
* A standalone CryptoLib service participates in the simulated ground radio path.
* YAMCS exposes separate direct debug and radio links.
* Radio unit tests cover CryptoLib initialization, missing association, apply failure, process failure, and successful paths using test stubs and hooks.
* Simulith provides an isolated and resettable environment for controlled simulator conditions.

The debug path is a lab convenience.
It bypasses the representative secured radio path and must not be treated as a flight security boundary.

## Prerequisites for developing the exercise

Do not run an improvised incident exercise against shared or operational infrastructure.
Use only the local SHIRE lab and nonoperational test material.

Before creating a runnable procedure:

1. Name the exact condition to test.
2. Identify the approved injection mechanism.
3. Define the expected Radio and CryptoLib event or counter.
4. Define which services and telemetry should remain unaffected.
5. Define a timeout and stop condition.
6. Define the restoration action and successful baseline command.
7. Review every retained field for sensitive material.

The current repository does not expose a reviewed YAMCS control for every condition represented by the Radio unit test hooks.
That missing control must be implemented safely before those conditions become operator scenarios.

## Establish the baseline

Start the default DRM and wait for Do No Harm startup to complete.
Start RTS 6 so the Radio enters Duplex mode.

Choose one harmless command with an observable counter or event.
Record:

* radio link state and data counts
* Radio mode and application counters
* the command acknowledgment and flight event
* representative telemetry received through the radio path
* CryptoLib service health and sanitized log context

Disable or separate the direct debug command output when the test design requires proof that the radio path carried the command.
Changing link state must be part of the reviewed procedure because both command outputs consume `tc_realtime` by default.

## Proposed exercise flow

1. Record the security association identifier, managed parameter set, key source type, mission configuration, and expected link behavior without recording key material.
2. Demonstrate the named baseline command and telemetry exchange through the radio path.
3. Introduce one approved condition such as an unavailable test association or a controlled processing failure.
4. Verify the specified Radio or CryptoLib event, counter, or rejection record.
5. Confirm that unrelated telemetry and simulation time follow the predefined containment expectation.
6. Remove the condition and restore the known good test configuration.
7. Repeat the same baseline command and telemetry exchange.
8. Retain sanitized results, timing, configuration identifiers, and the final recovery decision.

Test one condition per run.
Combining several changes makes detection and recovery evidence difficult to interpret.

## Acceptance criteria to define

| Area | Required expected result |
| --- | --- |
| Baseline | A named command and telemetry exchange succeeds through the configured Radio and CryptoLib path. |
| Injection | One approved condition is introduced without changing unrelated configuration. |
| Detection | A specified event, counter, or rejected frame record proves the condition was detected. |
| Containment | Named unrelated services and telemetry continue according to the predefined boundary. |
| Recovery | The known good configuration is restored and the original baseline exchange succeeds again. |
| Timing | Detection and recovery complete within limits chosen before the run. |
| Sanitization | Retained evidence contains no keys, credentials, sensitive association content, or proprietary mission data. |

## Stop conditions

Stop the exercise and preserve state if:

* the approved condition cannot be introduced exactly as reviewed
* the expected detection evidence is absent
* unrelated services cross the containment boundary
* restoration does not recover the original baseline
* logs or exports may contain sensitive material

Do not weaken the configuration or expose test material merely to make the scenario pass.

## Evidence and screenshots

Retain the repository revision, generated configuration, procedure revision, link state, sanitized events and counters, baseline result, condition timing, recovery result, and reviewer decision.

A useful screenshot sequence shows the successful radio baseline, the expected detection observation, and the successful repeated baseline after restoration.
Redact tokens, keys, raw security association content, host credentials, and unrelated mission data before publication.

## Current verification boundary

Run `make test-fsw` to execute the current component flight software unit tests, including the Radio coverage tests.
Those tests provide automated evidence for application behavior under stubbed CryptoLib outcomes.
They do not validate the complete container path, operator response, real key management, network controls, timing, or flight radio hardware.

Before promoting this page to an executable scenario, add a reviewed injection control, a YAMCS procedure, explicit assertions, timeouts, cleanup, and a requirement mapping.
The [Recommended Scenarios](recommended.md) lists a radio path comparison as a safer precursor to failure injection.

***
Last reviewed: 14 August 2026
