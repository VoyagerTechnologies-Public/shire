# SHIRE Component - Electrical Power System
This repository is an example Electrical Power System (EPS) component for the SHIRE environment.

## Overview
Command line interface (CLI), flight software (FSW), ground software (GSW), and simulation (SIM) directories are included in this repository.

The EPS component is an I2C device with a fixed ID of 0x1E and a maximum speed of 400kHz.
The EPS is a system as it includes a solar array, batteries, and power rails / switches to the space vehicle.
Eight switches are available for routing to specific components.
Each command sent to the device requires a CRC-8-CCITT polynomial 0x07 that is calculated over the I2C address through the end of the payload.
The specific command format is as follows:
* uint8, I2C address (7-bit), 0x1E
* uint8, command
  * (0) No operation
  * (1) Get housekeeping
  * (2) Set switch OFF
  * (3) Set switch ON
* uint8, payload
  * Unused except for set switch commands
* uint8, CRC-8-CCITT polynomial 0x07

EPS telemetry is updated once per second.
The housekeeping format is as follows:
* uint8, battery voltage (32V / 255 = 0.12549V per count)
* uint8, battery temperature (250C / 255 = 0.9803921568627451C per count)
* uint8, solar array voltage (32V / 255 =0.125V per count)
* uint8, solar array temperature (250C / 255 = 0.9803921568627451C per count)
* uint8, switch 0 state (0 off, 1 on, else fault)
* uint8, switch 0 voltage (32V / 255 =0.125V per count)
* uint8, switch 0 current (10A / 255 = 0.0392156862745098A per count)
* uint8, switch 1 state (0 off, 1 on, else fault)
* uint8, switch 1 voltage (32V / 255 =0.125V per count)
* uint8, switch 1 current (10A / 255 = 0.0392156862745098A per count)
* uint8, switch 2 state (0 off, 1 on, else fault)
* uint8, switch 2 voltage (32V / 255 =0.125V per count)
* uint8, switch 2 current (10A / 255 = 0.0392156862745098A per count)
* uint8, switch 3 state (0 off, 1 on, else fault)
* uint8, switch 3 voltage (32V / 255 =0.125V per count)
* uint8, switch 3 current (10A / 255 = 0.0392156862745098A per count)
* uint8, switch 4 state (0 off, 1 on, else fault)
* uint8, switch 4 voltage (32V / 255 =0.125V per count)
* uint8, switch 4 current (10A / 255 = 0.0392156862745098A per count)
* uint8, switch 5 state (0 off, 1 on, else fault)
* uint8, switch 5 voltage (32V / 255 =0.125V per count)
* uint8, switch 5 current (10A / 255 = 0.0392156862745098A per count)
* uint8, switch 6 state (0 off, 1 on, else fault)
* uint8, switch 6 voltage (32V / 255 =0.125V per count)
* uint8, switch 6 current (10A / 255 = 0.0392156862745098A per count)
* uint8, switch 7 state (0 off, 1 on, else fault)
* uint8, switch 7 voltage (32V / 255 =0.125V per count)
* uint8, switch 7 current (10A / 255 = 0.0392156862745098A per count)
* uint8, CRC-8-CCITT polynomial 0x07

### Command Line Interface
The CLI can be configured to connect to either the hardware or simulation.
This enables direct checkouts these without interference.
Note that `make cfg` (shire_orchestrator.py) must be run at the top level SHIRE to produce the required `./shared/device_cfg.h`.

### Flight Software
The core Flight System (cFS) flight software application receives commands from the software bus.
Two message IDs exist for commands:
* 0x18D0 - Commands
  * (0) No operation
  * (1) Reset counters
  * (3) Set switch OFF
  * (4) Set switch ON
* 0x18D1 - Requests
  * (0) Request telemetry

Two message IDs exist for telemetry:
* 0x08D0 - Application Housekeeping
* 0x08D1 - Component Telemetry

### Ground Software
The XTCE file provided details the CCSDS Space Packet Protocol format used for commanding and telemetry.

### Simulation
The simulation available is built as a library that is loaded by the shire-director for use.
This maintains the state of the simulation and enables communication to the FSW via simulith.
Similar to the CLI, the `make cfg` call at the top level shire-lab is required prior to building.
