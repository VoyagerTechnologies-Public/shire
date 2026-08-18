# Scenarios

Scenarios turn the SHIRE services into guided mission exercises.
They explain the operator goal, the available implementation, the expected observations, and the evidence worth retaining.

## Choose a scenario

| Scenario | What a new user learns | Current status |
| --- | --- | --- |
| [Commissioning](commissioning.md) | How SHIRE boots into Do No Harm, how to establish a pass, and how to check out core functions | Manual exercise using current commands, tables, links, and component procedures |
| [Nominal Operations](nominal-operations.md) | How to prepare and execute a representative payload data pass | Proposed operator flow built from implemented capabilities |
| [Fault Detection, Isolation, and Recovery](fdir.md) | How an injected Demo data fault moves through LC and SC into an automated restart | Executable YAMCS procedure using current simulator controls and flight tables |
| [Security Incident Response](security-incident-response.md) | How to plan a controlled failure and recovery exercise for the secured radio path | Proposed exercise supported by current Radio unit tests and link architecture |
| [Data Lifecycle](data-lifecycle.md) | How Demo telemetry becomes a stored file and CFDP delivery | Draft manual exercise using current data services |
| [Debug and Radio Path Comparison](link-path-comparison.md) | How to isolate and observe each lab communications path | Draft manual exercise using current YAMCS links |
| [EPS Load Switching](eps-load-switching.md) | How simulated EPS switches respond to current procedures | Draft simulated exercise with an all off cleanup state |
| [ADCS Truth Comparison](adcs-truth-comparison.md) | How to compare ADCS observations with aligned 42 truth | Draft exercise that still needs an approved numeric comparison |
| [Component Hardware Development](component-hardware-development.md) | How to move a molded component from simulation toward board hardware | Draft development workflow with explicit implementation gates |
| [Recommended Scenarios](recommended.md) | Which exercises would add the most learning and verification value next | Documentation and implementation roadmap |

New users should begin with Commissioning.
Nominal Operations follows naturally because it reuses the pass, subsystem, data storage, and file transfer concepts.
The FDIR exercise is a good third scenario because it introduces controlled fault injection and automated recovery.

## Before every run

1. Select the mission, spacecraft, and scenario using [Configuration](../manual/how-to/configuration.md).
2. Run `make` after source or configuration changes.
3. Start SHIRE with `make start`.
4. Wait for the startup handshakes and automatic RTS activity to settle.
5. Confirm simulation time is advancing.
6. Inspect YAMCS **Links** before sending a command.
7. Record the repository revision and generated configuration.

The default DRM uses both a direct debug path and a representative radio path.
A green link does not by itself prove that the path you intend to test carried a particular command or telemetry packet.
Use command history, packet time, counters, events, and service logs together.

## How to read scenario status

An **executable** scenario points to a procedure that already contains commands and assertions.
It still needs a retained successful result before it can support a verification claim.

A **manual** scenario composes working commands and procedures into an operator walkthrough.
The steps can be performed now, but the repository does not yet provide one procedure that asserts the entire result.

A **proposed** scenario describes a useful flow whose building blocks exist only in part.
It should not be presented as demonstrated mission behavior until the missing controls, assertions, and cleanup are implemented.

A **draft** scenario provides enough repository grounded detail for review and trial execution.
It still needs procedure automation or target validation before its status can advance.

## Evidence packet

A scenario run becomes useful evidence only when its context and result are retained.
For each run, collect:

* repository and submodule revisions
* `build/active.yaml` and `build/build.yaml`
* the exact YAMCS procedure or documented step revision
* YAMCS link state before commanding
* expected and observed telemetry or event results
* procedure result, command history, and relevant service logs
* generated files and hashes when the scenario transfers data
* operator, date, host context, and final pass or fail decision

Use [Verification and Validation](../drm/verification-and-validation.md) when connecting a scenario result to a DRM requirement.

## Screenshot capture points

Capture screenshots only after the corresponding step is repeatable and its expected result is known.
The most useful scenario captures are:

* healthy YAMCS link state before a run
* Do No Harm housekeeping after automatic startup actions complete
* telemetry immediately before an injected condition
* the event or parameter that proves the condition occurred
* the recovery result after cleanup
* CFDP completion and the received file record
* 42 truth beside the related component telemetry

Crop screenshots to the information needed by the step.
Remove credentials, host details, keys, and unrelated mission data before publication.

## Stop or recover a run

If an assertion fails, stop the procedure before sending additional commands.
Save the first failure, current telemetry, command history, and relevant logs.
Run the scenario cleanup when one is defined.
Use `make stop` to stop the lab without treating the interruption as a passing result.

Questions and improvement ideas are welcome in [GitHub Discussions](https://github.com/VoyagerTechnologies-Public/shire/discussions).
Report reproducible defects through [GitHub Issues](https://github.com/VoyagerTechnologies-Public/shire/issues).

***
Last reviewed: 14 August 2026
