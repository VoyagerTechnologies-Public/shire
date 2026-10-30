# SHIRE Component Radio
This repository is an example radio component in the SHIRE environment.

## Overview
Command line interface (CLI), flight software (FSW), ground software (GSW), and simulation (SIM) directories are included in this repository.

The radio component is an SPI device with an active-high GPIO power enable and interrupt line.
The radio is duplex, or able to send and receive data at the same time.
It achieves this by buffering data on-board for the onboard computer (OBC) and signaling via the interrupt line to notify that a buffer is half full.
The radio passes the sent and received data straight through without inspection or modification.
The radio does not possess any direct from ground commands.
The simulator does expose test-only backdoor commands that control its RF link (see [RF link model](#rf-link-model)).

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

#### RF link model
The simulator sits between the flight software and the ground UDP interface, and models three things that real RF links do to traffic:

* **Visibility.** A packet only crosses the link while the spacecraft is "in view".
  With `link_mode: auto` the radio computes the spacecraft's elevation above each configured ground station from the 42 truth (Earth-fixed position, spherical Earth as in 42) and the link is up when any enabled station is at or above its `min_elev_deg`.
  `link_mode: always` and `link_mode: never` ignore geometry.
  The HK ground-lock flag (`GroundLock`) reports the current link state; AOS and LOS transitions are logged as `RADIO_LINK {...}` lines.
* **Delay.** Each direction has an independent one-way delay in milliseconds, measured in simulated time (so it holds at any `simulith_speed`).
  Delayed packets keep their order; a packet still in flight when the radio loses power or the radio leaves a mode that can carry it is lost.
* **Loss.** Each direction has an independent drop probability in parts per million, drawn from a seeded generator so a run is repeatable.
  Packets sent while out of view are discarded and counted separately.

Every ground station shares the same downlink, so stations differ only in when they can see the spacecraft.
Lost packets are invisible to the flight software: a transmit that the link drops still completes successfully.

The defaults are an ideal link (`always`, no delay, no loss), so existing scenarios are unchanged.
Defaults live in `support/device_config.yaml` and can be overridden at any configuration layer (spacecraft, scenario, ...):

| Key | Meaning |
| --- | --- |
| `link_mode` | `auto`, `always` or `never` |
| `uplink_delay_ms`, `downlink_delay_ms` | one-way delay per direction |
| `uplink_drop_ppm`, `downlink_drop_ppm` | drop probability per direction, 0 to 1000000 |
| `link_seed` | seed for the loss generator |
| `link_queue_depth` | packets that can be in flight per direction (overflow is dropped and counted) |
| `ground_stations` | optional list of station names from `cfg/drm/ground_stations.yaml`; omitted means every Earth station |

Stations come from the shared catalog `cfg/drm/ground_stations.yaml`, which also feeds 42.
Its optional per-station `min_elev_deg` (default 5) and `enabled` (default true) fields only affect the radio.
Run `make cfg` after changing any of these, since the values are compiled into the simulator.

#### Backdoor commands
The simulator target name is `radio_sim`.
The `/RADIO/BACKDOOR_RADIO_*` commands in `gsw/radio.xtce` use the `tc_backdoor` stream (they need `/RADIO/BACKDOOR_RADIO_.*` in that stream's `tcPatterns`).
Payload integers are big-endian.

| ID | Command | Payload |
| --- | --- | --- |
| 1 | `SET_LINK_MODE` | mode: 0 auto, 1 always in view, 2 never in view |
| 2 | `SET_DELAY` | direction (0 uplink, 1 downlink, 2 both), delay in ms (u32) |
| 3 | `SET_DROP_RATE` | direction, drop probability in ppm (u32, at most 1000000) |
| 4 | `SET_IMPAIRMENTS_ENABLED` | 0 bypasses delay and loss without forgetting their values, 1 applies them |
| 5 | `SET_STATION_ENABLED` | station index (255 for all), 0 or 1 |
| 6 | `SET_SEED` | loss generator seed (u32) |
| 7 | `RESET_LINK` | none; restores the configured defaults and discards packets in flight |
| 8 | `CORRUPT_PACKETS` | direction (0 uplink, 1 downlink, 2 both), count (u16, 0 cancels), byte offset (u16); flips one bit at that offset in each of the next count packets that cross the link |
| 9 | `REPLAY_PACKET` | direction (0 uplink, 1 downlink); re-injects the last packet that crossed the link, bypassing visibility, delay and loss |

Each command prints a parseable `RADIO_BACKDOOR {...}` line with the resulting link settings and drop counters.
Malformed or out-of-range commands are rejected and leave the link unchanged.
Link settings survive a radio power cycle; packets in flight do not.

Commands that must reach the spacecraft while the link is impaired should use the debug uplink; the `radio-link-testing` scenario's `RadioLinkBackdoor.ycs` stack does this, and its `RadioCommandPaths.ycs` stack sends the same command over both paths and checks the flight counter.

#### Link security
Both directions of the radio path are protected by CCSDS SDLS using CryptoLib, which runs at both ends: the ground `shire-cryptolib` container applies security to commands and processes telemetry, and the flight radio app does the reverse.
By default both directions are encrypted and authenticated with AES-256-GCM:

| Direction | Radio view | Virtual channel | Security association |
| --- | --- | --- | --- |
| Uplink (telecommand) | RX | TC VCID 0 | SPI 4 (SPI 1 is the clear-mode alternative) |
| Downlink (telemetry) | TX | TM VCID 1 | SPI 6 (SPI 5 is the clear-mode alternative) |

`uplink_encryption` and `downlink_encryption` in `support/device_config.yaml` (both default `true`) select clear or encrypted mode per direction.
They are compiled into the radio app's `device_cfg.h` and passed to the ground CryptoLib container as `SHIRE_UPLINK_ENCRYPTION` and `SHIRE_DOWNLINK_ENCRYPTION`, so the two ends always agree.
Both ends call `Crypto_Shire_Configure_Link_Security()` from `comp/cryptolib/include/crypto_shire.h` after `Crypto_SC_Init()`.

The keys are CryptoLib's built-in in-memory test keys, the same on both ends.
This protects a simulation, not a real link.
Replays are rejected by the SDLS anti-replay window, which is set to 65535 frames so that a pass worth of lost frames (radio drops or loss of signal) does not desynchronize the receiver.
Each end tracks its own IV, so restart the ground CryptoLib and the flight software together, as the mission Compose file does.

The `CORRUPT_PACKETS` and `REPLAY_PACKET` backdoors exist to exercise this protection.
The `radio-security-testing` scenario shows tampered and replayed commands rejected onboard, and tampered and replayed telemetry rejected by the ground CryptoLib.

