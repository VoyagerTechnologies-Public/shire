# SHIRE Software Bill of Materials (SBOM)

**Reviewed:** 2026-09-14

**Parent revision reviewed:** `fc12054054249fb594cc315abcff51ef1b77813e`
(`17-performance`)

**Repository:** `https://github.com/VoyagerTechnologies-Public/shire`

**Machine-readable SBOM:** [sbom.cdx.json](sbom.cdx.json) (CycloneDX 1.6 JSON)

## License boundary

SHIRE is a multi-license distribution. The root Voyager Permissive Open-Source
License version 1.0 applies only to Voyager-authored SHIRE material. It does
not supersede or replace any license, copyright, attribution, notice, or asset
term in a submodule, vendored source, package dependency, or container layer.
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for path-level boundaries.
This inventory is not legal advice.

## Scope and method

This curated source SBOM was reconciled against the Git links and recursive
`.gitmodules` files at the reviewed parent revision, license and notice files
in the populated checkout, declared Maven and Python manifests, Dockerfiles,
and root-vendored source. The Git commit recorded for each submodule is the
immutable parent pin; a `.gitmodules` branch is only an update hint.

This SBOM does not claim full dependency resolution. Maven/Python transitive
dependencies, inherited operating-system packages, and packages installed
without exact versions must be generated from the final release artifacts.

## Voyager-authored SHIRE components

These path descriptions establish repository organization, not ownership of
third-party material copied into or referenced by those paths.

| Component | Path | Language | Description |
|---|---|---|---|
| Build and mission integration | `cfg/` | Python, C configuration | Mission rendering, build orchestration, and SHIRE integration; NASA-derived files are separately identified below |
| Simulith | `simulith/` | C/C++ | Synchronized server, director, transport, and integration; vendored Unity is separately identified below |
| ADCS component | `comp/adcs/` excluding submodules | C | Flight application integration, simulator, CLI, and ground definitions |
| EPS component | `comp/eps/` | C | Flight application integration, simulator, CLI, and ground definitions |
| Radio component | `comp/radio/` | C | Flight application integration, simulator, CLI, and ground definitions |
| Demo component | `comp/demo/` | C | Reference component implementation |
| Atlas | `atlas/` | Markdown and assets | SHIRE documentation site |

## Recursive submodule inventory

The following table is guarded by `scripts/check_licensing.py`. Its path and
commit set must exactly match the recursive Git links and the corresponding
table in `THIRD_PARTY_NOTICES.md`.

