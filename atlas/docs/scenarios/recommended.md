# Recommended Scenarios

The next scenarios should deepen the learning path for someone new to space operations while turning more of the current repository into repeatable evidence.
This page records what already exists for each candidate, what is still missing, and the order in which to finish them.
Only [Commissioning](commissioning.md) and [Cmd/Tlm Validation](cmd-tlm-validation.md) are published in the Atlas navigation today.

## Drafted scenarios

| Scenario | Status and next work |
| --- | --- |
| [FDIR](fdir.md) | `DemoFDIR.ycs` passes repeatedly with LC action point 11 and RTS 11 in the boot tables, so it needs a registered scenario, screenshots, and a retained run. |
| [Debug and Radio Path Comparison](link-path-comparison.md) | The `/SHIRE_GROUND` commands choose a path and [Radio Link Testing](radio-link-testing.md) already runs as a scenario, so the two pages should merge with scripted trials and screenshots. |
| [Radio Security Testing](radio-security-testing.md) | The `radio-security-testing` scenario tampers with and replays traffic and asserts the rejections, so it needs publishing, screenshots, and the planning draft below retired. |
| [Security Incident Response](security-incident-response.md) | Its detection half is Radio Security Testing, so it needs the operator decision, containment, and evidence steps added on top. |
| [ADCS Truth Comparison](adcs-truth-comparison.md) | The `adcs-truth-verification` and `adcs-modes-sweep` scenarios pass, so they should combine as one pointing modes scenario that notes the truth check runs headless only. |
| [EPS Load Switching](eps-load-switching.md) | The `eps-functional` and `eps-backdoor-testing` scenarios, three campaigns, and report tools run, so the page needs a newcomer walkthrough with a learning outcome and screenshots. |
| [Data Lifecycle](data-lifecycle.md) | Commissioning already shows DS, FM, and CFDP and `CheckoutTest.ycs` asserts a CFDP round trip, so it needs file selection and a hash check or should fold into Commissioning. |
| [Nominal Operations](nominal-operations.md) | Every phase can be done by hand, so it should be finished last after the scenarios it depends on. |
| [Component Hardware Development](component-hardware-development.md) | The CLI and cFS simulation stages run and processor in the loop profiles run `cpu1` on ARM Linux, so it needs a rewrite around real hardware gates that require hardware. |

The Do No Harm checks remain part of [Commissioning](commissioning.md) rather than becoming a separate scenario.

## Priority order

The order favors scenarios that need little new work, that build on the ones before them, and that give a newcomer the clearest picture of how a spacecraft is operated.

1. **FDIR.** It is the only evidence for the autonomy requirements, the stack exists, and it builds on the stored command and limit checking that Cmd/Tlm Validation introduced.
2. **Debug and Radio Path Comparison.** Newcomers need to know that a green link does not prove which path carried a command, and the ground commands to prove it exist.
3. **Radio Security Testing.** It is already automated, so publishing it is mostly screenshots and review.
4. **ADCS Pointing Modes.** The scenarios pass, and a newcomer sees attitude control confirmed against 42 truth.
5. **EPS Load Switching, then Eclipse and Power Budget.** The limits, the eclipse initial conditions, and the viewer are in place, so a newcomer can watch the battery respond to loads and eclipse.
6. **Lost Contact and Pass Recovery.** Link modes, delay, and loss now exist, and the open question is how an interrupted CFDP transfer behaves.
7. **Data Lifecycle.** It connects the most building blocks, but it overlaps Commissioning and needs hash evidence.
8. **Detumble and Sun Acquisition.** The initial tumble and BDOT mode exist, no stack exercises them, and a Monte Carlo campaign over the tumble rate makes a strong demonstration.
9. **Nominal Operations.** It is the capstone and depends on the scenarios above.

## Lab skill scenarios

These are short and each teaches one skill.

| Scenario | What it teaches |
| --- | --- |
| Simulation Time Control | Pause, resume, and change speed through the Server, and read simulation time against the host clock. |
| Spacecraft Configuration Comparison | Build `flatsat`, `sat-1`, and `sat-2` and see that the generated component set differs. |
| Spacecraft Timekeeping | The GPS time and time file fallback stacks, and how the ADCS time source changes. |
| Repeatability Run | Run one scenario twice from the same initial conditions and compare results. |

The `flatsat` spacecraft has Demo, EPS, and Radio, `sat-1` has ADCS, Demo, EPS, and Radio, and `sat-2` has ADCS, EPS, and Radio.

## Scenarios that need new implementation

| Scenario | Missing capability |
| --- | --- |
| ADCS fault and recovery | Simulator controls for sensor, actuator, or pointing faults plus LC and recovery logic. |
| EPS low power response | An LC action point and a load shedding RTS tied to the battery watchpoint, with thresholds and recovery criteria. |
| Safe mode transition | A spacecraft mode owner, transition commands, mode telemetry, entry criteria, and exit criteria. |
| Camera payload | A camera component. |
| Constellation crosslink | Runtime support for more than one active spacecraft, plus addressing and ground routing. |
| Hardware checkout | Validated peripheral adapters, safety constraints, calibration steps, and acceptance limits for real hardware buses. |

Do not represent these as runnable mission capabilities merely because the Concept of Operations describes the desired behavior.
Each needs an implementation, unit and integration tests, an operator procedure, cleanup, and retained execution evidence.

## Definition of done for a new scenario

A scenario is ready to publish as executable when it has:

* a named objective and learning outcome
* a supported mission and spacecraft configuration
* explicit prerequisites and initial state
* one controlled action or condition at a time
* observable expected results with numeric limits where appropriate
* timeouts and stop conditions
* cleanup and a defined final state
* an evidence checklist with sanitization guidance
* a reviewed YAMCS procedure or equivalent automation
* scripted screenshots from a manifest, as described in [Scenario screenshots](../manual/how-to/scenario-screenshots.md)
* a retained successful result tied to repository and submodule revisions
* a requirement mapping when the result supports verification
* an entry in the Atlas navigation and in the [Scenarios overview](overview.md)

***
Last reviewed: 20261027
