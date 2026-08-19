# Verification and Validation

> **Status:** This page defines the proposed verification and validation approach for the Design Reference Mission.
> It identifies current evidence sources and remaining gaps.
> It does not mark any requirement as verified or any stakeholder objective as validated.

## Purpose

Verification determines whether the DRM satisfies each `shall` statement in the [Mission Requirements](mission-requirements.md).
Validation determines whether the verified DRM satisfies the `OBJ-*` stakeholder objectives and its intended use.

The Mission Requirements page assigns one planned method, verification level, and owner role to every requirement.
This page defines how those assignments become controlled evidence.
A future verification cross reference matrix must record the result for each individual requirement.

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
| Simulith core | `cd simulith && make test` | Server, client, timing, transport, and 42 adapter unit behavior |
| Component simulators | `make test-sim` | Selected simulator lifecycle, protocol, and dynamics behavior |
| Flight software | `make test-fsw` | Configured cFS and component application unit behavior |
| YAMCS | `cd yamcs && make test` | Tests supplied by the YAMCS submodule build |
| Focused component integration | `make cli` followed by `make cli-start` | CLI and simulator protocol checkout without cFS or YAMCS |
| DRM integration | `make` followed by `make start` | Manual observation of the generated flight, ground, security, dynamics, and simulation stack |
| Component procedures | `comp/*/gsw/procedures/*.ycs` | Repeatable YAMCS component command sequences |
| DRM procedures | `yamcs/src/main/yamcs/procedures/*.ycs` | Repeatable YAMCS system command sequences |
| Atlas scenarios | `atlas/docs/scenarios/` | Manual workflows, expected results, limitations, and evidence guidance |
| Atlas validation | `make docs-check` | Markdown, asset, procedure, command reference, and strict site build checks |
| Repository CI | `.github/workflows/ci.yml` | Simulith, flight software, and CLI builds plus flight software and component simulator tests |

The CI test jobs upload flight software and simulator coverage to Codecov.
The current CI does not run the Simulith core test target, the YAMCS submodule test target, or a complete DRM scenario.
The documentation workflow validates the Atlas on pull requests and pushes to `main` or `dev`.
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
Both current command output links consume `tc_realtime` when enabled, so an unrecorded path selection can invalidate command count evidence.

## Planned verification by requirement group

The following table identifies the activity needed to close each portion of the current baseline.
An available source means that relevant code, tests, or a manual scenario exist.
It does not mean the requirement has passed verification.

| Requirement IDs | Planned activity | Primary evidence | Current boundary |
| --- | --- | --- | --- |
| `DRM-001` | Execute a controlled integrated DRM demonstration | [Commissioning](../scenarios/commissioning.md) and [Nominal Operations](../scenarios/nominal-operations.md) records | Manual workflows available without one procedure containing complete assertions |
| `DRM-002` | Demonstrate command and telemetry exchange through each declared path | [Commissioning](../scenarios/commissioning.md) and [Link Path Comparison](../scenarios/link-path-comparison.md) records | Manual workflows available with duplicate path risk |
| `DRM-003` | Demonstrate one complete stored file transfer to YAMCS | [Data Lifecycle](../scenarios/data-lifecycle.md) record and received file | Manual workflow available without automated integrity assertions |
| `DRM-004` | Demonstrate an active LC condition reaching its configured response | [FDIR](../scenarios/fdir.md) record with LC, SC, event, and state telemetry | Implemented action points 5 and 11 only |
| `DRM-005` | Demonstrate one component through simulator and physical hardware transports | [Component Hardware Development](../scenarios/component-hardware-development.md) evidence packet | Physical target and hardware evidence not present |
| `CFG-001` | Test configuration generation for a declared selection | Active configuration and orchestrator output | Build path available without a test record traced to `CFG-001` |
| `CFG-002`, `CFG-003` | Inspect generated cFS and Simulith content against the spacecraft selection | Generated startup and simulator configuration checklist | Generated artifacts available |
| `SIM-001`, `SIM-002` | Test 42 propagation and shared Simulith context during a controlled run | Simulith tests, 42 truth, and component observations | Unit sources available with integrated acceptance work remaining |
| `SIM-003` | Test the selected CLI against its simulator | CLI transcript and simulator log | Focused environment available |
| `SIM-004` | Test the same device protocol against physical hardware | Board CLI transcript, transport capture, and hardware record | Physical target and hardware evidence not present |
| `OPS-001` | Test the observable Do No Harm checkpoint after power on | [Commissioning](../scenarios/commissioning.md) startup telemetry and events | Manual checkpoint available |
| `AUT-001`, `AUT-002` | Test stored sequence execution and LC response initiation | Flight software tests plus SC and LC telemetry | Unit sources and two active integrated responses available |
| `EVT-001` | Test rejected commands for each DRM component application | Flight software test output and event telemetry | Unit sources available |
| `CMD-001` | Test command and telemetry exchange through the direct debug path | [Link Path Comparison](../scenarios/link-path-comparison.md) trial record | Manual workflow available |
| `CMD-002` | Test command and telemetry exchange through the representative radio path | [Link Path Comparison](../scenarios/link-path-comparison.md) trial record | Manual workflow available |
| `DAT-001` | Test DS packet selection and persistent file creation | DS table, FM listing, and stored file evidence | [Data Lifecycle](../scenarios/data-lifecycle.md) workflow available |
| `DAT-002` | Test CF transfer of the selected file to YAMCS | CF events, YAMCS transfer record, and received file | [Data Lifecycle](../scenarios/data-lifecycle.md) workflow available |
| `FSW-001`, `FSW-002` | Inspect the flight build and application interfaces | Build configuration and Software Bus interface checklist | Source and generated build artifacts available |
| `FSW-003` | Test accepted and rejected command counters for each DRM component application | Flight software test output and housekeeping telemetry | Unit sources available |
| `ADCS-001`, `ADCS-002`, `ADCS-003`, `ADCS-004` | Test modes, targets, housekeeping, and 42 actuation | ADCS simulator tests and [ADCS Truth Comparison](../scenarios/adcs-truth-comparison.md) record | Unit sources and manual truth comparison available |
| `DEMO-001`, `DEMO-002` | Test Sun vector encoding and enabled publication behavior | Demo simulator tests and component telemetry | Unit sources and component procedure available |
| `EPS-001`, `EPS-002`, `EPS-003` | Test energy behavior, eight switch commands, and telemetry | EPS simulator tests and [EPS Load Switching](../scenarios/eps-load-switching.md) record | Unit sources and manual switching workflow available |
| `COM-001`, `COM-002`, `COM-003` | Test Radio modes plus mode gated uplink and downlink | Radio simulator tests and [Link Path Comparison](../scenarios/link-path-comparison.md) record | Unit sources and manual path workflow available |
| `CRYPTO-001`, `CRYPTO-002` | Test command and telemetry processing with the declared security association | Frame captures, CryptoLib results, and Radio events | Development configuration present without a security test traced to these requirements |
| `GND-001`, `GND-002`, `GND-003`, `GND-004`, `GND-005` | Test commanding, telemetry, archive retrieval, and stack execution | YAMCS test output plus ground operation records | Ground features present while CI omits YAMCS tests |
| `GND-006` | Inspect the checked in DRM timeline bands | Timeline configuration checklist | Timeline bands present without an inspection record |
| `DOC-001`, `DOC-002` | Inspect every published scenario and maturity statement | Atlas checklist and `make docs-check` output | Automated format checks available with content inspection still required |

