# Zynq QEMU PIL

Run ARM32 cFS in upstream QEMU while SHIRE controls simulation time and device models on the host.
This is the processor software baseline for a later Zybo Z7-20 deployment.
The emulator uses the ZC702 processor-system reference and does not reproduce Zybo FPGA peripherals.
Physical Zybo acceptance is a separate gate.

## Prerequisites

Use a Linux host with Docker, a recent Python 3, and PyYAML.
The tools use current Python syntax and no script checks the interpreter version, so use the Python 3 release of a current distribution.
The first build downloads public sources and packages.
Allow several gigabytes for the SDK, kernel build, and source/license archive.
The tools image builds on the public SHIRE base image.
No PetaLinux installation, Vivado installation, private registry, or proprietary boot image is required.

## Build

Select the `qemu-zynq` board profile to use the shared processor PIL workflow:

```bash
make board-prepare BOARD=qemu-zynq
make board-build BOARD=qemu-zynq
make board-deploy BOARD=qemu-zynq
```

`board-prepare` builds the public guest kernel, root filesystem, and ARM32 SDK.
`board-build` builds 42, Simulith, configured device simulators, CryptoLib, Yamcs,
and the ARM cFS bundle.
`board-deploy` validates the bundle hashes and ELF ABI, packages the guest initramfs,
builds its runtime image, and renders Compose without rebuilding cFS.
Start and test check all required local images before allocating a run archive.
If an image has been removed, the error names its rebuild command.
For example, restore a missing `shire-42-<mission>:<spacecraft>` image with `make 42`.
The QEMU profile uses local runtime images and disables automatic registry pulls.

The platform uses the pinned Buildroot 2025.02.6 `zynq_zc702_defconfig` with the small configuration fragment in `cfg/qemu/zynq.fragment`.
It boots Linux directly, uses a public Bootlin ARMv7 hard-float glibc toolchain, and includes ZeroMQ and libgcrypt.
Buildroot verifies source hashes and retains applicable source and license information under `build/qemu/output/legal-info/`.
Review its generated README for any toolchain source distribution notes.
The platform manifest is `build/qemu/platform.json`.

The emulation kernel uses one CPU, disables SMP, and selects non-preemptive kernel execution
to reduce guest scheduling and locking overhead.
It retains high-resolution timers and the default ARM clocksource.
These settings apply to the QEMU image.
Physical Zybo validation uses its own kernel.

The target build reuses SHIRE's synchronized PSP, OSAL, scheduler, and software-bus observer.
Its compiler and libraries come from the guest SDK.
It does not use the disabled generic-Linux CPU2 scaffold.
ARM artifacts stay under `build/qemu/<mission>/<spacecraft>/`, separate from native host artifacts.

`make board-inspect BOARD=qemu-zynq` inspects local platform provenance and checks the
bundle hashes and architecture when a bundle is present.
It does not require a running guest.
The QEMU profile supports only the Zynq ZC702 reference model.
It does not emulate RPi5.
`--work-dir` must remain inside the checkout and must be repeated for subsequent commands.
SSH hardware overrides and physical serial mirroring do not apply to this profile.

## Run

```bash
make board-start BOARD=qemu-zynq
```

Open Yamcs at `http://localhost:8090`.
The start command allocates a separate retained Yamcs archive for the run.
The guest starts without a display and writes cFS output to the emulator's serial console.
QEMU user networking forwards guest command port 1234 through the FSW service.
It requires no TAP device or host network administration.

To attach to the server controls in another terminal:

```bash
docker attach shire-server-drm
```

Use `pause`, `step`, and `resume`.
`step` runs one complete PREPARE/EXECUTE/COMMIT tick and remains paused.
The existing `p`, speed controls, and Yamcs pause/play commands remain available.
Detach from Docker with Ctrl-P followed by Ctrl-Q.
Pause affects SHIRE simulation time.
Linux and network timeouts continue to use operating-system time.

For SSH, use the helper and the per-checkout key generated under `build/qemu`:

