# Glossary

This glossary defines terms as they are used in the current SHIRE repository and Atlas.

| Term | Meaning in SHIRE |
| --- | --- |
| 42 | NASA Goddard Space Flight Center spacecraft dynamics and environment simulator used by SHIRE. |
| ADCS | Attitude Determination and Control System reference component. |
| Atlas | The public SHIRE documentation site under `atlas/`. |
| Backdoor | A simulation only interface used to inject a controlled component condition through the Director. |
| cFE | Core Flight Executive, which supplies the runtime services at the center of cFS. |
| cFS | NASA core Flight System used as the SHIRE flight software framework. |
| CFDP | CCSDS File Delivery Protocol used for file transfer between flight and ground software. |
| CI_LAB | cFS laboratory command ingest application used by the direct debug command path. |
| Component | A SHIRE subsystem package containing a flight application, simulated device, shared protocol code, tests, configuration, and ground artifacts as applicable. |
| CryptoLib | NASA cryptographic library used in the representative radio command and telemetry path. |
| Design Reference Mission | The example mission configuration, operating concept, requirements, and scenarios supplied with SHIRE. |
| Director | Simulith process that loads component simulators, advances them on each tick, exchanges state with 42, handles backdoor traffic, and publishes truth telemetry. |
| DRM | Design Reference Mission. |
| DS | cFS Data Storage application used to record selected packets to files. |
| EPS | Electrical Power System reference component. |
| FDIR | Fault Detection, Isolation, and Recovery. |
| FSW | Flight software. |
| GSW | Ground software. |
| HWLIB | Hardware interface library in the SHIRE PSP that provides UART, I2C, SPI, GPIO, and other interfaces. |
| IPC | Interprocess communication used for simulated device and 42 connections within the shared Docker volume. |
| LC | cFS Limit Checker application used to evaluate watchpoints and trigger configured actions. |
| OSAL | Operating System Abstraction Layer used by cFS. |
| PSP | Platform Support Package used by cFS to reach platform and hardware services. |
| Radio | SHIRE reference component that models device commanding, buffered uplink and downlink, and radio operating modes. |
| RTS | Relative Time Sequence executed by the cFS Stored Command application. |
| SC | cFS Stored Command application. |
| Server | Simulith process that owns simulation time, broadcasts ticks, and waits for registered clients. |
| Simulith | SHIRE simulation middleware that coordinates time, loads component models, provides simulated device transport, and connects to 42. |
| TO_LAB | cFS laboratory telemetry output application used by the direct debug telemetry path. |
| Truth telemetry | Selected 42 state published by the Director to YAMCS for comparison with spacecraft observations. |
| XTCE | XML Telemetric and Command Exchange format used for YAMCS mission database definitions. |
| YAMCS | Ground software used for commanding, telemetry, archives, procedures, displays, timelines, and CFDP. |
| ZeroMQ | Messaging library used by simulated HWLIB transports over IPC endpoints. |

When a term in the Atlas is ambiguous, use the source or configuration described in [Quick Reference](quick-reference.md) to confirm its meaning in the current checkout.
