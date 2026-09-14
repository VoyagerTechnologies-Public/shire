# Docker and Containers

SHIRE uses containers for both compilation and runtime isolation.
The top level Makefile is the supported entry point for normal development.

## Build image

The default build image is `ghcr.io/voyagertechnologies-public/shire-base:0.0.0`.
It contains GCC 14 and LCOV with branch and MC/DC support.
The Base image workflow publishes this multi-architecture tag from `dev` and
`main`, and also publishes immutable commit tags from `dev`.
When `.container.stamp` needs rebuilding, `make container` first attempts to pull the image and builds `cfg/Dockerfile.base` locally if the pull fails.
The stamp avoids repeating that work until the Dockerfile or `cfg/requirements.txt` changes.
Changing `BUILD_IMAGE` alone does not invalidate the stamp.
Remove `.container.stamp` or run `make -B container` when the selected image must be resolved again.

Use the same image interactively with:

```bash
make debug
```

The repository is mounted at the same absolute path and the container runs with the host UID/GID.
The debug container also receives the message queue, real time priority, and `SYS_NICE` settings used by the FSW build.

## Generated compose files

`make cfg` writes compose files under the active mission directory:

* `build/<mission>/shire-compose.yaml` for the DRM
* `build/<mission>/cli-compose.yaml` for the focused component CLI

For the default DRM, these are `build/drm/shire-compose.yaml` and `build/drm/cli-compose.yaml`.

The DRM contains 42, GSW, Server, Director, CryptoLib, and FSW services.
The CLI contains 42, Server, Director, and the selected component CLI.
The component simulator libraries run inside the Director rather than as separate services.

Build and start the DRM with:

```bash
make
make start
```

`make start` uses the existing `build/active.yaml` and generated DRM Compose file.
It does not regenerate configuration or rebuild images.

Build and start the focused CLI environment with:

```bash
make cli
make cli-start
```

`make cli-start` runs `make cfg` before starting the generated CLI Compose file.
Stop either environment with:

```bash
make stop
```

Do not run the DRM and focused CLI environment at the same time for one spacecraft.
They reuse explicit container names, host port 5801, the spacecraft network, and the Simulith IPC volume.

## Inspect the DRM

For the default DRM:

```bash
docker compose -f build/drm/shire-compose.yaml ps
docker compose -f build/drm/shire-compose.yaml logs --tail 200
docker compose -f build/drm/shire-compose.yaml logs -f shire-director
```

Compose service names such as `shire-director` are stable within the generated file.
Explicit container names vary by mission or spacecraft, so prefer Compose commands when possible.
The 42 and GSW services set `attach: false`, so their logs are not attached to normal `docker compose up` output.
Use the generated Compose file when those services need inspection:

```bash
docker compose -f build/drm/shire-compose.yaml logs shire-42 shire-gsw
```

Compose `depends_on` entries provide creation and startup order only.
They do not prove that a dependency is ready to exchange data.
Use service health, logs, and application status when diagnosing startup timing.

## Runtime images and storage

The build creates mission and spacecraft tagged runtime images for 42, Director, Server, FSW, GSW, and CryptoLib.
Component CLI images use the component name.

The generated DRM Compose file defines:

* `simulith_ipc_<spacecraft>` is mounted at `/tmp` in 42, Server, Director, CryptoLib, and FSW for Simulith and 42 IPC files.
* `gsw-data_<mission>` is mounted at `/app/yamcs-data` in GSW for archives, buckets, timelines, displays, stacks, CFDP files, and other YAMCS state.

The focused CLI Compose file defines only the Simulith IPC volume.
Docker Compose can prefix the physical volume name with its project name, so inspect the resolved volumes rather than assuming the logical key is the Docker volume name:

```bash
docker compose -f build/drm/shire-compose.yaml config --volumes
docker volume ls --filter label=com.docker.compose.project
```

The Simulith volume is runtime coordination state rather than an evidence archive.
The YAMCS volume is persistent operator state and can contain results that must be retained before cleanup.

`make stop` removes containers but does not intentionally remove these volumes.
It also removes the Compose networks when they are no longer in use.
A later start reuses the retained volumes.

