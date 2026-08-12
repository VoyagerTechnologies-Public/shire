# SHIRE Software Bill of Materials (SBOM)

**Generated:** 2026-08-11

**Source revision:** `1d4ee967fb2fe21aadd7b51a31fde56e5214c261` (`1-setup`)

**Repository:** `https://github.com/VoyagerTechnologies-Public/shire`

**License:** Permissive Open-Source License v1.0 — Copyright 2025 Voyager Technologies Inc.

**Machine-readable SBOM:** [sbom.cdx.json](sbom.cdx.json) (CycloneDX 1.6 JSON)

## Scope and method

This is a source SBOM for the checked-out repository and its pinned Git submodules. It was reconciled manually against `.gitmodules`, dependency manifests, Maven configuration, and Dockerfiles. It does not enumerate Maven or Python transitive dependencies, the package set inherited from container base images, or exact versions of packages installed without a version constraint. It also does not contain vulnerability scan results.

For a release SBOM, generate and merge resolved dependency and image inventories (for example, CycloneDX Maven output and Syft output for every published image), then scan the result with the organization's approved vulnerability scanner.

## SHIRE-maintained components

| Component | Directory | Language | Description |
|---|---|---|---|
| Build orchestrator | `cfg/` | Python | Mission configuration, rendering, and build orchestration |
| Simulith | `simulith/` | C/C++ | Simulation server, director, transport, and component integration |
| ADCS component | `comp/adcs/` | C | Attitude determination and control FSW, simulator, CLI, and GSW definitions |
| EPS component | `comp/eps/` | C | Electrical power FSW, simulator, CLI, and GSW definitions |
| Radio component | `comp/radio/` | C | Radio FSW, simulator, CLI, and GSW definitions |
| Demo component | `comp/demo/` | C | Reference component implementation |

## Pinned source submodules

The commit IDs below are the versions actually selected by the parent repository. Branch values in `.gitmodules` are update hints and do not replace these pins.

| Component | Directory | Commit | Source |
|---|---|---|---|
| 42 spacecraft simulator | `42/` | `af31058313ad9837c0fba296897098c198bd17fd` | `VoyagerTechnologies-Public/external-42` |
| Core Flight Executive (cFE) | `cfs/cfe/` | `72a865663c5c167fbe523b3dd5fa950cf3796473` | `VoyagerTechnologies-Public/external-cFE` |
| Operating System Abstraction Layer (OSAL) | `cfs/osal/` | `5654aa05548be698b1f8e840c6ac86e241280a32` | `VoyagerTechnologies-Public/external-osal` |
| Platform Support Package (PSP) | `cfs/psp/` | `1b8f96ea225caefe763f4097bc411c6f58c9702a` | `VoyagerTechnologies-Public/external-PSP` |
| elf2cfetbl | `cfs/tools/elf2cfetbl/` | `e888aa04fd4dcb77ace5ac218300266fe568f1dc` | `VoyagerTechnologies-Public/external-elf2cfetbl` |
| CF | `cfs/apps/cf/` | `4f751647df1a83e9d0897879f759213e7e803e14` | `VoyagerTechnologies-Public/external-CF` |
| CI Lab | `cfs/apps/ci_lab/` | `7a006e4429e08b50d4a58fafbcb9e4899306fcb4` | `VoyagerTechnologies-Public/external-ci_lab` |
| DS | `cfs/apps/ds/` | `73d680405833b849486511b9c2d4ab60209e9b9e` | `VoyagerTechnologies-Public/external-DS` |
| FM | `cfs/apps/fm/` | `7c1982e8bde1f227e786d1aa02f98b018fd56529` | `VoyagerTechnologies-Public/external-FM` |
| IO Lib | `cfs/apps/io_lib/` | `328f79ca75208dc4b6da950133f4ae418460da1c` | `VoyagerTechnologies-Public/external-CFS_IO_LIB` |
| LC | `cfs/apps/lc/` | `0ad442f83f01bf94afd9e4376e16245af0af1f36` | `VoyagerTechnologies-Public/external-LC` |
| SC | `cfs/apps/sc/` | `86cfe00d89f1dc09c09a2e3bcd84d968050ef25e` | `VoyagerTechnologies-Public/external-SC` |
| SCH | `cfs/apps/sch/` | `28110189859131739375414b80d2af6d604690dc` | `VoyagerTechnologies-Public/external-SCH` |
| TO Lab | `cfs/apps/to_lab/` | `9ca4f85d9d10df5170b542792ab34d79144e417f` | `VoyagerTechnologies-Public/external-to_lab` |
| CryptoLib | `comp/cryptolib/` | `f8ee0237ac36acd8bed0c490caa3b932a6d71a75` | `VoyagerTechnologies-Public/external-CryptoLib` |
| SHIRE YAMCS | `yamcs/` | `7c77ecce36025c0e314a5eed265d15098214d4ed` | `VoyagerTechnologies-Public/external-yamcs` |

The cFE, OSAL, PSP, elf2cfetbl, CF, CI Lab, DS, FM, LC, SC, and TO Lab checkouts contain Apache-2.0 license files. CryptoLib contains NASA Open Source Agreement 1.3. License identification for the remaining submodules should be confirmed from their upstream distributions before release.

