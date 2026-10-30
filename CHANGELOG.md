# Changelog

All notable changes to SHIRE are documented here.

This project follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Version numbers follow MAJOR.MINOR.PATCH:
- **MAJOR** — breaking changes to the component API, build system interface, or submodule structure
- **MINOR** — new components, new scenarios, new capabilities, or deprecation notices
- **PATCH** — bug fixes, documentation corrections, security patches, and dependency updates

No release has been tagged yet, so everything below is listed under Unreleased and describes SHIRE as it stands today.
The container images that the Makefile and CI use carry the placeholder tag `0.0.0`.

## [Unreleased]

### Added

- **Synchronized simulation (Simulith)**
  - A deterministic simulation clock that every participant follows in prepare, execute, and commit phases, with pause, play, and speed control, including long pauses
  - A Director that steps the component simulators and the 42 dynamics simulator together, with a control trace for repeatability checks
  - Initial Condition bins in `cfg/drm/initial_conditions/` (`nominal-baseline`, `eclipse-entry`, `eclipse-exit`) and a per-run snapshot, so a run can be repeated exactly
- **Flight software**
  - cFS (cFE, OSAL, PSP) with a SHIRE platform layer that runs cFS on simulation time, with the CF, DS, FM, LC, SC, SCH, CI_LAB, TO_LAB, and IO_LIB applications
  - Mission tables for limit checking, stored commands, data storage, file transfer, scheduling, and telemetry subscriptions
  - Applications can be reloaded while simulation time is synchronized
- **Components**, each with a flight application, a simulator, a command line tool, ground definitions, YAMCS stacks, and unit tests
  - ADCS with B-dot detumble, Sun pointing, target tracking, GPS and file based time, and a truth comparison against 42
  - EPS with eight switched loads wired to the Demo, ADCS, and Radio simulators, backdoors, and fault injection
  - Radio with ground station visibility, one-way delay, packet loss, and link security through CryptoLib (AES-256-GCM) with backdoors that corrupt or replay packets
  - Demo reference component, and `make mold COMP=<name>` to create a new component from it
- **Ground software (YAMCS)**
  - Mission database, displays, and stacks for every component, CFDP file transfer, and a choice between the debug and radio command paths
  - Yellow and red telemetry limits set under `limits:` in the mission, spacecraft, or scenario configuration and applied as alarm ranges
  - Upload of files from the `cfdpUp` bucket to `/d` with `/CCSDS/CFDP_UPLOAD`, used for stored command tables, a limit checker table, and an application patch
  - Power and Attitude timeline views, a Cesium based live visualization, and a replay archive for every run (`make replay-list`, `replay`, `replay-delete`)
- **Scenarios and automation**
  - `make scenario SCENARIO=<name>` runs a scenario headless and confirms a clean pass from its YAMCS stacks and container log assertions, and `make scenario-smoke` confirms an exact repeat
  - 20 Design Reference Mission scenarios covering checkout, ADCS, EPS, Radio link and security, Cmd/Tlm validation, fault detection and recovery, and a complete nominal pass
  - Monte Carlo campaigns with `make campaign CAMPAIGN=<name>` and seven campaign definitions in `cfg/drm/campaigns/`
  - Synchronized performance tooling (`make perf-smoke`, `perf`, `perf-compare`) and CPU placement for the simulation processes
  - Scripted documentation screenshots (`make screenshots-setup`, `screenshots`, `screenshots-check`)
- **Processor in the loop**
  - Board targets `make board-prepare`, `board-build`, `board-deploy`, `board-inspect`, `board-start`, `board-connect`, `board-test`, `board-scenario`, and `board-stop`, with profiles in `cfg/boards/`
  - `qemu-zynq`, an open source Zynq ARM32 Linux guest in QEMU, `zybo7020`, a physical Zybo Z7, and `rpi5`, a physical Raspberry Pi 5 with AArch64 Linux
  - Acceptance runs on the physical `zybo7020` and `rpi5` boards, covering the processor with simulated devices
- **Documentation (Atlas)**
  - A documentation site with concepts, how-to guides, a quick reference, a glossary, and the Design Reference Mission documents, checked by `atlas/scripts/check_docs.py` and built strictly in the documentation workflow
  - A 12 lesson plan for new space operations users, from Commissioning through Processor in the Loop, with a guide to writing a lesson
- **Build, test, and repository**
  - Docker based build on Linux, Windows 11 with WSL 2, and Apple Silicon, with images under `ghcr.io/voyagertechnologies-public/`
  - Flight software, component simulator, and Simulith unit tests with coverage uploaded to Codecov, a cyclomatic complexity report (`make complexity`), and a CI job for the Python tools, the YAMCS commander, and the YAMCS Java checks
  - External software as git submodules (`42`, cFE, OSAL, PSP, `elf2cfetbl`, nine cFS applications, `yamcs`, and CryptoLib), with a submodule workflow in the Atlas guides
  - `SBOM.md`, `sbom.cdx.json`, and `THIRD_PARTY_NOTICES.md` for licenses and provenance, Dependabot for GitHub Actions, Docker, pip, and Maven, and issue and pull request templates

### Known limitations

- Processor in the loop qualifies the processor with simulated devices, and no profile qualifies a physical component bus
- The second CPU target is scaffolded, disabled, and unvalidated
- Link security uses built in development keys, which are an example and not flight keys
