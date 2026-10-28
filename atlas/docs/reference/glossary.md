# Glossary

This glossary defines terms as they are used in the current SHIRE repository and Atlas.

| Term | Meaning in SHIRE |
| --- | --- |
| 42 | NASA Goddard Space Flight Center spacecraft dynamics and environment simulator used by SHIRE. |
| Action point | LC entry in the Actionpoint Definition Table that evaluates watchpoint results and starts an RTS after its failure threshold, such as action points 5 and 11 in the DRM. |
| ADCS | Attitude Determination and Control System reference component. |
| ADCS modes | Six control modes that the ground command selects, namely `PASSIVE` for no control, `BDOT` for B-dot de-tumbling with the magnetic torquers, `SUNSAFE` for Sun pointing with a mild roll, `NADIR`, `TARGET` for tracking a commanded inertial direction, and `INERTIAL`. |
| ADT | LC Actionpoint Definition Table, built from `cfg/shire_defs/tables/lc_def_adt.c`. |
| AES-GCM | Authenticated encryption mode that encrypts and authenticates each frame, with AES-256-GCM as the default for the DRM radio command and telemetry security associations. |
| Alarm | YAMCS record raised when a parameter leaves its yellow or red limit range, which an operator can acknowledge and which resolves when the value returns. |
| AOS and LOS | Acquisition of signal and loss of signal, which the Radio link model logs as `RADIO_LINK` lines when the spacecraft enters or leaves view of an enabled ground station. |
| Atlas | The public SHIRE documentation site under `atlas/`. |
| Backdoor | Simulation only interface that passes a packet to a named simulation target's backdoor handling, either the Director's component simulator `backdoor` callback for fault injection or the Simulith Server's own pause/play/speed control. |
| Board profile | YAML file under `cfg/boards/` that selects a PIL target such as `qemu-zynq`, `zybo7020`, or `rpi5`, chosen with `BOARD=<profile>`. |
| BSP | Board Support Package used to adapt startup and platform services to a particular target environment. |
| CCSDS | Consultative Committee for Space Data Systems, which publishes the packet and file delivery standards used by the DRM. |
| CF | cFS CFDP application that supplies onboard file delivery services. |
| CFDP | CCSDS File Delivery Protocol used for file transfer between flight and ground software. |
| cFE | Core Flight Executive, which supplies the runtime services at the center of cFS. |
| cFS | NASA core Flight System used as the SHIRE flight software framework. |
| CI_LAB | cFS laboratory command ingest application used by the direct debug command path. |
| CLI | Command line interface used for focused component protocol and hardware checkout without cFS or YAMCS. |
| COMMIT | Final Simulith tick phase that quiesces component service, publishes actuator outputs, and commits the combined command batch to 42. |
| Component | A SHIRE subsystem package containing a flight application, simulated device, shared protocol code, tests, configuration, and ground artifacts as applicable. |
| Component mold | Mechanical starting point created from the Demo component with `make mold COMP=<name>`. |
| Component simulator | Director loadable shared library that implements the Simulith lifecycle and models a component's device behavior. |
| CPU1 | Active cFS target using `amd64-shire` by default, `armv7l-shire` for ARM32 PIL, or `aarch64-shire` for a configured AArch64 SDK target. |
| CPU2 | Disabled 32 bit ARM Zybo 7020 target scaffold that still requires target completion and validation, because the Zybo 7020 PIL runs as CPU1 with `armv7l-shire`. |
| CryptoLib | NASA cryptographic library used in the representative radio command and telemetry path. |
| Debug path | Direct UDP command and telemetry path between YAMCS and CI_LAB or TO_LAB that bypasses the Radio and CryptoLib. |
| Demo | Minimal reference payload component and source used by the component mold. |
| Design Reference Mission | The example mission configuration, operating concept, requirements, and scenarios supplied with SHIRE. |
| Director | Simulith process that loads component simulators, runs ordered PREPARE and COMMIT callbacks plus concurrent device service workers, exchanges state with 42, handles backdoor traffic, and publishes truth telemetry. |
| Dispersion | Variation that a Monte Carlo campaign samples for a swept parameter around its baseline, defined by the parameter's `mode` and `distribution`. |
| Do No Harm (DNH) | Conservative DRM startup configuration established through component defaults, SC startup behavior, LC actions, and RTS tables rather than one spacecraft mode command. |
| DRM | Common abbreviation for Design Reference Mission. |
| DS | cFS Data Storage application used to record selected packets to files. |
| EPS | Electrical Power System reference component. |
| ES | cFE Executive Services, which manages cFE startup, applications, resources, and reset behavior. |
| EVS | cFE Event Services, which distributes application event messages. |
| EXECUTE | Simulith tick phase in which SCH releases one slot and component workers service complete FSW device transactions. |
| FDIR | Fault Detection, Isolation, and Recovery. |
| FM | cFS File Manager application used for onboard file and directory operations. |
| FSW | Flight software, which in the current DRM is the cFS runtime and its loaded applications. |
| GPIO | General purpose input and output pin interface in HWLIB, which the Radio component uses for its power enable and interrupt lines. |
| Ground station catalog | List of named ground stations in `cfg/drm/ground_stations.yaml` that feeds the 42 ground station block and the Radio link model, with an optional elevation mask and enabled flag per station. |
| GSW | Ground software, which in SHIRE primarily means the included YAMCS instance and component ground artifacts. |
| HK | Housekeeping telemetry that an application publishes with its counters and state. |
| HWLIB | Hardware interface library in the SHIRE PSP that provides UART, I2C, SPI, GPIO, and other interfaces. |
| I2C | Two wire serial bus interface in HWLIB, which the EPS component uses. |
| ICD | Interface Control Document used to define the component bus, protocol, timing, state, and error contract before implementation. |
| Initial Condition (IC) bin | Reusable starting orbit, epoch, attitude, and optional per-component state referenced by a DRM scenario's `initial_conditions` field. |
| Instance | A Monte Carlo campaign trial's short token, used to suffix its containers, networks, volumes, and ports so concurrent trials never collide. |
| IPC | Interprocess communication used for simulated device and 42 connections through endpoints in the shared `/tmp` volume. |
| LC | cFS Limit Checker application used to evaluate watchpoints and trigger configured actions. |
| Limit | Yellow and red range configured for a numeric telemetry parameter under `limits:`, where a value outside the yellow range is `WARNING` and outside the red range is `CRITICAL`. |
| Link mode | Radio `link_mode` setting where `auto` computes visibility from the ground station catalog, `always` keeps the link in view, and `never` keeps it out of view. |
| log_assertions | Scenario key that lists a container, a regular expression, and optional minimum and maximum match counts checked against container logs after a run. |
| MDB | YAMCS mission database assembled from the checked in XTCE command and telemetry definitions. |
| Monte Carlo Campaign | Many trials of one DRM scenario run against varied Initial Condition bins and aggregated by `tools/shire-campaign.py`. |
| NOOP | No Operation command used to confirm command acceptance and counters without requesting mission behavior. |
| OBJ-* and requirement prefixes | Identifier families on the Mission Requirements page where `OBJ-` marks a stakeholder objective and the `DRM-`, `CFG-`, `SIM-`, `OPS-`, `AUT-`, `EVT-`, `CMD-`, `DAT-`, `FSW-`, `ADCS-`, `DEMO-`, `EPS-`, `COM-`, `CRYPTO-`, `GND-`, and `DOC-` prefixes mark requirements. |
| OSAL | Operating System Abstraction Layer used by cFS. |
| PIL | Processor in the loop, which runs the cFS target on an emulated or physical Linux processor while the host supplies simulation time, 42 dynamics, and simulated devices. |
| PREPARE | First Simulith tick phase that obtains 42 truth and advances autonomous component state before FSW executes. |
| Procedure stack | YAMCS `.ycs` artifact that contains ordered commands, checks, and operator instructions. |
| PSP | cFE Platform Support Package used to reach platform and hardware services. |
| QEMU | Emulator that hosts the `qemu-zynq` board profile, which runs the ARM32 cFS bundle in a Zynq ZC702 guest. |
| Radio | SHIRE reference component that models device commanding, buffered uplink and downlink, radio operating modes, a link model for visibility, delay, and packet loss, and SDLS link security through CryptoLib. |
| Radio modes | Radio operating configurations `SLEEP`, `TX`, `RX`, and `DUPLEX`, where `RX` accepts commands, `TX` returns telemetry, and `DUPLEX` does both. |
| Radio path | Representative DRM command and telemetry route through CryptoLib and the Radio simulator rather than a validated physical RF link. |
| Replay | Offline viewing of a retained run archive without starting the simulation, started with `make replay`. |
| RF | Radio frequency, the physical layer that the Radio link model abstracts as visibility, delay, and loss without modeling propagation or signal strength. |
| RTOS | Real Time Operating System that may sit below OSAL on a flight target. |
| RTS | Relative Time Sequence executed by the cFS Stored Command application. |
| Run archive | Docker volume labeled with `shire.archive=true` and `shire.run=<run-id>` that holds one run's native YAMCS data. |
| SB | cFE Software Bus, which routes messages among flight applications and cFE services. |
| SC | cFS Stored Command application. |
| SC wakeup | Scheduled `SC_1HZ_WAKEUP` message that advances SC relative time sequences at an expected one hertz, so RTS 6 waits 480 of them. |
| Scenario | Named configuration under `cfg/drm/scenarios/` that selects overrides, an Initial Condition bin, a run duration, and optional `verify_stacks` and `log_assertions`, run headlessly with `make scenario`, while an Atlas scenario page is the manual exercise that explains one. |
| SCH | cFS Scheduler application that releases configured messages according to its schedule table. |
| SDLS | Space Data Link Security, the CCSDS protocol that CryptoLib applies to radio command and telemetry frames. |
| Server | Simulith process that owns simulation time and advances only after every required PREPARE, EXECUTE, and COMMIT completion. |
| SHIRE | Software & Hardware Integration Runtime Environment. |
| Simulith | SHIRE simulation middleware that coordinates time, loads component models, provides simulated device transport, and connects to 42. |
| SoC | Battery state of charge as a fraction from 0 to 1, set initially by the EPS `battery_initial_soc` and mapped to battery voltage as 16 V plus 8 V times the fraction with the default span. |
| SPI (security parameter index) | SDLS identifier of a security association, where the DRM uses SPI 4 for encrypted uplink, SPI 1 for clear uplink, SPI 6 for encrypted downlink, and SPI 5 for clear downlink. |
| SPI (serial peripheral interface) | Serial bus interface in HWLIB, which the Radio component uses with a chip select. |
| Target | Named cFS processor and platform build definition such as the active CPU1 target or scaffolded CPU2 target. |
| TBL | cFE Table Services, which manages application configuration tables at runtime. |
| TC and TM | Telecommand and telemetry, the uplink and downlink frame types, which the DRM carries on virtual channel 0 and virtual channel 1. |
| TIME | cFE Time Services, which supplies and distributes spacecraft time. |
| Timeline | YAMCS view of parameter bands and optional scheduled items, with the current DRM default containing bands but no scheduled items. |
| TO_LAB | cFS laboratory telemetry output application used by the direct debug telemetry path. |
| Trial | One run of a Monte Carlo campaign with its own sampled parameter values and Initial Condition bin. |
| Truth telemetry | Selected 42 state published by the Director to YAMCS for comparison with spacecraft observations. |
| UART | Asynchronous serial interface in HWLIB, which the ADCS and Demo components use. |
| UDP | User Datagram Protocol used by the current ground links, Director backdoor, and 42 truth telemetry path. |
| Uplink patch | File built for the ground to uplink instead of for boot, named with an `up_` prefix, such as an uploadable table or the `up_eps` application module. |
| Uploadable table | Flight table built as an `up_` file that has no effect until the ground uploads it and loads, validates, and activates it, such as RTS 12 or the LC watchpoint table. |
| VCID | Virtual Channel Identifier in a CCSDS frame, which the DRM sets to 0 for TC and 1 for TM. |
| VCRM | Verification cross reference matrix that records the result and evidence for each requirement. |
| verify_stacks | Scenario key that lists YAMCS stacks to run at a given `at_s` time, which `tools/shire-scenario.py` executes headlessly. |
| Watchpoint | LC entry in the Watchpoint Definition Table that compares one field of a message against a value, such as watchpoint 12 for EPS battery voltage in the uplink table. |
| WDT | LC Watchpoint Definition Table, built from `cfg/shire_defs/tables/lc_def_wdt.c`. |
| XTCE | XML Telemetric and Command Exchange format used for YAMCS mission database definitions. |
| YAMCS | Ground software used for commanding, telemetry, archives, procedures, displays, timelines, and CFDP. |
| YAMCS Commander | Developer utility under `yamcs/` that inspects the active MDB, issues commands through the realtime processor, and with `--stack` runs a `.ycs` procedure stack headlessly including its verify steps, as `verify_stacks` does. |
| ZeroMQ | Messaging library used by simulated HWLIB transports over IPC endpoints. |

When a term in the Atlas is ambiguous, use the source or configuration described in [Quick Reference](quick-reference.md) to confirm its meaning in the current checkout.

***
Last reviewed: 20261027
