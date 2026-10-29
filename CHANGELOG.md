# Changelog

All notable changes to SHIRE are documented here.

This project follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Version numbers follow MAJOR.MINOR.PATCH:
- **MAJOR** — breaking changes to the component API, build system interface, or submodule structure
- **MINOR** — new components, new scenarios, new capabilities, or deprecation notices
- **PATCH** — bug fixes, documentation corrections, security patches, and dependency updates

No release has been tagged yet, so every change below is listed under Unreleased.
The container images that the Makefile and CI use carry the placeholder tag `0.0.0`.

## [Unreleased]

### Added
- Power and Eclipse lesson, the third in the Atlas lesson plan, with its screenshot manifest and scripted screenshots
- Simulation Time and Repeatability lesson, the second in the Atlas lesson plan, with its screenshot manifest and scripted screenshots
- `ServerPauseResume.ycs` in the `checkout` scenario, which holds a 12 second pause and proves the clock stood still and then resumed, with `unchanged` and `advances` conditions in `yamcs_commander.py`
- Atlas lesson plan: the Scenarios overview arranges the scenarios in five units with a shared mission story, a lesson page template enforced by `check_docs.py`, and a guide to writing a lesson
- Host Python requirements in `tools/requirements-host.txt` for `make scenario`, `make campaign`, and the screenshot check
- Mission brief, limitations, and review sections with a checkpoint and a link to the next lesson on the Commissioning and Cmd/Tlm Validation pages
- Submodule workflow documentation in the Atlas Getting Started and Installation guides
- Dependabot configuration for GitHub Actions, Docker, pip, and Maven dependency updates
- Issue and pull request templates under `.github/`
- Atlas documentation site with `atlas/scripts/check_docs.py` content checks and a strict build in the documentation workflow
- Code coverage for flight software, component simulators, and the Simulith core uploaded to Codecov, plus a cyclomatic complexity report (`make complexity`)
- Processor-in-the-loop (PIL) targets `make board-prepare`, `board-connect`, `board-inspect`, `board-build`, `board-deploy`, `board-start`, `board-stop`, `board-scenario`, and `board-test` with board profiles in `cfg/boards/` for a Zynq QEMU guest, a Zybo 7020, and a Raspberry Pi 5
- Monte Carlo campaigns (`make campaign`) with campaign definitions in `cfg/drm/campaigns/`
- Synchronized performance tooling (`make perf-smoke`, `make perf`, `make perf-compare`) and CPU placement for the simulation processes
- Deterministic scenario initialization through Initial Condition bins in `cfg/drm/initial_conditions/` with a per-run snapshot
- Headless `make scenario SCENARIO=<name>` and `make scenario-smoke`, with 17 DRM scenarios that run YAMCS stacks through `verify_stacks` and container log assertions through `log_assertions`
- Replay archives per run (`make replay-list`, `make replay`, `make replay-delete`) and a Cesium based spacecraft visualization served by YAMCS
- ADCS expansion with GPS time, a time file fallback, Sun point rotisserie roll, target tracking, and component stacks for each
- EPS expansion with eight switched loads wired to the Demo, ADCS, and Radio simulators, EPS backdoors, a fault injection stack, and EPS campaigns
- Radio link model with ground station visibility from `cfg/drm/ground_stations.yaml`, one-way delay, and packet loss, controlled through Radio backdoors
- Radio link security with AES-256-GCM through CryptoLib by default, backdoors that corrupt or replay packets, and the `radio-security-testing` scenario
- Yellow and red telemetry limits configured under `limits:` in `cfg/drm/drm.yaml` and resolved into the YAMCS mission database under `build/<mission>/scenario/mdb`
- `/CCSDS/CFDP_UPLOAD` command for uploading a file from the YAMCS `cfdpUp` bucket to `/d`
- Uploadable flight files built from `cfg/shire_defs/tables/up_*.c` and `comp/eps`, namely RTS 12, a deliberately corrupt RTS 13, an LC watchpoint table with the uplink-only watchpoint 12, and the `up_eps` application patch
- Cmd/Tlm Validation scenario with the `CmdTlmLimits.ycs`, `CmdTlmStoredCommands.ycs`, and `CmdTlmPatches.ycs` stacks
- Scripted scenario screenshot capture (`make screenshots-setup`, `make screenshots`, `make screenshots-check`)
- Apple Silicon (arm64) support for the SHIRE build image and component CLI builds

### Changed
- The EPS Load Switching scenario draft moved to the EPS Validation how-to guide, because the Power and Eclipse lesson replaces it as the scenario page
- All container images moved to the `ghcr.io/voyagertechnologies-public/` registry
- `yamcs/` is wired in as the `external-yamcs` submodule
- External vendored directories converted to git submodules (`42`, `cfs/cfe`, `cfs/osal`, `cfs/psp`, `cfs/tools/elf2cfetbl`, nine cFS apps, `yamcs`, and `comp/cryptolib`)
- Pinned Python dependencies live in `tools/requirements.txt`

### Fixed
- Pausing the simulation for more than about 5 seconds stopped 42 and the Director, because 42 gave up waiting for the next command batch, and 42 now keeps waiting while the Director holds its lock file and still stops if the Director exits
- LC watchpoint table corrections that accompany the Cmd/Tlm Validation scenario
- PSP retires the deliveries of exiting tasks, releases the tick mutex when a task is cancelled, and reports stalls by elapsed time so that application reload works in the synchronized simulation
- Target tracking quaternion scalar order in the ADCS simulator
