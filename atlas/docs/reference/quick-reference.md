# Quick Reference

This page collects the commands, targets, endpoints, and source locations most often needed while working with the default DRM.
Run `make cfg` after changing `build/active.yaml`.
`make list` also regenerates configuration before reporting the resolved selection.

## Common commands

| Command | Purpose |
| --- | --- |
| `make help` | List the supported top level targets. |
| `make cfg` | Resolve the active configuration and render generated artifacts. |
| `make list` | Report the resolved mission, spacecraft, scenario, and component build features. |
| `make` or `make build` | Configure SHIRE and build simulation, FSW, and GSW runtime images. |
| `make 42` | Configure SHIRE and build only the 42 runtime image. |
| `make sim` | Configure SHIRE and build 42, Simulith, the selected component simulators, Director, and Server. |
| `make fsw` | Configure SHIRE and build the selected cFS target and FSW runtime image. |
| `make board-prepare BOARD=<profile>` | Build the QEMU platform or configured board SDK/userspace, or check supplied inputs. |
| `make board-connect BOARD=<profile>` | Establish hardware SSH or open the running QEMU guest shell. |
| `make board-inspect BOARD=<profile>` | Inspect board Linux over SSH, or local QEMU platform/bundle artifacts. |
| `make board-build BOARD=<profile>` | Build the host simulation and ground images, then cross-build the cFS bundle with the selected SDK. |
| `make board-deploy BOARD=<profile>` | Upload the hardware runtime or package the QEMU guest image and Compose. |
| `make board-scenario SCENARIO=<name> BOARD=<profile>` | Build/deploy target cFS and run a named scenario with host simulators and Yamcs verification. |
| `make board-start BOARD=<profile>` | Start physical or emulated cFS with host simulators and Yamcs. |
| `make board-stop BOARD=<profile>` | Stop the selected board lab and retain its archive. |
| `make board-test BOARD=<profile>` | Run repeated full processor PIL acceptance. |
| `make gsw` | Configure SHIRE and build CryptoLib and the YAMCS runtime image. |
| `make start` | Start the existing generated DRM Compose environment attached to the terminal without rebuilding or regenerating it. |
| `make stop` | Stop the generated DRM and CLI environments and remove transient Compose volumes. |
| `make replay-list` | List retained native Yamcs run databases. |
| `make replay RUN=<run-id> PORT=<port>` | Open one retained database without starting the simulation. |
| `make replay-delete RUN=<run-id>` | Explicitly delete one retained Yamcs archive. |
| `make cfg-cli` | Enable debugging for the selected CLI component and render its configuration without building. |
| `make cli` | Enable debugging for the selected CLI component and build 42, Simulith, mission simulators, Director, Server, and the selected CLI image. |
| `make cli-start` | Regenerate normal configuration and start the focused CLI Compose environment attached to the terminal. |
| `make mold COMP=<name>` | Create a component scaffold from the Demo component. |
| `make test-sim` | Clean and run simulator tests selected by the existing `build/build.yaml` snapshot. |
| `make test-simulith` | Clean and run the standalone Simulith core tests. |
| `make test-fsw` | Clean, regenerate configuration, and run the cFS and application tests. |
| `make complexity` | Write the informational cyclomatic complexity report to `build/coverage-complexity.txt` inside the build image. |
| `make perf-smoke` | Build and run one short synchronized diagnostic trial. |
| `make perf` | Build and run the full-output reference, 1x/25x/50x trials, and three >50x unbounded trials. |
| `make perf-compare BASELINE=<report.json>` | Repeat the performance matrix and compare it with an accepted performance report. |
| `make campaign CAMPAIGN=<name> MAX_PARALLEL=<n>` | Run a Monte Carlo campaign's trials and aggregate the results. |
| `make scenario SCENARIO=<name>` | Run a named scenario headlessly, execute its stacks and log assertions, and write the result under `build/scenario-runs/`. |
| `make scenario-smoke` | Build and run the active scenario twice and confirm that the terminal dynamics state, command deliveries, and protocol counts repeat exactly. |
| `make docs-check` | Validate the Atlas content and run a strict production build. |
| `make docs-serve` | Start the local Atlas preview server. |
| `make screenshots-setup` | Install Playwright and headless Chromium for scenario screenshots under `build/`. |
| `make screenshots SCENARIO=<name>` | Start the DRM, replay the scenario's screenshot manifest in a headless browser, and write its images, with optional `ONLY="<shot> <shot>"` to write only named shots and `ATTACH=1` to use a lab that is already running. |
| `make screenshots-check SCENARIO=<name>` | Confirm a scenario's screenshot manifest, page references, and images agree. |
| `make debug` | Open an interactive shell in the SHIRE build image with the repository mounted. |
| `make container` | Build or verify the dependency images that other targets use. |
| `make container-export FILE=<bundle.tar>` | Save the dependency images to one archive for a disconnected host. |
| `make container-import FILE=<bundle.tar>` | Load a dependency image archive and verify it against the checkout. |
| `make container-use-local` | Verify locally present dependency images against the checkout without rebuilding them. |
| `make clean-42`, `make clean-cli`, `make clean-fsw`, `make clean-gsw`, and `make clean-sim` | Clean one build area, which is the 42 image, the CLI components, the FSW tree, the GSW tree, or the simulation components. |
| `make clean` | Stop the environments and remove the active mission build tree, selected runtime artifacts, and matching SHIRE volumes when the build image exists locally. |
| `make clean-cache` | Delete all labeled SHIRE replay archives and pinned run images, prune Docker builder cache, and retain unlabeled legacy GSW volumes. |
| `make uninstall` | Remove SHIRE build artifacts, containers, images, networks, and labeled replay archives through `clean-cache`. |

