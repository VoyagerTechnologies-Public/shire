# Processor in the Loop

## Mission brief

Everything so far ran the flight software on the same computer as the simulation.
The flight computer is a different processor with a different instruction set, a different kernel, and slower timing, and some faults appear only there.
The team moves the flight software onto a processor of the flight kind, while everything else stays simulated.
This is the twelfth and last lesson in the [lesson plan](overview.md#the-lesson-plan), in the Engineering and analysis unit.
The engineering lessons work in a terminal, so this one shows command output in place of screenshots.

## Objective

In this scenario, you will learn how SHIRE runs the flight software on a target processor.
As an engineer, your job is to build the target platform and the flight software for it, start the lab with the flight software running on an ARM Linux guest, prove that the flight software is running on that processor, run the acceptance check, run the nominal pass from [Nominal Operations](nominal-operations.md) on it, and state what this proves about the flight computer and what it does not.

## Prerequisites

This lesson needs three earlier lessons, [Commissioning](commissioning.md), [Simulation Time and Repeatability](simulation-time-repeatability.md), and [Nominal Operations](nominal-operations.md).
You use these skills from them:

* Starting the lab and reading its startup output
* Knowing that SHIRE controls simulation time, and that pausing it stops the simulated world
* Running the pass and reading a scenario result

Before you begin, ensure you have:

* Completed [Getting Started](../manual/handbook/getting-started.md)
* A Linux host with Docker, Python 3, and PyYAML, and several gigabytes of free disk for the first platform build
* About an hour, and much longer the first time, because the first platform build downloads and compiles a kernel and a root filesystem

This lesson starts from a clean checkout.
Stop any running lab with `make stop` before you start, and run one lab at a time.

## Overview

Processor in the loop, or PIL, puts the real flight software binary on the target processor and keeps everything else on the host.
The 42 dynamics simulator, the Simulith server and its clock, the device simulators, CryptoLib, and YAMCS all run on the host as before.
Only the cFS applications run on the target, and they talk to the simulated devices over the network.
The name says what happens, because the flight software is in the loop on the processor and the devices are still simulated.

| Profile | Target | What it is |
| --- | --- | --- |
| `qemu-zynq` | An emulated Zynq ZC702 board | Open source ARM32 Linux in the QEMU emulator, which any host can run |
| `zybo7020` | A physical Zybo Z7 board | The same ARM32 userspace on real hardware, with retained acceptance results |
| `rpi5` | A physical Raspberry Pi 5 | An AArch64 Linux board, with its build and acceptance run validated |

This lesson uses `qemu-zynq`, because it needs no hardware.
The other profiles use the same commands, and the [Configurable Linux Board PIL](../manual/how-to/board-pil.md) guide describes them.

You will follow these phases:

* Prepare the platform
* Build and deploy
* Start the lab and look at the processor
* Run the acceptance check
* Run the pass on the processor
* Decide what this proves

The output below comes from one set of runs.
Your times and values will differ.

## Prepare the platform

The platform is the kernel, the root filesystem, and the ARM compiler that the flight software is built against.

```bash
make board-prepare BOARD=qemu-zynq
```

The first run downloads public sources and builds them with Buildroot, which takes much longer than anything else in this lesson and needs several gigabytes.
Buildroot checks each source against a recorded hash, and it saves the license information under `build/qemu/output/legal-info/`.
This run reused a platform that was built earlier, so it finished in about a minute.

## Build and deploy

Build the host simulators and the ground software, and cross build the flight software for the processor.

```bash
make board-build BOARD=qemu-zynq
make board-deploy BOARD=qemu-zynq
```

The build writes the ARM bundle under `build/qemu/`, separate from the native files, and the deploy step checks the hash of each file and the ELF architecture before it packages the guest.
Inspect what was built.

```bash
make board-inspect BOARD=qemu-zynq
```

```text
"hardware_validated": false,
"inspection_scope": "local platform and bundle artifacts; use board-connect for a live guest shell",
"platform": { "machine": "xilinx-zynq-a9", "board_reference": "zc702", ... },
"bundle": { "system": "armv7l-shire", "mission": "drm", "spacecraft": "sat-1", ... }
```

The report says plainly that the hardware is not validated.
It also names the system the flight software was built for, `armv7l-shire`, which is not the host.

## Start the lab and look at the processor

Start the lab with the flight software on the guest.

```bash
make board-start BOARD=qemu-zynq
```

The flight software container now runs the emulator, and the emulator boots Linux before it starts cFS.
Its serial console is the container log.

```text
shire-fsw-sat-1  | [qemu] booting open-source Zynq Linux / ARM32 SHIRE
shire-fsw-sat-1  | Linux version 6.6.70-xilinx ...
shire-fsw-sat-1  | CPU: ARMv7 Processor [413fc090] revision 0 (ARMv7), cr=10c5387d
shire-fsw-sat-1  | OF: fdt: Machine model: Xilinx ZC702 board
```

After Linux, the cFS startup appears as you saw it in [Commissioning](commissioning.md#startup).
Open YAMCS at `http://localhost:8090` and use the lab as before.
Now prove that the flight software is on the processor, and not on the host.
Open a shell in the guest from a second terminal.

```bash
printf 'uname -m\nuname -r\nps | grep core-cpu1\nexit\n' | make board-connect BOARD=qemu-zynq
```

```text
armv7l
6.6.70-xilinx
  109 root     ./core-cpu1
```

The machine is `armv7l`, the kernel is the Xilinx one that Buildroot built, and the flight software, `core-cpu1`, is a process on it.
On the host, the same checks would show `x86_64`.
Pausing the lab pauses SHIRE simulation time and nothing in the guest's own clock, and Linux and network timeouts continue to use operating system time.
Stop the lab when you are done.

```bash
make board-stop BOARD=qemu-zynq
```

Stopping saves the container logs under `build/qemu/runs/` and keeps the YAMCS archive.

## Run the acceptance check

The acceptance check starts a fresh flight software instance for each trial, runs for a fixed simulated time, and checks the result.
Stop any lab first, and then run it.

```bash
make board-test BOARD=qemu-zynq
```

```text
[board] Running 3 trial(s), 30 simulated seconds each. Flight software starts fresh for each trial.
[board] Trial 1/3: PASSED: 3000 ticks, 2/2 accepted commands, 2.888x real time.
[board] Trial 2/3: PASSED: 3000 ticks, 2/2 accepted commands, 2.904x real time.
[board] Trial 3/3: PASSED: 3000 ticks, 2/2 accepted commands, 2.868x real time.
[board] median simulation speed: 2.888x real time
[board] Acceptance PASSED: 3/3 trials completed.
```

It took under two minutes.
Each trial passed because the synchronized ticks completed, the simulated devices exchanged their transactions, both commands were accepted, the control trace was complete, the YAMCS truth advanced, and the archive could be replayed.
The report in `build/qemu/reports/` records `"hardware_validated": false`, and it names the scope as QEMU PIL with host simulated devices.
The speed is measured, and it carries no pass threshold, so it describes this host and this emulator and no other.

## Run the pass on the processor

The acceptance check uses a fixed short scenario.
You can also run any registered scenario with the flight software on the processor.
Run the pass from [Nominal Operations](nominal-operations.md).

```bash
make board-scenario SCENARIO=nominal-pass BOARD=qemu-zynq
```

```text
[scenario] PASS: clean completion (director + fsw terminal markers present)
[scenario] Yamcs archive: 3600/3600 packets; replay_ready=True; missing=0
```

The same stack that ran on the host ran against the guest.
It opened the window with RTS 6, enabled the Demo device, closed the data files, and downloaded the boot file with CFDP, and every check passed.
The host kept the simulators, the timing, the command stacks, and the verification, and only cFS ran on the processor.
It took about three and a half minutes, which is the same as the scenario took on the host, because the run is a fixed length of simulated time and the host runs the rest of the lab.

The command changes the board saved in `build/active.yaml`.
Look at the file afterward, and put the `board:` and `scenario:` lines back to what you had.

## Decide what this proves

A passing run on the processor shows that the flight software builds for ARM, boots on Linux there, talks to the simulators over the network, and gives the same results as on the host for the checks that you ran.
It is evidence about the processor path.

It is not evidence about the device buses.
The flight software here talks to simulated devices over the network, and no UART, I2C, SPI, or GPIO line is exercised, so the lesson says nothing about a peripheral of the real board.
It is also not evidence about the physical board.
The emulator models a Zynq ZC702 reference and not the Zybo or any other board, so boot, timing, and drivers on real hardware remain unproven by this run.
The `zybo7020` profile runs the same userspace on a physical Zybo, and its retained results are in the [Zybo Linux PIL](../manual/how-to/zybo-pil.md) guide.
The `rpi5` profile builds for 64 bit ARM and runs on a physical Raspberry Pi 5.
The team ran the same acceptance check on that board, and all three trials passed at about eight times real time.
It is the profile to use when you need an AArch64 target, and its results carry the same limits about buses and flight readiness.

## Limitations

This lesson shows the processor path in an emulator, and it does not qualify a flight computer.
Keep these limits in mind when you read its results:

* The processor is emulated, so instruction timing, cache behavior, and interrupt latency are not those of a real board.
* All devices are simulated on the host, and no physical bus is exercised.
* The acceptance check has no speed threshold, so a slower or faster host passes it, and the speed is a measurement and not a requirement.
* The nominal pass proves the commands and checks of the stack, as it did on the host, and the hash comparison of its file is still a manual step.
* The first platform build was done earlier than this lesson, so its time and download size are not reported here.
* A second CPU target is disabled and unvalidated.
* The runs on this page used only the emulator, so the results of the `zybo7020` and `rpi5` boards are described from their guides and from the team's validation, and not from this lesson.
* The numbers on this page come from one host and one set of runs.

Use the [evidence packet](overview.md#evidence-packet) when you need to retain the result of a run.

## Review

You can now run the flight software on a target processor and say what the result covers.

You have:

* Prepared the ARM platform and built and deployed the flight software for it
* Started the lab and shown from the guest that the flight software runs on an ARM processor
* Run the acceptance check and read its report, including the line that says the hardware is not validated
* Run the nominal pass with the flight software on the processor
* Stated what the run proves about the processor and what it leaves unproven

This lesson adds a habit to the evidence packet.
Record the board profile and the build revision beside the board output and the simulated result, because a result from the emulator and a result from a physical board are different evidence.
This completes the lesson plan, and the packet that you assembled in [Nominal Operations](nominal-operations.md) now has a target to name.

Check that you can answer these before you move on:

* Why is this lesson named for the processor and not for the board?
* What did the commands `uname -m` and `ps` show, and why do they matter?
* Which two things does a passing emulator run leave unproven?

You have finished the lesson plan.
The [Scenarios overview](overview.md) lists every lesson, and the [guide to writing a lesson](../manual/how-to/writing-lessons.md) is the place to add the next one.

***
Last reviewed: 20261030
