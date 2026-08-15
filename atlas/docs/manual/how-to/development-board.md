# Development Board

SHIRE contains cross compilation scaffolding for a 32 bit ARM Zybo 7020 target, but that target is **disabled in the default configuration** and is not part of the documented automated test path.
Treat this page as integration guidance, not evidence of a validated hardware deployment.

## What exists in the repository

* `cfg/shire_defs/targets.cmake` defines the active host target as `cpu1`.
  The `cpu2` declarations are commented out.
* `cfg/shire_defs/toolchain-armv7l-linux.cmake` selects the `arm-linux-gnueabihf` compiler family and an ARM sysroot.
* Component command line builds contain conditional handling for a `cpu2` target.
* The ARM configuration currently selects the cFS `pc-linux` PSP and POSIX OSAL rather than a BSP made for the board.

## Before enabling CPU2

1. Install and validate the ARM compiler and target sysroot expected by the toolchain file.
2. Review the PSP, BSP, OSAL, device drivers, startup script, and application set for the board image.
3. Enable `cpu2` in a working copy of `cfg/shire_defs/targets.cmake` and regenerate the mission configuration.
4. Build and inspect the generated CPU2 output.
   Do not assume the path exists until the build for the target completes successfully.
5. Establish hardware tests for boot, timing, interfaces, command handling, telemetry, and fault response.

## Packaging a validated build

If a successful build creates `build/<mission>/<spacecraft>/fsw/exe/cpu2/`, package that exact directory only after inspecting its contents:

```bash
tar -C build/<mission>/<spacecraft>/fsw/exe/cpu2 \
  -czvf cpu2_fsw.tar.gz .
```

Transfer and installation procedures depend on the board image and operational environment.
The earlier serial/ZMODEM steps are therefore not presented as a generally validated SHIRE deployment procedure.

## Verification boundary

A successful target build demonstrates only that the source built for the selected toolchain.
It does not establish board compatibility or flight readiness.
Record the exact toolchain, sysroot, SHIRE revision, board revision, boot image, and test results for any hardware qualification work.
Use the staged [Component Hardware Development](../../scenarios/component-hardware-development.md) scenario before attempting board cFS with a physical component.

***
Last reviewed: 14 August 2026
