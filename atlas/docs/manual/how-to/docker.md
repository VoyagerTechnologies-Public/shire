# Docker and Containers

SHIRE uses containers for both compilation and runtime isolation.
The top level Makefile is the supported entry point for normal development.

## Build image

The default build image is `ghcr.io/voyagertechnologies-public/shire-base:latest`.
`make container` first attempts to pull it.
If that fails, it builds `cfg/Dockerfile.base` locally.
A `.container.stamp` file avoids repeating that work until the Dockerfile or `cfg/requirements.txt` changes.

Use the same image interactively with:

```bash
make debug
```

The repository is mounted at the same absolute path and the container runs with the host UID/GID.
The debug container also receives the message queue, real time priority, and `SYS_NICE` settings used by the FSW build.

## Generated compose files

`make cfg` writes compose files under the active mission directory:

* `build/<mission>/shire-compose.yaml` for the full lab
* `build/<mission>/cli-compose.yaml` for the focused component CLI lab

For the default DRM, these are `build/drm/shire-compose.yaml` and `build/drm/cli-compose.yaml`.

The full lab contains 42, GSW, Server, Director, CryptoLib, and FSW services.
The CLI lab contains 42, Server, Director, and the selected component CLI.
The component simulator libraries run inside the Director rather than as separate services.

Use the Make targets so they resolve the active mission consistently:

```bash
make start
make cli-start
make stop
```

## Inspect the full lab

For the default DRM:

```bash
docker compose -f build/drm/shire-compose.yaml ps
docker compose -f build/drm/shire-compose.yaml logs --tail 200
docker compose -f build/drm/shire-compose.yaml logs -f shire-director
```

Compose service names such as `shire-director` are stable within the generated file.
Explicit container names vary by mission or spacecraft, so prefer Compose commands when possible.

## Runtime images and volumes

The build creates mission and spacecraft tagged runtime images for 42, Director, Server, FSW, GSW, and CryptoLib.
Component CLI images use the component name.

The generated full lab compose file defines:

* a Simulith volume shared by 42, Server, Director, CryptoLib, and FSW for IPC sockets
* a mission scoped YAMCS data volume for archives, buckets, and other persistent ground state

`make stop` removes containers but does not intentionally remove these volumes.
Removing volumes resets state and cannot be undone through SHIRE.

## Common build patterns

* `make cfg` runs the Python orchestrator inside the build image.
* C/C++ and cFS builds run in the build image with the repository mounted.
* Director, Server, FSW, 42, YAMCS, CryptoLib, and CLI Dockerfiles package runtime artifacts into separate images.
* `make` invokes the configured simulation, FSW, and GSW build sequence.

## Resource and security notes

The compose template applies CPU and memory limits to services.
These are resource ceilings, not guarantees, and the combined stack still requires a capable host.
Check `docker stats` when the simulation cannot maintain the requested rate.

The runtime is a development lab.
It exposes browser and UDP interfaces, shares a Docker network and IPC volume, and includes YAMCS defaults intended for development.
Do not expose it to an untrusted network without reviewing credentials, secrets, ports, privileges, volumes, and application level authentication.
