# Mission Requirements

> **Status:** This page is a proposed requirements baseline for the Design Reference Mission.
> Each `shall` statement defines intended DRM behavior and does not claim completed verification.
> The baseline is limited to needs with a defined repository scope and a credible verification path.
> Current implementation status and verification evidence remain separate from the requirement statements.

## Purpose

This document translates the DRM stakeholder objectives into traceable technical requirements.
The baseline applies these writing rules:

* Include only behavior needed for the DRM purpose
* Use one positive `shall` statement for each requirement
* State the required result rather than an operator action
* Avoid ambiguous or unverifiable terms
* Record the reason for each requirement
* Trace every requirement to a parent need or requirement
* Assign one verification method, one verification level, and one owner role

The [Concept of Operations](concept-of-operations.md) defines how the current DRM is operated.
The [Verification and Validation](verification-and-validation.md) page identifies available evidence mechanisms and verification gaps.

## Scope

The requirement boundary includes the generated DRM configuration, 42 dynamics, selected component simulators, cFS mission software, the representative Radio and CryptoLib path, YAMCS, and the Atlas operational material.

The requirement boundary excludes physical launch and deployment environments, spacecraft lifetime, flight qualified hardware, RF propagation, ground station availability, operational key infrastructure, and security accreditation.
Those subjects need stakeholder sources, mission hardware, and acceptance criteria before they can become DRM requirements.

## Reference material

* [DRM Concept of Operations](concept-of-operations.md)
* [SHIRE Architecture](../manual/core-concepts/architecture.md)
* [Development Workflow](../manual/core-concepts/development-workflow.md)
* [Verification and Validation](verification-and-validation.md)

## Stakeholder objectives

Stakeholder objectives state why the DRM exists.
They are source statements rather than technical `shall` requirements.

| ID | Stakeholder objective | Source rationale |
| --- | --- | --- |
| OBJ-001 | An integrated software reference environment for spacecraft development | Primary SHIRE DRM purpose |
| OBJ-002 | Observable command and telemetry operations across the development lifecycle | Operator and developer use of the DRM |
| OBJ-003 | An onboard to ground data handling example | Integrated data system development |
| OBJ-004 | Configurable detection and response behavior | Fault management development |
| OBJ-005 | A progression from component simulation to physical component integration | Hardware independent early development |

## Design Reference Mission requirements

| ID | Requirement | Rationale |
| --- | --- | --- |
| DRM-001 | The DRM shall execute an integrated spacecraft simulation that includes 42, selected component simulators, cFS, and YAMCS. | Integrated reference environment need |
| DRM-002 | The DRM shall exchange spacecraft commands and telemetry with the ground segment. | Observable operations need |
| DRM-003 | The DRM shall deliver one selected onboard data file to YAMCS through CFDP. | Onboard to ground data lifecycle need |
| DRM-004 | The DRM shall initiate a configured response when a monitored flight condition satisfies its action criterion. | Fault response integration need |
| DRM-005 | The DRM shall preserve a component device protocol when its simulator transport is replaced with a hardware transport. | Progressive component development need |

## Technical requirements

### Configuration

| ID | Requirement | Rationale |
| --- | --- | --- |
| CFG-001 | The SHIRE orchestrator shall generate one runtime configuration from the selected mission, spacecraft, and scenario. | Reproducible configuration boundary |
| CFG-002 | The generated cFS startup configuration shall contain the component applications selected for the active spacecraft. | Flight configuration consistency |
| CFG-003 | The generated Simulith configuration shall contain the component simulators selected for the active spacecraft. | Simulator configuration consistency |

### Simulation runtime

| ID | Requirement | Rationale |
| --- | --- | --- |
| SIM-001 | 42 shall propagate the configured spacecraft orbit and attitude state during an integrated DRM run. | Spacecraft dynamics source |
| SIM-002 | The Simulith Director shall pass the shared simulation tick and 42 context to each loaded component simulator. | Coordinated component behavior |
| SIM-003 | The CLI environment shall exchange the selected component device protocol with its simulator without cFS or YAMCS. | Focused protocol development |
| SIM-004 | A hardware transport implementation shall exchange the same device protocol used by its component simulator. | Simulator to hardware continuity |

### Startup and automation

| ID | Requirement | Rationale |
| --- | --- | --- |
| OPS-001 | The default `sat-1` flight configuration shall establish the Do No Harm checkpoint after a power on reset without a ground command. | Conservative observable startup |
| AUT-001 | Stored Command shall execute an enabled relative time sequence after receiving its start request. | Onboard activity automation |
| AUT-002 | Limit Checker shall start the configured relative time sequence after an enabled action point reaches its failure threshold. | Configurable fault response |
| EVT-001 | Each DRM component application shall publish an event when its command handler rejects a command. | Command fault observability |

### Command and data handling

