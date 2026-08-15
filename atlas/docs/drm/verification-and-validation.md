# Verification and Validation

This page separates verification mechanisms present in the repository from verification work that a mission still needs to define and execute.

## Current evidence entry points

| Area | Repository evidence or command | What it can establish |
| --- | --- | --- |
| Simulith core | `simulith/test/` with `cd simulith && make test` | Unit behavior for Server, client, time, transport, and 42 adapter code included by the test build |
| Component simulators | `comp/<name>/test-sim/` with `make test-sim` | Simulator lifecycle, protocol, selected dynamics behavior, and combined simulator coverage for configured components |
| cFS and component apps | cFS and app unit tests with `make test-fsw` | Unit behavior and LCOV coverage for the configured cFS test build |
| YAMCS | `cd yamcs && make test` | Tests supplied by the YAMCS submodule build |
| Focused integration | `make cli`, then `make cli-start` | Manual component protocol and simulator checkout without cFS/GSW |
| Full integration | `make`, then `make start` | Manual observation of the configured flight, ground, security, dynamics, and simulation stack |
| Operator procedures | `comp/*/gsw/procedures/*.ycs` | Repeatable YAMCS steps when the procedure version, configuration, inputs, and results are retained |
| Repository CI | `.github/workflows/ci.yml` | On pull requests and pushes to `main` or `dev`, defines Simulith, FSW, and CLI builds plus FSW and component simulator test jobs |

The CI test jobs upload FSW and simulator coverage to Codecov.
`.github/workflows/docs.yml` validates the Atlas on pull requests and pushes to `main` or `dev`, then publishes only a successful `main` build.
A workflow definition is not itself evidence that a particular revision passed.
Retain the GitHub Actions run, job logs, coverage result, and exact revision when using CI as verification evidence.
The current CI does not run the YAMCS submodule test target or a complete integrated lab scenario.

## Evidence requirements

For a result to support a DRM requirement, retain at least:

* the requirement ID and verification method
* repository and submodule revisions
* `build/active.yaml` and the relevant merged and generated configuration
* host and container image information that can affect the result
* exact test command or procedure revision
* inputs, expected result, actual result, and pass or fail decision
* logs, telemetry, coverage, analysis, or inspection output needed to reproduce the decision.

Coverage is useful development evidence but does not prove requirements compliance.
A successful commissioning walkthrough demonstrates one configured path but does not verify every fault case, performance limit, or hardware target.

## DRM verification status

The [Mission Requirements](mission-requirements.md) page is currently a proposed requirements baseline.
It does not yet contain a verification cross reference matrix with executed evidence for every requirement.
Until that matrix exists, a listed verification method (`T`, `A`, `I`, or `D`) describes the intended method, not a completed result.

Several requirements need particular care:

* physical launch survival, lifetime, storage, radio rate, and processor performance require defined hardware, configuration, and analysis or test evidence
* pointing, autonomy, and fault management requirements require controlled scenarios and quantitative acceptance limits
* cryptographic requirements require an explicit security configuration and test scope
* claims involving physical hardware require physical interfaces and recorded target results
* claims about speeds above real time require a benchmark definition, host description, workload, and observed achieved rate.

## Recommended progression

1. Test protocol and application logic at the unit level.
2. Test each component simulator and its 42 coupling where applicable.
3. Use the CLI environment for focused interface checkout.
4. Run the full stack and retain command/telemetry evidence.
5. Add nominal and contingency automated procedures with explicit assertions.
6. Repeat applicable tests on target processors and physical hardware.
7. Populate a verification cross reference matrix linking every requirement to executed evidence or an approved rationale.

Validation should then use representative operator workflows to show that the configured DRM satisfies its intended use, not merely that individual requirements passed in isolation.
