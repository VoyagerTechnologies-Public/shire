# SHIRE Component Radio
This repository is an example radio component in the SHIRE environment.

## Overview
Command line interface (CLI), flight software (FSW), ground software (GSW), and simulation (SIM) directories are included in this repository.

The radio component is an SPI device with an active-high GPIO power enable and interrupt line.
The radio is duplex, or able to send and receive data at the same time.
It achieves this by buffering data on-board for the onboard computer (OBC) and signaling via the interrupt line to notify that a buffer is half full.
The radio passes the sent and received data straight through without inspection or modification.
The radio does not possess any direct from ground commands.

The specific command format is as follows:
* uint8, header, 0xAA
* uint8, command
  * (0) no operation
  * (1) get housekeeping
  * (2) set configuration
  * (3) receive data
  * (4) send data
* uint16, payload length in bytes (0-65535)
  * (0) no operation, 0
  * (1) get housekeeping, 0
  * (2) set configuration, 5
  * (3) receive data, 2
  * (4) send data, data length in bytes
* uint8 array, payload
  * (0) no operation, N/A
  * (1) get housekeeping, N/A
  * (2) set configuration
    * uint8, mode (0 Sleep, 1 TX, 2 RX, 3 Duplex)
    * uint8, rx speed setting (TBD)
    * uint8, rx wavelength setting (TBD)
    * uint8, tx speed setting (TBD)
    * uint8, tx wavelength setting (TBD)
  * (3) receive length, 2, data to receive in bytes
  * (4) send data, data array
* uint8, trailer, 0x11

Response formats:
* Housekeeping (Get housekeeping)
  * uint8, header, 0xAA
  * uint16, command counter (number of commands accepted)
  * uint8, mode (0 Sleep, 1 TX, 2 RX, 3 Duplex)
  * uint8, ground lock
  * uint8, rx speed setting (TBD)
  * uint8, rx wavelength setting (TBD)
  * uint8, tx speed setting (TBD)
  * uint8, tx wavelength setting (TBD)
  * uint32, bytes in received buffer
  * uint32, bytes in transmit buffer
  * uint32, bytes received
  * uint32, bytes sent
  * uint8, trailer, 0x11
* Received data
  * uint8, header, 0xAA
  * uint16, payload length in bytes (0-65535)
  * uint8 array, payload
  * uint8, trailer, 0x11

### Command Line Interface
The CLI can be configured to connect to either hardware (serial/USB) or the simulation backend. This enables direct checkouts without interfering with other systems.
* Hardware mode: host-side serial bridge to MCU on Feather. The MCU firmware implements the framing above and interacts with the RFM9x driver.
* Simulation mode: exercises the same framing/state machine in software so acceptance tests run identically without hardware.
Note: as with other SHIRE components, `make cfg` at the top-level SHIRE repository may be required to generate shared configuration headers (e.g., `device_cfg.h`).

### Flight Software
The core Flight System (cFS) flight software application receives commands from the software bus.
Two message IDs exist for commands:
* 0x18D2 - Commands
  * (0) No operation
  * (1) Reset counters
  * (2) Enable
  * (3) Disable
  * (4) Set configuration
  * (5) Service uplink/downlink
* 0x18D3 - Requests
  * (0) Request housekeeping
  * (1) Service radio (send/receive data)

Telemetry message IDs
* 0x08D2 - Application Housekeeping

Note that data received during the radio service will be placed on the software bus for transmission to other applications.

### Ground Software
No components of the ground station are implemented and a simple UDP interface is used to send/receive data to the radio simulator.
The XTCE file provided details the CCSDS Space Packet Protocol format used for commanding and telemetry.

### Simulation
The simulation available is built as a library that is loaded by the shire-director for use.
This maintains the state of the simulation and enables communication to the FSW via simulith.
Similar to the CLI, the `make cfg` call at the top level shire-lab is required prior to building.
