# SHIRE Component Demo
This repository is a demonstration component for the SHIRE environment, showcasing the standard patterns for component development.

## Overview
Command line interface (CLI), flight software (FSW), ground software (GSW), and simulation (SIM) directories are included in this repository.

The demo component is a UART device that is speak when spoken to.
Each command that is successfully interpreted is echoed back.
If additional telemetry is to be generated, it will follow.
The specific command format is as follows:
* uint16, header, 0xC0FF
* uint16, command
  * (0) No operation
  * (1) Get housekeeping
  * (2) Get data
  * (3) Set configuration
* uint16, payload
  * Unused except for set configuration command
* uint16, trailer, 0xFEFE

Response formats:
* Housekeeping
  * uint16, header, 0xC0FF 
  * uint16, command counter
  * uint16, configuration
  * uint16, trailer, 0xFEFE
* Data
  * uint16, header, 0xC0FF
  * uint16, data channel 1
  * uint16, data channel 2
  * uint16, data channel 3
  * uint16, trailer, 0xFEFE

## Command Line Interface
The CLI can be configured to connect to either the hardware or simulation.
This enables direct checkouts these without interference.

## Flight Software
The core Flight System (cFS) flight software application receives commands from the software bus.
Two message IDs exist for commands:
* 0x18FA - Commands
  * (0) No operation
  * (1) Reset counters
  * (2) Enable
  * (3) Disable
  * (4) Set configuration
* 0x18FB - Requests
  * (0) Request housekeeping
  * (1) Request data point

Two message IDs exist for telemetry:
* 0x08FA - Application Housekeeping
* 0x08FB - Component Telemetry

## Ground Software
The XTCE file provided details the CCSDS Space Packet Protocol format used for commanding and telemetry.

## Simulation
The simulation available is built as a library that is loaded by the shire-director for use.
This maintains the state of the simulation and enables communication to the FSW via simulith.
Similar to the CLI, the `make cfg` call at the top level shire-lab is required prior to building.

Demo component simulator backdoor commands:
- 0x0001 DEMO_SET_CONFIG: payload `uint16` new config value; emits HK.
- 0x0002 DEMO_RAND_HK: payload optional 1 enable / 0 disable (default enable); randomizes HK counter; emits HK.
- 0x0003 DEMO_RAND_DATA: payload optional 1 enable / 0 disable (default enable); randomizes data; emits data.
