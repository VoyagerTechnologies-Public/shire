# Installation

SHIRE builds and runs through Docker.
The host needs only the orchestration tools because compilers and most project dependencies are supplied by container images.

## Prerequisites

* A supported Linux environment, or Windows 11 with WSL 2
* Docker Engine with the Docker Compose v2 plugin
* GNU Make
* Git with submodule support
* Python 3 with the PyYAML package used by `cfg/shire-build.py`
* Enough memory and disk space for multiple runtime/build images and the generated mission tree

Linux is the primary execution environment.
On Windows, keep the repository inside the WSL filesystem rather than a mounted Windows directory for better file and Docker performance.

Install Docker from the documentation for your operating system.
If using Docker Engine without Docker Desktop, configure Docker access for your user according to Docker's installation guidance, then start a new login session before building.

## Verify the host

```bash
docker --version
docker compose version
docker run --rm hello-world
make --version
git --version
python3 --version
python3 -c "import yaml"
```

All commands must succeed from the same shell used to run SHIRE.
`docker run` is important: a working client binary is insufficient if the daemon is unavailable or the user lacks permission.

## Git submodules

SHIRE uses submodules for cFE, OSAL, PSP, reusable cFS applications, 42, YAMCS, and CryptoLib.
Clone recursively, or initialize them after cloning:

```bash
git submodule update --init --recursive
git submodule status --recursive
```

Every initialized line in `git submodule status --recursive` should begin with a commit ID rather than `-`.

## Network access

The first build requires access to GitHub, GitHub Container Registry, and upstream package repositories used while building images.
Corporate proxies, registry authentication, DNS filtering, or rate limits can prevent an otherwise correct installation.

After the host checks pass, continue to [Getting Started](getting-started.md).

***
Last reviewed: 20260817
