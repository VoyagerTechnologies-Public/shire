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
| Visualization does not open | The `shire-gsw` service state and host port 8090. |
| YAMCS opens without telemetry | YAMCS link state and FSW, Director, CryptoLib, and GSW logs. |
| A component is absent | Active spacecraft selection, merged configuration, and Director image contents. |
| A procedure change does not appear | The persistent YAMCS stacks bucket. |
| A command uses the debug path unexpectedly | The state of the preferred `radio-out` link. |
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
The Director registers for PREPARE, EXECUTE, and COMMIT, while FSW registers for
EXECUTE.
The Server must receive every required phase completion before it advances a
tick.

Inspect the three services together:

```bash
docker compose -f build/drm/shire-compose.yaml logs --tail 300 shire-server shire-director shire-fsw
```

Look for a failed Director connection to `/tmp/42_ipc.sock`, an FSW startup failure, or a client that registered but stopped completing its phases.
The Server watchdog names the client and phase holding a stalled sequence.
For FSW stalls, the PSP watchdog also reports the schedule entry, message ID,
and whether the participant is claimed or running.
Do not reduce `NUM_CLIENTS` to one in the DRM unless you are deliberately changing its architecture.

## Performance varies or falls below the target

The [Synchronized Simulation Performance](../how-to/performance.md) guide
records the accepted workload, expected results, and regression procedure.

Use the synchronized harness instead of estimating speed from startup logs:

```bash
make perf-smoke
make perf
```

The complete performance run includes active ADCS and radio transactions,
a full-output reference, paced 1x, 25x, and 50x trials, and three unbounded
trials that must exceed 50x.
It compares complete control traces and verifies advancing Yamcs truth.
Its report path is printed at completion and defaults to a timestamped directory
under `build/performance/`.

Inspect the report before changing priorities or disabling outputs.
Check phase and device latency, resource samples, protocol errors, and queue
overflows.

After accepting a report for this workstation, test a candidate with:

```bash
make perf-compare BASELINE=/path/to/accepted-report.json
```

Do not relax the phase barrier, remove device transactions, or disable flight
application work to obtain a faster result.

## Visualization does not open

Confirm Yamcs is running and inspect its viewer route:

```bash
docker compose -f build/drm/shire-compose.yaml ps shire-gsw
curl -f http://localhost:8090/visualization/
docker compose -f build/drm/shire-compose.yaml logs --tail 200 shire-gsw
```

Check `make replay-list` if the simulation has already stopped.
Replay a selected
run with `make replay RUN=<run-id>`.

## YAMCS has no telemetry

1. Open YAMCS **Links** and identify whether `debug-in`, `radio-in`, `truth42-in`, or `visual-in` is unavailable.
2. Inspect the FSW, Director, CryptoLib, and GSW logs.
3. Confirm UDP ports 1235, 12346, 50042, and 50044 match `yamcs/src/main/yamcs/etc/yamcs.shire.yaml` and the corresponding source configuration.
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

## A command uses the debug path unexpectedly

YAMCS prefers `radio-out` and falls back to `debug-out` when the preferred interface is unavailable.

Check the `radio-out` state and service logs to determine why YAMCS selected the fallback.
Command history shows submission and acknowledgements, while link counters and service logs establish the transport path.

## Port 8090 is already in use

Stop the conflicting process or change the host mapping in `cfg/shire-compose.j2`, then rerun `make cfg`.
For standalone replay, use `make replay RUN=<run-id> PORT=<free-port>`.
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

`make clean-cache` deletes all labeled SHIRE Yamcs run archives, their stored replay data, and their pinned image tags before pruning the Docker builder cache.
It refuses to proceed while any archive is mounted by a running container, and it removes stopped containers still attached to those archives.
It keeps older unlabeled `gsw-data` volumes and attempts to remove the legacy `simulith_ipc` volume.
`make clean` retains labeled Yamcs archives, while `make uninstall` calls `clean-cache` and therefore deletes them.
Use `make replay-list` to inspect retained runs and `make replay-delete RUN=<run-id>` to remove just one.

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
Last reviewed: 20260913