## Current verification gaps

No checked in VCRM maps all 45 requirements to controlled results.
No complete DRM scenario runs in CI.
Current YAMCS stacks do not contain complete requirement assertions for commissioning, nominal operations, CFDP, or contingency response.
The CryptoLib setup is a development configuration and does not establish an operational security implementation.
No checked in evidence demonstrates `SIM-004` or `DRM-005` with a physical component.
No independent review record approves requirement dispositions.

These gaps do not mean that the implementation fails the requirements.
They mean that the repository does not yet contain sufficient evidence to make a verification decision.

## Recommended verification sequence

1. Freeze the requirement revision and active DRM configuration for the verification cycle.
2. Define an objective acceptance criterion and evidence location for every requirement.
3. Execute the applicable Simulith, component simulator, flight software, YAMCS, and Atlas checks.
4. Run the CLI and simulator activities for each selected component protocol.
5. Execute focused cFS and simulator integration checks for the component applications.
6. Execute controlled DRM scenarios for startup, link paths, data lifecycle, and configured fault responses.
7. Repeat `SIM-004` and `DRM-005` activities on the declared target processor and physical component.
8. Populate the VCRM with results, evidence links, anomalies, and dispositions.
9. Obtain owner and independent reviewer concurrence for each verification decision.

## Validation plan

Validation reviews the consistency of stakeholder objectives, scenarios, requirements, and design throughout development.
Final validation uses representative workflows after enough lower level verification is complete.

| Objective | Validation question | Representative activity | Current boundary |
| --- | --- | --- | --- |
| `OBJ-001` | Is the integrated DRM useful as a software reference environment? | [Commissioning](../scenarios/commissioning.md) followed by [Nominal Operations](../scenarios/nominal-operations.md) | Manual workflows without a complete validation record |
| `OBJ-002` | Can a developer observe and control the selected spacecraft through the intended paths? | [Commissioning](../scenarios/commissioning.md) and [Link Path Comparison](../scenarios/link-path-comparison.md) | Current debug and representative radio paths only |
| `OBJ-003` | Does the data handling example represent a useful onboard to ground lifecycle? | [Data Lifecycle](../scenarios/data-lifecycle.md) scenario | Selected DS telemetry file rather than a payload science product |
| `OBJ-004` | Does configured detection and response behavior aid fault management development? | [FDIR](../scenarios/fdir.md) scenario using action points 5 and 11 | Focused examples without general spacecraft safing |
| `OBJ-005` | Does the development progression reduce risk before and after hardware arrival? | [Component Hardware Development](../scenarios/component-hardware-development.md) stages 0 through 6 | Simulator stages documented without physical hardware evidence |

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
Last reviewed: 20260819