### Cleanup effects

| Command | Generated files and images | Volumes | Host wide effects |
| --- | --- | --- | --- |
| `make stop` | Stops both generated Compose environments for the active mission and removes their containers | Retains named volumes | Attempts to remove every dangling Docker image visible to the current Docker daemon |
| `make clean` | Runs `make stop`, removes the active mission build tree, and cleans the 42 and GSW images | Removes volumes whose names match `gsw-data` or `simulith_ipc` | Also prunes dangling images through the 42 cleanup path |
| `make clean-cache` | Retains the SHIRE source and generated build tree | Attempts to remove only the literal volumes `gsw-data` and `simulith_ipc` | Runs `docker builder prune -f` for all unused build cache on the current Docker daemon |
| `make uninstall` | Removes `build/`, `.container.stamp`, and Docker containers and images matching SHIRE names | Removes volumes matching the SHIRE GSW and Simulith names | Removes matching SHIRE networks after running the broader clean and cache actions |

`make clean-cache` is not a reliable way to reset mission scoped Compose volumes because their physical names can include project and mission text.
Use `docker volume ls` to identify the exact retained volumes before deciding whether they should be removed.
The artifact and volume removal inside `make clean` is conditional on the configured build image existing locally.

To stop the default DRM and delete both volumes declared by that Compose project, use:

```bash
docker compose -f build/drm/shire-compose.yaml down --volumes --remove-orphans
```

This permanently removes the project copies of both Simulith IPC state and YAMCS persistent state.
Export required timelines, procedures, displays, archives, and CFDP files before deleting the YAMCS volume.
Capture container logs and test evidence before cleanup removes their source containers.
Do not use volume deletion as an initial troubleshooting step when retained ground state matters.

## Common build patterns

* `make cfg` runs the Python orchestrator inside the build image.
* C/C++ and cFS builds run in the build image with the repository mounted.
* Director, Server, FSW, 42, YAMCS, CryptoLib, and CLI Dockerfiles package runtime artifacts into separate images.
* `make` invokes the configured simulation, FSW, and GSW build sequence.

## Resource notes

The compose template applies CPU and memory limits to services.
These are resource ceilings, not guarantees, and the combined stack still requires a capable host.
Check `docker stats` when the simulation cannot maintain the requested rate.

## Network exposure

The generated Compose files use a bridge network named from the selected spacecraft.
Docker service names provide discovery between containers on that network.

| Interface | Current exposure | Purpose |
| --- | --- | --- |
| Host TCP 5801 | Published as `5801:80` by the DRM and focused CLI environment | Browser access to the 42 noVNC display |
| Host TCP 8090 | Published as `8090:8090` by the DRM | Browser and API access to YAMCS |
| Internal UDP 1234 and 1235 | Not published by Compose | Direct command and telemetry between YAMCS and FSW |
| Internal UDP 12343 through 12346 | Not published by Compose | Radio and CryptoLib command and telemetry path |
| Internal UDP 50042 and 50060 | Not published by Compose | Director truth telemetry and simulator backdoor commanding |
| Shared `/tmp` volume | Not a network interface | ZeroMQ IPC and 42 socket exchange among participating services |

The short port mappings for 5801 and 8090 bind through Docker on host interfaces rather than restricting access to localhost.
Host firewall rules and Docker daemon configuration can affect reachability, but the checked in Compose templates do not request a loopback only bind.

The UDP links are intended for communication inside the Compose bridge and are not listed under `ports`.
Do not rely on the bridge network alone as a security boundary because any connected container can attempt to reach those listeners.

For a workstation that can receive untrusted network traffic, consider changing the generated template mappings to `127.0.0.1:5801:80` and `127.0.0.1:8090:8090`.
Make that change in `cfg/shire-compose.j2` and `cfg/cli-compose.j2`, then run `make cfg` rather than editing generated Compose files.

The runtime is a development environment and not production grade.
It publishes browser interfaces, uses internal UDP services, shares a Docker network and IPC volume, and includes YAMCS defaults intended for development.
Do not expose it to an untrusted network without reviewing credentials, secrets, ports, privileges, volumes, and application level authentication.

***
Last reviewed: 20260817
