# Nominal Operations

> **Scenario status:** The repository does not yet contain one complete YAMCS procedure for this scenario.
> This page defines a reviewable operator flow assembled from current cFS applications, DRM tables, and component procedures.
> It is not an executed acceptance record.

## Objective

Exercise a representative ground pass after commissioning: assess spacecraft health, establish the simulated radio link, operate the demonstration payload, manage stored data, and downlink a file.

## Implemented building blocks

* RTS 6 configures a simulated pass lasting eight minutes by placing the Radio in Duplex mode, waiting 480 scheduler wakeups, then returning it to Receive mode.
* YAMCS provides debug and radio command/telemetry links.
* `AdcsComponent.ycs`, `DemoComponent.ycs`, `EpsComponent.ycs`, and `RadioComponent.ycs` provide focused subsystem checkouts.
* DS records selected telemetry to files.
  FM can list and manage files.
  CF and YAMCS provide CFDP services.
* 42 truth and component telemetry are available for comparing simulated environment state with spacecraft observations.

## Proposed operator flow

1. Start the default DRM and confirm YAMCS links, cFS events, and Simulith time are healthy.
2. Review cFE and component housekeeping before commanding.
3. Start RTS 6 and confirm the Radio enters Duplex mode and `radio-in`/`radio-out` carry traffic.
4. Confirm the intended ADCS mode and compare ADCS telemetry with 42 truth.
5. Enable and configure the demonstration payload, then confirm data telemetry.
6. Close the active DS file set before transfer.
7. Request an FM directory packet for `/d` and choose a completed data file.
8. Initiate a CFDP transfer and confirm completion in both flight and ground telemetry.
9. Stop RTS 6 if the exercise must continue outside the simulated pass, or allow it to return the Radio to Receive mode.
10. Record anomalies, command history, relevant telemetry, file hashes, configuration, and repository revision.

## Completion criteria to automate

An executable procedure should add explicit assertions for link state, command counters, radio mode, ADCS pointing tolerance, payload state, DS closure, FM response, CFDP completion, and destination file integrity.
Until those assertions and retained results exist, this outline demonstrates available mechanisms rather than verified nominal mission performance.
