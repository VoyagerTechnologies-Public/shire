# Security Incident Response

> **Scenario status:** No executable security incident procedure covering the complete flow is currently included in the repository.
> The items below identify implemented mechanisms and the evidence needed before publishing a verified exercise.

## Available mechanisms

* The Radio cFS application initializes CryptoLib, processes received telecommand transfer frames, and applies telemetry security on downlink frames.
* The standalone CryptoLib service sits in the simulated ground radio path.
* YAMCS exposes separate debug and radio links, making it possible to compare a direct lab path with the representative secured path.
* Radio unit tests include CryptoLib initialization, missing association, apply/process failure, and successful path cases using test stubs and hooks.
* Simulith provides an isolated, resettable environment for controlled simulator faults.

## Proposed exercise structure

1. Record the security association, managed parameters, key source, mission configuration, and expected link behavior without exposing key material.
2. Establish a nominal command and telemetry baseline over the radio path.
3. Introduce one precisely defined condition, such as an invalid frame, unavailable association, or CryptoLib processing failure.
4. Verify rejection or error handling in Radio and CryptoLib events and counters.
5. Confirm that unrelated telemetry, debug access, and simulation time behavior match the expected containment boundary.
6. Restore the known good configuration and demonstrate successful command and telemetry processing.
7. Retain sanitized commands, events, counters, packet metadata, logs, configuration identifiers, and recovery timing.

## Safety and publication

Do not commit operational keys, credentials, sensitive security association material, exploit payloads, or proprietary mission configurations.
The default lab configuration is not a hardened deployment, and successful processing in the simulated pipeline does not validate an operational key management system, network boundary, or flight radio implementation.

Before promoting this page to a runnable scenario, add a reviewed YAMCS procedure, explicit expected results, cleanup steps, and a mapping from requirements to evidence.
