# Components

This page explains what a component repository contains in SHIRE and walks through the demo component (`comp/demo`) as a concrete example.
Components are self-contained units that include everything a developer needs to exercise, test and run a single subsystem in the lab: CLI utilities, the flight application (cFS), unit tests, a deterministic simulator, and ground software artifacts.

The demo component is also the basis for the repository's component mold (see [Component Mold](./component-mold.md)).
Use it as a reference when adding new components.

## Repository structure (example: `comp/demo`)

* `comp/demo/cli/` - host-side command-line tools and their build files (CMake/Makefile).
* `comp/demo/src/` - flight application (cFS) source files.
* `comp/demo/shared/` - shared helpers used by both the flight app and simulator (e.g., device framing/parsers).
* `comp/demo/sim/` - simulator implementation that builds into a `.so` loaded by the Director.
* `comp/demo/support/` - templates and helpers (e.g. `device_config.j2`, `Dockerfile.cli`) used by the orchestrator and CI.
* `comp/demo/mission_inc/`, `comp/demo/platform_inc/` - mission & platform include files used when building FSW (perf IDs, msg IDs).
* `comp/demo/gsw/` - ground software artifacts (XTCE, display definitions) used by YAMCS or other ground systems.
* `comp/demo/test-fsw/` - unit tests for the cFS application and any shared logic.
* `comp/demo/test-sim/` - unit tests for the component simulator (loads `demo_sim.so` and drives the `component_interface_t` lifecycle).

Below we expand the minimal content for each piece and show where the demo files live and how they operate together.

## Command Line Interface (CLI)

Purpose: developer-facing host utilities for manual test and quick verification. 
The CLI connects to the same IPC transport the hwlib and simulator use so you can inject commands or observe telemetry without the full flight software and ground stack.

Files of interest:

* `comp/demo/cli/demo_cli.c` - simple example client that formats commands and prints responses.
* `comp/demo/cli/CMakeLists.txt`, `comp/demo/cli/Makefile` - build rules to produce the native host binary.

Behavior and usage:

* The CLI opens the Simulith IPC address for the device handle (same address hwlib and sim use) and sends the uint16-based command packets described below. It's useful for manual smoke tests while developing the app or sim.

Build example:

```bash
cd comp/demo/cli
make clean
make build
```

## cFS App (FSW)

Purpose: the real flight application that will be built into the cFE/cFS image.
In the lab this is run inside the FSW container and interacts with simulated devices via the hwlib.

Files of interest:

* `comp/demo/src/demo_app.c`, `comp/demo/src/demo_app.h` - application entry points, command handlers, and telemetry producers.
* `comp/demo/shared/demo_device.c`, `comp/demo/shared/demo_device.h` - device framing, parsing helpers and host-side helpers used by both the app and the sim.
* `comp/demo/mission_inc/demo_perfids.h`, `comp/demo/platform_inc/demo_msgids.h` - perf and message ID headers used by the app.

How it integrates:

* The app talks to hardware through the hwlib UART abstraction; in the simulator lab the hwlib is configured to use the Simulith transport (see `fsw/apps/hwlib/sim/src/libuart.c`) and will connect to the simulator's IPC address.
* The repository's orchestrator may prune `cpu1_cfe_es_startup.scr` so the demo app is only started for spacecraft configurations that include it.

## cFS Unit Tests

Purpose: unit tests for application logic and shared framing/parsing helpers.

Files of interest:

* `comp/demo/test-fsw/` - contains the CMake/unit-test harness and stubs for running the app logic in a host environment.

Build example:
```bash
make cfg
make debug 			# Starts docker container shell for debug use
cd fsw
make build-test
exit 				# Exit docker container
```

Notes:

* Unit tests should exercise framing, pack/unpack, and high-level command handlers. 
* CI should run these tests before integration runs that also exercise the simulator and Director.

## Simulation

Purpose: provide a deterministic runtime model of the device that can be loaded by the `shire-director`.
The sim maintains device state, maps 42 dynamics into telemetry where appropriate, and participates in the Simulith transport to exchange UART-style bytes with the FSW hwlib.

Files of interest:

* `comp/demo/sim/demo_sim.c`, `comp/demo/sim/demo_sim.h` - the simulator component implementing `init`, `tick`, `cleanup`, and `backdoor` and registering via `REGISTER_COMPONENT(demo_sim)`.
* `comp/demo/sim/CMakeLists.txt`, `comp/demo/sim/Makefile` - build rules producing `demo_sim.so`.
* `comp/demo/shared/demo_device.c` - shared framing helpers used by sim and app.
* `comp/demo/support/device_config.j2` - optional Jinja2 template used by the orchestrator to render `comp/demo/shared/device_cfg.h`.

Simulator-specific behavior and helpers:

* The sim binds an IPC address of the form `ipc:///tmp/simulith_pub:<base + handle>` and typically sets `is_server=1` so the simulator accepts connections from hwlib (which connects as a client).
* Every Director tick the sim receives a `simulith_42_context_t` and may map dynamics (e.g., `sun_vector_body`) to telemetry channels. The demo sim also inspects incoming bytes and replies using the same framing used by the app.
* The sim supports backdoor commands to be used for testing.

Build example:
```bash
make cfg
cd comp/demo/sim
make
```

Building and running CLI with simulator:
```bash
make cfg
# Confirm ./cfg/active.yaml has `cli: demo` on the first line
# Adjust `log_mode` as desired (none, stdout, file, or both)
make sim
make cli
make cli-start
```

## YAMCS / Ground Software

Purpose: provide XTCE-based definitions for command/telemetry and displays for operators.

Files of interest:

* `comp/demo/gsw/demo.xtce` - contains the CCSDS/XTCE descriptions for commands and telemetry used by YAMCS or other ground ingest systems.
* `comp/demo/gsw/displays/` - example displays used by the ground system.

Usage:

* Import the XTCE file into YAMCS or your ground system to get formatted telemetry fields. 
* The XTCE file mirrors the framing and field definitions used by the FSW.

----
Last updated: 20251203
