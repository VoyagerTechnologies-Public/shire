# Verification and Validation

> **Status:** This page defines the proposed verification and validation approach for the Design Reference Mission.
> It identifies current evidence sources and remaining gaps.
> It does not mark any requirement as verified or any stakeholder objective as validated.

## Purpose

Verification determines whether the DRM satisfies each `shall` statement in the [Mission Requirements](mission-requirements.md).
Validation determines whether the verified DRM satisfies the `OBJ-*` stakeholder objectives and its intended use.

The Mission Requirements page assigns one planned method, verification level, and owner role to every requirement.
This page defines how those assignments become controlled evidence.
A future verification cross reference matrix (VCRM) must record the result for each individual requirement.

## Verification methods

The current baseline uses three methods.

| Method | Application | Expected result |
| --- | --- | --- |
| Test | Controlled execution with defined inputs and measured outputs | Objective data compared with an acceptance criterion |
| Inspection | Examination of source, configuration, interfaces, generated artifacts, or documentation | Recorded conformity decision against an inspection checklist |
| Demonstration | Operation of the integrated DRM in a representative workflow | Qualitative evidence that the top level behavior is present |

Test is preferred when controlled execution can produce objective data.
Demonstration does not replace test evidence for a quantitative or fault response requirement.
Analysis may be added for a future requirement when a model or calculation is the appropriate evidence source.

## Verification principles

* Verify the exact approved requirement revision
* Define acceptance criteria before executing the activity
* Use the method and level assigned by the requirement attributes
* Verify lower level behavior before relying on it in an integrated demonstration
* Record the active mission, spacecraft, scenario, and link path
* Treat simulator controls and backdoors as test equipment
* Separate test observations from analysis and operator interpretation
* Retain enough evidence for an independent reviewer to reproduce the decision

A passing unit test is candidate evidence for the behavior it exercises.
It is not automatically verification evidence for a DRM requirement.
Coverage reports show which code executed and do not establish that an acceptance criterion passed.

## Current evidence sources

| Area | Repository source or command | Current use |
| --- | --- | --- |
| Configuration | `make list` plus `build/active.yaml` and generated mission files | Active selection and generated configuration review |
| Simulith core | `cd simulith && make test`, run by the CI `test-simulith` job as `make -C simulith build-test` | Server, client, timing, transport, and 42 adapter unit behavior |
| Component simulators | `make test-sim` | Selected simulator lifecycle, protocol, and dynamics behavior |
| Flight software | `make test-fsw` | Configured cFS and component application unit behavior |
| YAMCS | `cd yamcs && make test` | Tests supplied by the YAMCS submodule build, with no CI job |
| Tool and commander tests | `python3 tools/test_shire_limits.py` and the other `tools/test_*.py` files, plus `python3 yamcs/test_yamcs_commander_verify.py` | Orchestrator, limits, scenario, board, QEMU, radio, EPS, archive, screenshot, performance topology, and commander verify engine unit behavior |
| Focused component integration | `make cli` followed by `make cli-start` | CLI and simulator protocol checkout without cFS or YAMCS |
| DRM integration | `make` followed by `make start` | Manual observation of the generated flight, ground, security, dynamics, and simulation stack |
| Scenario initialization | `make scenario SCENARIO=<name>` | Headless, no-GUI confirmation that a named scenario (deterministic orbit, epoch, attitude, and per-component state) completes cleanly, with a reproducible Initial Condition snapshot and container logs retained per run |
| Scenario determinism | `make scenario-smoke` | Cross-trial repeatability check (exact terminal dynamics state, command deliveries, and protocol counts) for the currently configured scenario |
| Synchronized performance | `make perf` and `make perf-compare BASELINE=<report.json>` | Active ADCS and radio workload with 50x throughput, trace, Yamcs truth, and control-loop acceptance |
| Component procedures | `comp/*/gsw/procedures/*.ycs`, executable manually in the YAMCS web UI or headlessly via `yamcs/yamcs_commander.py --stack` or a scenario's `verify_stacks` | Repeatable YAMCS component command sequences |
| DRM procedures | `cfg/<mission>/gsw/procedures/*.ycs`, staged into the `yamcs/` submodule at build time, same manual or headless execution paths | Repeatable YAMCS system command sequences |
| Published scenarios | `atlas/docs/scenarios/commissioning.md` and `atlas/docs/scenarios/cmd-tlm-validation.md`, the only scenarios in the Atlas navigation | Manual commissioning and command and telemetry validation workflows with expected results and evidence guidance |
| Executable scenarios | `make scenario SCENARIO=<name>` for the 17 scenarios in `cfg/drm/scenarios` | Headless runs where 13 scenarios execute YAMCS stacks through `verify_stacks` and `nominal`, `debug`, `eclipse-entry-adcs`, and `eclipse-exit-adcs` confirm initialization only |
| Stack coverage by scenario | `checkout` runs `CheckoutTest.ycs`, seven `adcs-*` scenarios run the ADCS stacks including `adcs-truth-verification`, `eps-functional` and `eps-backdoor-testing` run the EPS stacks, `radio-link-testing` and `radio-security-testing` run the Radio stacks, and `cmd-tlm-validation` runs `CmdTlmLimits.ycs`, `CmdTlmStoredCommands.ycs`, and `CmdTlmPatches.ycs` | Executable assertions on component behavior, limits and alarms, uploaded tables, patches, link impairment, and secured Radio traffic |
| Limits and alarms | `CmdTlmLimits.ycs`, the `limits:` section of `cfg/drm/drm.yaml`, and `tools/test_shire_limits.py` | Yellow and red limit resolution into the mission database and the alarm behavior of battery parameters |
| Radio security | `RadioSecurity.ycs` with the `log_assertions` of `radio-security-testing` and `comp/cryptolib/test/unit/ut_shire_security.c` | Tamper and replay rejection in both directions plus CryptoLib clear and encrypted mode unit checks, where no CI job runs the CryptoLib tests |
| Monte Carlo campaigns | `make campaign CAMPAIGN=<name>` with `cfg/drm/campaigns/*.yaml` | Dispersed trials of one scenario aggregated into a campaign report and dataset |
| Processor in the loop | `make board-test BOARD=<profile>` and `make board-scenario SCENARIO=<name> BOARD=<profile>` with `cfg/boards/*.yaml` | The cFS target on an emulated or physical Linux processor with host simulators and YAMCS, without qualifying peripherals |
| Atlas validation | `make docs-check` | Markdown, asset, procedure, command reference, and strict site build checks |
| Repository CI | `.github/workflows/ci.yml` and `.github/workflows/docs.yml` | Simulith, flight software, and CLI builds, flight software, component simulator, and Simulith core tests, complexity analysis, and Atlas validation |

