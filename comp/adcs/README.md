# SHIRE Component ADCS
This repository is an example component for an Attitude Determination and Control System (ADCS) in a box in the SHIRE environment.

## Overview
Command line interface (CLI), flight software (FSW), ground software (GSW), and simulation (SIM) directories are included in this repository.

This example ADCS in a box manages the following hardware suite:
* Six coarse sun sensors (CSS), one on each side
* Two fine sun sensors (FSS), both on the sun side
* A global positioning system (GPS)
* A three axis inertial measurement unit (IMU)
* A magnetometer (MAG)
* Three orthogonal magnetorquer bars (MTBs)
* Three orthogonal reaction wheels (RWs)
* A star tracker (ST)

The interface to the ADCS is a UART that is speak when spoken to.
Each command that is successfully interpreted is echoed back.
If additional telemetry is to be generated, it will follow.
The specific command format is as follows:
* uint16, header, 0xADC5
* uint16, command
  * (0) no operation
  * (1) get housekeeping
  * (2) set mode
    * 0, disabled
    * 1, de-tumble
    * 2, sun point
    * 3, nadir point
    * 4, target track
    * 5, inertial point mode
    * 6, diagnostic (enables override commands)
  * (3) set target
  * ...
  * (10) get CSS data
  * (11) get FSS data
  * (12) get GPS data
  * (13) get IMU data
  * (14) get MAG data
  * (15) get MTB data
  * (16) get RW data
  * (17) get ST data
  * ...
  * (20) override MTB
  * (21) override RW
* uint16, trailer, 0x5CDA

Note that all frames are reference are the inertial frame
Response formats:
* Housekeeping
  * uint16, header, 0xADC5 
  * uint16, command counter
  * uint8,  mode
  * uint32, gps seconds
  * uint32, gps subseconds
  * float32[3], gps position
  * float32[3], velocity
  * uint8,  attitude source
  * float32[3], estimated angular rate
  * float32[4], estimated quaternion
  * uint8,  eclipse flag (1 yes, 0 no)
  * float32[3], estimated sun vector in body frame
  * uint16, trailer, 0x5CDA
* Get sensor data
  * uint16, header, 0xADC5
  * TBD
  * uint16, trailer, 0x5CDA

### Command Line Interface
The CLI can be configured to connect to either the hardware or simulation.
This enables direct checkouts these without interference.

### Flight Software
The core Flight System (cFS) flight software application receives commands from the software bus.
Two message IDs exist for commands and requests:
* 0x18D4 - Commands
  * (0) No operation
  * (1) Reset counters
  * (2) Enable
  * (3) Disable
  * (4) Set mode
  * (5) Set target
* 0x18D5 - Requests
  * (0) Request housekeeping
  * (1) Request CSS data
  * (2) Request FSS data
  * (3) Request GPS data
  * (4) Request IMU data
  * (5) Request MAG data
  * (6) Request MTB data
  * (7) Request RW data
  * (8) Request ST data

Multiple message IDs exist for telemetry:
* 0x08D4 - Housekeeping
* 0x08D5 - CSS data
* 0x08D6 - FSS data
* 0x08D7 - GPS data
* 0x08D8 - IMU data
* 0x08D9 - MAG data
* 0x08DA - MTB data
* 0x08DB - RW data
* 0x08DC - ST data

### Ground Software
The XTCE file provided details the CCSDS Space Packet Protocol format used for commanding and telemetry.

### Simulation
The simulation available is built as a library that is loaded by the shire-director for use.
This maintains the state of the simulation and enables communication to the FSW via simulith.
Similar to the CLI, the `make cfg` call at the top level SHIRE is required prior to building.
