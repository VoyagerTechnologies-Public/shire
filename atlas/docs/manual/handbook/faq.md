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

## A generated file is missing

Run `make cfg`, then inspect:

* `build/active.yaml`
* `build/build.yaml`
* `build/drm/shire-compose.yaml`
* `build/drm/sat-1/shire_defs/` for the default target
* `comp/<selected-component>/shared/device_cfg.h`

The old `cfg/active.yaml`, `cfg/build.yaml`, and `cfg/lab-compose.yaml` paths are not used by the current orchestrator.

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
In the full lab, both FSW and the Director must handshake before time advances.
The Director also exits if it cannot connect to 42.

## YAMCS has no telemetry

1. Open YAMCS **Links** and identify whether `debug-in`, `radio-in`, or `truth42-in` is unavailable.
2. Inspect the FSW, Director, CryptoLib, and GSW logs.
3. Confirm UDP ports 1235, 12346, and 50042 match `yamcs/src/main/yamcs/etc/yamcs.shire.yaml` and the corresponding source configuration.
4. Confirm the radio mode permits the intended direction when testing the radio link.

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