The CI workflow runs on pull requests and on pushes to `main` or `dev` inside the pinned `shire-base:0.0.0` image.
It builds the component simulators with Simulith, the flight software, and the component CLIs.
The `test-fsw` job runs the cFS unit tests with coverage and the cyclomatic complexity analysis through `make build-complexity`.
The `test-component-simulators` job runs `make -C simulith build-comp-test`, and the `test-simulith` job runs `make -C simulith build-test` for the Simulith core.
Those three test jobs upload coverage to Codecov with the flags `fsw`, `component-sim`, and `simulith`.

CI does not run the YAMCS submodule tests, any scenario or campaign, the performance harness, processor in the loop runs, the `tools/test_*.py` unit tests, `yamcs/test_yamcs_commander_verify.py`, or the CryptoLib unit tests.
Running a complete DRM scenario or a `.ycs` procedure headlessly is possible on demand through `make scenario`, `make scenario-smoke`, and `yamcs_commander.py --stack`, but none of it is wired into CI.
Bringing up the full Docker Compose stack on a hosted runner is new territory for this pipeline, so its resource and timing behavior needs validating on a draft pull request before anything gates merges on it.
The synchronized performance matrix remains a developer run activity because `make perf-compare` accepts a baseline only from the same host, logical CPU count, workload, and CPU placement.
The documentation workflow runs `atlas/scripts/check_docs.py` and a strict Atlas build on pull requests and pushes to `main` or `dev`.
It publishes the Atlas only from a successful `main` build.

A workflow definition is not evidence that a particular revision passed.
Retain the workflow run, job logs, coverage result, and exact revision when using CI output as verification evidence.

## Verification record

Every verification result must retain:

* Requirement ID and requirement revision
* Planned method, verification level, owner, and reviewer
* Repository revision and all submodule revisions
* `build/active.yaml` plus relevant merged and generated configuration
* Host, container image, target processor, and physical hardware information
* Procedure, test case, checklist, or analysis revision
* Preconditions, controlled inputs, and test equipment
* Acceptance criteria defined before execution
* Actual result and pass or fail decision
* Logs, telemetry, command history, coverage, files, screenshots, or inspection notes
* Anomalies, deviations, waivers, and their disposition
* Execution date and evidence location

For an integrated run, also record which YAMCS command links were enabled.
YAMCS sends each command on every enabled command link, and the ground commands `USE_DEBUG_UPLINK` and `USE_RADIO_UPLINK` select one path.
Record the selected interface when the command transport path is part of the evidence.
For a performance record, retain the complete performance report rather than only a
throughput value.
See [Synchronized Simulation Performance](../manual/how-to/performance.md) for
the current accepted workload and report interpretation.
The report captures the scenario digest, Software Bus deliveries, application
success-event acceptance counts, deterministic command counts, explicitly
accounted wall-clock output, participant and device counts, scheduling policy,
resource samples, phase latencies, queue
errors, and terminal-state digest needed to interpret the result.

