# Fault Detection, Isolation, and Recovery

> **Scenario status:** `DemoFDIR.ycs` is an executable YAMCS procedure with commands and parameter assertions in the current repository.
> Its presence does not prove that it passed for a particular revision.
> Retain the procedure result and run context before using it as evidence.

## What you will learn

This scenario follows one fault from injection through recovery.
It is a compact introduction to how simulator controls, telemetry, Limit Checker, Stored Command, and a component device interact.

You will learn how to:

* establish a known Demo baseline
* inject a simulator condition without modifying flight software
* distinguish the injected condition from the flight response
* observe LC detection and SC execution
* confirm that the Demo device restarts and valid data returns
* clean up the simulator even when a procedure stops early

## Prerequisites

1. Build and start the default DRM `sat-1` lab.
2. Wait for Do No Harm startup activity to complete.
3. Confirm `debug-in`, `debug-out`, and `sim-backdoor` are enabled on the YAMCS **Links** page.
4. Confirm Demo, LC, and SC housekeeping is fresh.
5. Open the FSW output or event view so the automated response remains visible.

A pass window is not required for this focused procedure.
Both command links are enabled when the lab starts, and the procedure does not select a command path.
YAMCS sends each command on every enabled command link, as described in [Choosing a command path](../manual/core-concepts/ground-software.md#choosing-a-command-path).
The radio path delivers a command only while the Radio is in a receiving mode.
When the path matters to your result, choose it with `/SHIRE_GROUND/USE_DEBUG_UPLINK` before the run and record the link states.

## Know the moving parts

| Element | Role in this exercise |
| --- | --- |
| `DemoFDIR.ycs` | Establishes the baseline, controls injection, and asserts key observations. |
| `tc_backdoor` | Carries a simulation only control to the Demo simulator. |
| Demo simulator | Changes its generated channel data while the injection is enabled. |
| LC watchpoint 10 | Checks that the Demo device enabled byte in Demo housekeeping equals 1. |
| LC watchpoint 11 | Checks whether Demo channel 1, an unsigned 16 bit little endian word at offset 16 of the Demo device packet, is below 256. |
| LC action point 11 | Combines both watchpoints with a logical AND, starts active, and requests RTS 11 after one failure. |
| RTS 11 | Disables Demo after one SC wakeup, then enables Demo again ten SC wakeups later. |

The flight response uses channel 1 as the detection input.
The YAMCS procedure also checks channels 2 and 3 to make the injected simulator condition clear to the operator.

## Follow the implemented path

1. The procedure arms LC action point 11 and resets its statistics, because LC makes an action point passive after it trips and a repeat run would otherwise see no response.
2. It resets Demo counters and verifies a NOOP.
3. It enables Demo and verifies the enabled state.
4. It checks that channels 1, 2, and 3 are at least 256.
5. It sends `BACKDOOR_DEMO_RAND_DATA` with `ENABLE` on `tc_backdoor`.
6. The simulator begins generating channel values below 256.
7. LC action point 11 detects that Demo is enabled while channel 1 is below its limit.
8. LC asks SC to start RTS 11, and action point 11 goes passive.
9. The procedure asserts `/SC/SC_HKTLM/RTSEXECUTINGSTATUS_1` is at least 1024, the value of bit 10, which is the executing flag for RTS 11.
Because the comparison is greater than or equal, a higher numbered RTS running at the same time would also satisfy it.
10. RTS 11 disables Demo and enables it again after ten scheduler wakeups.
11. The procedure disables injection and checks the disabled and enabled transition.
12. It verifies that all three channels return to values at least 256.
13. It arms action point 11 again, so the spacecraft ends in its boot state and the procedure can run again.

## Run the exercise

Open **Procedures / Stacks / DemoFDIR.ycs**.
Read the complete procedure before running it.
Locate the two backdoor commands so you know which step turns the condition on and which step turns it off.

Select the first step and choose **Run all from selected step**.
Watch these views during the run:

* Demo `DEVICE_ENABLED`, `CHAN1`, `CHAN2`, and `CHAN3`
* LC events or action point status
* SC RTS execution status and events
* FSW output
* YAMCS procedure progress

Do not manually advance past a failed assertion without saving the observation.
An unexpected result is useful diagnostic evidence only if the first failure and surrounding state are retained.

## Expected evidence

| Point in the run | Expected result |
| --- | --- |
| Command baseline | Demo command count resets and increments after a NOOP. |
| Enabled baseline | `/DEMO/DEVICE_ENABLED` reports `ENABLED`. |
| Nominal data | Demo channels 1, 2, and 3 are at least 256. |
| Fault injection | All three Demo channels fall below 256. |
| Detection | LC action point 11 reports the configured failure or action. |
| Response | `/SC/SC_HKTLM/RTSEXECUTINGSTATUS_1` is at least 1024, which means the RTS 11 executing bit is set, and SC events show the RTS 11 response. |
| Restart | Demo passes through the disabled and enabled states asserted by the procedure. |
| Recovery | All three Demo channels return to values at least 256. |

The cleanest visual evidence is a sequence of three observations.
Capture nominal channels before injection, invalid channels with LC and SC response, then valid channels after recovery.
Include timestamps so the order is unambiguous.

## If the procedure fails

| Failure point | Useful checks |
| --- | --- |
| Baseline counter | Confirm fresh Demo housekeeping and check whether both command paths delivered the command. |
| Enabled state | Inspect Demo application events and the Demo simulator connection. |
| Invalid channel assertion | Confirm `sim-backdoor`, the `tc_backdoor` stream, and the injection command acknowledgment. |
| SC execution assertion | Confirm LC is active, action point 11 is active and not passive, RTS 11 is enabled, Demo channel 1 crossed below 256, and `/SC/SC_HKTLM/RTSEXECUTINGSTATUS_1` is fresh because the RTS is short lived. |
| Disabled state assertion | Inspect the timing between procedure verification and the short RTS transition. |
| Recovered data assertion | Confirm injection is disabled and wait for fresh Demo data telemetry. |

The procedure uses zero configured wait between steps.
Host load and telemetry timing can therefore affect when a short lived state is sampled.
Preserve that timing evidence before changing the procedure.

## Cleanup

The normal procedure sends `BACKDOOR_DEMO_RAND_DATA` with `DISABLE` before its final recovery assertions.
If the procedure stops after enabling injection, send the same backdoor command with `DISABLE` manually.
Confirm that fresh Demo channel values return to at least 256.

If RTS 11 did not finish, inspect SC state before issuing further Demo commands.
If the procedure stopped before its last steps, action point 11 may still be passive, so send `/LC/LC_COMMANDS/LC_SET_AP_STATE` with `APNUMBER` 11 and `NEWAPSTATE` `LC_APSTATE_ACTIVE`.
Retain the evidence before you restart the lab for a fully clean repeat.

## Scope of the result

This scenario demonstrates one injected Demo data range condition and one automated restart response.
It proves neither broad fault coverage nor root cause isolation.
It also does not demonstrate spacecraft Safe mode entry or recovery for ADCS, EPS, Radio, CryptoLib, 42, containers, networks, or physical hardware.

Add one controlled condition, one explicit detection rule, one bounded response, and one cleanup path for each additional failure mode.
[Writing a Scenario Lesson](../manual/how-to/writing-lessons.md#lessons-that-need-new-implementation) lists the fault exercises that still need implementation.

***
Last reviewed: 20261027
