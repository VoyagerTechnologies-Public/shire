# Zybo Linux PIL

Run the ARM32 cFS bundle on a physical Zybo 7020 while SHIRE drives time,
42 dynamics, and simulated devices on the host.
This validates the processor and network PIL path.
It does not qualify the board's physical I2C, SPI, UART, GPIO, or FPGA peripherals.

The reusable launcher and configuration contract are documented in [Configurable Linux Board PIL](board-pil.md).
The Zybo settings live in `cfg/boards/zybo7020.yaml`.
Select this profile with `BOARD=zybo7020` when using the shared `board-*` Make commands.

## Runtime boundary

The board keeps its existing Linux kernel and boot image.
The deployed userspace comes from the same public Buildroot sources and ARM SDK
used by [Zynq QEMU PIL](qemu-pil.md).
It runs in a temporary chroot with separate mount, IPC, and PID namespaces.
Only null, zero, and random device nodes are created inside that runtime.
The board's physical bus device nodes are not exposed.

The checked board runs PetaLinux 2025.1, Linux 6.12.10, ARMv7 hard-float,
and glibc 2.39 on two Cortex-A9 cores.
No PetaLinux SDK, vendor image rebuild, or proprietary tool is needed for the SHIRE deployment.
The existing board boot image is not redistributed by this workflow.

The namespace sets its own POSIX message-queue limit and uses root scheduling permissions.
The board's original message queues and queue limits remain outside that namespace.
Closing the test session terminates the namespace and its cFS processes.
Files under the temporary runtime directory remain until removed or the board reboots.

## Connect

Connect the Zybo Ethernet port to the host adapter and log in through its serial console.
For the current direct-link setup, the board uses `169.254.55.55/24` on `end0`
and the host uses `169.254.55.56/24` on its Ethernet adapter.
Remove a duplicate `169.254.55.55` address from the board's `dummy0` interface if present.
Keep host and board addresses distinct.

On the host, substitute the actual adapter name for `eth6`:

```bash
sudo ip addr add 169.254.55.56/24 dev eth6 scope link
ip route get 169.254.55.55
ssh petalinux@169.254.55.55
```

Adding the address is only needed if it is not already configured.
Use `scope link` when the adapter already has a link-scoped address in the same subnet.
These address changes are temporary.

The board commands establish an SSH control connection automatically if it is missing
or has expired, prompting for normal SSH authentication.
You can also connect explicitly before inspecting or deploying:

```bash
make board-connect BOARD=zybo7020
make board-inspect BOARD=zybo7020
```

The connection expires after 15 minutes of inactivity.
The next board command reconnects automatically without storing an account password.
SSH host keys are retained in `build/zybo/known_hosts`.
To request account password authentication instead of an SSH key when reconnecting:

```bash
python3 tools/shire-board.py inspect --profile zybo7020 --password-auth
```

An existing live connection is reused without another SSH authentication prompt.
The test command separately prompts for sudo authentication and keeps that password
in memory only for the duration of its trials.
Use `--target user@address` and `--ssh-control /absolute/socket/path` for another connection.

## Build and deploy

For a checkout using the earlier Zybo-specific helper, run `make board-build board-deploy BOARD=zybo7020`
once to create a profile-stamped deployment with the common runtime scripts.
Existing retained reports and Yamcs archives remain available.
The generic CLI replaces the earlier `tools/shire-zybo.py` entry point.

Build the public platform, ARM application bundle, and host services:

```bash
make board-prepare BOARD=qemu-zynq
make board-build BOARD=zybo7020
make board-inspect BOARD=zybo7020
make board-deploy BOARD=zybo7020
```

Build writes a separate board bundle under `build/zybo/<mission>/<spacecraft>/bundle`.
It also builds the host 42, Simulith/device simulator, CryptoLib, and Yamcs images.
After `make clean`, repeat `make board-build board-deploy BOARD=zybo7020` before starting or testing the lab.
Deploy cross-compiles a small UDP tunnel with the profile-selected ARM SDK and uploads
the public root filesystem and application into a fresh `/tmp/shire-pil-*` directory.
It verifies all application hashes on the board and checks the executable's library dependencies.
The deployment manifest is `build/zybo/deployment.json`.
The boot image, network configuration, and system package installation remain under the operator's control.
Docker is needed on the host, not on the board.

## Run with Yamcs

After deployment, stop any other SHIRE lab and launch the interactive board profile:

```bash
make board-start BOARD=zybo7020
```