## Planned verification by requirement group

The following table identifies the activity needed to close each portion of the current baseline.
An available source means that relevant code, tests, or a manual scenario exist.
It does not mean the requirement has passed verification.

| Requirement IDs | Planned activity | Primary evidence | Current boundary |
| --- | --- | --- | --- |
| `DRM-001` | Execute a controlled integrated DRM demonstration | Commissioning record plus a draft Nominal Operations record | Published Commissioning workflow and the headless `checkout` and `nominal` scenarios without one procedure containing complete assertions |
| `DRM-002` | Demonstrate command and telemetry exchange through each declared path | Commissioning record plus a draft Link Path Comparison record | Published Commissioning and Cmd/Tlm Validation workflows with a duplicate path risk when both links are enabled |
| `DRM-003` | Demonstrate one complete stored file transfer to YAMCS | Draft Data Lifecycle record and received file | Commissioning includes manual CFDP steps, `CheckoutTest.ycs` asserts the `CF_EOTPACKET` direction and status for an upload and downlink round trip, and no stack covers a DS produced file selected from an FM listing |
| `DRM-004` | Demonstrate an active LC condition reaching its configured response | Draft FDIR record with LC, SC, event, and state telemetry | Implemented action points 5 and 11 plus `DemoFDIR.ycs`, with no scenario that runs it and no published scenario |
| `DRM-005` | Demonstrate one component through simulator and physical hardware transports | Draft Component Hardware Development evidence packet | Processor in the loop evidence exists for the flight processor path, and component hardware transport evidence is not present |
| `CFG-001` | Test configuration generation for a declared selection | Active configuration and orchestrator output | Build path and `tools/test_shire_orchestrator.py` available without a test record traced to `CFG-001` |
| `CFG-002`, `CFG-003` | Inspect generated cFS and Simulith content against the spacecraft selection | Generated startup and simulator configuration checklist | Generated artifacts available |
| `SIM-001`, `SIM-002` | Test 42 propagation and shared Simulith context during a controlled run | Simulith tests run in CI, 42 truth, `make scenario-smoke` repeatability, and component observations | Unit sources and a repeatability check available with integrated acceptance work remaining |
| `SIM-003` | Test the selected CLI against its simulator | CLI transcript and simulator log | Focused environment available |
| `SIM-004` | Test the same device protocol against physical hardware | Board CLI transcript, transport capture, and hardware record | Physical target and hardware evidence not present |
| `OPS-001` | Test the observable Do No Harm checkpoint after power on | [Commissioning](../scenarios/commissioning.md) startup telemetry and events | Published manual checkpoint available |
| `AUT-001`, `AUT-002` | Test stored sequence execution and LC response initiation | Flight software tests plus SC and LC telemetry | Unit sources, two active integrated responses, and the `CmdTlmStoredCommands.ycs` RTS 12 upload available |
| `EVT-001` | Test rejected commands for each DRM component application | Flight software test output and event telemetry | Unit sources available |
| `CMD-001` | Test command and telemetry exchange through the direct debug path | Draft Link Path Comparison trial record | Focused workflow remains a draft with no scenario that isolates the debug path |
| `CMD-002` | Test command and telemetry exchange through the representative radio path | Draft Link Path Comparison trial record plus `radio-security-testing` and `radio-link-testing` results | Radio scenarios exercise the path headlessly while the focused comparison remains a draft |
| `DAT-001` | Test DS packet selection and persistent file creation | DS table, FM listing, and stored file evidence | Manual steps included in Commissioning |
| `DAT-002` | Test CF transfer of the selected file to YAMCS | CF events, YAMCS transfer record, and received file | Manual steps in Commissioning, with `CheckoutTest.ycs` and `CmdTlmPatches.ycs` asserting end of transaction status and uploaded byte counts for uplink and downlink transfers |
| `FSW-001`, `FSW-002` | Inspect the flight build and application interfaces | Build configuration and Software Bus interface checklist | Source and generated build artifacts available |
| `FSW-003` | Test accepted and rejected command counters for each DRM component application | Flight software test output and housekeeping telemetry | Unit sources available |
| `ADCS-001`, `ADCS-002`, `ADCS-003`, `ADCS-004` | Test modes, targets, housekeeping, and 42 actuation | ADCS simulator tests plus the executable `adcs-truth-verification` scenario and the other six `adcs-*` stack scenarios | Unit sources and executable scenarios available without a record traced to each requirement |
| `DEMO-001`, `DEMO-002` | Test Sun vector encoding and enabled publication behavior | Demo simulator tests and component telemetry | Unit sources and component procedure available |
| `EPS-001`, `EPS-002`, `EPS-003` | Test energy behavior, eight switch commands, and telemetry | EPS simulator tests plus the executable `eps-functional` and `eps-backdoor-testing` scenarios | Unit sources and executable scenarios available without a record traced to each requirement |
| `COM-001`, `COM-002`, `COM-003` | Test Radio modes plus mode gated uplink and downlink | Radio simulator tests plus the `radio-link-testing` scenario and a draft Link Path Comparison record | Unit sources and an executable link model scenario available while path comparison remains a draft |
| `CRYPTO-001`, `CRYPTO-002` | Test command and telemetry processing with the declared security association | `radio-security-testing` stack and log assertions, CryptoLib unit tests, and Radio events | Executable scenario and unit tests present with built in test keys, no CI job for the CryptoLib tests, and no record traced to these requirements |
| `GND-001`, `GND-002`, `GND-003`, `GND-004`, `GND-005` | Test commanding, telemetry, archive retrieval, and stack execution | YAMCS test output plus ground operation records | Ground features present while CI omits the YAMCS tests and commander verify tests |
| `GND-006` | Inspect the checked in DRM timeline bands | Timeline configuration checklist | Timeline bands present without an inspection record |
| `DOC-001`, `DOC-002` | Inspect every published scenario and maturity statement | Atlas checklist and `make docs-check` output | Automated format checks available with content inspection still required |

