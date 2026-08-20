# Scenarios

Scenarios turn the SHIRE services into guided mission exercises.
They explain the operator goal, the available implementation, the expected observations, and the evidence worth retaining.

## Available scenario

| Scenario | What a new user learns | Current status |
| --- | --- | --- |
| [Commissioning](commissioning.md) | How SHIRE boots into Do No Harm, how to establish a pass, and how to check out core functions | Manual exercise using current commands, tables, links, and component procedures |

Commissioning is the only scenario currently included in the published Atlas navigation.
Additional scenario drafts remain planned work until their procedures, assertions, and review evidence are ready.

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

## Current scenario status

Commissioning is a **manual** scenario that composes working commands and procedures into an operator walkthrough.
The steps can be performed now, but the repository does not yet provide one procedure that asserts the entire result.

## Evidence packet

A scenario run becomes useful evidence only when its context and result are retained.
For each run, collect:

* Repository and submodule revisions
* `build/active.yaml` and `build/build.yaml`
* The exact YAMCS procedure or documented step revision
* YAMCS link state before commanding
* Expected and observed telemetry or event results
* Procedure result, command history, and relevant service logs
* Generated files and hashes when the scenario transfers data
* Operator, date, host context, and final pass or fail decision

Use [Verification and Validation](../drm/verification-and-validation.md) when connecting a scenario result to a DRM requirement.

## Screenshot capture points

Capture screenshots only after the corresponding step is repeatable and its expected result is known.
The most useful scenario captures are:

* Healthy YAMCS link state before a run
* Do No Harm housekeeping after automatic startup actions complete
* Radio housekeeping after the pass sequence enters `DUPLEX`
* Successful component checkout results
* The FM listing used to select the stored file
* CFDP completion and the received file record

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
Last reviewed: 20260819