## Declared application dependencies

### Java — `yamcs/pom.xml`

| Artifact | Group | Declared version | Scope |
|---|---|---|---|
| `yamcs-core` | `org.yamcs` | 5.13.0 | Runtime |
| `yamcs-web` | `org.yamcs` | 5.13.0 | Runtime |

| Build plugin | Declared version |
|---|---|
| `maven-compiler-plugin` | 3.15.0 |
| `maven-site-plugin` | 3.21.0 |
| `yamcs-maven-plugin` | 1.3.7 |
| `maven-project-info-reports-plugin` | 3.4.3 |

The project targets Java 17. The Maven wrapper and YAMCS build image use Maven 3.9.9. Maven transitive dependencies are not enumerated here.

### Python

| Manifest | Package | Constraint |
|---|---|---|
| `cfg/requirements.txt` | `pyyaml` | Unpinned |
| `cfg/requirements.txt` | `jinja2` | Unpinned |
| `yamcs/requirements-commander.txt` | `yamcs-client` | `>=1.9.0` |
| `yamcs/requirements-commander.txt` | `requests` | `>=2.31.0` |
| `comp/cryptolib/docs/wiki/requirements.txt` | `sphinx` | `>=8.0` |
| `comp/cryptolib/docs/wiki/requirements.txt` | `sphinx-rtd-theme` | Unpinned |
| `comp/cryptolib/docs/wiki/requirements.txt` | `myst-parser` | Unpinned |

All listed Python requirements resolve mutable versions at installation time.

### Native and security tooling

| Dependency | Version source | Use |
|---|---|---|
| wolfSSL | `5.7.6-stable` in `comp/cryptolib/support/Dockerfile` | CryptoLib cryptographic backend |
| AFL++ | `v4.31c` in `comp/cryptolib/support/Dockerfile` | CryptoLib fuzz testing |
| libgcrypt | Distribution package; optional helper downloads 1.11.0 | CryptoLib cryptographic backend |
| libgpg-error | Distribution package; optional helper downloads 1.50 | libgcrypt support |
| ZeroMQ | Distribution package | Simulith messaging |
| libcurl | Distribution package | HTTP/network support |
| SocketCAN development library | Distribution package | CAN hardware integration |

The source downloads and Git clone in CryptoLib's support tooling are not checksum- or commit-pinned.

## Container images

### External base images

| Image | Pinning | Used by |
|---|---|---|
| `debian:bookworm-slim@sha256:6ac2c08566499cc2415926653cf2ed7c3aedac445675a013cc09469c9e118fdd` | Digest | `cfg/Dockerfile.base` |
| `maven:3.9.9-eclipse-temurin-17` | Mutable tag | `yamcs/Dockerfile.yamcs` |
| `ubuntu:noble-20250127` | Date tag, no digest | `comp/cryptolib/support/Dockerfile` |
| `ghcr.io/haisamido/x-vnc:latest` | Mutable tag | `cfg/Dockerfile.42` default build argument |

### SHIRE images referenced by Dockerfiles

| Image | Status |
|---|---|
| `ghcr.io/voyagertechnologies-public/shire-base:latest` | Built from `cfg/Dockerfile.base`; consumed by FSW, Simulith, and CryptoLib standalone images |
| `ghcr.io/voyagertechnologies-public/shire-yamcs:latest` | Built from `yamcs/Dockerfile.yamcs`; consumed by `yamcs/Dockerfile.gsw` |

### Direct packages in `cfg/Dockerfile.base`

`build-essential`, `cmake`, `curl`, `gcovr`, `gdb`, `git`, `gpg`, `lcov`, `libcurl4-openssl-dev`, `libgcrypt20-dev`, `libsocketcan-dev`, `libzmq3-dev`, `pkg-config`, `python3`, and `python3-pip` are installed without version constraints. Their resolved versions depend on the pinned Debian image and repository state at build time.

The 42 image additionally installs `libglu1-mesa-dev`, `freeglut3-dev`, `mesa-common-dev`, and `libglfw3-dev` without version constraints. The YAMCS image additionally installs `curl`, `python3`, and `python3-requests` without version constraints.

## Build and CI tooling

| Tool | Declared version or source |
|---|---|
| GNU Make | Host/container package |
| CMake | Distribution package |
| GCC/G++ | `build-essential` distribution package |
| Apache Maven | 3.9.9 |
| Eclipse Temurin JDK | 17 |
| Docker Engine and Compose | User prerequisites; unpinned |

There are no parent-repository workflows under `.github/workflows/` in this checkout. Workflow files inside Git submodules belong to those submodule repositories and do not run as SHIRE parent-repository workflows.

## Known gaps and release actions

- Resolve and include Maven and Python transitive dependencies.
- Generate SBOMs from the built `shire-base`, YAMCS, FSW, Simulith, component, and 42 images so inherited operating-system packages are captured.
- Pin mutable container tags and downloaded source archives by digest/checksum.
- Add an automated SBOM generation and validation workflow to prevent source and SBOM drift.
- Run vulnerability and license-policy scans on the resolved release SBOM; this document is an inventory, not a security assessment.