## Current verification gaps

No checked in VCRM maps all 45 requirements to controlled results.
No scenario, campaign, performance run, YAMCS test, or tool test runs in CI.
Executable stacks assert many behaviors but none is traced to a requirement ID.
No stack covers nominal operations, a DS produced file selected from an FM listing, or contingency response end to end.
The CryptoLib setup uses built in test keys and does not establish an operational security implementation.
No checked in evidence demonstrates `SIM-004` or `DRM-005` with a physical component, and processor in the loop runs do not qualify peripherals.
No independent review record approves requirement dispositions.

These gaps do not mean that the implementation fails the requirements.
They mean that the repository does not yet contain sufficient evidence to make a verification decision.

## Recommended verification sequence

1. Freeze the requirement revision and active DRM configuration for the verification cycle.
2. Define an objective acceptance criterion and evidence location for every requirement.
3. Execute the applicable Simulith, component simulator, flight software, YAMCS, and Atlas checks.
4. Run the CLI and simulator activities for each selected component protocol.
5. Execute focused cFS and simulator integration checks for the component applications.
6. Execute controlled DRM scenarios with `make scenario` for startup, link paths, data lifecycle, and configured fault responses.
7. Repeat `SIM-004` and `DRM-005` activities on the declared target processor and physical component.
8. Populate the VCRM with results, evidence links, anomalies, and dispositions.
9. Obtain owner and independent reviewer concurrence for each verification decision.

## Validation plan

Validation reviews the consistency of stakeholder objectives, scenarios, requirements, and design throughout development.
Final validation uses representative workflows after enough lower level verification is complete.

| Objective | Validation question | Representative activity | Current boundary |
| --- | --- | --- | --- |
| `OBJ-001` | Is the integrated DRM useful as a software reference environment? | [Commissioning](../scenarios/commissioning.md) followed by draft Nominal Operations | Published manual workflow without a complete validation record |
| `OBJ-002` | Can a developer observe and control the selected spacecraft through the intended paths? | Commissioning followed by draft Link Path Comparison | Current debug and representative radio paths only |
| `OBJ-003` | Does the data handling example represent a useful onboard to ground lifecycle? | Draft Data Lifecycle scenario | Selected DS telemetry file rather than a payload science product |
| `OBJ-004` | Does configured detection and response behavior aid fault management development? | Draft FDIR scenario using action points 5 and 11 and `DemoFDIR.ycs` | Focused examples without general spacecraft safing |
| `OBJ-005` | Does the development progression reduce risk before and after hardware arrival? | Draft Component Hardware Development stages 0 through 6 | Simulator stages and processor in the loop documented without component hardware evidence |

An objective is validated only when the stakeholder accepts the representative result for the intended use.
A requirement can pass verification while its parent objective still fails validation.

## Completion criteria

A requirement may be marked verified only when:

* its approved statement and attributes are unchanged from the executed revision
* the assigned method and verification level were used
* acceptance criteria were approved before execution
* retained evidence supports the pass decision
* anomalies and deviations have an approved disposition
* the owner and independent reviewer concur
* the VCRM links the requirement to its evidence record

A stakeholder objective may be marked validated only when its representative workflow is complete and stakeholder acceptance is recorded.

## Related material

Use the [Mission Requirements](mission-requirements.md) page for requirement statements, parents, methods, levels, and owner roles.
Use the [Concept of Operations](concept-of-operations.md) page for the current operational baseline and limitations.
Use the [Scenarios](../scenarios/overview.md) section for representative workflows and evidence guidance.

***
Last reviewed: 20261027