| ID | Requirement | Rationale |
| --- | --- | --- |
| CMD-001 | The direct debug path shall exchange cFS commands and telemetry between YAMCS and the lab applications. | Development path observability |
| CMD-002 | The representative radio path shall exchange cFS commands and telemetry through the ground CryptoLib service, Radio simulator, and Radio cFS application. | Representative communication path |
| DAT-001 | Data Storage shall write each packet selected by its active filter table to a persistent onboard data file. | Onboard telemetry retention |
| DAT-002 | The cFS CF application shall transfer an operator selected onboard file to the YAMCS CFDP service. | Ground delivery of onboard data |

### Flight software architecture

| ID | Requirement | Rationale |
| --- | --- | --- |
| FSW-001 | The DRM flight software shall execute on the cFS framework. | Selected flight software architecture |
| FSW-002 | DRM flight applications shall exchange interapplication commands and telemetry through the cFS Software Bus. | Defined flight application interface |
| FSW-003 | Each DRM component application shall report accepted and rejected command counts in housekeeping telemetry. | Command processing observability |

### Attitude determination and control

| ID | Requirement | Rationale |
| --- | --- | --- |
| ADCS-001 | The ADCS simulator shall accept `PASSIVE`, `BDOT`, `SUNSAFE`, `NADIR`, `TARGET`, and `INERTIAL` mode selections. | ADCS mode interface definition |
| ADCS-002 | The ADCS simulator shall apply the commanded target selection while operating in `TARGET` mode. | Commanded target behavior |
| ADCS-003 | The ADCS simulator shall publish its attitude quaternion, angular rate, Sun vector, mode, and target in device housekeeping. | Attitude state observability |
| ADCS-004 | The ADCS simulator shall send modeled reaction wheel and magnetic torque bar commands to 42 when the selected control mode requests actuation. | Closed loop attitude simulation |

### Demonstration component

| ID | Requirement | Rationale |
| --- | --- | --- |
| DEMO-001 | The Demo simulator shall encode the 42 body Sun vector in its three data channels when random data generation is disabled. | Representative payload data source |
| DEMO-002 | The Demo component application shall publish device data only while its device interface is enabled. | Commanded payload activity |

### Electrical power system

| ID | Requirement | Rationale |
| --- | --- | --- |
| EPS-001 | The EPS simulator shall update battery energy from modeled solar generation and enabled load consumption. | Representative energy balance |
| EPS-002 | The EPS component shall accept an on command and an off command for each of its eight switches. | `EPS_NUM_SWITCHES` interface definition |
| EPS-003 | The EPS component shall publish battery voltage, battery temperature, solar voltage, solar temperature, and switch state telemetry. | Power state observability |

### Radio communication

| ID | Requirement | Rationale |
| --- | --- | --- |
| COM-001 | The Radio component shall accept `SLEEP`, `TX`, `RX`, and `DUPLEX` mode selections. | Radio mode interface definition |
| COM-002 | The Radio cFS application shall process uplink data while the enabled Radio is in `RX` or `DUPLEX`. | Mode controlled command path |
| COM-003 | The Radio cFS application shall process downlink data while the enabled Radio is in `TX` or `DUPLEX`. | Mode controlled telemetry path |

### Cryptographic processing

| ID | Requirement | Rationale |
| --- | --- | --- |
| CRYPTO-001 | The representative radio path shall apply the configured CryptoLib processing to command uplink frames. | Flight command security processing |
| CRYPTO-002 | The representative radio path shall apply the configured CryptoLib processing to telemetry downlink frames. | Flight telemetry security processing |

### Ground software

| ID | Requirement | Rationale |
| --- | --- | --- |
| GND-001 | YAMCS shall issue commands defined by the active XTCE mission database through its realtime processor. | Ground command interface |
| GND-002 | YAMCS shall ingest telemetry parameters defined by the active XTCE mission database. | Ground telemetry interface |
| GND-003 | YAMCS shall archive the telemetry received by its configured recorders. | Historical telemetry retention |
| GND-004 | YAMCS shall retrieve archived telemetry for a requested historical interval. | Historical telemetry analysis |
| GND-005 | YAMCS shall execute a checked in command stack selected by the operator. | Repeatable ground procedure execution |
| GND-006 | YAMCS shall display the checked in DRM timeline bands. | Mission planning context |

### Operational documentation

| ID | Requirement | Rationale |
| --- | --- | --- |
| DOC-001 | Each published DRM scenario shall identify its prerequisites, procedure, expected evidence, and current limitations. | Repeatable scenario use |
| DOC-002 | The Atlas shall identify described behavior as implemented, conceptual, or unverified. | Accurate interpretation of maturity |

## Requirement attributes

The verification method identifies the planned method rather than a completed result.
Test is preferred when controlled execution can produce objective data.
Inspection is used for static configuration, interface, and documentation requirements.
Demonstration is reserved for top level mission behavior that must be verified as an integrated operator workflow.
Validation separately determines whether the complete baseline satisfies the stakeholder objectives.