Run `make cfg` before `make test-sim` when the active selection has changed.
Board commands use `board` from `build/active.yaml` immediately, without requiring `make cfg`.
An explicit `BOARD=<profile>` validates and saves that selection for subsequent board commands.
Other active settings are retained, and a failed operation still leaves the validated board selected.
The fallback is `qemu-zynq` when the file or key is absent.
Direct Python calls follow the same precedence using `--profile` as the override.
Review [Docker](../manual/how-to/docker.md) before using cleanup commands that remove persisted state.

## Default browser endpoints

| Interface | Address |
| --- | --- |
| YAMCS | [http://localhost:8090](http://localhost:8090) |
| Visualization | [http://localhost:8090/visualization/](http://localhost:8090/visualization/) |

The generated DRM Compose file publishes Yamcs on port 8090.

## DRM services

| Compose service | Current responsibility |
| --- | --- |
| `shire-42` | Runs headless 42 dynamics and the environment model. |
| `shire-gsw` | Runs the included YAMCS instance. |
| `shire-server` | Owns Simulith time and coordinates the registered Director and FSW clients. |
| `shire-director` | Loads selected component simulator libraries, exchanges state with 42, and publishes simulator telemetry. |
| `shire-cryptolib` | Runs the standalone CryptoLib process used by the representative ground radio path. |
| `shire-fsw` | Runs the selected cFS target and startup configuration. |

Component simulators run as shared libraries inside `shire-director` rather than as Compose services.

## DRM network endpoints

| Flow | Endpoint |
| --- | --- |
| Debug command | YAMCS to FSW on UDP 1234 |
| Debug telemetry | FSW to YAMCS on UDP 1235 |
| Radio command | YAMCS to CryptoLib on UDP 12345, then to the Radio simulator in Director on UDP 12343 |
| Radio telemetry | Radio simulator in Director to CryptoLib on UDP 12344, then to YAMCS on UDP 12346 |
| Simulator backdoor | YAMCS to Director on UDP 50060 |
| Server pause/play/speed backdoor | YAMCS to Server on UDP 50061 |
| Server status telemetry | Server to YAMCS on UDP 50043 |
| 42 truth | Director to YAMCS on UDP 50042 |
| Visualization truth | Director to YAMCS on UDP 50044 |
| 42 IPC | Director and 42 exchange data through the Unix socket at `/tmp/42_ipc.sock` |

The UDP endpoints remain inside the Compose bridge because the templates do not publish them to the host.
EPS and Radio backdoors use the existing simulator backdoor endpoint.
Radio backdoors control link visibility, delay and packet loss inside the Radio simulator, so they do not change which UDP ports carry the radio path.
EPS displays and stack verification use FSW housekeeping.
Optional EPS simulator diagnostics are console output, with no dedicated Yamcs truth stream.

## DRM selections

| Spacecraft | Selected component simulators |
| --- | --- |
| `flatsat` | `demo`, `eps`, and `radio` |
| `sat-1` | `adcs`, `demo`, `eps`, and `radio` |
| `sat-2` | `adcs`, `eps`, and `radio` |

The spacecraft selection controls which component simulators are built and loaded and which component applications remain in the generated CPU1 startup script.
The current `cfg/shire_defs/targets.cmake` still compiles all four reference component applications.

The DRM provides the 18 scenarios in the following table.
Run one headlessly with `make scenario SCENARIO=<name>`.

| Scenario | What it does |
| --- | --- |
| `nominal` | Applies `debug: false` to every selected component before its device header is rendered and runs no stack. |
| `debug` | Applies `debug: true` to every selected component before its device header is rendered and runs no stack. |
| `checkout` | Runs `CheckoutTest.ycs` and then `ServerPauseResume.ycs` headlessly through `verify_stacks` instead of a person stepping through them. |
| `eclipse-entry-adcs` | Starts the spacecraft at the `eclipse-entry` Initial Condition bin instead of the default launch state. |
| `eclipse-exit-adcs` | Starts the spacecraft at the `eclipse-exit` Initial Condition bin instead of the default launch state. |
| `adcs-bdot-detumble` | Confirms that `BDOT` mode reduces the initial tumble using only the magnetorquers. |
| `adcs-gps-time-sync` | Confirms that cFE TIME becomes valid once ADCS pushes a GPS time sample. |
| `adcs-boot-config` | Confirms that the boot loaded ADCS gains table reaches the device and can be pushed again. |
| `adcs-sunpoint-rotisserie` | Confirms that `SUNSAFE` pointing stays converged while the boot configured rotisserie roll rate holds. |
| `adcs-truth-verification` | Asserts that the settled ADCS Sun vector agrees with the live 42 truth within a tolerance. |
| `adcs-target-track` | Confirms that `TARGET` mode drives the body rates to near zero. |
| `adcs-modes-sweep` | Sweeps `SUNSAFE`, `NADIR`, and `TARGET` and verifies the pointing goal of each. |
| `adcs-time-file-fallback` | Confirms the boot time fallback file, its replacement by GPS time, and its refresh. |
| `eps-functional` | Runs `EpsPowerFunctional.ycs` for switching and device power cycle acceptance. |
| `eps-backdoor-testing` | Runs `EpsBackdoorFunctional.ycs` and `EpsFaultInjection.ycs` for backdoor application and fault recovery. |
| `radio-link-testing` | Runs `RadioLinkBackdoor.ycs` to exercise Radio link visibility, one way delay, and packet loss through backdoors. |
| `radio-security-testing` | Runs `RadioSecurity.ycs` plus CryptoLib log assertions for AES-256-GCM, tamper rejection, and replay rejection. |
| `cmd-tlm-validation` | Runs three stacks that check commanding, telemetry limits, RTS upload and activation, and flight software table and application patches. |

See [Scenarios and initial conditions](../manual/how-to/scenarios.md) for the Initial Condition bin and `verify_stacks` mechanisms.
The default active selection is mission `drm`, spacecraft `sat-1`, scenario `nominal`, and CLI component `demo`.

## cFS targets

| Target | Current state |
| --- | --- |
| CPU1 | Enabled target using `amd64-shire` by default, `armv7l-shire` for ARM32 PIL or `aarch64-shire` for a configured AArch64 SDK target, and `amd64-linux` for unit test builds. |
| CPU2 | Disabled `armv7l-linux` Zybo 7020 scaffold that is not built or validated by the current workflow, while the Zybo 7020 PIL runs as CPU1 with `armv7l-shire`. |

## Generated paths

| Path | Contents |
| --- | --- |
| `build/active.yaml` | Editable active selection. |
| `build/build.yaml` | Merged configuration snapshot. |
| `build/<mission>/shire-compose.yaml` | DRM Compose file. |
| `build/<mission>/cli-compose.yaml` | Focused component CLI compose file. |
| `build/<mission>/42_config/Inp_Sim.txt` | Rendered 42 simulation input. |
| `build/<mission>/<spacecraft>/shire_defs/` | Generated cFS mission definitions and pruned CPU1 startup script. |
| `build/<mission>/<spacecraft>/sim/` | Simulith build output used to package Director and Server. |
| `build/<mission>/<spacecraft>/fsw/` | cFS build and installation output. |
| `build/<mission>/<spacecraft>/comp/<component>/` | Centralized simulator and CLI build output for selected components. |
| `comp/<component>/shared/device_cfg.h` | Ignored device configuration rendered in the source tree for each selected component with a template. |
| `build/<mission>/scenario/mdb` | Copies of mission database parameter types that carry the resolved yellow and red limits. |
| `build/<mission>/scenario/<scenario>.snapshot.yaml` | Scenario name, resolved Initial Condition bin, and git SHA, written on every `make cfg`. |
| `build/<mission>/<instance>/shire-compose.yaml` | A Monte Carlo campaign trial's own DRM Compose file, one numbered subdirectory per trial. |
| `build/scenario-runs/<scenario>-<UTC timestamp>/` | `make scenario`'s `result.json`, container logs, and IC snapshot copy. |
| `build/monte-carlo-runs/<campaign>-<UTC timestamp>/` | `make campaign`'s per-trial artifacts, `campaign_report.json`, and `campaign_dataset.jsonl`. |
| `cfg/drm/initial_conditions/<name>.yaml` | A reusable Initial Condition bin: orbit, epoch, attitude, and optional per-component state. |
| `cfg/drm/campaigns/<name>.yaml` | A Monte Carlo campaign definition: swept parameters, distributions, and metrics to extract. |

## Performance reports

See [Synchronized Simulation Performance](../manual/how-to/performance.md) for
the complete workload, acceptance criteria, measurements, and regression
workflow.

The performance harness uses a 75 simulated second active workload by default.
It injects ADCS ENABLE and SUNSAFE through CI_LAB at fixed commit sequences and
requires nonzero actuator commands to reach 42.
Generated artifacts default to
`build/performance/shire-perf-<UTC timestamp>/` so profiles and reports remain
host-visible without modifying tracked source.
Each command prints a high-level terminal summary and the path to the detailed
JSON report when it finishes.

The standalone Simulith Server accepts:

```text
simulith_server_standalone [clients] [--speed FACTOR|max]
    [--duration SECONDS] [--warmup SECONDS] [--metrics-json PATH]
```

The positional client count and interactive `p`, `+`, and `-` controls remain
available.
The performance report schema includes configuration identity, revisions, actual build
provenance, timestamped resource samples, phase and FSW participant latency,
device transactions, scenario
results, deterministic command counts, explicitly accounted wall-clock output,
command queue errors, and terminal-state fidelity.
Use a reviewed report as `BASELINE` rather than copying only its median speed.

## Sources of truth

| Question | Current source |
| --- | --- |
| What can be selected? | `cfg/shire-config.yaml` and `cfg/<mission>/<mission>.yaml` |
| What is selected for the next run? | `build/active.yaml` |
| What was resolved most recently? | `build/build.yaml` |
| Which components are on a spacecraft? | `cfg/<mission>/spacecraft/<spacecraft>.yaml` |
| Which values does a scenario override? | `cfg/<mission>/scenarios/<scenario>.yaml` |
| Which telemetry limits apply? | The `limits:` section of `cfg/drm/drm.yaml`, with spacecraft and scenario overrides resolved into `build/<mission>/scenario/mdb` |
| Which ground stations does the Radio link model use? | `cfg/drm/ground_stations.yaml` and the Radio `ground_stations` setting in `comp/radio/support/device_config.yaml` |
| Which tables and patches are built for uplink? | `cfg/shire_defs/tables/up_*.c` registered in `cfg/shire_defs/cpu1/install_custom.cmake`, plus `up_eps` in `comp/eps/CMakeLists.txt` |
| Which services will start? | The generated `build/<mission>/shire-compose.yaml` or `build/<mission>/cli-compose.yaml` |
| How are Compose files generated? | `tools/shire-orchestrator.py` renders `cfg/shire-compose.j2` and `cfg/cli-compose.j2` |
| Which applications compile and start? | `cfg/shire_defs/targets.cmake` and the generated CPU startup script |
| Which ground links exist? | `yamcs/src/main/yamcs/etc/yamcs.shire.yaml`, `comp/cryptolib/support/standalone/standalone.h`, `comp/radio/support/device_config.yaml`, `simulith/include/simulith_director.h`, and `simulith/src/simulith_director.c` |
| Which procedures exist? | `cfg/<mission>/gsw/procedures/` (DRM-level) and `comp/<name>/gsw/procedures/` (component-level), both staged into the `yamcs/` submodule at build time |
| Which developer commands are supported? | The root `Makefile` and subsystem Makefiles |
| Where do shared executable utilities live? | `tools/`, including Python helpers and tests, shell utilities, requirements, and Docker build recipes |
| Where do the cFS mission coverage tests live? | `cfg/shire_defs/coverage-tests/` |
| Which performance workload is injected? | `cfg/perf-scenario.json` |
| How is a performance report produced and evaluated? | `tools/shire-perf.py` |
| How is a Monte Carlo campaign defined and run? | `cfg/drm/campaigns/<name>.yaml` and `tools/shire-campaign.py` |

Treat this page as a convenience index.
When it disagrees with a generated file or source listed above, inspect the current checkout and report the documentation mismatch.

***
Last reviewed: 20261027
