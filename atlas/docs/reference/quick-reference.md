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
| `make gsw` | Configure SHIRE and build CryptoLib and the YAMCS runtime image. |
| `make start` | Start the existing generated DRM Compose environment attached to the terminal without rebuilding or regenerating it. |
| `make stop` | Stop the generated DRM and CLI environments for the active mission and remove dangling Docker images. |
| `make cli` | Regenerate with CLI debugging enabled and build 42, Simulith, the selected simulator, Director, Server, and focused CLI image. |
| `make cli-start` | Regenerate normal configuration and start the focused CLI Compose environment attached to the terminal. |
| `make mold COMP=<name>` | Create a component scaffold from the Demo component. |
| `make test-sim` | Clean and run simulator tests selected by the existing `build/build.yaml` snapshot. |
| `make test-fsw` | Clean, regenerate configuration, and run the cFS and application tests. |
| `make docs-check` | Validate the Atlas content and run a strict production build. |
| `make docs-serve` | Start the local Atlas preview server. |
| `make debug` | Open an interactive shell in the SHIRE build image with the repository mounted. |
| `make clean` | Stop the environments and remove the active mission build tree, selected runtime artifacts, and matching SHIRE volumes when the build image exists locally. |
| `make clean-cache` | Prune the Docker builder cache and attempt to remove two legacy unsuffixed volumes. |
| `make uninstall` | Remove SHIRE build artifacts, containers, images, networks, and volumes. |

Run `make cfg` before `make test-sim` when the active selection has changed.
Review [Docker](../manual/how-to/docker.md) before using cleanup commands that remove persisted state.

## Default browser endpoints

| Interface | Address |
| --- | --- |
| YAMCS | [http://localhost:8090](http://localhost:8090) |
| 42 VNC | [http://localhost:5801/vnc_auto.html](http://localhost:5801/vnc_auto.html) |

The generated Compose files publish both browser ports using Docker's short port syntax rather than a loopback only binding.

## DRM services

| Compose service | Current responsibility |
| --- | --- |
| `shire-42` | Runs 42 dynamics and the browser accessible graphics display. |
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
| 42 truth | Director to YAMCS on UDP 50042 |
| 42 IPC | Director and 42 exchange data through the Unix socket at `/tmp/42_ipc.sock` |

The UDP endpoints remain inside the Compose bridge because the templates do not publish them to the host.

## DRM selections

| Spacecraft | Selected component simulators |
| --- | --- |
| `flatsat` | `demo`, `eps`, and `radio` |
| `sat-1` | `adcs`, `demo`, `eps`, and `radio` |
| `sat-2` | `adcs`, `eps`, and `radio` |

The spacecraft selection controls which component simulators are built and loaded and which component applications remain in the generated CPU1 startup script.
The current `cfg/shire_defs/targets.cmake` still compiles all four reference component applications.

The current DRM scenarios are `nominal` and `debug`.
They apply `debug: false` or `debug: true` to every selected component before its device header is rendered.
The default active selection is mission `drm`, spacecraft `sat-1`, scenario `nominal`, and CLI component `demo`.

## cFS targets

| Target | Current state |
| --- | --- |
| CPU1 | Enabled host target that uses `amd64-shire` for normal builds and `amd64-linux` for unit test builds. |
| CPU2 | Disabled `armv7l-linux` Zybo 7020 scaffold that is not built or validated by the current workflow. |

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

## Sources of truth

| Question | Current source |
| --- | --- |
| What can be selected? | `cfg/shire-config.yaml` and `cfg/<mission>/<mission>.yaml` |
| What is selected for the next run? | `build/active.yaml` |
| What was resolved most recently? | `build/build.yaml` |
| Which components are on a spacecraft? | `cfg/<mission>/spacecraft/<spacecraft>.yaml` |
| Which values does a scenario override? | `cfg/<mission>/scenarios/<scenario>.yaml` |
| Which services will start? | The generated `build/<mission>/shire-compose.yaml` or `build/<mission>/cli-compose.yaml` |
| How are Compose files generated? | `cfg/shire-compose.j2` and `cfg/cli-compose.j2` |
| Which applications compile and start? | `cfg/shire_defs/targets.cmake` and the generated CPU startup script |
| Which ground links exist? | `yamcs/src/main/yamcs/etc/yamcs.shire.yaml`, `comp/cryptolib/support/standalone/standalone.h`, `comp/radio/support/device_config.yaml`, `simulith/include/simulith_director.h`, and `simulith/src/simulith_director.c` |
| Which procedures exist? | `yamcs/src/main/yamcs/procedures/` and `comp/<name>/gsw/procedures/` |
| Which developer commands are supported? | The root `Makefile` and subsystem Makefiles |

Treat this page as a convenience index.
When it disagrees with a generated file or source listed above, inspect the current checkout and report the documentation mismatch.

***
Last reviewed: 20260818