Enter the board's sudo password when prompted and keep this terminal open.
The command starts the host simulators, runs cFS on the physical board,
and creates a separate retained Yamcs archive.
Open [Yamcs](http://localhost:8090) and select the `shire` instance and `realtime` processor.
Use the normal command browser, telemetry displays, events, and visualization.
The `debug-out` and `debug-in` links carry board commands and telemetry through the tunnel.
For a simple command check, send `/ADCS/ADCS_NOOP_CC` and watch the cFS output in the start terminal.

For a demo with matching live output in minicom, start the optional serial mirror:

```bash
make board-start BOARD=zybo7020 BOARD_ARGS="--serial-console /dev/ttyPS0"
```

In another host terminal, open `minicom -D /dev/ttyUSB1 -b 115200 -8`
with hardware and software flow control disabled.
The board uses `/dev/ttyPS0`, while `/dev/ttyUSB1` is the host's USB serial endpoint.
Both terminals show the same cFS event lines from one board process.
The serial transcript also includes session metadata.
The minicom keyboard remains the board's Linux console, so use Yamcs for cFS commands.
No flight software rebuild or redeployment is needed for this session override.
See [Configurable Linux Board PIL](board-pil.md#show-the-same-output-on-a-serial-console) for profile defaults and requirements.

The default simulation speed is real time.
Override `SIMULITH_SPEED` in the environment when needed.
The start command uses manual operation rather than injecting the acceptance scenario.
The board's configured startup RTS still runs.
SHIRE pause and resume controls remain available through Yamcs.
When paused, flight time waits for the next host tick.

Stop from another terminal:

```bash
make board-stop BOARD=zybo7020
```

Ctrl-C in the start terminal also stops the lab.
Stop terminates board cFS, stops the host services, removes the session's SSH forwards,
and retains logs under `build/zybo/sessions/` plus the Yamcs archive for replay.
The stop command can clean up the host side even if SSH is no longer available.
Startup does not rebuild or redeploy flight software.
Run `make board-build board-deploy BOARD=zybo7020` after changing the ARM flight application.
Redeploy after rebooting the board because its temporary runtime directory is lost.

## Validate

Stop any existing SHIRE lab before testing:

```bash
python3 tools/shire-board.py test --profile zybo7020 --duration 10 --trials 1
make board-test BOARD=zybo7020
```

The short command checks connectivity and startup and allows time for the
startup RTS to enable board telemetry.
Runs that end before that enable command fail the telemetry gate.
The default acceptance run executes three 30-second simulated-time trials.
It reuses the QEMU full-stack gates for synchronized ticks, completed FSW work,
device transactions, accepted commands, dynamics, complete control traces,
Yamcs truth, archives, and repeatability.
It additionally requires valid board telemetry on Yamcs's `debug-in` link.

Compare each trial with a passing 30-second QEMU report when available:

```bash
python3 tools/shire-board.py test --profile zybo7020 \
  --baseline build/qemu/reports/<baseline-run>/report.json
```

SSH reverse forwards carry all synchronization and device TCP connections.
A small framed TCP tunnel carries UDP commands and telemetry while preserving datagram boundaries.
This works when the host accepts SSH replies but blocks new inbound board connections.
Published host TCP ports bind only to loopback.
SHIRE still waits for every PREPARE, EXECUTE, COMMIT, and transitive device completion.

The FSW container is a host bridge that forwards commands, telemetry, and board logs.
The cFS executable runs on the physical Cortex-A9 processor.
Docker resource statistics therefore describe the bridge, not board CPU utilization.
Measured speed includes the SSH transport overhead and excludes startup.

Reports and service logs are retained under `build/zybo/reports/`.
The report identifies the board, deployed bundle, public userspace, validation scope,
and ground link counters.
Per-session board output is also retained under `build/zybo/sessions/`.
The test stops its processes and removes its SSH forwards on exit.
Yamcs run archives are retained for replay.

Close the control connection when finished:

```bash
ssh -S "$PWD/build/zybo/ssh-control" -O exit petalinux@169.254.55.55
```

## Physical hardware follow-on

PIL device transactions exercise host simulator interfaces over Ethernet.
Physical bus testing needs an explicit fixture, reviewed pin and voltage assignments,
device-tree support, suitable drivers, and a separate board configuration.
Continue with [Development Board](development-board.md) for that platform work.
This PIL result does not establish physical peripheral compatibility or flight readiness.

***
Last reviewed: 20261021
