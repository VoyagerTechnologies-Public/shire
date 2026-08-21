# Glossary

This glossary defines terms as they are used in the current SHIRE repository and Atlas.

| Term | Meaning in SHIRE |
| --- | --- |
| 42 | NASA Goddard Space Flight Center spacecraft dynamics and environment simulator used by SHIRE. |
| ADCS | Attitude Determination and Control System reference component. |
| Atlas | The public SHIRE documentation site under `atlas/`. |
| Backdoor | Simulation only Director interface that passes a component packet to the named simulator's backdoor callback. |
| BSP | Board Support Package used to adapt startup and platform services to a particular target environment. |
| CCSDS | Consultative Committee for Space Data Systems, which publishes the packet and file delivery standards used by the DRM. |
| cFE | Core Flight Executive, which supplies the runtime services at the center of cFS. |
| cFS | NASA core Flight System used as the SHIRE flight software framework. |
| CF | cFS CFDP application that supplies onboard file delivery services. |
| CFDP | CCSDS File Delivery Protocol used for file transfer between flight and ground software. |
| CI_LAB | cFS laboratory command ingest application used by the direct debug command path. |
| CLI | Command line interface used for focused component protocol and hardware checkout without cFS or YAMCS. |
| Component | A SHIRE subsystem package containing a flight application, simulated device, shared protocol code, tests, configuration, and ground artifacts as applicable. |
| Component mold | Mechanical starting point created from the Demo component with `make mold COMP=<name>`. |
| Component simulator | Director loadable shared library that implements the Simulith lifecycle and models a component's device behavior. |
| CPU1 | Active host cFS target that currently selects the `amd64-shire` system for normal builds. |
| CPU2 | Disabled 32 bit ARM Zybo 7020 target scaffold that still requires target completion and validation. |
| CryptoLib | NASA cryptographic library used in the representative radio command and telemetry path. |
| Debug path | Direct UDP command and telemetry path between YAMCS and CI_LAB or TO_LAB that bypasses the Radio and CryptoLib. |
| Demo | Minimal reference payload component and source used by the component mold. |
| Design Reference Mission | The example mission configuration, operating concept, requirements, and scenarios supplied with SHIRE. |
| Director | Simulith process that loads component simulator shared libraries into its address space, runs their callbacks on each tick, exchanges state with 42, handles backdoor traffic, and publishes truth telemetry. |
| Do No Harm (DNH) | Conservative DRM startup configuration established through component defaults, SC startup behavior, LC actions, and RTS tables rather than one spacecraft mode command. |
| DRM | Common abbreviation for Design Reference Mission. |
| DS | cFS Data Storage application used to record selected packets to files. |
| EPS | Electrical Power System reference component. |
| ES | cFE Executive Services, which manages cFE startup, applications, resources, and reset behavior. |
| EVS | cFE Event Services, which distributes application event messages. |
| FDIR | Fault Detection, Isolation, and Recovery. |
| FM | cFS File Manager application used for onboard file and directory operations. |
| FSW | Flight software, which in the current DRM is the cFS runtime and its loaded applications. |
| GSW | Ground software, which in SHIRE primarily means the included YAMCS instance and component ground artifacts. |
| HWLIB | Hardware interface library in the SHIRE PSP that provides UART, I2C, SPI, GPIO, and other interfaces. |
| ICD | Interface Control Document used to define the component bus, protocol, timing, state, and error contract before implementation. |
| IPC | Interprocess communication used for simulated device and 42 connections through endpoints in the shared `/tmp` volume. |
| LC | cFS Limit Checker application used to evaluate watchpoints and trigger configured actions. |
| MDB | YAMCS mission database assembled from the checked in XTCE command and telemetry definitions. |
| NOOP | No Operation command used to confirm command acceptance and counters without requesting mission behavior. |
| OSAL | Operating System Abstraction Layer used by cFS. |
| Procedure stack | YAMCS `.ycs` artifact that contains ordered commands, checks, and operator instructions. |
| PSP | cFE Platform Support Package used to reach platform and hardware services. |
| Radio | SHIRE reference component that models device commanding, buffered uplink and downlink, and radio operating modes. |
| Radio path | Representative DRM command and telemetry route through CryptoLib and the Radio simulator rather than a validated physical RF link. |
| RTS | Relative Time Sequence executed by the cFS Stored Command application. |
| RTOS | Real Time Operating System that may sit below OSAL on a flight target. |
| SB | cFE Software Bus, which routes messages among flight applications and cFE services. |
| SC | cFS Stored Command application. |
| Scenario | Atlas exercise that applies SHIRE capabilities to an operational or development objective and states its current evidence level. |
| SCH | cFS Scheduler application that releases configured messages according to its schedule table. |
| Server | Simulith process that owns simulation time, broadcasts ticks, and waits for registered clients. |
| SHIRE | Software & Hardware Integration Runtime Environment. |
| Simulith | SHIRE simulation middleware that coordinates time, loads component models, provides simulated device transport, and connects to 42. |
| Target | Named cFS processor and platform build definition such as the active CPU1 target or scaffolded CPU2 target. |
| TBL | cFE Table Services, which manages application configuration tables at runtime. |
| TIME | cFE Time Services, which supplies and distributes spacecraft time. |
| Timeline | YAMCS view of parameter bands and optional scheduled items, with the current DRM default containing bands but no scheduled items. |
| TO_LAB | cFS laboratory telemetry output application used by the direct debug telemetry path. |
| Truth telemetry | Selected 42 state published by the Director to YAMCS for comparison with spacecraft observations. |
| UDP | User Datagram Protocol used by the current ground links, Director backdoor, and 42 truth telemetry path. |
| XTCE | XML Telemetric and Command Exchange format used for YAMCS mission database definitions. |
| YAMCS | Ground software used for commanding, telemetry, archives, procedures, displays, timelines, and CFDP. |
| YAMCS Commander | Developer utility under `yamcs/` that inspects the active MDB and issues commands through the realtime processor. |
| ZeroMQ | Messaging library used by simulated HWLIB transports over IPC endpoints. |

When a term in the Atlas is ambiguous, use the source or configuration described in [Quick Reference](quick-reference.md) to confirm its meaning in the current checkout.

***
Last reviewed: 20260818
