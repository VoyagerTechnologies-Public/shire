# Quick Reference

This page collects the commands, targets, endpoints, and source locations most often needed while working with the default DRM.
Run `make cfg` and `make list` before relying on generated paths because `build/active.yaml` can select a different mission or spacecraft.

## Common commands

| Command | Purpose |
| --- | --- |
| `make cfg` | Resolve the active configuration and render generated artifacts. |
| `make list` | Report the resolved mission, spacecraft, scenario, and component build features. |
| `make` | Configure SHIRE and build the simulation, flight software, and ground software. |
| `make start` | Start the generated full lab compose environment. |
| `make stop` | Stop the generated full lab compose environment. |
| `make cli` | Build the selected focused component CLI environment. |
| `make cli-start` | Start the generated component CLI compose environment. |
| `make test-sim` | Build Simulith and run the selected component simulator tests. |
| `make test-fsw` | Build and run the configured cFS and application tests. |
| `make docs-check` | Validate the Atlas content and run a strict production build. |
| `make docs-serve` | Start the local Atlas preview server. |
| `make clean` | Stop the stack and remove active mission build artifacts. |
| `make clean-cache` | Prune the Docker builder cache and attempt to remove two legacy unsuffixed volumes. |
| `make uninstall` | Remove SHIRE build artifacts, containers, images, networks, and volumes. |

Review [Docker](../manual/how-to/docker.md) before using cleanup commands that remove persisted state.

## Default browser endpoints

| Interface | Address |
| --- | --- |
| YAMCS | [http://localhost:8090](http://localhost:8090) |
| 42 VNC | [http://localhost:5801/vnc_auto.html](http://localhost:5801/vnc_auto.html) |

## Lab network endpoints

| Flow | Endpoint |
| --- | --- |
| Debug command | YAMCS to FSW on UDP 1234 |
| Debug telemetry | FSW to YAMCS on UDP 1235 |
| Radio command | YAMCS to CryptoLib on UDP 12345, then to Radio on UDP 12343 |
| Radio telemetry | Radio to CryptoLib on UDP 12344, then to YAMCS on UDP 12346 |
| Simulator backdoor | YAMCS to Director on UDP 50060 |
| 42 truth | Director to YAMCS on UDP 50042 |
| 42 IPC | Director to 42 through `/tmp/42_ipc.sock` |

## DRM selections

| Spacecraft | Selected component simulators |
| --- | --- |
| `flatsat` | Demo, EPS, and Radio |
| `sat-1` | ADCS, Demo, EPS, and Radio |
| `sat-2` | ADCS, EPS, and Radio |

The spacecraft selection controls which component simulators are built and loaded and which component applications remain in the generated CPU1 startup script.
The current `cfg/shire_defs/targets.cmake` still compiles all four reference component applications.

The current DRM scenarios are `nominal` and `debug`.
The default active selection is mission `drm`, spacecraft `sat-1`, scenario `nominal`, and CLI component `demo`.

## Generated paths

| Path | Contents |
| --- | --- |
| `build/active.yaml` | Editable active selection. |
| `build/build.yaml` | Merged configuration snapshot. |
| `build/<mission>/shire-compose.yaml` | Full lab compose file. |
| `build/<mission>/cli-compose.yaml` | Focused component CLI compose file. |
| `build/<mission>/42_config/Inp_Sim.txt` | Rendered 42 simulation input. |
| `build/<mission>/<spacecraft>/shire_defs/` | Generated cFS mission definitions and pruned CPU1 startup script. |
| `comp/<component>/shared/device_cfg.h` | Rendered device configuration for a selected component with a template. |

## Sources of truth

| Question | Current source |
| --- | --- |
| What can be selected? | `cfg/shire-config.yaml` and `cfg/<mission>/<mission>.yaml` |
| Which components are on a spacecraft? | `cfg/<mission>/spacecraft/<spacecraft>.yaml` |
| Which values does a scenario override? | `cfg/<mission>/scenarios/<scenario>.yaml` |
| Which services run? | `cfg/shire-compose.j2` and `cfg/cli-compose.j2` |
| Which applications compile and start? | `cfg/shire_defs/targets.cmake` and the generated CPU startup script |
| Which ground links exist? | `yamcs/src/main/yamcs/etc/yamcs.shire.yaml` |
| Which procedures exist? | `yamcs/src/main/yamcs/procedures/` and `comp/<name>/gsw/procedures/` |
| Which developer commands are supported? | The root `Makefile` and subsystem Makefiles |

Treat this page as a convenience index.
When it disagrees with a generated file or source listed above, inspect the current checkout and report the documentation mismatch.