```bash
make board-connect BOARD=qemu-zynq
```

Override `SHIRE_QEMU_SSH_PORT` if port 2222 is occupied.
The helper keeps a separate SSH host-key record for each guest launch because the ephemeral guest generates fresh host keys.
The writable guest is recreated at each launch and does not modify the base initramfs.
Hostnames and selected runtime settings are injected into a separate initramfs archive.

```bash
make board-stop BOARD=qemu-zynq
```

Stop saves Compose logs under `build/qemu/runs/` and retains Yamcs archives.
Use the existing replay commands described in [Visualization and Replay](visualization-replay.md).

## Acceptance

Stop any running SHIRE lab before the automated check:

```bash
make board-test BOARD=qemu-zynq
```

The default runs three 30-second simulated-time trials using the existing full-stack validation runner and DRM acceptance scenario.
It checks synchronized ticks, device transactions, command acceptance, complete control traces, advancing Yamcs truth, archive replay readiness, and repeatability.
Reports and service logs are retained under `build/qemu/reports/`.
Achieved speed is measured without a real-time pass threshold.
Host process statistics describe the emulator process, not individual guest cFS threads.

To compare a change against a saved passing run with the same duration and scenario:

```bash
make board-test BOARD=qemu-zynq BOARD_ARGS="--baseline build/qemu/reports/<baseline-run>/report.json"
```

Each trial must also match the baseline's deterministic counts and dynamics trace.
The report records the baseline path and hash, median simulation speed, and speedup ratio.
Timing covers simulated execution and excludes guest boot and ground-system startup.
Use matching synchronization settings when comparing QEMU with native execution.

A shorter boot and connectivity check is available during development:

```bash
make board-test BOARD=qemu-zynq BOARD_ARGS="--duration 3 --trials 1"
```

This shorter run moves the two acceptance commands within its duration.
It does not replace the default acceptance run or physical board tests.

## Network configuration

| Setting | Purpose |
| --- | --- |
| `SIMULITH_PUB_BIND`, `SIMULITH_REP_BIND` | Synchronization server bind endpoints. |
| `SIMULITH_PUB_ADDR`, `SIMULITH_REP_ADDR` | Participant connection endpoints. |
| `SIMULITH_SYNC_TRANSPORT=zmq` | Use message barriers for every participant in a network session. |
| `SIMULITH_DEVICE_TRANSPORT=tcp` | Map logical simulated-device IPC endpoints to TCP. |
| `SIMULITH_DEVICE_HOST` | Host running the device models, required for TCP device clients. |
| `SIMULITH_DEVICE_BIND` | Optional device-model bind address with TCP default `0.0.0.0`. |
| `SIMULITH_START_PAUSED=1` | Wait for explicit control before executing the first tick. |

Unset settings preserve the native host IPC/shared-memory workflow.
The QEMU Compose file configures all participants consistently.
It also enables `SHIRE_SYNCHRONIZE_GROUND_OUTPUT=1` so each scheduled downlink drain completes before simulation time advances.
It does not mix shared-memory and network barriers within a session.
An incomplete participant or device transaction prevents completion of the corresponding phase.

## Physical board follow-on

The [Zybo Linux PIL](zybo-pil.md) workflow deploys this public ARM userspace
to a physical board without replacing its existing kernel or boot image.
It reuses these acceptance checks and adds a board telemetry link check.

The available Zybo already has Linux with Ethernet and SSH access.
First inspect its architecture, hard-float ABI, dynamic loader, glibc version, kernel message-queue support, and available storage.
The QEMU bundle uses glibc 2.39.
An older board userspace can require rebuilding against its compatible SDK or deploying a matching open-source root filesystem.
Reuse the existing boot environment where compatible, then repeat the acceptance scenarios through Ethernet and a serial console.
Any replacement board image needs Zybo-specific bootloader initialization and a device tree.
The QEMU ZC702 reference image is not an SD-card image for the Zybo.
QEMU success does not establish board boot, physical bus compatibility, or flight readiness.

***
Last reviewed: 20261027
