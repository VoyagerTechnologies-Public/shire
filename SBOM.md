# SHIRE Software Bill of Materials (SBOM)

**Generated:** 2026-06-26
**Repository:** `https://github.com/VoyagerTechnologies/shire`
**License:** Permissive Open-Source License v1.0 — Copyright 2025 Voyager Technologies Inc.
**Machine-readable SBOM:** [sbom.cdx.json](sbom.cdx.json) (CycloneDX 1.6 JSON)

---

## System Components

These subsystems are developed and maintained within this repository.

| Component | Directory | Language | Description |
|-----------|-----------|----------|-------------|
| Flight Software (FSW) | `cfs/` | C | NASA Core Flight System (cFS) — flight executive and applications |
| Ground Software (GSW) | `yamcs/` | Java | YAMCS-based mission control and telemetry |
| Simulith | `simulith/` | C/C++ | Simulator integration and director layer |
| 42 Simulator | `42/` | C | Space vehicle dynamics simulator (Eric Stoneking / NASA GSFC, SHIRE-modified) |
| ADCS Component | `comp/adcs/` | C | Attitude Determination and Control System |
| EPS Component | `comp/eps/` | C | Electrical Power System |
| Radio Component | `comp/radio/` | C | Radio communication subsystem |
| CryptoLib Component | `comp/cryptolib/` | C | Cryptographic library for secure commanding |
| Demo Component | `comp/demo/` | C | Template/reference component |
| Build Orchestrator | `cfg/` | Python | Mission configuration and build orchestration scripts |
| Atlas Documentation | `atlas/` | Markdown | MkDocs-based project documentation site |

### CFS Applications

| Application | Directory | Description |
|-------------|-----------|-------------|
| CF (CFDP) | `cfs/apps/cf/` | CCSDS File Delivery Protocol — file transfer |
| CI Lab | `cfs/apps/ci_lab/` | Command interface lab |
| DS | `cfs/apps/ds/` | Data storage |
| FM | `cfs/apps/fm/` | File manager |
| IO Lib | `cfs/apps/io_lib/` | I/O library |
| LC | `cfs/apps/lc/` | Limit checker |
| SC | `cfs/apps/sc/` | Stored commands |
| SCH | `cfs/apps/sch/` | Scheduler |
| TO Lab | `cfs/apps/to_lab/` | Telemetry output lab |

### CFS Core Libraries

| Library | Directory | Description |
|---------|-----------|-------------|
| CFE | `cfs/cfe/` | Core Flight Executive |
| OSAL | `cfs/osal/` | Operating System Abstraction Layer |
| PSP | `cfs/psp/` | Platform Support Package |

---

## Third-Party Dependencies

### Java (Maven) — `yamcs/pom.xml`

| Artifact | Group | Version | License | Description |
|----------|-------|---------|---------|-------------|
| yamcs-core | org.yamcs | 5.12.0 | AGPL-3.0 | YAMCS mission control core framework |
| yamcs-web | org.yamcs | 5.12.0 | AGPL-3.0 | YAMCS web interface |

**Build Plugins:**

| Plugin | Version | Description |
|--------|---------|-------------|
| yamcs-maven-plugin | 1.3.5 | YAMCS build and run integration |
| maven-site-plugin | 3.12.1 | Maven site generation |
| maven-project-info-reports-plugin | 3.4.3 | Project info reports |

**Build Toolchain:** Maven 3.13.0 · Java 17

### Python — `cfg/requirements.txt`

| Package | Version | License | Description |
|---------|---------|---------|-------------|
| pyyaml | latest | MIT | YAML parsing for build orchestration |
| jinja2 | latest | BSD-3-Clause | Template rendering for configuration files |

### Python — `comp/cryptolib/docs/wiki/requirements.txt`

| Package | Version | License | Description |
|---------|---------|---------|-------------|
| sphinx | ≥ 8.0 | BSD-2-Clause | Documentation generator |
| sphinx-rtd-theme | latest | MIT | Read the Docs theme for Sphinx |
| myst-parser | latest | MIT | Markdown support for Sphinx |

### Python — `yamcs/requirements-commander.txt`

| Package | Version | License | Description |
|---------|---------|---------|-------------|
| yamcs-client | ≥ 1.9.0 | LGPL-3.0 | Python YAMCS client — timeline and commander automation |
| requests | ≥ 2.31.0 | Apache-2.0 | HTTP client library |

### C/C++ External Libraries (installed via Dockerfile)

