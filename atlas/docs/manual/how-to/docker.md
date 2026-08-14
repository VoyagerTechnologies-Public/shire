# Docker & containers (how SHIRE uses Docker)

This page documents how SHIRE uses Docker for development and CI: how to enter the base build container (`make debug`), how the repository builds the base container (`make container`) and how that base image is leveraged to build artifacts and produce runtime images for FSW, GSW and Simulith.

## Open a container shell

Use `make debug` from the top-level to get an interactive shell in the repository's configured build image.
This is the quickest way to reproduce the environment used by the project build steps when you need to debug build failures, inspect generated files, or run orchestration locally.

What it does:

* Runs the build image (default `ghcr.io/voyagertechnologies-public/shire-base:latest`) with the repository mounted read-write. 
    * The container is started with the current user UID/GID so files produced inside it have sensible permissions on the host.
* The container is started with elevated realtime caps and adjusted kernel settings to better match the CI/lab runtime (see the `Makefile` for `--sysctl fs.mqueue.msg_max=10000 --ulimit rtprio=99 --cap-add=sys_nice`).

Typical use:
```bash
# From repo root
make debug
# inside the container you'll be at a shell in the repository workspace with user "shire"
```

Common tasks inside the debug container:

* Build simulith, components or FSW using the same commands the CI uses.
    * These "in container" commands are typically `make build-*` or similar where `make *` would invoke a container for you
* Run linters or introspect generated files under `build/`.

Notes:

* `make debug` depends on the base build image being available locally (pulls from registry if missing). 
* If you need to customize the base image (add apt packages or tools) edit `cfg/Dockerfile.base` and re-run `make container` (see below).

## Building the base build image

The repository provides a minimal build image that contains the toolchain and common dependencies used to compile FSW, GSW, Simulith and component sims.
The top-level `Makefile` target `container` builds this base image from `cfg/Dockerfile.base`.

Why a base build image?

* Reproducibility: everyone uses the same compiler, CMake, Python and helper tools, avoiding "works on my machine" issues.
* Performance in CI: building inside a container ensures the CI runner has the same environment locally.
* Convenience: scripts and Makefiles assume certain tools exist (python3, cmake, ninja, gcc, g++, lcov, genhtml, etc.). 
    * Putting them in a single base image avoids installing them on each developer machine.

How `make container` works:

* The Makefile target `container` depends on `cfg/Dockerfile.base` and runs `docker build -t $(BUILD_IMAGE) -f cfg/Dockerfile.base --build-arg USER_ID=$(id -u) --build-arg GROUP_ID=$(id -g) .`.
* The build uses `USER_ID` / `GROUP_ID` build args so the container can create files owned by the invoking user when run with `--user`.

Edit the `cfg/Dockerfile.base` to add packages or change the default toolchain. 
After editing, run:
```bash
make container
```

## How the build flow uses the base image

Overview: the repository uses the base build image as the execution environment for building different artifacts.
There are two common patterns:

* Docker run to execute build commands inside the base image (used by the `fsw/Makefile` and some other component-level Makefiles).
* Docker build to produce runtime images (FSW runtime image, Simulith director/server, GSW image) using build artifacts copied from local build directories.

The `fsw/Makefile` uses the build image to run a containerized build of the FSW.
The `build` target executes a `docker run --rm -it -v $(SHIRE_DIR):$(SHIRE_DIR) ... $(BUILD_IMAGE) make -j$(JOBS) build-fsw` which runs the `build-fsw` target inside the container.
Component and simulith builds may similarly use `docker run` to ensure a consistent toolchain.

After native builds complete inside the container the top-level `fsw:runtime` target (invoked via `make fsw`) builds a runtime image for FSW using `docker build -t $(RUNTIME_FSW_IMAGE_NAME):$(SPACECRAFT) -f fsw/tools/Dockerfile.fsw --build-arg SPACECRAFT=$(SPACECRAFT) --build-arg MISSION=$(MISSION) .`
The Simulith `Makefile` produces director and server runtime images via `docker build -t $(RUNTIME_DIRECTOR_NAME):$(SPACECRAFT) -f docker/Dockerfile.director .` after collecting built artifacts into a build context.

Why two-step builds (build then runtime image)?

* Separation of concerns: toolchains and build dependencies are large; keeping them in a build-only image avoids including compilers in runtime images.
* Smaller runtime images: runtime images are lean and contain only the runtime binaries and libraries required to run Director, FSW, or GSW.

## How `make cfg` interacts with Docker

`make cfg` runs the orchestrator inside the base build container. The top-level Makefile target `cfg` calls:

```bash
docker run --rm -v $(CURDIR):$(CURDIR) -w $(CURDIR)/cfg --user $(id -u):$(id -g) $(BUILD_IMAGE) python3 shire-orchestrator.py
```

This ensures the orchestrator has a stable Python environment and any Python dependencies installed in the base image.

## Using compose files (cli vs shire)

The orchestrator renders two compose files you will use to start the lab:
* `cfg/cli-compose.yaml` - small compose to run CLI components
* `cfg/shire-compose.yaml` - the full lab compose (Director, Simulith server, FSW runtime, GSW, and CLI containers)

Use `make cli-start` and `make start` to bring these up respectively.
These targets call `docker compose -f ./cfg/cli-compose.yaml up` and `docker compose -f ./cfg/shire-compose.yaml up`.

## Practical tips & troubleshooting

* Permissions: the Makefiles pass `--user $(id -u):$(id -g)` when running containers so files created by the container are writable by the host user. If you see permission errors, check that UID/GID are passed and that file ownership inside `build/` is correct.
* Missing image: if `make debug` or other targets try to run the image and it is not present locally, Docker will pull it automatically. 
    * If the registry is inaccessible, build locally with `make container`.
* Build failures inside container: use `make debug` to enter the container and reproduce the failing commands interactively.
* Slow rebuilds: edit `cfg/Dockerfile.base` carefully - adding large apt installs will make `make container` slow.
    * Use multi-stage Dockerfiles or apt caching to speed up CI.

## Docker Summary

* `make container` - build the base build image used for compilation and orchestration
* `make debug` - drop into an interactive shell inside the base build image
* `make cfg` - run the orchestrator inside the build image to produce `cfg/build.yaml`, compose files and per-component `device_cfg.h`
* `make sim` / `make fsw` / `make gsw` - perform builds that use the base image and produce runtime images for Director, FSW and GSW
* `make start` / `make cli-start` - start the lab using the generated compose files

----
Last updated: 20251203
