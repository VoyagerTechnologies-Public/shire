# Writing a Scenario Lesson

The scenarios form a lesson plan in which each lesson builds on the ones before it.
This page is for authors.
It records the story beats, the template every lesson page follows, what already exists for each lesson that is not yet published, and the definition of done.
The [Scenarios overview](../../scenarios/overview.md) shows the lesson plan that learners follow.

## Story beats

Each lesson opens with a short mission brief that gives it a place in the story of the first months of the spacecraft.

* Commissioning is launch day, and the spacecraft is waiting to hear from the ground.
* Simulation Time and Repeatability is the team learning how its rehearsal environment keeps time and how to run an exercise again.
* Power and Eclipse is the spacecraft surviving its first eclipses on battery.
* Attitude Control is the spacecraft finding the Sun and then pointing where the mission needs.
* Radio Link and Command Paths is contact that is scarce and imperfect, so the team learns how a command travels.
* Cmd/Tlm Validation is the team proving that commands, limits, and uploads behave before it trusts routine work.
* Link Security is someone tampering with or replaying traffic, and the team learning what the spacecraft rejects.
* Fault Detection and Recovery is the spacecraft on its own between passes, handling a fault without the ground.
* Nominal Operations is a normal day that brings every skill together in one complete pass.
* Monte Carlo Analysis is the engineering team asking how far it can trust a result from a single run.
* New Component Development is the team adding a new device to the spacecraft.
* Processor in the Loop is the team moving the flight software off the host and onto a real processor.

## The lesson page template

Every page in the Scenarios navigation, except the overview, has these sections in this order.
`make docs-check` fails when a required section is missing or out of order.

| Section | Required | What it holds |
| --- | --- | --- |
| Mission brief | Yes | The story beat and the unit the lesson belongs to |
| Objective | Yes | What the learner does and learns |
| Prerequisites | Yes | The earlier lessons and skills the lesson uses, with links, and the setup the learner needs before starting |
| Phases | No | The body of the lesson, with scripted screenshots |
| Run it automatically | No | The `make scenario` command when a headless scenario exists |
| Limitations | Yes | What the lesson does not show and what its results do not prove |
| Review | Yes | What the learner has done, three questions the learner can answer from the lesson, and a link to the next lesson |

A lesson starts from a freshly booted spacecraft, so it never depends on state that an earlier lesson left behind.
The engineering lessons start from a clean checkout and may show terminal output in place of scripted screenshots where the work happens outside YAMCS.
It explains only the concepts it introduces and links back to the lesson that introduced the others.

## Planned lessons

| Lesson | Status and next work |
| --- | --- |
| Simulation Time and Repeatability | The lesson is published, and its Prerequisites should list the earlier lessons as they are published. |
| Power and Eclipse | The lesson is published, and the [EPS validation](eps-validation.md) guide holds the acceptance scenarios, fault injection, and campaign material that it links to. |
| Attitude Control | The lesson is published, and the [ADCS validation](adcs-validation.md) guide holds the seven ADCS scenarios and the manual truth comparison that it links to. |
| Radio Link and Command Paths | The `/SHIRE_GROUND` path commands and the `radio-link-testing` scenario exist, so [Debug and Radio Path Comparison](../../scenarios/link-path-comparison.md) and [Radio Link Testing](../../scenarios/radio-link-testing.md) need merging with a scripted path trial and a lost contact phase. |
| Cmd/Tlm Validation | The lesson is published, and its Prerequisites should list the earlier lessons as they are published. |
| Link Security | The `radio-security-testing` scenario exists, so [Radio Security Testing](../../scenarios/radio-security-testing.md) and [Security Incident Response](../../scenarios/security-incident-response.md) need merging with a response phase and screenshots. |
| Fault Detection and Recovery | `DemoFDIR.ycs` passes repeatedly, so [FDIR](../../scenarios/fdir.md) needs a registered scenario and screenshots, and a battery response phase once an LC action point and a load shedding RTS exist. |
| Nominal Operations | Every phase can be done by hand, so [Nominal Operations](../../scenarios/nominal-operations.md) and [Data Lifecycle](../../scenarios/data-lifecycle.md) need merging with file selection, a hash check, a pass stack, and screenshots, and it is written last. |
| Monte Carlo Analysis | `make campaign`, the campaign files for EPS and the sun pointing tests, and the report and dataset exist, so it needs a small campaign whose run time is measured, lesson text, and a plain account of what the statistics do not prove, while the [Monte Carlo campaigns](monte-carlo-campaigns.md) guide stays the reference. |
| New Component Development | `make mold` and the [Component Mold](component-mold.md) guide exist, so it needs one worked example that goes from the mold to a simulator, XTCE definitions, and a passing checkout stack. |
| Processor in the Loop | The [board](board-pil.md), [Zybo](zybo-pil.md), and [QEMU](qemu-pil.md) guides exist, and the Zybo path has retained results, so the `rpi5` profile needs an acceptance run on the powered board first, then the [Component Hardware Development](../../scenarios/component-hardware-development.md) draft needs a rewrite around that run. |

The open question for Radio Link and Command Paths is how an interrupted CFDP transfer behaves, which decides how its lost contact phase ends.
Processor in the Loop is named for what happens, because the flight software runs on the board and the devices stay simulated.
The lesson must say so, and must not claim any peripheral qualification.

## What is not a lesson

* Running the component checkout stacks is already `make scenario SCENARIO=checkout`, which Commissioning describes.
* Comparing the `flatsat`, `sat-1`, and `sat-2` configurations is a terminal exercise for the Configuration how-to.
* Run replay has a how-to guide that Simulation Time and Repeatability uses, and the Monte Carlo how-to guide stays the reference for its lesson.

## Lessons that need new implementation

| Capability | Missing piece |
| --- | --- |
| ADCS fault and recovery | Simulator controls for sensor, actuator, or pointing faults plus LC and recovery logic |
| EPS low power response | An LC action point and a load shedding RTS tied to the battery watchpoint, with thresholds and recovery criteria |
| Safe mode transition | A spacecraft mode owner, transition commands, mode telemetry, entry criteria, and exit criteria |
| Camera payload | A camera component |
| Constellation crosslink | Runtime support for more than one active spacecraft, plus addressing and ground routing |
| Hardware checkout | Validated peripheral adapters, safety constraints, calibration steps, and acceptance limits for real hardware buses |

Do not represent these as runnable mission capabilities merely because the Concept of Operations describes the desired behavior.
Each needs an implementation, unit and integration tests, an operator procedure, cleanup, and retained execution evidence.

## Definition of done for a lesson

A lesson is ready to publish when it has:

* a mission brief and a named learning outcome
* a supported mission and spacecraft configuration
* prerequisites that link the earlier lessons and name the initial state
* one controlled action or condition at a time
* observable expected results with numeric limits where appropriate
* timeouts and stop conditions
* cleanup and a defined final state
* a reviewed YAMCS procedure or equivalent automation
* scripted screenshots from a manifest, as described in [Scenario screenshots](scenario-screenshots.md)
* a Limitations section, and a Review with a checkpoint and a link to the next lesson
* an evidence habit in the overview and sanitization guidance
* a retained successful result tied to repository and submodule revisions
* a requirement mapping when the result supports verification
* an entry in the Atlas navigation, in the lesson table of the [Scenarios overview](../../scenarios/overview.md), and in its habit table

***
Last reviewed: 20261028