<!-- BEGIN RECURSIVE SUBMODULE INVENTORY -->
| Path | Component | Pinned commit | License status | Checkout repository |
|---|---|---|---|---|
| `42` | 42 spacecraft simulator | `37defafe5e3fabfaa5d51b15d8220c7faa800d2f` | NASA Open Source Agreement, exact version unresolved; asset-specific terms | `https://github.com/VoyagerTechnologies-Public/external-42` |
| `cfs/apps/cf` | cFS CFDP application | `deaeacfa0fe6ffd040a4d63e460941b591f5b169` | Apache-2.0 | `https://github.com/VoyagerTechnologies-Public/external-CF` |
| `cfs/apps/ci_lab` | cFS Command Ingest Lab | `68d91759f6f0cb50e8383dc358790598f8a277e8` | Apache-2.0 | `https://github.com/VoyagerTechnologies-Public/external-ci_lab` |
| `cfs/apps/ds` | cFS Data Storage | `f141f837199a7395c1c17fef3df6618f019cd08f` | Apache-2.0 | `https://github.com/VoyagerTechnologies-Public/external-DS` |
| `cfs/apps/fm` | cFS File Manager | `9b7a720b44f72dae9446dc558162e9f500be6a76` | Apache-2.0 | `https://github.com/VoyagerTechnologies-Public/external-FM` |
| `cfs/apps/io_lib` | cFS Input/Output Library | `c8a0771545a0828e6765a0b564a2412bcb9deb24` | NASA Open Source Agreement, exact version and agreement text unresolved | `https://github.com/VoyagerTechnologies-Public/external-CFS_IO_LIB` |
| `cfs/apps/lc` | cFS Limit Checker | `75f85088351f931958651a2b97d19ce5ffcb6567` | Apache-2.0 | `https://github.com/VoyagerTechnologies-Public/external-LC` |
| `cfs/apps/sc` | cFS Stored Command | `274d798553b915e94c7b02e516f40cf766deec10` | Apache-2.0 | `https://github.com/VoyagerTechnologies-Public/external-SC` |
| `cfs/apps/sch` | cFS Scheduler | `5aac56028b98fd3335b847bbd919a1371285f42c` | NASA Open Source Agreement, exact version and agreement text unresolved | `https://github.com/VoyagerTechnologies-Public/external-SCH` |
| `cfs/apps/to_lab` | cFS Telemetry Output Lab | `7c3a9b788ca4d209bfc42647b8bdb87ff5edf9b5` | Apache-2.0 | `https://github.com/VoyagerTechnologies-Public/external-to_lab` |
| `cfs/cfe` | Core Flight Executive | `3fcb62c31804ed11fb73670b86744e5735c90bc8` | Apache-2.0 | `https://github.com/VoyagerTechnologies-Public/external-cFE` |
| `cfs/osal` | Operating System Abstraction Layer | `3934fe868e08fc9e30e733a68ae674e1e47a5c2d` | Apache-2.0 | `https://github.com/VoyagerTechnologies-Public/external-osal` |
| `cfs/psp` | Platform Support Package | `6c71e0dd1aefe5392a100fecb29f13cf87d3e72c` | Apache-2.0; nested HWLib separately NASA-1.3 | `https://github.com/VoyagerTechnologies-Public/external-PSP` |
| `cfs/psp/fsw/hwlib` | Hardware Library | `c72e497b7ee714f350903a7d95ce523cb3c6985e` | NASA-1.3 per maintainer determination; authoritative agreement/designation absent from checkout | `https://github.com/VoyagerTechnologies-Public/external-hwlib` |
| `cfs/tools/elf2cfetbl` | cFS ELF-to-table tool | `e888aa04fd4dcb77ace5ac218300266fe568f1dc` | Apache-2.0 | `https://github.com/VoyagerTechnologies-Public/external-elf2cfetbl` |
| `comp/cryptolib` | Core Flight System Cryptography Library | `be524887ab9ad4efa8eed86e6988d7d81a129967` | NASA-1.3 | `https://github.com/VoyagerTechnologies-Public/external-CryptoLib` |
| `yamcs` | SHIRE YAMCS integration / Quickstart-derived source | `bbd841c09b7b1e57c230c5b3f2e097e348e9edf9` | Yamcs binary dependencies AGPL-3.0 | `https://github.com/VoyagerTechnologies-Public/external-yamcs` |
<!-- END RECURSIVE SUBMODULE INVENTORY -->

## Other source included in the root repository

| Component | Path | Version/source | License | Evidence and status |
|---|---|---|---|---|
| Unity C test framework | `simulith/test/unity/` | 2.6.1; `https://github.com/ThrowTheSwitch/Unity` | MIT | Version and SPDX identifier in source headers |

## Declared application dependencies

These entries describe direct declarations, not a resolved release dependency
graph. A final SBOM must record the actual resolved versions and hashes.

### Java and YAMCS

| Artifact | Declared version | Scope | Verified project license | Evidence/status |
|---|---|---|---|---|
| `org.yamcs:yamcs-core` | 5.13.0 | Runtime | AGPL-3.0 | Yamcs upstream license and POM |
| `org.yamcs:yamcs-web` | 5.13.0 | Runtime | AGPL-3.0 | Yamcs upstream license and POM |
| `org.yamcs:yamcs-maven-plugin` | 1.3.7 | Build | LGPL-3.0 | Official Yamcs plugin repository; retain the resolved artifact's license |
| `maven-compiler-plugin` | 3.15.0 | Build | Apache-2.0 | Apache Maven project license; retain the resolved artifact's license/notice |
| `maven-site-plugin` | 3.21.0 | Build | Apache-2.0 | Apache Maven project license; retain the resolved artifact's license/notice |
| `maven-project-info-reports-plugin` | 3.4.3 | Build/reporting | Apache-2.0 | Apache Maven project license; retain the resolved artifact's license/notice |

The project targets Java 17. The Maven wrapper and build image select Maven
3.9.9. Maven transitive dependencies are not enumerated in this curated SBOM.

### Python

