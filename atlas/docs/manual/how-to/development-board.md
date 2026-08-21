# Development Board

SHIRE contains cross compilation scaffolding for a 32 bit ARM Zybo 7020 target, but the target is disabled and is not part of the automated test path.
Treat this page as a target enablement plan rather than evidence of a validated hardware deployment.

## Establish the target before component development

When a program intends to use a specific development board, establish the board target and its complete configuration before beginning component integration.
This keeps compiler, operating system, boot, driver, permission, deployment, and ground communication problems outside the component development loop.

The platform baseline should prove that the board can boot the selected software environment, run a minimal cFS configuration, expose the required hardware buses, and exchange commands and telemetry through the intended development path.
This baseline does not validate any physical component.
It provides a stable environment in which component behavior can be evaluated.

After the platform baseline passes, develop each component from the smallest useful boundary.
Proceed through CLI with Simulith, cFS with Simulith, board CLI with hardware, simulator reconciliation, and board cFS with hardware.
See [Development Workflow](../core-concepts/development-workflow.md) for that progression.

## Current repository boundary

| Area | Current state |
| --- | --- |
| cFS target | `cfg/shire_defs/targets.cmake` enables only `cpu1`, while the `cpu2` declarations remain commented out. |
| CPU2 mission files | CPU2 platform configuration and startup files exist under `cfg/shire_defs/`. |
| ARM toolchain | `cfg/shire_defs/toolchain-armv7l-linux.cmake` selects the `arm-linux-gnueabihf` compiler family and `/usr/arm-linux-gnueabihf` sysroot. |
| Build image | `cfg/Dockerfile.base` does not install the ARM compiler, ARM sysroot, or target versions of required libraries. |
| cFS abstractions | The ARM toolchain currently selects the generic cFS `pc-linux` PSP and POSIX OSAL rather than board specific support. |
| Component CLI | Each reference CLI has a `cpu2` branch that selects Linux HWLIB sources for its bus. |
| CLI toolchain | The current CLI Makefiles pass `TGTNAME=cpu2` but do not select the ARM CMake toolchain or provide packaging and deployment. |
| Device configuration | The orchestrator renders one `device_cfg.h` per selected component and does not produce separate host and board settings. |
| Startup selection | The orchestrator currently prunes the CPU1 startup script for the selected spacecraft but does not perform the same operation for CPU2. |
| Automation | Current CI builds and tests host targets only. |

The CPU2 CLI branches currently map ADCS and Demo to Linux UART, EPS to Linux I2C, and Radio to Linux SPI and GPIO.
These source selections are useful scaffolding, but they do not demonstrate that the drivers match the board kernel, device tree, or hardware connections.

## Target enablement sequence

Complete this sequence before using the board as a component development target.

### 1 - Define the platform baseline

Record the board model and revision, processor, boot image, operating system, kernel, device tree, storage layout, network configuration, and recovery method.
Choose the PSP, BSP, and OSAL combination that matches that environment.
If the board runs Linux and retains the generic `pc-linux` PSP and POSIX OSAL, validate every required service rather than assuming host Linux behavior transfers to the board.

Define the initial cFS application set and startup behavior.
Start with the smallest application set needed to prove boot, events, command ingest, and telemetry output before enabling mission components.

### 2 - Create a reproducible build environment

Add the selected ARM compiler, sysroot, target headers, and target libraries to a reviewed build image or another reproducible environment.
Pin their versions and record their source.
Confirm that the toolchain can compile and link a minimal program before using it for cFS or a component CLI.

Review `toolchain-armv7l-linux.cmake` against the chosen environment.
Its current compiler paths, sysroot, search paths, PSP, and OSAL values are repository scaffolding rather than a validated board definition.

### 3 - Complete the CPU2 mission configuration

Enable `cpu2` in `cfg/shire_defs/targets.cmake` only after the toolchain is available.
Review its processor ID, system name, application list, startup file, platform configuration, message resources, scheduler resources, and table set.

