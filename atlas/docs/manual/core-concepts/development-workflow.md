# Development Workflow

SHIRE supports two concrete development loops in the current repository: a focused component loop and a full mission lab loop.

## Component loop

Use the component loop while developing a device protocol, simulator, or shared library:

1. Select a mission, spacecraft, scenario, and CLI component in `build/active.yaml`.
2. Run `make cfg` to render the selected component configuration.
3. Run `make cli` to build 42, Simulith, the selected simulator, and the component CLI image.
4. Run `make cli-start` to start the CLI compose environment.
5. Exercise commands and telemetry through the component CLI.
6. Run `make test-sim` and the relevant component tests after changes.

The CLI and cFS application should share device framing and interpretation code from the component's `shared/` directory where practical.
This reduces drift between direct checkout and integrated operation.

## Full mission loop

Use the full lab to verify integration across cFS, YAMCS, CryptoLib, the Radio simulator, component simulators, and 42:

```bash
make cfg
make
make start
```

Inspect YAMCS links and FSW/Director logs before running procedures.
Stop with `Ctrl+C`, then use `make stop` if Compose did not shut down cleanly.

## Supported build targets

| Command | Current behavior |
| --- | --- |
| `make cfg` | Resolves configuration and writes generated artifacts. |
| `make list` | Reports the resolved target and enabled component build features. |
| `make sim` | Builds 42, Simulith, selected component simulators, Director, and Server images. |
| `make fsw` | Builds the configured cFS target and FSW runtime image. |
| `make gsw` | Builds CryptoLib and the YAMCS runtime image. |
| `make` | Runs configuration, then builds simulation, FSW, and GSW. |
| `make cli` | Builds the selected component CLI environment. |
| `make test-sim` | Builds Simulith, runs the selected component simulator tests, and produces combined simulator coverage. |
| `make test-fsw` | Builds and runs cFS/application tests and produces coverage output. |

## Moving toward hardware

The component layout is intended to keep protocol handling useful across simulation and hardware checkout:

* Start with the CLI and simulator.
* Use the CLI with a physical device when an appropriate transport implementation exists.
* Integrate the shared protocol code into the cFS application.
* Select target specific HWLIB and PSP implementations.
* Repeat interface, timing, failure, and system tests on the actual target.

This is a workflow, not a guarantee of seamless portability.
Electrical behavior, driver semantics, concurrency, timing, endianness, alignment, and target resource limits must be verified on hardware.

## Automation status

`.github/workflows/ci.yml` runs on pull requests and pushes to `main` or `dev`.
It defines separate Simulith, FSW, and CLI build jobs, runs the FSW and component simulator test builds, and uploads their coverage to Codecov.
`.github/workflows/docs.yml` separately builds and publishes the Atlas on pushes to `main` or `dev`.

These jobs do not currently run the YAMCS submodule tests or a complete full lab scenario.
When a CI result is used as evidence, retain the workflow run, logs, coverage, resolved configuration, container image, and revision identifiers rather than treating the presence of the workflow file as a passing result.
