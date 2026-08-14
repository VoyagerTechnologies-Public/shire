# Verification & Validation

## Overview

This short Verification and Validation (V&V) plan identifies the activities that will establish compliance with the requirements (verification) and to establish that the system will meet the customers’ expectations (validation) for the SHIRE Design Reference Mission (DRM).
It summarizes how a DRM can leverage SHIRE and standard engineering practices (unit testing, CI/CD, coverage, simulation, and hardware-in-the-loop) to verify requirements and validate mission behavior.

## Verification Activities

- Requirements traceability: maintain a VCRM linking each DRM requirement to at least one verification artifact (test, analysis, or inspection).
- Unit testing & code quality: per-application cFS unit tests (ut-assert), static analysis, and code coverage reporting to ensure implementation correctness and measurable coverage targets.
- CI/CD automation: run unit tests, linting, and a subset of integration checks on pull requests; run nightly or scheduled full regressions in the CI pipeline.
- CLI + simulator checks: repeatable command-line driven component checkouts using SHIRE simulators to validate interfaces, telemetry, and functional behavior without hardware.
- Hardware-in-the-loop (HITL): targeted CLI tests run against physical devices (IMU, wheels, PDB, RF modules) to verify timing, currents, and I/O interactions when hardware is available.
- Integration & system scripts: compact end-to-end FSW scenarios (commissioning, nominal day, science ops, CFDP transfers, fault responses) executed in the full SHIRE stack to validate operational behavior.
- FDIR verification: scenario-based fault injection and monitoring to verify fault detection, isolation, and recovery behaviors.
- Performance and resource testing: power, CPU, memory, and timing stress tests under realistic simulated profiles.
- Security verification: exercise CryptoLib and security-related behaviors (key management, authorized command enforcement) using simulated and hardware-assisted checks where applicable.

## Validation Activities

- Operational scenario validation: run representative mission scenarios in SHIRE to demonstrate operator procedures, expected operator-in-the-loop actions, and mission outcomes.
- End-user acceptance: provide demonstration runs (recordings, telemetry extracts) for stakeholders to validate that the DRM meets customer expectations.
- Data integrity validation: verify CFDP and file management flows for end-to-end data integrity (checksums, file locations, and archival behaviors).

## Test artifacts & locations

- Tests and scripts: atlas/tests/ (unit | cli | hardware | fsw)
- Coverage reports: build/coverage/ (per-app)
- Logs and telemetry: build/test-logs/ or CI artifacts

## Acceptance criteria (summary)

- Tier 1: unit tests pass and per-app coverage meets targets.
- Tier 2: CLI+simulator suites pass for all simulated components.
- Tier 3: applicable hardware tests pass when devices are available.
- Tier 4: core FSW scenarios (commissioning, nominal, science, fault response) execute successfully in SHIRE.
- VCRM: every DRM requirement has at least one verification entry or a documented rationale for exception.

## How missions should use SHIRE for V&V

- Start with unit tests and static analysis locally and in CI.
- Run CLI+simulator checks for interface verification and developer debugging.
- Add hardware tests as devices arrive, focusing on timing and I/O.
- Use compact FSW scenario scripts to validate operator procedures and mission-level behavior.

## Roles & responsibilities (brief)

- Developers: unit-tests, component CLI tests, and coverage targets.
- Integration leads: manage simulators, system tests, and VCRM updates.
- Operations/QA: run scenario validations, accept results, and log issues.

---
Last updated: 20251218
