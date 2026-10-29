# Scenarios

Scenarios turn the SHIRE services into guided mission exercises.
They explain the operator goal, the available implementation, the expected observations, and the evidence worth retaining.
They are arranged as a lesson plan, so each one builds on the skills of the ones before it.

## The mission story

You are a flight controller for the Design Reference Mission spacecraft, a small Earth orbit demonstration satellite named `sat-1`.
The first nine lessons follow its first months, from launch day to routine operations.
The last three follow the engineering team that checks how far to trust a result, adds a component, and moves the flight software onto a board.
SHIRE is your operations team's rehearsal environment, where every procedure is tried before it is trusted on the spacecraft.

## The lesson plan

| Unit | Lesson | You will learn | Status |
| --- | --- | --- | --- |
| Foundations | [Commissioning](commissioning.md) | Do No Harm, first contact, health checks, Sun safe pointing, and a file download | Published |
| Foundations | [Simulation Time and Repeatability](simulation-time-repeatability.md) | pausing, changing speed, simulation time against wall time, initial conditions, and replaying a run | Published |
| Spacecraft systems | [Power and Eclipse](power-eclipse.md) | loads, battery response, solar input, and eclipse | Published |
| Spacecraft systems | [Attitude Control](attitude-control.md) | detumbling, pointing modes, and comparing pointing with truth | Published |
| Ground control | Radio Link and Command Paths | which path carries a command, link modes, delay and loss, and recovery from lost contact | Planned |
| Ground control | [Cmd/Tlm Validation](cmd-tlm-validation.md) | command counting, limits and alarms, stored command upload, and flight software patches | Published |
| Ground control | Link Security | encryption, tamper and replay rejection, and incident response | Planned |
| Autonomy and operations | Fault Detection and Recovery | onboard detection and recovery with limit checking and stored commands | Planned |
| Autonomy and operations | Nominal Operations | a complete pass that uses every earlier skill | Planned |
| Engineering and analysis | Monte Carlo Analysis | running a campaign, reading its report, and what the spread of results shows | Planned |
| Engineering and analysis | New Component Development | creating a component from the mold, simulating it, and checking it out | Planned |
| Engineering and analysis | Processor in the Loop | running the flight software on a board while SHIRE drives time, dynamics, and simulated devices | Planned |

Start with Commissioning.

## How the lessons work

* Each lesson starts from a freshly booted spacecraft or a clean checkout, so you can repeat a lesson without leftover state.
* Each page names the lessons it needs and the skills it expects from them in its prerequisites.
* A concept is explained in the lesson that introduces it, and later lessons link back to it.
* Each lesson adds one habit to the evidence packet described below.
* Each lesson ends with a review that has a checkpoint and a link to the next lesson.

The DRM also registers scenarios that run headless with `make scenario`.
See [Scenarios and initial conditions](../manual/how-to/scenarios.md) for every registered scenario and how to run one.

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
The steps can be performed now, and `make scenario SCENARIO=checkout` automates the component checks with `CheckoutTest.ycs`, but no single procedure asserts the entire Commissioning result.
Cmd/Tlm Validation is also a manual walkthrough, and its automated run proves only that the stacks' commands and checks pass, not the alarm state in YAMCS.
Simulation Time and Repeatability is a manual walkthrough too, and `make scenario SCENARIO=checkout` automates its pause check while `make scenario-smoke` automates its repeatability check.
Power and Eclipse is a manual walkthrough as well, and `make scenario SCENARIO=eps-functional` automates the switch checks but not the orbit or the battery limit.
Attitude Control is a manual walkthrough too, and `make scenario SCENARIO=adcs-modes-sweep` and `make scenario SCENARIO=adcs-truth-verification` automate its mode and truth checks.

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

Each lesson adds one habit to this packet, so Nominal Operations ends with a complete one.

| Lesson | Habit it adds |
| --- | --- |
| Commissioning | Record the link state and the command acknowledgements. |
| Simulation Time and Repeatability | Record the simulation time and the run identifier so the run can be replayed. |
| Power and Eclipse | Record parameter values and limit states with their times. |
| Attitude Control | Record pointing values next to the truth values. |
| Radio Link and Command Paths | Record which links were enabled and the counters on each path. |
| Cmd/Tlm Validation | Record the alarm history and the file transfer records. |
| Link Security | Record the log assertions with keys and addresses removed. |
| Fault Detection and Recovery | Record the events and the stored command status as a timeline. |
| Nominal Operations | Assemble the complete evidence packet. |
| Monte Carlo Analysis | Record the campaign file, the seed, and the number of trials beside every statistic. |
| New Component Development | Record the generated files and the checkout result against the reference component. |
| Processor in the Loop | Record the board profile and build revision beside the board output and the simulated result. |

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

Scenario screenshots are captured by script and not by hand.
See [Scenario screenshots](../manual/how-to/scenario-screenshots.md) to refresh a scenario's images after a change.

## Stop or recover a run

If an assertion fails, stop the procedure before sending additional commands.
Save the first failure, current telemetry, command history, and relevant logs.
Run the scenario cleanup when one is defined.
Use `make stop` to stop the lab without treating the interruption as a passing result.

To add a lesson, see [Writing a Scenario Lesson](../manual/how-to/writing-lessons.md).
Questions and improvement ideas are welcome in [GitHub Discussions](https://github.com/VoyagerTechnologies-Public/shire/discussions).
Report reproducible defects through [GitHub Issues](https://github.com/VoyagerTechnologies-Public/shire/issues).

***
Last reviewed: 20261028