| Library | Version | License | Description |
|---------|---------|---------|-------------|
| libgcrypt | 1.11.0 | LGPL-2.1 | General-purpose cryptographic library |
| libgpg-error | 1.50 | LGPL-2.1 | Error values for GnuPG components |
| libzmq (ZeroMQ) | system | LGPL-3.0 | Asynchronous messaging library |
| libcurl (OpenSSL) | system | curl / MIT | URL transfer library |
| wolfSSL | 5.7.6-stable | GPL-2.0 / commercial | Embedded TLS/crypto library (CryptoLib) |

### Documentation — `atlas/`

| Package | Version | License | Description |
|---------|---------|---------|-------------|
| mkdocs-material | latest | MIT | Material theme for MkDocs documentation |

---

## Container Base Images

| Image | Tag / Digest | Used In | Description |
|-------|-------------|---------|-------------|
| `debian:bookworm-slim` | `sha256:6ac2c08566499cc2415926653cf2ed7c3aedac445675a013cc09469c9e118fdd` | `cfg/Dockerfile.base` | Base build and runtime environment |
| `maven:3.9.9-eclipse-temurin-17` | latest | `yamcs/Dockerfile.yamcs` | YAMCS build environment |
| `ubuntu:noble-20250127` | pinned | `comp/cryptolib/support/Dockerfile` | CryptoLib build environment |
| `ghcr.io/haisamido/x-vnc` | latest | `cfg/Dockerfile.42` | 42 simulator VNC desktop |

**Internal Images (built and published to GHCR):**

| Image | Dockerfile | Platforms | Description |
|-------|-----------|-----------|-------------|
| `ghcr.io/voyagertechnologies/shire-base` | `cfg/Dockerfile.base` | linux/amd64, linux/arm64 | Shared base image for all builds |
| `ghcr.io/voyagertechnologies/shire-yamcs` | `yamcs/Dockerfile.yamcs` | linux/amd64, linux/arm64 | YAMCS build layer with pre-fetched Maven dependencies |

---

## System Packages (installed via apt in `cfg/Dockerfile.base`)

| Package | Description |
|---------|-------------|
| build-essential | GCC, G++, make, and essential build tools |
| cmake | Cross-platform build system |
| binutils | Binary utilities (linker, assembler) |
| crossbuild-essential-armhf | ARM hard-float cross-compilation toolchain |
| gcc-arm-linux-gnueabihf | ARM 32-bit cross-compiler |
| libcurl4-openssl-dev | cURL development headers |
| libgcrypt20-dev | libgcrypt development headers |
| libzmq3-dev | ZeroMQ development headers |
| gcovr / lcov | Code coverage reporting |
| gdb | GNU debugger |
| python3 / python3-pip | Python 3 runtime and package manager |
| curl / git / gpg / pkg-config | General-purpose build utilities |

---

## Build & CI Infrastructure

| Tool | Version | Description |
|------|---------|-------------|
| GNU Make | system | Primary build orchestration (`Makefile`) |
| Apache Maven | 3.9.9 | Java build tool — resolves YAMCS deps and packages GSW bundle |
| Docker Engine | latest | Container build and runtime |
| Docker Compose | latest | Multi-container orchestration |
| Docker Buildx | latest | Multi-platform image builds |
| GitHub Actions | — | CI/CD automation |
| Codecov | — | Code coverage reporting |
| QEMU | — | ARM emulation for multi-platform container builds |

### CI/CD Workflows

| Workflow | File | Trigger | Description |
|----------|------|---------|-------------|
| CI | `.github/workflows/ci.yml` | Pull request | Build and test FSW, simulators, CLIs |
| Containers | `.github/workflows/containers.yml` | Push to `dev` / `v*.*.*` tags | Build and publish container images to GHCR |
| Atlas Deploy | `atlas/.github/workflows/ci.yml` | Push to `master`/`main` | Deploy documentation site via `mkdocs gh-deploy` |

---

## Fuzzing & Security Testing Tools (CryptoLib only)

| Tool | Version | License | Description |
|------|---------|---------|-------------|
| AFLplusplus | v4.31c | Apache-2.0 | Coverage-guided fuzzer with QEMU mode |
| clang / LLVM | 14 | Apache-2.0 | Compiler toolchain used for instrumented builds |

---

## Target Platforms

| Platform | Architecture | Description |
|----------|-------------|-------------|
| Linux x86-64 | amd64 | Primary development and simulation host |
| Linux ARM64 | arm64 | Container cross-compilation target |
| Zybo 7020 | ARM v7 (32-bit) | Hardware FSW target (Xilinx Zynq SoC) |

---

## Communication Standards

| Standard | Description |
|----------|-------------|
| CCSDS Space Packet Protocol | Telemetry and telecommand framing |
| CCSDS File Delivery Protocol (CFDP) | File transfer between GSW and FSW |
| XTCE | Telemetry/command dictionary format consumed by YAMCS |