| Manifest | Requirement | Constraint | License status |
|---|---|---|---|
| `cfg/requirements.txt` | `pyyaml` | Unpinned | MIT; resolved version/hash unresolved for release |
| `cfg/requirements.txt` | `jinja2` | Unpinned | BSD-3-Clause; resolved version/hash unresolved for release |
| `yamcs/requirements-commander.txt` | `yamcs-client` | `>=1.9.0` | LGPL-3.0; resolved version/hash unresolved for release |
| `yamcs/requirements-commander.txt` | `requests` | `>=2.31.0` | Apache-2.0; resolved version/hash unresolved for release |
| `comp/cryptolib/docs/wiki/requirements.txt` | `sphinx` | `>=8.0` | BSD-2-Clause; documentation-only, resolved version/hash unresolved |
| `comp/cryptolib/docs/wiki/requirements.txt` | `sphinx-rtd-theme` | Unpinned | MIT; documentation-only, resolved version/hash unresolved |
| `comp/cryptolib/docs/wiki/requirements.txt` | `myst-parser` | Unpinned | MIT; documentation-only, resolved version/hash unresolved |

Unpinned or ranged requirements can resolve to different artifacts over time;
the release process must lock, hash, and inventory the artifacts actually used.

### Native and security tooling

| Dependency | Version source | Use | License status |
|---|---|---|---|
| wolfSSL | `5.7.6-stable` in `comp/cryptolib/support/Dockerfile` | CryptoLib backend | Dual/commercial licensing selection for distributed artifacts unresolved |
| AFL++ | `v4.31c` in CryptoLib support tooling | Fuzz testing | Build/test-only; exact distributed scope and license files must be generated |
| libgcrypt | Distribution package; optional tooling references 1.11.0 | CryptoLib backend | Resolved package/license inventory required |
| libgpg-error | Distribution package; optional tooling references 1.50 | libgcrypt support | Resolved package/license inventory required |
| ZeroMQ / libzmq | Distribution package | Simulith transport | Resolved package/license inventory required |
| libcurl | Distribution package | HTTP/network support | Resolved package/license inventory required |
| libsocketcan | Distribution package | CAN hardware integration | Resolved package/license inventory required |

## Container images

| Image | Pinning | Used by | Release status |
|---|---|---|---|
| `debian:trixie-slim@sha256:d7e12182ce18b85b93007c1dedf31f2d29e01ccf3182cc4017c709b6259bc132` | Digest | `cfg/Dockerfile.base` | Base is pinned; resolved OS-package SBOM and licenses still required |
| `maven:3.9.9-eclipse-temurin-17` | Mutable tag | `yamcs/Dockerfile.yamcs` | Must be digest-pinned and scanned before release |
| `ubuntu:noble-20250127` | Date tag, no digest | CryptoLib support image | Must be digest-pinned if distributed |
| `ghcr.io/haisamido/x-vnc:latest` | Mutable tag | Default 42 graphical image base | pin, inventory, and license-review before release |
| `ghcr.io/voyagertechnologies-public/shire-base:0.0.0` | SHIRE tag | Build/runtime base | Generate SPDX and CycloneDX image SBOMs for final digest |
| `ghcr.io/voyagertechnologies-public/shire-yamcs:0.0.0` | SHIRE tag | YAMCS image | Generate SPDX and CycloneDX image SBOMs and include AGPL/source compliance material |

Packages installed by Dockerfiles without exact versions remain dependent on
the repository state at build time. Image SBOM generation must enumerate every
inherited and installed package and its license evidence.

## Build and release tooling

| Tool | Declared version or source |
|---|---|
| GNU Make, CMake, GCC/G++ | Host or container distribution packages |
| Apache Maven | 3.9.9 selected by wrapper/image |
| Eclipse Temurin JDK | 17 selected by image |
| Docker Engine and Compose | User prerequisite; version not pinned |
| SBOM and vulnerability scanners | **UNRESOLVED — select and record organization-approved tools and versions at release** |

## Required release actions

- Resolve every conspicuous unresolved entry with a written counsel or
  maintainer disposition.
- Restore missing authoritative license texts for NOSA components before
  distribution; do not substitute a generic agreement without confirmation.
- Generate and merge CycloneDX and SPDX inventories from resolved Maven,
  Python, native, source-archive, and container-image artifacts.
- Pin mutable images and downloaded source by digest or checksum.
- Preserve all upstream licenses, NASA notices, modification records, asset
  credits, source-availability information, and no-endorsement boundaries.
- Run `make licensing-check`, the approved vulnerability/license-policy scans,
  and every item in `RELEASE_CHECKLIST.md` against the final artifacts.
