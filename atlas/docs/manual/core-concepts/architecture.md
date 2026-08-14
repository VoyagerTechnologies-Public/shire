# System Architecture

SHIRE is designed as a high-fidelity software "digital twin" of a complete spacecraft mission.
It is not just one piece of software, but an integrated ecosystem of multiple open-source components working together to replicate the relationship between a spacecraft, its environment, and mission control.

We use actual flight software (FSW) and ground software (GSW) connected through a realistic simulation environment or middleware.
The FSW selected for use is the [core Flight System (cFS)](https://etd.gsfc.nasa.gov/capabilities/core-flight-system/) developed by NASA and open source.
The GSW selected is [Yet Another Mission Control System (YAMCS)](https://yamcs.org/) which is an open source framework for command and control.
This allows you to develop, test, and "fly" your mission in a software-only environment that closely mirrors the real world.

## High-level Architecture

In SHIRE you'll note five containers running in docker prefaced with `shire-`:

* CryptoLib: Utilized to provide command authenticated encryption to the uplinked data.
* Director: Runs the 42 dynamics and component simulators.
* FSW: cFS with a standard application suite and SHIRE specific component applications.
* GSW: YAMCS with database support for cFS and SHIRE specific applications.
* Server: Drives and synchronizes time between Director and FSW so they are in lock step.

``` mermaid
graph TD
    subgraph A[GSW]
        CryptoLib
        YAMCS
    end
    A <--> B[Director]
    C[FSW] <--> B[Director]
    C <--> A
    E[Server] <--> B
    E <--> C
```

## Communications

CryptoLib is running internal to the space vehicle as a library to the radio component in addition to running in the ground pipeline as its own container.
Remember that in an actual scenario involving the space link there would be a translation to radio frequency (RF) at the radio and ground station which has been omitted.

``` mermaid
graph LR
    C[FSW] <--> |DEBUG| D
    C <--> A[CryptoLib] <--> |RADIO| B[Director] <--> D[GSW]
```

Network transports of both UDP and IPC are used in SHIRE:
``` mermaid
graph LR
    C[FSW] <--> |IPC| B
    A[CryptoLib] --> |UDP:12346| D
    B[Director] --> |UDP:12344| A
    A --> |UDP:12343| B
    D[GSW] --> |UDP:12345| A
    E[Server] <--> |IPC| C
    E <--> |IPC| B
```

Additionally specific data formats are used at various points.
The Consultative Committed for Space Data Systems (CCSDS) captures a number of the standards leveraged for space communications.
Each layer encapsulates or augments the prior:

* Space Packets or CCSDS packets are they are sometimes referred to are the base.
* Telecommands or TCs contain a number of command space packets that would go to the space vehicle.
* Telemetry or TMs contain a number of telemetry space packets.
* Space Data Link Security (SDLS) and its extended procedures (SDLS-EP) augment these to enable authenticated encryption.
* CryptoLib standalone handles the TC framing from the GSW while the cFS IO_Lib called within the radio application interprets those frames and provides space packets that are placed on the software bus.

``` mermaid
graph TD
    B[Director] --> |Space Packet Telemetry| A
    A[CryptoLib] --> |TC Frame| B
    C[FSW] <--> |HWLIB| B
    C <--> |Space Packets| D
    D[GSW] --> |Space Packet Commands| A
    E[Server] <--> |Simulith ZMQ| B
    E <--> |Simulith ZMQ| C
```

It should be noted that the cFS Hardware Library (HWLIB) is leveraging Simulith transport, ZMQ under the hood, to communicate in the SHIRE environment.
HWLIB enables swapping of drivers between simulation and flight to ease the transition to the physical space vehicle.

## Time Synchronization

It was alluded to in the previous section that Simulith is driving time synchronization.
Simulith requires a number of connections to expect passed as an argument to it.
In the SHIRE environment this is always two per spacecraft as the FSW and Director are required for each.
This is possible because each of these distribute the received time tic to the various processes running within them.
Each tic also sends a toc response that is received by the server to know that processing has completed and the next tic may be called after any necessary time delays.

``` mermaid
graph LR
    A[CryptoLib]
    subgraph B[Director]
        42
        Sims
    end
    B[Director] <--> E
    C[FSW] <--> E
    D[GSW]
    E[Server]
```

----
Last updated: 20251202
