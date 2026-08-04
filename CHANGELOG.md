# Changelog

All notable changes to SHIRE are documented here.

This project follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Version numbers follow MAJOR.MINOR.PATCH:
- **MAJOR** — breaking changes to the component API, build system interface, or submodule structure
- **MINOR** — new components, new scenarios, new capabilities, or deprecation notices
- **PATCH** — bug fixes, documentation corrections, security patches, and dependency updates

## [Unreleased]

### Added
- Open-source release preparation: governance files (`CONTRIBUTING.md`, `SECURITY.md`, `CODE_OF_CONDUCT.md`, `CLAUDE.md`)
- Component lifecycle guide (`atlas/docs/manual/handbook/component-lifecycle.md`)
- Submodule workflow documentation in Getting Started guide
- Dependabot configuration for automated dependency updates
- CodeQL workflow for C/C++ and Java static analysis
- Cosign keyless signing for GHCR container images (`shire-base`, `shire-yamcs`)
- Environment variable documentation added to Atlas Getting Started guide
- Pinned Python dependencies in `cfg/requirements.txt`

### Changed
- Repository migrated to a public repository
- All container images moved to `ghcr.io/voyagertechnologies/` registry
- `yamcs/` extracted to `VoyagerTechnologies/shire-yamcs` and wired back as a submodule
- External vendored directories converted to git submodules (`external-42`, `external-cfe`, `external-osal`, `external-psp`, nine cFS apps, `external-cryptolib`)
- `VNC_PASSWORD` fallback removed; variable is now required at container start
- YAMCS `secretKey` moved from hardcoded `changeme` to `YAMCS_SECRET_KEY` environment variable

### Security
- Removed hardcoded VNC password (`cfg/entrypoint-42.sh`)
- Removed hardcoded YAMCS session secret (`yamcs/src/main/yamcs/etc/yamcs.yaml`)