| ID | Parent | Method | Verification level | Owner role |
| --- | --- | --- | --- | --- |
| DRM-001 | OBJ-001 | Demonstration | DRM system | DRM lead |
| DRM-002 | OBJ-002 | Demonstration | DRM system | DRM lead |
| DRM-003 | OBJ-003 | Demonstration | DRM system | DRM lead |
| DRM-004 | OBJ-004 | Demonstration | DRM system | DRM lead |
| DRM-005 | OBJ-005 | Demonstration | Component integration | Component lead |
| CFG-001 | DRM-001 | Test | DRM configuration | SHIRE lead |
| CFG-002 | DRM-001 | Inspection | Flight configuration | Flight software lead |
| CFG-003 | DRM-001 | Inspection | Simulation configuration | Simulation lead |
| SIM-001 | DRM-001 | Test | Simulation integration | Simulation lead |
| SIM-002 | DRM-001 | Test | Simulation integration | Simulation lead |
| SIM-003 | DRM-005 | Test | Component integration | Component lead |
| SIM-004 | DRM-005 | Test | Component integration | Component lead |
| OPS-001 | DRM-002 | Test | Flight and ground integration | Flight software lead |
| AUT-001 | DRM-002 | Test | Flight software | Flight software lead |
| AUT-002 | DRM-004 | Test | Flight software | Flight software lead |
| EVT-001 | DRM-002 | Test | Flight software | Flight software lead |
| CMD-001 | DRM-002 | Test | Flight and ground integration | Ground software lead |
| CMD-002 | DRM-002 | Test | Flight and ground integration | Communications lead |
| DAT-001 | DRM-003 | Test | Flight software | Flight software lead |
| DAT-002 | DRM-003 | Test | Flight and ground integration | Ground software lead |
| FSW-001 | DRM-001 | Inspection | Flight software | Flight software lead |
| FSW-002 | DRM-001 | Inspection | Flight software | Flight software lead |
| FSW-003 | DRM-002 | Test | Flight software | Flight software lead |
| ADCS-001 | DRM-001 | Test | ADCS integration | ADCS lead |
| ADCS-002 | DRM-001 | Test | ADCS integration | ADCS lead |
| ADCS-003 | DRM-002 | Test | ADCS integration | ADCS lead |
| ADCS-004 | DRM-001 | Test | ADCS integration | ADCS lead |
| DEMO-001 | DRM-001 | Test | Demo integration | Payload lead |
| DEMO-002 | DRM-002 | Test | Demo integration | Payload lead |
| EPS-001 | DRM-001 | Test | EPS integration | EPS lead |
| EPS-002 | DRM-002 | Test | EPS integration | EPS lead |
| EPS-003 | DRM-002 | Test | EPS integration | EPS lead |
| COM-001 | DRM-002 | Test | Radio integration | Communications lead |
| COM-002 | DRM-002 | Test | Radio integration | Communications lead |
| COM-003 | DRM-002 | Test | Radio integration | Communications lead |
| CRYPTO-001 | CMD-002 | Test | Security integration | Security lead |
| CRYPTO-002 | CMD-002 | Test | Security integration | Security lead |
| GND-001 | DRM-002 | Test | Ground software | Ground software lead |
| GND-002 | DRM-002 | Test | Ground software | Ground software lead |
| GND-003 | DRM-002 | Test | Ground software | Ground software lead |
| GND-004 | GND-003 | Test | Ground software | Ground software lead |
| GND-005 | DRM-002 | Test | Ground software | Ground software lead |
| GND-006 | DRM-002 | Inspection | Ground software | Ground software lead |
| DOC-001 | DRM-002 | Inspection | Atlas | DRM lead |
| DOC-002 | DRM-002 | Inspection | Atlas | DRM lead |

Owner roles identify the responsible discipline until the project assigns named requirement owners.
Executed evidence must record the exact configuration, revision, procedure, expected result, actual result, and disposition in a verification cross reference matrix.

## Candidate needs outside this baseline

The previous page treated several unapproved values and unimplemented behaviors as requirements.
They remain candidate needs until a stakeholder source and measurable acceptance criterion are available.

| Candidate subject | Reason it is not a current requirement |
| --- | --- |
| Ninety day mission life | No defined flight hardware or lifetime analysis basis |
| Launch and deployment survival | No launch environment or spacecraft qualification configuration |
| One ground contact per day | No selected orbit operations concept or ground network commitment |
| Five to fifteen minute pass duration | No geometry based Radio availability model or pass analysis |
| One gigabyte onboard storage | No flight storage hardware allocation |
| One hundred kilobit per second file transfer | No RF link budget or representative network benchmark |
| Quantified ADCS pointing accuracy | No approved pointing budget or sensor and actuator fidelity basis |
| Autonomous spacecraft Safe mode | No spacecraft mode manager or approved Safe configuration |
| EPS load shedding and battery protection | No approved power limits or implemented protection response |
| Payload science file generation | Demo currently publishes telemetry rather than payload files |
| Radio signal strength and configurable data rate performance | Radio currently exchanges packets without an RF model |
| Operational key management and security accreditation | CryptoLib uses a development configuration |

Do not convert a candidate into a `shall` statement by copying the previous value.
First identify its source, owner, rationale, acceptance criterion, verification level, and one planned verification method.

***
Last reviewed: 20260819