Update the configuration generator so CPU2 receives the intended spacecraft component selection.
The current orchestrator modifies only `cpu1_cfe_es_startup.scr`, even though `cpu2_cfe_es_startup.scr` is present.

Define how component device settings differ between host simulation and the board.
The current generator writes one shared `device_cfg.h`, so a complete target configuration must prevent simulated endpoints from being reused accidentally for physical hardware.

### 4 - Establish deployment and runtime control

Define how binaries and tables reach the board, how cFS starts, where logs and persistent files live, and how an operator stops or rolls back the software.
Configure user and group permissions for UART, I2C, SPI, GPIO, network, storage, and any other required interfaces.

Do not treat container privileges or host development permissions as a board deployment design.
Record the minimum permissions required by the target runtime.

### 5 - Prove the base target

Build CPU2 and inspect the generated output before transferring it.
Confirm the executable architecture, dynamic library requirements, startup files, tables, and configuration match the board image.

Boot the minimal cFS target without mission components.
Verify startup, timekeeping, command ingest, telemetry output, event reporting, file access, clean shutdown, restart, and recovery from a failed launch.

Exercise each required physical bus with a bounded platform test or safe fixture.
Do not use an unreviewed mission component as the first proof that a board driver works.

### 6 - Add automated evidence

Add an automated CPU2 cross compilation job once the build is reproducible.
Retain the toolchain identity, resolved target configuration, build logs, executable metadata, and packaged artifact.

A cross compilation job cannot replace hardware tests.
Use board based tests for boot, timing, interfaces, permissions, command handling, telemetry, restart, and fault response.

### Target readiness gate

Begin target dependent component work only when:

1. The toolchain and sysroot are reproducible
2. The selected PSP, BSP, OSAL, and boot environment are documented
3. CPU2 builds with the intended application and startup configuration
4. The board runs the minimal cFS target repeatably
5. Command and telemetry paths work without a mission component
6. Required hardware buses and runtime permissions have bounded platform tests
7. Deployment, logging, shutdown, rollback, and recovery are documented
8. Host and board device configurations cannot be confused silently

## Component integration after target readiness

Once the target readiness gate passes, use the component mold and host simulation workflow to develop the device contract.
The platform first recommendation does not replace focused component testing.
It ensures that later board results measure the component rather than an unfinished target environment.

Run the board CLI before board cFS for each physical component.
The current CLI source can select Linux HWLIB implementations with `TGTNAME=cpu2`, but the Makefiles do not cross compile or deploy those executables.
Add a reviewed CLI target that selects the ARM toolchain, the board device configuration, packaging, transfer, permissions, and launch behavior.

After the board CLI passes, reconcile the simulator with the hardware evidence and rerun host regression tests.
Then integrate the component application into the validated CPU2 cFS target and repeat the focused YAMCS procedure through the intended command and telemetry path.
Use the [Development Workflow](../core-concepts/development-workflow.md) for the component gates and evidence expected at each stage.

## Packaging a validated CPU2 build

If a successful build creates `build/<mission>/<spacecraft>/fsw/exe/cpu2/`, inspect that exact directory before packaging it:

```bash
tar -C build/<mission>/<spacecraft>/fsw/exe/cpu2 \
  -czvf cpu2_fsw.tar.gz .
```

Record a manifest and hashes for the packaged files.
Transfer and installation procedures depend on the validated board image and operating environment.
Serial or ZMODEM transfer is not presented as a generally validated SHIRE deployment procedure.

## Verification boundary

A successful cross compilation proves only that the source built with the selected toolchain.
A successful base target test proves only the named board and platform configuration.
Neither establishes component compatibility or flight readiness.

Record the exact toolchain, sysroot, SHIRE revision, generated configuration, board revision, boot image, device tree, deployed artifact, and test results.
Keep host simulation regression results with the board evidence so later hardware changes can be compared with a repeatable baseline.

***
Last reviewed: 20260817
