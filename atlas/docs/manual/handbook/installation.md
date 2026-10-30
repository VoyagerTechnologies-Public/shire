# Installation

SHIRE builds and runs through Docker.
The host needs only the orchestration tools because compilers and most project dependencies are supplied by container images.

## Prerequisites

* A supported Linux environment, or Windows 11 with WSL 2
* Docker Engine with BuildKit and the Docker Compose v2 plugin, version 2.20 or later
* GNU Make
* Git with submodule support
* Python 3 with the PyYAML and requests packages, which `tools/requirements-host.txt` lists
* Enough memory and disk space for multiple runtime/build images and the generated mission tree

Linux is the primary execution environment.
On Windows, keep the repository inside the WSL filesystem rather than a mounted Windows directory for better file and Docker performance.

The generated compose file uses the `attach: false` service option, which needs Compose 2.20 or later.
The Yamcs image build uses `docker build --build-context`, which needs BuildKit and Buildx.
Current Docker Engine releases provide both.

Install Docker from the documentation for your operating system.
If using Docker Engine without Docker Desktop, configure Docker access for your user according to Docker's installation guidance, then start a new login session before building.

### Supported hosts

The README and this guide document Linux and Windows 11 with WSL 2.
The multi-platform base image recipes in `tools/Dockerfile.base` and `tools/Dockerfile.native-runtime` also describe `linux/amd64` and `linux/arm64` builds, and an earlier change (#27) adjusted the build for Apple Silicon.
The Atlas contains no validation record for macOS hosts, so treat them as unverified.

### Python

No minimum Python 3 version is enforced for the host tools, and the repository declares no `python_requires` or equivalent.
The host tools use modern type annotation syntax, so use a currently supported Python 3 release rather than an old distribution default.
The board and QEMU workflow documents Python 3.12 or later.
Install the host packages with `python3 -m pip install --requirement tools/requirements-host.txt`.
`make scenario`, `make campaign`, `make screenshots-check`, and `yamcs/yamcs_commander.py` need them, and `tools/requirements.txt` is a separate list that only builds the container image.
Configuration generation runs inside the build image, so Jinja2 is not needed on the host.

### Resources

The generated compose file caps container resources.
The six DRM services together can use up to about 8 GiB of memory and about 17 CPU shares.
Those figures are limits rather than reservations, so an idle stack uses less.
Reserve at least that much on the Docker host, which for Docker Desktop and WSL 2 means the VM or WSL memory and CPU settings, not only the physical machine.
Building the images needs additional disk space and memory.

### Privileged settings

The FSW service sets `fs.mqueue.msg_max=10000`, the `rtprio` ulimit to 99, and the `SYS_NICE` capability.
`make debug` applies the same settings.
Rootless Docker and some Docker Desktop configurations can refuse them, which appears as a container start error or an FSW startup failure.
If that happens, use a rootful Docker Engine or a Linux VM that allows these settings.

## Verify the host

```bash
docker --version
docker compose version
docker buildx version
docker run --rm hello-world
make --version
git --version
python3 --version
python3 -c "import yaml"
```

All commands must succeed from the same shell used to run SHIRE.
`docker run` is important: a working client binary is insufficient if the daemon is unavailable or the user lacks permission.

## Git submodules

SHIRE uses submodules for cFE, OSAL, PSP, the `elf2cfetbl` table tool, reusable cFS applications including `io_lib`, 42, YAMCS, and CryptoLib.
Clone recursively, or initialize them after cloning:

```bash
git submodule update --init --recursive
git submodule status --recursive
```

Every initialized line in `git submodule status --recursive` should begin with a commit ID rather than `-`.

## Network access

The first build needs access to these services:

* GitHub, for the Git submodules
* Docker Hub, for the base images such as Debian, Node, Maven, and Eclipse Temurin
* The Debian package mirrors, Maven Central, and the npm registry, for packages installed while building images
* The Python package index, for the packages listed in `tools/requirements.txt` and `tools/requirements-host.txt`

`make container` builds the five `ghcr.io/voyagertechnologies-public/...:0.0.0` dependency images from this checkout, and later builds use them with `--pull=false`.
Neither `make` nor `make container` pulls those images from GitHub Container Registry.
After the dependency images exist, `make` builds the mission images without network access.
Corporate proxies, DNS filtering, or rate limits can prevent an otherwise correct installation.

After the host checks pass, continue to [Getting Started](getting-started.md).

***
Last reviewed: 20261027
