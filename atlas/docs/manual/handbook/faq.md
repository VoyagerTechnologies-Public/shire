# Troubleshooting and FAQ

Commands below use the default DRM path.
Substitute the active mission when `build/active.yaml` selects another mission.

## First checks

```bash
make cfg
make list
docker info
docker compose -f build/drm/shire-compose.yaml config --quiet
docker compose -f build/drm/shire-compose.yaml ps
docker compose -f build/drm/shire-compose.yaml logs --tail 200
```

| Symptom | First place to look |
| --- | --- |
| Build cannot pull an image | Docker login, network access, proxy settings, and the image named by `BUILD_IMAGE`. |
| Simulation time does not advance | Server, Director, and FSW logs for missing Simulith registration. |
| 42 VNC does not open | The `shire-42` service state and host port 5801. |
| YAMCS opens without telemetry | YAMCS link state and FSW, Director, CryptoLib, and GSW logs. |
| A component is absent | Active spacecraft selection, merged configuration, and Director image contents. |
| A procedure change does not appear | The persistent YAMCS stacks bucket. |
| A command travels over two paths | The enabled `debug-out` and `radio-out` links. |
| Generated files have the wrong owner | The UID and GID used by build containers. |

## A generated file is missing

Run `make cfg`, then inspect:

* `build/active.yaml`
* `build/build.yaml`
* `build/drm/shire-compose.yaml`
* `build/drm/sat-1/shire_defs/` for the default target
* `comp/<selected-component>/shared/device_cfg.h`

## A submodule is empty or marked with `-`

```bash
git submodule sync --recursive
git submodule update --init --recursive
git submodule status --recursive
```

Run `sync` after `.gitmodules` URLs change.

## A service exits during startup

```bash
docker compose -f build/drm/shire-compose.yaml ps --all
docker compose -f build/drm/shire-compose.yaml logs --tail 300 shire-server
docker compose -f build/drm/shire-compose.yaml logs --tail 300 shire-director
docker compose -f build/drm/shire-compose.yaml logs --tail 300 shire-fsw
```

The Server waits for the configured number of Simulith clients.
In the DRM environment, both FSW and the Director must handshake before time advances.
The Director also exits if it cannot connect to 42.

## Simulation time does not advance

The DRM Server configuration expects two clients.
The Director and FSW must both register and acknowledge every tick.

Inspect the three services together:

```bash
docker compose -f build/drm/shire-compose.yaml logs --tail 300 shire-server shire-director shire-fsw
```

Look for a failed Director connection to `/tmp/42_ipc.sock`, an FSW startup failure, or a client that registered but stopped acknowledging ticks.
Do not reduce `NUM_CLIENTS` to one in the DRM unless you are deliberately changing its architecture.

## The 42 browser interface does not open

Confirm that the service is running and inspect its published port:

```bash
docker compose -f build/drm/shire-compose.yaml ps shire-42
docker compose -f build/drm/shire-compose.yaml port shire-42 80
docker compose -f build/drm/shire-compose.yaml logs --tail 200 shire-42
```

The default host mapping is port 5801.
If the service is healthy but the port is unavailable, check for another local process using that port.

## YAMCS has no telemetry

1. Open YAMCS **Links** and identify whether `debug-in`, `radio-in`, or `truth42-in` is unavailable.
2. Inspect the FSW, Director, CryptoLib, and GSW logs.
3. Confirm UDP ports 1235, 12346, and 50042 match `yamcs/src/main/yamcs/etc/yamcs.shire.yaml` and the corresponding source configuration.
4. Confirm the radio mode permits the intended direction when testing the radio link.

## A component simulator is missing

Confirm the selected spacecraft and merged component list:

```bash
make cfg
make list
docker compose -f build/drm/shire-compose.yaml exec shire-director ls -1 /app/components
```

Only component simulators selected for the active spacecraft are copied into the Director image.
If `build/active.yaml` changed after the image was built, rerun the build before starting the stack.

## A new display or procedure does not appear in YAMCS

The YAMCS entrypoint copies packaged displays and procedures only when the corresponding persistent bucket is empty.
This protects changes made through YAMCS, but it also means rebuilding the image does not overwrite an existing bucket.

Export any work that must be preserved before resetting storage.
Inspect the generated mission volume and the cleanup behavior in [Docker](../how-to/docker.md) before deleting it.

## A command appears on both command paths

The current `debug-out` and `radio-out` YAMCS links both consume the `tc_realtime` stream.
When both links are enabled, one command can be emitted through both paths.

Disable the path that is not part of the exercise and confirm the intended link state before commanding.
Command history shows submission and acknowledgements, but service logs and link counters are needed to confirm the transport path.

## Port 8090 or 5801 is already in use

Stop the conflicting process or change the host mapping in `cfg/shire-compose.j2`, then rerun `make cfg`.
Editing only the generated compose file is temporary and will be overwritten.

## Build artifacts have the wrong owner

Most build-container commands pass the host UID/GID.
If files were created by an older command or a manually run root container, inspect ownership in `build/` before changing it.
Do not recursively change ownership outside this repository.

## Docker storage is growing

Inspect before deleting:

```bash
docker system df --verbose
docker volume ls
```

`make clean-cache` prunes the Docker builder cache and attempts to remove volumes named exactly `gsw-data` and `simulith_ipc`.
The generated Compose files use suffixed volume keys, so inspect `docker volume ls` rather than assuming this target removed every SHIRE volume.
`make clean` and `make uninstall` remove broader sets of SHIRE artifacts and volumes.
These targets are destructive, so preserve needed YAMCS data first.

## Reporting an issue

Include:

* `git rev-parse --short HEAD`
* `git status --short`
* `git submodule status --recursive`
* the relevant portion of `build/active.yaml` and `build/build.yaml` that contains no secrets
* `docker compose ... ps --all`
* relevant service logs
* exact commands and expected versus observed behavior

Remove credentials, keys, proprietary mission data, and other secrets before attaching files.

***
Last reviewed: 20260817
